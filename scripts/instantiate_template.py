import sys
import argparse
import os
import shutil
from pathlib import Path
from typing import Optional

from agent_pack import init_agent_pack
from project_repository import (
    initialize_and_register_submodule,
    plan_project_submodule,
)
from tooling_pack import sync_tooling_pack

#PLACEHOLDER_IN_FILE  = "{SIM_NAME}"
TEXT_EXTENSIONS      = {".cpp", ".h", ".txt", ".json", ".md"}
TEMPLATE_TEXT_EXTENSIONS = TEXT_EXTENSIONS | {".cmake"}
GUIDE_BEGIN          = "BITLOOP_TEMPLATE_GUIDE_BEGIN"
GUIDE_ELSE           = "BITLOOP_TEMPLATE_GUIDE_ELSE"
GUIDE_END            = "BITLOOP_TEMPLATE_GUIDE_END"
AI_BEGIN             = "BITLOOP_TEMPLATE_AI_BEGIN"
AI_ELSE              = "BITLOOP_TEMPLATE_AI_ELSE"
AI_END               = "BITLOOP_TEMPLATE_AI_END"
AI_ONLY_FILES        = (Path("AGENTS.md"),)
#RENAME_TARGETS       = {"SIM_NAME.cpp", "SIM_NAME.h"}
#RENAME_FOLDER_TARGET = "SIM_NAME"

def replace_in_file(path:Path, placeholder:str, project_name:str) -> None:
    if path.suffix not in TEXT_EXTENSIONS:
        return
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return
    new_text = text.replace(placeholder, project_name)
    if new_text != text:
        path.write_text(new_text, encoding="utf-8")


def process_template_section(
    path: Path,
    *,
    enabled: bool,
    begin: str,
    otherwise: str,
    end: str,
) -> None:
    if path.suffix not in TEMPLATE_TEXT_EXTENSIONS:
        return

    begin_markers = {f"// {begin}", f"# {begin}"}
    otherwise_markers = {f"// {otherwise}", f"# {otherwise}"}
    end_markers = {f"// {end}", f"# {end}"}

    try:
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    except UnicodeDecodeError:
        return

    output: list[str] = []
    block_begin_line: int | None = None
    in_else_branch = False
    saw_marker = False

    for line_number, line in enumerate(lines, start=1):
        marker = line.strip()

        if marker in begin_markers:
            saw_marker = True
            if block_begin_line is not None:
                raise RuntimeError(
                    f"Nested {begin} in {path} at line {line_number}; "
                    f"the current block began at line {block_begin_line}."
                )
            block_begin_line = line_number
            in_else_branch = False
            continue

        if marker in otherwise_markers:
            saw_marker = True
            if block_begin_line is None:
                raise RuntimeError(f"Unmatched {otherwise} in {path} at line {line_number}.")
            if in_else_branch:
                raise RuntimeError(f"Duplicate {otherwise} in {path} at line {line_number}.")
            in_else_branch = True
            continue

        if marker in end_markers:
            saw_marker = True
            if block_begin_line is None:
                raise RuntimeError(f"Unmatched {end} in {path} at line {line_number}.")
            block_begin_line = None
            in_else_branch = False
            continue

        if begin in marker or otherwise in marker or end in marker:
            raise RuntimeError(f"Malformed {begin} marker in {path} at line {line_number}.")

        if (
            block_begin_line is None
            or (enabled and not in_else_branch)
            or (not enabled and in_else_branch)
        ):
            output.append(line)

    if block_begin_line is not None:
        raise RuntimeError(
            f"Unmatched {begin} in {path} at line {block_begin_line}."
        )

    if saw_marker:
        path.write_text("".join(output), encoding="utf-8")


def process_template_guidance(path: Path, *, guided: bool) -> None:
    process_template_section(
        path,
        enabled=guided,
        begin=GUIDE_BEGIN,
        otherwise=GUIDE_ELSE,
        end=GUIDE_END,
    )


def process_ai_scaffolding(path: Path, *, include_ai: bool) -> None:
    process_template_section(
        path,
        enabled=include_ai,
        begin=AI_BEGIN,
        otherwise=AI_ELSE,
        end=AI_END,
    )

def rename_file_if_needed(path:Path, project_name:str, TARGET:str, RENAME_TARGETS:set[str]) -> Path:
    if path.name not in RENAME_TARGETS:
        return path
    new_name = path.name.replace(TARGET, project_name)
    new_path = path.with_name(new_name)
    if new_path.exists():
        raise FileExistsError(f"Cannot rename '{path}' to '{new_path}': destination exists.")
    path.rename(new_path)
    return new_path

#def rename_folder_if_needed(path:Path, project_name:str, RENAME_FOLDER_TARGET:str) -> Path:
#    if path.name != RENAME_FOLDER_TARGET:
#        return path
#    new_name = project_name
#    new_path = path.with_name(new_name)
#    if new_path.exists():
#        raise FileExistsError(f"Cannot rename folder '{path}' to '{new_path}': destination exists.")
#    path.rename(new_path)
#    return new_path

def _on_rm_error(func, path, exc_info):
    # Make read-only paths writable, then retry
    try:
        os.chmod(path, stat.S_IWRITE)
    except Exception:
        pass
    func(path)

def _merge_move(src: Path, dst: Path) -> None:
    dst.mkdir(parents=True, exist_ok=True)
    for item in src.iterdir():
        d = dst / item.name
        if item.is_dir():
            # If a file is in the way, remove it
            if d.exists() and d.is_file():
                d.unlink()
            _merge_move(item, d)
        else:
            # If a dir is in the way, remove it
            if d.exists() and d.is_dir():
                shutil.rmtree(d, onerror=_on_rm_error)
            # Move (atomic on same volume)
            item.replace(d)
    # Remove now-empty src (rmdir if empty, else rmtree)
    try:
        src.rmdir()
    except OSError:
        shutil.rmtree(src, onerror=_on_rm_error)

def rename_folder_if_needed(path: Path, project_name: str, target: str) -> Path:
    if path.name != target:
        return path

    new_path = path.with_name(project_name)
    if new_path == path:
        return path  # nothing to do

    # Fast path: try plain rename when destination doesn't exist
    if not new_path.exists():
        try:
            path.rename(new_path)
            return new_path
        except (PermissionError, OSError):
            # Fall through to merge on failure
            pass

    # Destination exists or rename failed: merge contents into new_path
    _merge_move(path, new_path)
    return new_path

def copy_template_files(src: Path, dst: Path, *, merge: bool = False) -> None:
    if not merge:
        shutil.copytree(src, dst)  # requires dst not to exist
        return

    dst.mkdir(parents=True, exist_ok=True)
    for entry in src.iterdir():
        s = entry
        d = dst / entry.name
        if entry.is_dir():
            shutil.copytree(s, d, dirs_exist_ok=True)  # Python 3.8+
        else:
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(s, d)

###############################################

def prompt_for_minimal_project() -> bool:
    print()
    print("Project variant")
    print()
    print("  1. Guided — Includes inline explanations and example code")
    print("  2. Minimal — Includes only the essential project scaffold")
    print()

    while True:
        choice = input("Variant [Guided]: ").strip().casefold()
        if choice in {"", "1", "g", "guided"}:
            return False
        if choice in {"2", "m", "minimal"}:
            return True
        print("Please choose Guided or Minimal.")


def prompt_for_ai_scaffolding() -> bool:
    while True:
        choice = input(
            "Include agent pack and scaffolding for AI-assisted development? [Y/n]: "
        ).strip().casefold()
        if choice in {"", "y", "yes"}:
            return True
        if choice in {"n", "no"}:
            return False
        print("Please choose Yes or No.")


def create_project(
    bitloop_root: Path,
    name: Optional[str] = None,
    *,
    minimal: bool = False,
    include_ai: bool | None = None,
    submodule: bool | None = None,
    remote: str | None = None,
):
    invocation_dir = Path.cwd().resolve()
    interactive = name is None
    prompt_for_variant = interactive and not minimal
    if name is None:
        print()
        print("New Bitloop project")
        print()
        name = input("Project name: ")
    name = name.strip()
    if not name:
        print("Project name cannot be empty.", file=sys.stderr)
        sys.exit(1)

    if prompt_for_variant:
        minimal = prompt_for_minimal_project()
    if include_ai is None:
        include_ai = prompt_for_ai_scaffolding() if interactive else True

    src:Path = bitloop_root / "templates" / "default"
    dst:Path = invocation_dir / name

    submodule_plan = plan_project_submodule(
        invocation_dir, dst, interactive=interactive, submodule=submodule, remote=remote,
    )

    print()
    print("Creating project")
    print(f"  Template:    {src}")
    print(f"  Destination: {dst}")
    print()

    if not src.is_dir():
        raise SystemExit(f"Template not found: {src}")
    if dst.exists():
        raise SystemExit(f"Destination already exists: {dst}")

    copy_template_files(src, dst)

    if not include_ai:
        for relative in AI_ONLY_FILES:
            (dst / relative).unlink(missing_ok=True)

    # Resolve optional blocks and replace placeholders in supported text files.
    for root, dirs, files in os.walk(dst):
        for fname in files:
            fpath = Path(root) / fname
            process_template_guidance(fpath, guided=not minimal)
            process_ai_scaffolding(fpath, include_ai=include_ai)
            replace_in_file(fpath, "{SIM_NAME}", name)

    # Replace only in files with right extensions
    for root, _, files in os.walk(dst):
        for fname in files:
            fpath = Path(root) / fname
            rename_file_if_needed(fpath, name, "SIM_NAME", {"SIM_NAME.cpp", "SIM_NAME.h"})

    # Rename folders called SIM_NAME (deepest first so paths don't break)
    dir_paths = []
    for root, dirs, _ in os.walk(dst):
        for d in dirs:
            dir_paths.append(Path(root) / d)
    dir_paths.sort(key=lambda p: len(p.parts), reverse=True)
    for dpath in dir_paths:
        if dpath.exists():
            rename_folder_if_needed(dpath, name, "SIM_NAME")

    # Validate generated CMake tooling and install optional AI guidance.
    sync_tooling_pack(bitloop_root, dst)
    if include_ai:
        init_agent_pack(bitloop_root, dst)
    if submodule_plan is not None:
        print()
        initialize_and_register_submodule(submodule_plan, name, output=sys.stdout)

def create_header(bitloop_root:Path):
    name = input("Enter header name: ").strip()
    if not name:
        print("Header name cannot be empty.", file=sys.stderr)
        sys.exit(1)

    src:Path = bitloop_root / "framework" / "templates" / "header"
    dst:Path = Path.cwd() 

    print(f"src = {src} dst = {dst}")
    print(f"Creating new header '{name}' at {dst}")

    if not src.is_dir():
        raise SystemExit(f"Template not found: {src}")

    copy_template_files(src, dst, merge=True)

    # Replace only in files with right extensions
    for root, dirs, files in os.walk(dst):
        for fname in files:
            fpath = Path(root) / fname
            replace_in_file(fpath, "{HEADER_NAME}", name)

    # Rename only HEADER_NAME.h
    for root, _, files in os.walk(dst):
        for fname in files:
            fpath = Path(root) / fname
            rename_file_if_needed(fpath, name, "HEADER_NAME", {"HEADER_NAME.h"})

    # Rename folders called HEADER_NAME (deepest first so paths don't break)
    dir_paths = []
    for root, dirs, _ in os.walk(dst):
        for d in dirs:
            dir_paths.append(Path(root) / d)
    dir_paths.sort(key=lambda p: len(p.parts), reverse=True)
    for dpath in dir_paths:
        if dpath.exists():
            rename_folder_if_needed(dpath, name, "HEADER_NAME")


def create_class(bitloop_root:Path):
    name = input("Enter class name: ").strip()
    if not name:
        print("Class name cannot be empty.", file=sys.stderr)
        sys.exit(1)

    src:Path = bitloop_root / "framework" / "templates" / "class"
    dst:Path = Path.cwd() 

    print(f"src = {src} dst = {dst}")
    print(f"Creating new header '{name}' at {dst}")

    if not src.is_dir():
        raise SystemExit(f"Template not found: {src}")

    copy_template_files(src, dst, merge=True)

    # Replace only in files with right extensions
    for root, dirs, files in os.walk(dst):
        for fname in files:
            fpath = Path(root) / fname
            replace_in_file(fpath, "{CLASS_NAME}", name)

    # Rename only CLASS_NAME.h
    for root, _, files in os.walk(dst):
        for fname in files:
            fpath = Path(root) / fname
            rename_file_if_needed(fpath, name, "CLASS_NAME", {"CLASS_NAME.h", "CLASS_NAME.cpp"})

    # Rename folders called CLASS_NAME (deepest first so paths don't break)
    dir_paths = []
    for root, dirs, _ in os.walk(dst):
        for d in dirs:
            dir_paths.append(Path(root) / d)
    dir_paths.sort(key=lambda p: len(p.parts), reverse=True)
    for dpath in dir_paths:
        if dpath.exists():
            rename_folder_if_needed(dpath, name, "CLASS_NAME")

###############################################

def instantiate_template(
    bitloop_root: Path,
    type: str,
    name: Optional[str] = None,
    *,
    minimal: bool = False,
    include_ai: bool | None = None,
    submodule: bool | None = None,
    remote: str | None = None,
):
    if type == "project":
        create_project(
            bitloop_root,
            name,
            minimal=minimal,
            include_ai=include_ai,
            submodule=submodule,
            remote=remote,
        )
    elif type == "header":
        create_header(bitloop_root)
    elif type == "class":
        create_class(bitloop_root)
    else:
        raise SystemExit(f"Unknown template type: {type}")
