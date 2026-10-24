#!/usr/bin/env python3
import sys
import argparse
import platform
import os
import subprocess
from pathlib import Path

script_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(script_dir))

from instantiate_template import instantiate_template
from copy_project         import ProjectCopyError, copy_project
from project_repository import ProjectRepositoryError
from bootstrap            import run_bootstrap
from agent_pack           import (
    AgentPackError,
    init_agent_pack,
    resolve_project_root,
    resolve_recursive_root,
    sync_agent_pack,
    sync_agent_packs_recursive,
)
from tooling_pack         import (
    ToolingPackError,
    resolve_tooling_project_root,
    sync_tooling_pack,
)

TRACE_RECONSTRUCT_TARGET = "reconstruct_full_frame"

def _tool_exe_name(tool_name):
    return f"{tool_name}.exe" if os.name == "nt" else tool_name

def _unique_append(paths, seen, path):
    try:
        key = str(path.resolve())
    except OSError:
        key = str(path.absolute())
    if key not in seen:
        seen.add(key)
        paths.append(path)

def _anchor_roots(frames_file):
    anchors = []
    if frames_file:
        anchors.append(Path(frames_file).expanduser())
    anchors.extend([Path.cwd(), script_dir.parent, script_dir.parent.parent])

    roots = []
    seen = set()
    for anchor in anchors:
        try:
            resolved = anchor.resolve()
        except OSError:
            resolved = anchor.absolute()

        if resolved.suffix and not resolved.is_dir():
            resolved = resolved.parent

        for root in [resolved, *resolved.parents]:
            _unique_append(roots, seen, root)
    return roots

def _find_reconstruct_exe(frames_file):
    exe_name = _tool_exe_name(TRACE_RECONSTRUCT_TARGET)
    candidates = []
    seen = set()

    env_path = os.environ.get("BITLOOP_RECONSTRUCT_FULL_FRAME")
    if env_path:
        _unique_append(candidates, seen, Path(env_path).expanduser())

    bitloop_root = script_dir.parent
    for direct in [
        bitloop_root / "bin" / exe_name,
        bitloop_root / exe_name,
        script_dir / exe_name,
    ]:
        _unique_append(candidates, seen, direct)

    for root in _anchor_roots(frames_file):
        for pattern in [
            f"build/*/_bitloop/tools/{exe_name}",
            f"build/*/tooling/{exe_name}",
            f"out/build/*/_bitloop/tools/{exe_name}",
            f"out/build/*/tooling/{exe_name}",
        ]:
            for match in root.glob(pattern):
                _unique_append(candidates, seen, match)

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None

def _configured_build_dirs(frames_file):
    build_dirs = []
    seen = set()

    for root in _anchor_roots(frames_file):
        if (root / "CMakeCache.txt").is_file():
            _unique_append(build_dirs, seen, root)
        for pattern in ["build/*/CMakeCache.txt", "out/build/*/CMakeCache.txt"]:
            for cache_file in root.glob(pattern):
                _unique_append(build_dirs, seen, cache_file.parent)

    return build_dirs

def _try_build_reconstruct_tool(frames_file):
    for build_dir in _configured_build_dirs(frames_file):
        cmd = ["cmake", "--build", str(build_dir), "--target", TRACE_RECONSTRUCT_TARGET]
        rc = subprocess.call(cmd)
        if rc == 0:
            exe = _find_reconstruct_exe(frames_file)
            if exe:
                return exe
    return None

def _reconstruct_args(args):
    forwarded = ["--file", args.file, "--frame", str(args.frame)]
    if args.metadata:
        forwarded.extend(["--metadata", args.metadata])
    if args.index:
        forwarded.extend(["--index", args.index])
    if args.scene:
        forwarded.extend(["--scene", args.scene])
    if args.no_index:
        forwarded.append("--no-index")
    if args.pretty:
        forwarded.append("--pretty")
    return forwarded

def run_trace_reconstruct(args):
    exe = _find_reconstruct_exe(args.file)
    if exe is None and not args.no_build:
        exe = _try_build_reconstruct_tool(args.file)

    if exe is None:
        print("bitloop trace reconstruct: could not find reconstruct_full_frame.", file=sys.stderr)
        print("Build it with: cmake --build <build-dir> --target reconstruct_full_frame", file=sys.stderr)
        print("Or set BITLOOP_RECONSTRUCT_FULL_FRAME to the executable path.", file=sys.stderr)
        return 1

    return subprocess.call([str(exe)] + _reconstruct_args(args))

def add_project_git_arguments(parser):
    submodule_mode = parser.add_mutually_exclusive_group()
    submodule_mode.add_argument(
        "--submodule",
        dest="submodule",
        action="store_true",
        default=None,
        help="initialize, publish, and register the project as a submodule",
    )
    submodule_mode.add_argument(
        "--no-submodule",
        dest="submodule",
        action="store_false",
        help="do not offer or perform submodule registration",
    )
    parser.add_argument("--remote", help="empty remote repository URL; implies --submodule")

def main():
    parser = argparse.ArgumentParser(prog="bitloop")

    # "[bitloop] ..." parser
    subparsers = parser.add_subparsers(dest="command", required=True)

    # "bitloop [...]" subparsers
    new_parser			= subparsers.add_parser("new",			help="scaffold something")
    copy_parser         = subparsers.add_parser("copy",        help="copy an existing project")
    bootstrap_parser	= subparsers.add_parser("bootstrap",	help="install bootstrap dependencies (Linux only)")
    vcpkg_parser        = subparsers.add_parser("vcpkg",        help="invoke bundled vcpkg")
    trace_parser        = subparsers.add_parser("trace",        help="inspect simulation trace output")
    agents_parser       = subparsers.add_parser("agents",       help="manage project-local Codex guidance")
    tooling_parser      = subparsers.add_parser("tooling",      help="manage project-local CMake tooling")

    vcpkg_parser.add_argument("vcpkg_args", nargs=argparse.REMAINDER,
                          help="arguments to pass through to vcpkg")

    trace_sub = trace_parser.add_subparsers(dest="trace_command", required=True)
    reconstruct_parser = trace_sub.add_parser("reconstruct", help="reconstruct one full frame from a trace")
    reconstruct_parser.add_argument("--file", required=True, help="frames.jsonl path")
    reconstruct_parser.add_argument("--frame", required=True, help="frame index to reconstruct")
    reconstruct_parser.add_argument("--metadata", help="metadata.json path; defaults beside frames file")
    reconstruct_parser.add_argument("--index", help="sidecar index path; defaults beside frames file")
    reconstruct_parser.add_argument("--scene", help="scene id or name to include")
    reconstruct_parser.add_argument("--no-index", action="store_true", help="scan from the beginning and do not create/use an index")
    reconstruct_parser.add_argument("--pretty", action="store_true", help="pretty-print JSON output")
    reconstruct_parser.add_argument("--no-build", action="store_true", help="do not try to build the helper if it is missing")

    agents_sub = agents_parser.add_subparsers(dest="agents_command", required=True)
    agents_init_parser = agents_sub.add_parser("init", help="install the managed Bitloop development skill")
    agents_init_parser.add_argument("--project", type=Path, help="Bitloop project root; defaults to the nearest root")
    agents_sync_parser = agents_sub.add_parser("sync", help="synchronize the managed Bitloop development skill")
    agents_sync_parser.add_argument("--project", type=Path, help="project root, or search root with --recursive")
    agents_sync_parser.add_argument(
        "--recursive",
        action="store_true",
        help="process already-managed projects recursively without installing new packs",
    )
    agents_mode = agents_sync_parser.add_mutually_exclusive_group()
    agents_mode.add_argument("--check", action="store_true", help="report status without writing")
    agents_mode.add_argument("--force", action="store_true", help="replace a locally modified generated pack")

    tooling_sub = tooling_parser.add_subparsers(dest="tooling_command", required=True)
    tooling_sync_parser = tooling_sub.add_parser("sync", help="synchronize managed project CMake tooling")
    tooling_sync_parser.add_argument("--project", type=Path, help="Bitloop project root; defaults to the nearest root")
    tooling_mode = tooling_sync_parser.add_mutually_exclusive_group()
    tooling_mode.add_argument("--check", action="store_true", help="report status without writing")
    tooling_mode.add_argument("--force", action="store_true", help="replace locally modified managed tooling")

    # bitloop new [...] parser
    new_sub = new_parser.add_subparsers(dest="entity", required=True)
    project_parser = new_sub.add_parser("project", help="scaffold a new project")
    project_parser.add_argument("name", nargs="?", help="new project name")
    project_parser.add_argument(
        "--minimal",
        action="store_true",
        help="omit inline guidance and example code",
    )
    project_parser.add_argument(
        "--no-ai",
        dest="include_ai",
        action="store_false",
        default=None,
        help="omit the agent pack and AI-oriented tracing/checkpoint scaffolding",
    )
    add_project_git_arguments(project_parser)
    new_sub.add_parser("header", help="add a header (.h)")
    new_sub.add_parser("class", help="add a class (.h  + .cpp) ")

    copy_sub = copy_parser.add_subparsers(dest="entity", required=True)
    copy_project_parser = copy_sub.add_parser("project", help="copy and rename a project in the current directory")
    copy_project_parser.add_argument("source_name", nargs="?", help="source project name in the current directory")
    copy_project_parser.add_argument("name", nargs="?", help="new project name")
    add_project_git_arguments(copy_project_parser)

    args = parser.parse_args()

    if args.command == "bootstrap":
        run_bootstrap()
        return

    if args.command == "vcpkg":
        bitloop_root = script_dir.parent
        exe_name = "vcpkg.exe" if os.name == "nt" else "vcpkg"
        vcpkg_exe = bitloop_root / "vcpkg" / exe_name

        forward = list(args.vcpkg_args or [])
        if forward and forward[0] == "--":
            forward = forward[1:]

        cmd = [str(vcpkg_exe)] + (forward if forward else ["--help"])

        rc = subprocess.call(cmd, cwd=str(vcpkg_exe.parent))
        sys.exit(rc)
        return

    if args.command == "agents":
        try:
            bitloop_root = script_dir.parent
            if args.agents_command == "init":
                project_root = resolve_project_root(args.project)
                sys.exit(
                    init_agent_pack(
                        bitloop_root,
                        project_root,
                        output=sys.stdout,
                    )
                )
            if args.recursive:
                root = resolve_recursive_root(args.project)
                sys.exit(
                    sync_agent_packs_recursive(
                        bitloop_root,
                        root,
                        check=args.check,
                        force=args.force,
                        output=sys.stdout,
                    )
                )
            project_root = resolve_project_root(args.project)
            sys.exit(
                sync_agent_pack(
                    bitloop_root,
                    project_root,
                    check=args.check,
                    force=args.force,
                    output=sys.stdout,
                )
            )
        except AgentPackError as exc:
            print(f"bitloop agents {args.agents_command}: {exc}", file=sys.stderr)
            sys.exit(2)
        return

    if args.command == "tooling":
        if args.tooling_command == "sync":
            try:
                project_root = resolve_tooling_project_root(args.project)
                bitloop_root = script_dir.parent
                sys.exit(
                    sync_tooling_pack(
                        bitloop_root,
                        project_root,
                        check=args.check,
                        force=args.force,
                        output=sys.stdout,
                    )
                )
            except ToolingPackError as exc:
                print(f"bitloop tooling sync: {exc}", file=sys.stderr)
                sys.exit(2)
        return

    if args.command == "trace":
        if args.trace_command == "reconstruct":
            sys.exit(run_trace_reconstruct(args))
        return

    if args.command == "copy":
        try:
            copy_project(args.source_name, args.name, submodule=args.submodule, remote=args.remote)
        except (ProjectCopyError, ProjectRepositoryError, OSError) as exc:
            print(f"bitloop copy project: {exc}", file=sys.stderr)
            sys.exit(2)
        return

    if args.command == "new":
        new_type = getattr(args, "entity", None)
        if new_type == "project":
            bitloop_root = script_dir.parent
            try:
                instantiate_template(
                    bitloop_root=bitloop_root,
                    type="project",
                    name=args.name,
                    minimal=args.minimal,
                    include_ai=args.include_ai,
                    submodule=args.submodule,
                    remote=args.remote,
                )
            except ProjectRepositoryError as exc:
                print(f"bitloop new project: {exc}", file=sys.stderr)
                sys.exit(2)
            return
        elif new_type == "header":
            bitloop_root = script_dir.parent
            instantiate_template(bitloop_root=bitloop_root, type="header")
            return
        elif new_type == "class":
            bitloop_root = script_dir.parent
            instantiate_template(bitloop_root=bitloop_root, type="class")
            return


if __name__ == "__main__":
    main()
