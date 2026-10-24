#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import os
import shutil
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO


PACK_NAME = "bitloop-development"
MANIFEST_NAME = "agent-pack.json"
SCHEMA_VERSION = 1
PACK_RELATIVE_PATH = Path(".agents") / "skills" / PACK_NAME
RECURSIVE_SKIP_DIRECTORIES = {
    ".agents",
    ".codex",
    ".git",
    ".vs",
    "__pycache__",
    "build",
    "node_modules",
    "out",
    "vcpkg",
}


class AgentPackError(RuntimeError):
    """Base error for malformed packs or project roots."""


class AgentPackConflict(AgentPackError):
    """Raised when synchronization would overwrite local pack changes."""


class AgentPackNotInitialized(AgentPackError):
    """Raised when synchronization is requested for an unmanaged project."""


@dataclass(frozen=True)
class PackState:
    status: str
    details: tuple[str, ...] = ()


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _managed_files(pack_dir: Path) -> dict[str, str]:
    files: dict[str, str] = {}
    for path in sorted(pack_dir.rglob("*")):
        if path.is_symlink():
            raise AgentPackError(f"agent pack may not contain symlinks: {path}")
        if not path.is_file() or path.name == MANIFEST_NAME:
            continue
        relative = path.relative_to(pack_dir).as_posix()
        files[relative] = _sha256(path)
    return files


def write_manifest(
    pack_dir: Path,
    *,
    pack_revision: str,
    source_revision: str,
    compatibility: str,
) -> dict:
    pack_dir = pack_dir.resolve()
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "pack_name": PACK_NAME,
        "pack_revision": pack_revision,
        "source_revision": source_revision,
        "compatibility": compatibility,
        "files": _managed_files(pack_dir),
    }
    manifest_path = pack_dir / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def load_manifest(pack_dir: Path) -> dict:
    manifest_path = pack_dir / MANIFEST_NAME
    if not manifest_path.is_file():
        raise AgentPackError(f"missing {MANIFEST_NAME}: {manifest_path}")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AgentPackError(f"invalid {MANIFEST_NAME}: {exc}") from exc

    required = {
        "schema_version",
        "pack_name",
        "pack_revision",
        "source_revision",
        "compatibility",
        "files",
    }
    missing = sorted(required - manifest.keys())
    if missing:
        raise AgentPackError(f"manifest is missing fields: {', '.join(missing)}")
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise AgentPackError(f"unsupported agent-pack schema: {manifest['schema_version']}")
    if manifest["pack_name"] != PACK_NAME:
        raise AgentPackError(f"unexpected agent-pack name: {manifest['pack_name']}")
    if not isinstance(manifest["files"], dict) or not manifest["files"]:
        raise AgentPackError("manifest files must be a non-empty object")

    for relative, digest in manifest["files"].items():
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise AgentPackError(f"unsafe manifest path: {relative}")
        if not isinstance(digest, str) or len(digest) != 64:
            raise AgentPackError(f"invalid SHA-256 for {relative}")
    return manifest


def validate_pack(pack_dir: Path, manifest: dict | None = None) -> tuple[str, ...]:
    pack_dir = pack_dir.resolve()
    manifest = manifest or load_manifest(pack_dir)
    expected = manifest["files"]
    actual = _managed_files(pack_dir)
    issues: list[str] = []

    for relative in sorted(expected.keys() - actual.keys()):
        issues.append(f"missing managed file: {relative}")
    for relative in sorted(actual.keys() - expected.keys()):
        issues.append(f"unmanaged file inside generated pack: {relative}")
    for relative in sorted(expected.keys() & actual.keys()):
        if actual[relative] != expected[relative]:
            issues.append(f"modified managed file: {relative}")
    return tuple(issues)


def canonical_pack_dir(bitloop_root: Path) -> Path:
    return bitloop_root.resolve() / PACK_RELATIVE_PATH


def _looks_like_project_root(path: Path) -> bool:
    cmake = path / "CMakeLists.txt"
    marker = path / ".bitloop-workspace"
    manifest = path / "vcpkg.json"
    if not cmake.is_file() or not marker.is_file() or not manifest.is_file():
        return False
    try:
        return "bitloop_new_project" in cmake.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False


def resolve_project_root(project: Path | None = None, *, start: Path | None = None) -> Path:
    if project is not None:
        candidate = project.expanduser().resolve()
        if not _looks_like_project_root(candidate):
            raise AgentPackError(
                f"not a Bitloop project root (expected CMakeLists.txt, vcpkg.json, and .bitloop-workspace): {candidate}"
            )
        return candidate

    current = (start or Path.cwd()).resolve()
    for candidate in (current, *current.parents):
        if _looks_like_project_root(candidate):
            return candidate
    raise AgentPackError("could not find a Bitloop project root from the current directory")


def resolve_recursive_root(root: Path | None = None, *, start: Path | None = None) -> Path:
    candidate = (root or start or Path.cwd()).expanduser().resolve()
    if not candidate.is_dir():
        raise AgentPackError(f"recursive search root is not a directory: {candidate}")
    return candidate


def discover_managed_projects(root: Path) -> tuple[Path, ...]:
    root = resolve_recursive_root(root)
    projects: list[Path] = []
    walk_errors: list[str] = []

    def record_walk_error(error: OSError) -> None:
        walk_errors.append(str(error))

    for current, directories, files in os.walk(root, onerror=record_walk_error):
        current_path = Path(current)
        directories[:] = sorted(
            (
                name
                for name in directories
                if name.casefold() not in RECURSIVE_SKIP_DIRECTORIES
                and not (current_path / name).is_symlink()
            ),
            key=str.casefold,
        )
        if ".bitloop-workspace" not in files:
            continue
        if (current_path / PACK_RELATIVE_PATH).exists():
            projects.append(current_path.resolve())

    if walk_errors:
        raise AgentPackError("recursive search was incomplete:\n  " + "\n  ".join(walk_errors))
    return tuple(sorted(set(projects), key=lambda path: str(path).casefold()))


def inspect_destination(destination: Path, source_manifest: dict) -> PackState:
    if not destination.exists():
        return PackState("missing")
    if not destination.is_dir():
        return PackState("modified", ("agent-pack destination is not a directory",))

    try:
        destination_manifest = load_manifest(destination)
        issues = validate_pack(destination, destination_manifest)
    except AgentPackError as exc:
        return PackState("modified", (str(exc),))

    if issues:
        return PackState("modified", issues)
    if destination_manifest == source_manifest:
        return PackState("current")
    return PackState("stale")


def _transaction_directory(parent: Path, role: str) -> Path:
    return parent / f".{PACK_NAME}.{role}-{uuid.uuid4().hex}"


def _remove_transaction_directory(path: Path, parent: Path, role: str) -> None:
    expected_prefix = f".{PACK_NAME}.{role}-"
    if path.parent.resolve() != parent.resolve() or not path.name.startswith(expected_prefix):
        raise AgentPackError(f"refusing to remove unexpected transaction directory: {path}")
    if not path.exists():
        return
    if path.is_symlink():
        path.unlink()
        return
    shutil.rmtree(path)


def _tree_entries(root: Path) -> tuple[set[Path], set[Path]]:
    directories: set[Path] = set()
    files: set[Path] = set()
    for path in root.rglob("*"):
        if path.is_symlink():
            raise AgentPackError(f"agent pack may not contain symlinks: {path}")
        relative = path.relative_to(root)
        if path.is_dir():
            directories.add(relative)
        elif path.is_file():
            files.add(relative)
        else:
            raise AgentPackError(f"unsupported agent-pack entry: {path}")
    return directories, files


def _files_equal(source: Path, destination: Path) -> bool:
    return (
        destination.is_file()
        and source.stat().st_size == destination.stat().st_size
        and _sha256(source) == _sha256(destination)
    )


def _replace_file(source: Path, destination: Path) -> None:
    if _files_equal(source, destination):
        return

    temporary = destination.parent / f".{destination.name}.staging-{uuid.uuid4().hex}"
    try:
        shutil.copy2(source, temporary)
        try:
            os.replace(temporary, destination)
        except PermissionError:
            # Some Windows file watchers allow writes but deny replacement of an
            # open file. The transaction backup still makes this path rollback-safe.
            shutil.copy2(temporary, destination)
    finally:
        if temporary.exists():
            temporary.unlink()


def _mirror_directory(source: Path, destination: Path) -> None:
    source_directories, source_files = _tree_entries(source)
    destination_directories, destination_files = _tree_entries(destination)

    for relative in sorted(destination_files - source_files, key=lambda path: path.as_posix()):
        (destination / relative).unlink()
    for relative in sorted(
        destination_directories - source_directories,
        key=lambda path: (-len(path.parts), path.as_posix()),
    ):
        (destination / relative).rmdir()

    for relative in sorted(source_directories, key=lambda path: (len(path.parts), path.as_posix())):
        (destination / relative).mkdir(exist_ok=True)

    ordered_files = sorted(
        source_files,
        key=lambda path: (path == Path(MANIFEST_NAME), path.as_posix()),
    )
    for relative in ordered_files:
        _replace_file(source / relative, destination / relative)


def _install_directory(source: Path, destination: Path) -> None:
    parent = destination.parent
    parent.mkdir(parents=True, exist_ok=True)
    staging = _transaction_directory(parent, "staging")
    shutil.copytree(source, staging)

    try:
        os.replace(staging, destination)
    finally:
        if staging.exists():
            _remove_transaction_directory(staging, parent, "staging")


def _replace_existing_directory(source: Path, destination: Path) -> None:
    parent = destination.parent
    backup = _transaction_directory(parent, "backup")
    backup_ready = False
    preserve_backup = False

    try:
        _tree_entries(destination)
        shutil.copytree(destination, backup)
        backup_ready = True
        _mirror_directory(source, destination)
    except Exception as update_error:
        if backup_ready:
            try:
                _mirror_directory(backup, destination)
            except Exception as rollback_error:
                preserve_backup = True
                raise AgentPackError(
                    f"agent-pack update failed and rollback also failed ({rollback_error}); "
                    f"the recovery copy is preserved at {backup}"
                ) from update_error
        raise
    finally:
        if backup.exists() and not preserve_backup:
            _remove_transaction_directory(backup, parent, "backup")


def _validated_canonical_pack(bitloop_root: Path) -> tuple[Path, dict]:
    source = canonical_pack_dir(bitloop_root)
    source_manifest = load_manifest(source)
    source_issues = validate_pack(source, source_manifest)
    if source_issues:
        raise AgentPackError("canonical agent pack is invalid:\n  " + "\n  ".join(source_issues))
    return source, source_manifest


def _replace_pack(source: Path, destination: Path) -> None:
    try:
        if destination.exists():
            _replace_existing_directory(source, destination)
        else:
            _install_directory(source, destination)
    except OSError as exc:
        raise AgentPackError(f"could not replace managed agent pack at {destination}: {exc}") from exc


def _sync_agent_pack(
    source: Path,
    source_manifest: dict,
    project_root: Path,
    *,
    check: bool,
    force: bool,
    output: TextIO | None,
) -> int:
    project_root = resolve_project_root(project_root)
    destination = project_root / PACK_RELATIVE_PATH
    state = inspect_destination(destination, source_manifest)

    if check:
        if output is not None:
            print(f"{PACK_NAME}: {state.status}", file=output)
            for detail in state.details:
                print(f"  {detail}", file=output)
        return 0 if state.status == "current" else 1

    if state.status == "missing":
        raise AgentPackNotInitialized(
            f"{project_root} has no managed {PACK_NAME} pack; "
            "run 'bitloop agents init --project <ProjectRoot>' first"
        )
    if state.status == "current":
        if output is not None:
            print(f"{PACK_NAME}: already current", file=output)
        return 0
    if state.status == "modified" and not force:
        details = "\n  ".join(state.details)
        raise AgentPackConflict(
            "local changes exist inside the managed Bitloop agent pack; "
            "move project guidance to AGENTS.md or rerun with --force"
            + (f":\n  {details}" if details else "")
        )

    _replace_pack(source, destination)
    if output is not None:
        print(f"{PACK_NAME}: replaced {destination}", file=output)
    return 0


def init_agent_pack(
    bitloop_root: Path,
    project_root: Path,
    *,
    output: TextIO | None = None,
) -> int:
    source, source_manifest = _validated_canonical_pack(bitloop_root)
    project_root = resolve_project_root(project_root)
    destination = project_root / PACK_RELATIVE_PATH
    state = inspect_destination(destination, source_manifest)

    if state.status == "current":
        if output is not None:
            print(f"{PACK_NAME}: already initialized", file=output)
        return 0
    if state.status != "missing":
        raise AgentPackConflict(
            f"a managed-pack path already exists at {destination}; "
            "use 'bitloop agents sync' to inspect or update it"
        )

    _replace_pack(source, destination)
    if output is not None:
        print(f"{PACK_NAME}: installed {destination}", file=output)
    return 0


def sync_agent_pack(
    bitloop_root: Path,
    project_root: Path,
    *,
    check: bool = False,
    force: bool = False,
    output: TextIO | None = None,
) -> int:
    if check and force:
        raise AgentPackError("--check and --force cannot be used together")
    source, source_manifest = _validated_canonical_pack(bitloop_root)
    return _sync_agent_pack(
        source,
        source_manifest,
        project_root,
        check=check,
        force=force,
        output=output,
    )


def sync_agent_packs_recursive(
    bitloop_root: Path,
    root: Path,
    *,
    check: bool = False,
    force: bool = False,
    output: TextIO | None = None,
) -> int:
    if check and force:
        raise AgentPackError("--check and --force cannot be used together")

    root = resolve_recursive_root(root)
    projects = discover_managed_projects(root)
    if not projects:
        if output is not None:
            print(f"{PACK_NAME}: no managed projects found under {root}", file=output)
        return 0

    source, source_manifest = _validated_canonical_pack(bitloop_root)
    failed = False
    for project in projects:
        if output is not None:
            print(f"{project}:", file=output)
        try:
            result = _sync_agent_pack(
                source,
                source_manifest,
                project,
                check=check,
                force=force,
                output=output,
            )
        except AgentPackError as exc:
            failed = True
            if output is not None:
                print(f"  error: {exc}", file=output)
        else:
            failed = failed or result != 0
    return 1 if failed else 0
