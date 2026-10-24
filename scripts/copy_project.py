from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from project_repository import initialize_and_register_submodule, plan_project_submodule


SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


class ProjectCopyError(RuntimeError):
    """Raised when a project cannot be copied safely."""


def _git(arguments: list[str], *, cwd: Path, input: bytes | None = None, accepted=(0,)) -> bytes:
    try:
        result = subprocess.run(["git", *arguments], cwd=cwd, input=input, capture_output=True, check=False)
    except FileNotFoundError as exc:
        raise ProjectCopyError("Git is not installed or is not available on PATH") from exc
    if result.returncode not in accepted:
        detail = (result.stderr or result.stdout).decode("utf-8", errors="replace").strip()
        raise ProjectCopyError(detail or f"Git exited with status {result.returncode}")
    return result.stdout


def _copy_files(source: Path, destination: Path, git_dir: Path, source_name: str, name: str) -> None:
    # An empty, separate index makes ignore rules independent of the source's Git status.
    _git(["init", "--bare", "--quiet", "--template=", str(git_dir)], cwd=source)
    git_options = [f"--git-dir={git_dir}", f"--work-tree={source}", "-c", f"core.excludesFile={os.devnull}"]
    mentions = re.compile(rf"\b{re.escape(source_name)}(?=\b|_(?:Project|Scene)\b)")

    def ignored(directory: str, names: list[str]) -> set[str]:
        excluded = {entry for entry in names if entry.casefold() == ".git"}
        candidates = {
            (Path(directory) / entry).relative_to(source).as_posix(): entry
            for entry in names if entry not in excluded
        }
        if candidates:
            matches = _git(
                [*git_options, "check-ignore", "--no-index", "-z", "--stdin"],
                cwd=source,
                input=b"".join(os.fsencode(path) + b"\0" for path in candidates),
                accepted=(0, 1),
            )
            excluded.update(candidates[os.fsdecode(path)] for path in matches.split(b"\0") if path)
        return excluded

    def copy_file(src: str, dst: str) -> str:
        data = Path(src).read_bytes()
        if b"\0" not in data:
            try:
                text = data.decode("utf-8")
            except UnicodeDecodeError:
                pass
            else:
                data = mentions.sub(name, text).encode("utf-8")
        Path(dst).write_bytes(data)
        shutil.copystat(src, dst)
        return dst

    shutil.copytree(source, destination, ignore=ignored, copy_function=copy_file, symlinks=True)

    for directory, _, files in os.walk(destination):
        for filename in files:
            path = Path(directory) / filename
            if path.stem == source_name and path.suffix.lower() in SOURCE_EXTENSIONS:
                _rename(path, path.with_name(name + path.suffix))

    for path in destination.iterdir():
        if path.name == source_name and path.is_dir() and not path.is_symlink():
            _rename(path, destination / name)


def _rename(source: Path, destination: Path) -> None:
    if destination.exists() or destination.is_symlink():
        raise ProjectCopyError(f"cannot rename {source.name} to {destination.name}: destination already exists")
    source.rename(destination)


def copy_project(
    source_name: str | None = None,
    name: str | None = None,
    *,
    submodule: bool | None = None,
    remote: str | None = None,
) -> None:
    invocation_dir = Path.cwd().resolve()
    interactive = source_name is None or name is None
    if interactive:
        print()
        print("Copy Bitloop project")
        print()
    if source_name is None:
        source_name = input("Source project name: ")
    if name is None:
        name = input("New project name: ")
    source_name, name = source_name.strip(), name.strip()
    for label, value in (("Source", source_name), ("New", name)):
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value):
            raise ProjectCopyError(f"{label} project name must be a C++ identifier, not a path: {value!r}")

    source = invocation_dir / source_name
    destination = invocation_dir / name
    if not source.is_dir() or not (source / "CMakeLists.txt").is_file():
        raise ProjectCopyError(f"source project not found (expected a directory with CMakeLists.txt): {source}")
    if source.resolve() in (invocation_dir, *invocation_dir.parents):
        raise ProjectCopyError("source project cannot contain the current directory")
    if destination.exists() or destination.is_symlink():
        raise ProjectCopyError(f"destination already exists: {destination}")

    submodule_plan = plan_project_submodule(
        invocation_dir, destination, interactive=interactive, submodule=submodule, remote=remote,
    )
    print()
    print("Copying project")
    print(f"  Source:      {source}")
    print(f"  Destination: {destination}")
    print()

    # Finish copying and renaming before exposing the new project or publishing it.
    with tempfile.TemporaryDirectory(prefix=".bitloop-copy-", dir=invocation_dir) as temporary:
        staging = Path(temporary)
        project = staging / name
        _copy_files(source, project, staging / "ignore.git", source_name, name)
        _rename(project, destination)

    if submodule_plan is not None:
        initialize_and_register_submodule(submodule_plan, name, output=sys.stdout)
