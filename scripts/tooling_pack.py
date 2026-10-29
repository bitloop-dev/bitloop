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


PACK_NAME = "bitloop-project-tooling"
MANIFEST_NAME = "bitloop-tooling.json"
SCHEMA_VERSION = 1
CANONICAL_RELATIVE_PATH = Path("templates") / "default" / "tooling"
MANAGED_FILES = (
    "findBitloop.cmake",
    "toolchain/bitloop_provider.cmake",
    "toolchain/discovery_shims.cmake",
    "toolchain/discovery_toolchain.cmake",
    "toolchain/merge_vcpkg_manifests.cmake",
    "toolchain/vcpkg_autodetect.cmake",
    "triplets/wasm32-emscripten-multithreaded.cmake",
    "ports/.gitattributes",
    "ports/LICENSE.vcpkg.txt",
    "ports/README.md",
    "ports/dawn/000-fix-emdawnwebgpu.patch",
    "ports/dawn/001-fix-windows-build.patch",
    "ports/dawn/002-fix-uwp.patch",
    "ports/dawn/003-fix-d3d11.patch",
    "ports/dawn/004-deps.patch",
    "ports/dawn/005-bsd-support.patch",
    "ports/dawn/006-fix-x11-include-dirs.patch",
    "ports/dawn/007-stable-d3d12-shader-cache.patch",
    "ports/dawn/008-windows-dxc-package.patch",
    "ports/dawn/DawnConfig.cmake",
    "ports/dawn/portfile.cmake",
    "ports/dawn/unofficial_webgpu_dawn.pc.in",
    "ports/dawn/usage",
    "ports/dawn/vcpkg.json",
)


class ToolingPackError(RuntimeError):
    """Base error for invalid packs and unsafe synchronization."""


class ToolingPackConflict(ToolingPackError):
    """Raised when synchronization would overwrite local modifications."""


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


def canonical_pack_dir(bitloop_root: Path) -> Path:
    return bitloop_root.resolve() / CANONICAL_RELATIVE_PATH


def _managed_hashes(tooling_dir: Path) -> dict[str, str]:
    hashes: dict[str, str] = {}
    for relative in MANAGED_FILES:
        path = tooling_dir / relative
        if not path.is_file() or path.is_symlink():
            raise ToolingPackError(f"missing or unsafe managed file: {path}")
        hashes[relative] = _sha256(path)
    return hashes


def write_manifest(bitloop_root: Path, *, pack_revision: str) -> dict:
    source = canonical_pack_dir(bitloop_root)
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "pack_name": PACK_NAME,
        "pack_revision": pack_revision,
        "files": _managed_hashes(source),
    }
    (source / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8", newline="\n",
    )
    return manifest


def load_manifest(tooling_dir: Path) -> dict:
    path = tooling_dir / MANIFEST_NAME
    if not path.is_file():
        raise ToolingPackError(f"missing {MANIFEST_NAME}: {path}")

    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ToolingPackError(f"invalid {MANIFEST_NAME}: {exc}") from exc

    required = {"schema_version", "pack_name", "pack_revision", "files"}
    missing = sorted(required - manifest.keys())
    if missing:
        raise ToolingPackError(f"manifest is missing fields: {', '.join(missing)}")
    if manifest["schema_version"] != SCHEMA_VERSION:
        raise ToolingPackError(f"unsupported tooling-pack schema: {manifest['schema_version']}")
    if manifest["pack_name"] != PACK_NAME:
        raise ToolingPackError(f"unexpected tooling-pack name: {manifest['pack_name']}")
    if not isinstance(manifest["files"], dict) or not manifest["files"]:
        raise ToolingPackError("manifest files must be a non-empty object")

    for relative, digest in manifest["files"].items():
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise ToolingPackError(f"unsafe manifest path: {relative}")
        if not isinstance(digest, str) or len(digest) != 64:
            raise ToolingPackError(f"invalid SHA-256 for {relative}")
    return manifest


def validate_pack(tooling_dir: Path, manifest: dict | None = None) -> tuple[str, ...]:
    manifest = manifest or load_manifest(tooling_dir)
    issues: list[str] = []
    for relative, expected in sorted(manifest["files"].items()):
        path = tooling_dir / relative
        if not path.is_file() or path.is_symlink():
            issues.append(f"missing or unsafe managed file: {relative}")
        elif _sha256(path) != expected:
            issues.append(f"modified managed file: {relative}")
    return tuple(issues)


def _looks_like_project_root(path: Path) -> bool:
    cmake = path / "CMakeLists.txt"
    if not cmake.is_file():
        return False
    try:
        return "bitloop_new_project" in cmake.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return False


def resolve_tooling_project_root(project: Path | None = None, *, start: Path | None = None) -> Path:
    if project is not None:
        candidate = project.expanduser().resolve()
        if not _looks_like_project_root(candidate):
            raise ToolingPackError(f"not a Bitloop project root: {candidate}")
        return candidate

    current = (start or Path.cwd()).resolve()
    for candidate in (current, *current.parents):
        if _looks_like_project_root(candidate):
            return candidate
    raise ToolingPackError("could not find a Bitloop project root from the current directory")


def inspect_destination(destination: Path, source_manifest: dict) -> PackState:
    manifest_path = destination / MANIFEST_NAME
    if not manifest_path.exists():
        existing = [relative for relative in MANAGED_FILES if (destination / relative).exists()]
        if existing:
            return PackState("modified", ("managed tooling has no version manifest",))
        return PackState("missing")

    try:
        destination_manifest = load_manifest(destination)
        issues = validate_pack(destination, destination_manifest)
    except ToolingPackError as exc:
        return PackState("modified", (str(exc),))

    if issues:
        return PackState("modified", issues)
    if destination_manifest == source_manifest:
        return PackState("current")
    return PackState("stale")


def _replace_managed_files(source: Path, destination: Path, source_manifest: dict) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    transaction = destination.parent / f".{PACK_NAME}-{uuid.uuid4().hex}"
    staged = transaction / "staged"
    backup = transaction / "backup"
    staged.mkdir(parents=True)
    backup.mkdir(parents=True)

    try:
        old_manifest = load_manifest(destination) if (destination / MANIFEST_NAME).is_file() else None
    except ToolingPackError:
        old_manifest = None

    old_files = set(old_manifest["files"]) if old_manifest else set()
    new_files = set(source_manifest["files"])
    transaction_files = sorted(old_files | new_files | {MANIFEST_NAME})

    for relative in sorted(new_files):
        target = staged / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / relative, target)
    shutil.copy2(source / MANIFEST_NAME, staged / MANIFEST_NAME)

    backed_up: set[str] = set()
    installed: set[str] = set()
    try:
        for relative in transaction_files:
            target = destination / relative
            if target.exists():
                if not target.is_file() or target.is_symlink():
                    raise ToolingPackError(f"unsafe managed destination: {target}")
                saved = backup / relative
                saved.parent.mkdir(parents=True, exist_ok=True)
                os.replace(target, saved)
                backed_up.add(relative)

        for relative in sorted(new_files | {MANIFEST_NAME}):
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(staged / relative, target)
            installed.add(relative)
    except Exception:
        for relative in installed:
            target = destination / relative
            if target.exists():
                target.unlink()
        for relative in backed_up:
            saved = backup / relative
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(saved, target)
        raise
    finally:
        shutil.rmtree(transaction, ignore_errors=True)


def sync_tooling_pack(
    bitloop_root: Path,
    project_root: Path,
    *,
    check: bool = False,
    force: bool = False,
    output: TextIO | None = None,
) -> int:
    if check and force:
        raise ToolingPackError("--check and --force cannot be used together")

    source = canonical_pack_dir(bitloop_root)
    source_manifest = load_manifest(source)
    source_issues = validate_pack(source, source_manifest)
    if source_issues:
        raise ToolingPackError("canonical tooling pack is invalid:\n  " + "\n  ".join(source_issues))

    project_root = resolve_tooling_project_root(project_root)
    destination = project_root / "tooling"
    state = inspect_destination(destination, source_manifest)

    if check:
        if output is not None:
            print(f"{PACK_NAME}: {state.status}", file=output)
            for detail in state.details:
                print(f"  {detail}", file=output)
        return 0 if state.status == "current" else 1

    if state.status == "current":
        if output is not None:
            print(f"{PACK_NAME}: already current", file=output)
        return 0

    if state.status == "modified" and not force:
        details = "\n  ".join(state.details)
        raise ToolingPackConflict(
            "local changes exist in managed Bitloop tooling; rerun with --force"
            + (f":\n  {details}" if details else "")
        )

    _replace_managed_files(source, destination, source_manifest)
    if output is not None:
        action = "installed" if state.status == "missing" else "updated"
        print(f"{PACK_NAME}: {action} {destination}", file=output)
    return 0
