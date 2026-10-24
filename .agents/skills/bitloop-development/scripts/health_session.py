#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Sequence


MARKER_NAME = ".bitloop-health-session.json"
SCHEMA_VERSION = 1
SESSION_ENVIRONMENT = "BITLOOP_HEALTH_SESSION"
LABEL_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,47}")


class HealthSessionError(RuntimeError):
    pass


def _resolved(path: Path) -> Path:
    return path.expanduser().resolve()


def _validate_label(label: str) -> str:
    if not LABEL_PATTERN.fullmatch(label):
        raise HealthSessionError("label must be 1-48 letters, digits, dots, underscores, or hyphens")
    return label


def create_session(root: Path, label: str = "session") -> Path:
    root = _resolved(root)
    _validate_label(label)
    root.mkdir(parents=True, exist_ok=True)
    if not root.is_dir():
        raise HealthSessionError(f"session root is not a directory: {root}")

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
    session_id = uuid.uuid4().hex
    session = root / f"{label}_{timestamp}_{session_id[:8]}"
    session.mkdir()
    for child in ("monitors", "raw", "analysis"):
        (session / child).mkdir()

    marker = {
        "schema_version": SCHEMA_VERSION,
        "session_id": session_id,
        "label": label,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "root": str(root),
        "session": str(session),
    }
    (session / MARKER_NAME).write_text(json.dumps(marker, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return session


def load_session(session: Path) -> tuple[Path, dict]:
    requested = session.expanduser().absolute()
    if requested.is_symlink():
        raise HealthSessionError(f"session path may not be a symlink: {requested}")

    session = requested.resolve()
    marker_path = session / MARKER_NAME
    if marker_path.is_symlink() or not marker_path.is_file():
        raise HealthSessionError(f"not a marked Bitloop health session: {session}")

    try:
        marker = json.loads(marker_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise HealthSessionError(f"invalid health-session marker: {exc}") from exc

    required = {"schema_version", "session_id", "label", "created_utc", "root", "session"}
    if not isinstance(marker, dict) or required - marker.keys():
        raise HealthSessionError("health-session marker is missing required fields")
    if not isinstance(marker["schema_version"], int) or isinstance(marker["schema_version"], bool) or marker["schema_version"] != SCHEMA_VERSION:
        raise HealthSessionError(f"unsupported health-session schema: {marker['schema_version']}")
    if not all(isinstance(marker[field], str) for field in ("session_id", "label", "created_utc", "root", "session")):
        raise HealthSessionError("health-session marker fields have invalid types")
    _validate_label(marker["label"])

    recorded_root = _resolved(Path(marker["root"]))
    recorded_session = _resolved(Path(marker["session"]))
    if recorded_session != session or session.parent != recorded_root:
        raise HealthSessionError("health-session marker does not own this exact directory")
    return session, marker


def clean_session(session: Path) -> Path:
    session, _ = load_session(session)
    shutil.rmtree(session)
    return session


def execute_session(session: Path, command: Sequence[str], cwd: Path | None = None) -> int:
    session, _ = load_session(session)
    command = list(command)
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        raise HealthSessionError("exec requires a command after '--'")

    command = [str(session) if argument == "{session}" else argument for argument in command]
    environment = os.environ.copy()
    environment[SESSION_ENVIRONMENT] = str(session)
    working_directory = _resolved(cwd) if cwd is not None else None
    return subprocess.run(command, cwd=working_directory, env=environment, shell=False, check=False).returncode


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Manage disposable Bitloop simulation-health sessions.")
    commands = parser.add_subparsers(dest="action", required=True)

    create = commands.add_parser("create", help="create a marked session under an explicit scratch root")
    create.add_argument("--root", type=Path, required=True)
    create.add_argument("--label", default="session")

    info = commands.add_parser("info", help="validate and print a session marker")
    info.add_argument("--session", type=Path, required=True)

    clean = commands.add_parser("clean", help="delete one exact marker-owned session")
    clean.add_argument("--session", type=Path, required=True)

    execute = commands.add_parser("exec", help="run a command with BITLOOP_HEALTH_SESSION set")
    execute.add_argument("--session", type=Path, required=True)
    execute.add_argument("--cwd", type=Path)
    execute.add_argument("command", nargs=argparse.REMAINDER)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.action == "create":
            print(create_session(arguments.root, arguments.label))
            return 0
        if arguments.action == "info":
            _, marker = load_session(arguments.session)
            print(json.dumps(marker, indent=2, sort_keys=True))
            return 0
        if arguments.action == "clean":
            print(clean_session(arguments.session))
            return 0
        if arguments.action == "exec":
            return execute_session(arguments.session, arguments.command, arguments.cwd)
    except (HealthSessionError, OSError) as exc:
        print(f"health_session: error: {exc}", file=sys.stderr)
        return 2
    raise AssertionError(f"unhandled action: {arguments.action}")


if __name__ == "__main__":
    raise SystemExit(main())
