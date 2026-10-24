from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import TextIO


ROOT_VALUE_PATTERN = re.compile(r"(?im)^[ \t]*root[ \t]*=[ \t]*([^ \t\r\n#]+)")
TRUE_VALUES = {"1", "on", "true", "yes"}


class ProjectRepositoryError(RuntimeError):
    """Raised when project Git integration cannot proceed safely."""


@dataclass(frozen=True)
class RepositoryContext:
    owner_root: Path
    workspace_root: Path | None

    @property
    def owner_is_workspace_root(self) -> bool:
        return self.workspace_root is not None and self.owner_root == self.workspace_root


@dataclass(frozen=True)
class SubmodulePlan:
    context: RepositoryContext
    destination: Path
    relative_path: Path
    remote: str


def _run(
    arguments: list[str],
    *,
    cwd: Path,
    accepted_returncodes: tuple[int, ...] = (0,),
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            arguments,
            cwd=str(cwd),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
    except FileNotFoundError as exc:
        raise ProjectRepositoryError("Git is not installed or is not available on PATH") from exc

    if result.returncode not in accepted_returncodes:
        detail = (result.stderr or result.stdout).strip()
        raise ProjectRepositoryError(detail or f"command exited with status {result.returncode}")
    return result


def _git(
    arguments: list[str],
    *,
    cwd: Path,
    accepted_returncodes: tuple[int, ...] = (0,),
) -> subprocess.CompletedProcess[str]:
    return _run(["git", *arguments], cwd=cwd, accepted_returncodes=accepted_returncodes)


def find_root_workspace(start: Path) -> Path | None:
    current = start.expanduser().resolve()
    if not current.is_dir():
        current = current.parent

    for candidate in (current, *current.parents):
        marker = candidate / ".bitloop-workspace"
        if not marker.is_file():
            continue
        try:
            match = ROOT_VALUE_PATTERN.search(marker.read_text(encoding="utf-8", errors="replace"))
        except OSError as exc:
            raise ProjectRepositoryError(f"could not read workspace marker {marker}: {exc}") from exc
        if match and match.group(1).casefold() in TRUE_VALUES:
            return candidate
    return None


def discover_repository_context(start: Path) -> RepositoryContext | None:
    start = start.expanduser().resolve()
    result = _git(
        ["rev-parse", "--show-toplevel"],
        cwd=start,
        accepted_returncodes=(0, 128),
    )
    if result.returncode != 0:
        return None

    owner_root = Path(result.stdout.strip()).resolve()
    try:
        start.relative_to(owner_root)
    except ValueError as exc:
        raise ProjectRepositoryError(
            f"Git reported repository {owner_root}, but {start} is outside that worktree"
        ) from exc

    return RepositoryContext(
        owner_root=owner_root,
        workspace_root=find_root_workspace(start),
    )


def prompt_for_submodule(context: RepositoryContext) -> bool:
    print()
    print("Git integration")
    print()
    if context.workspace_root is not None:
        print(f"  Workspace root:       {context.workspace_root}")
    print(f"  Enclosing repository: {context.owner_root}")
    print()

    default_yes = context.owner_is_workspace_root
    if default_yes:
        prompt = f"Add project as a direct submodule of {context.owner_root.name}? [Y/n]: "
    else:
        prompt = f"Create project as an independent child submodule of {context.owner_root.name}? [y/N]: "

    while True:
        choice = input(prompt).strip().casefold()
        if choice == "":
            return default_yes
        if choice in {"y", "yes"}:
            return True
        if choice in {"n", "no"}:
            return False
        print("Please choose Yes or No.")


def prompt_for_remote() -> str:
    while True:
        remote = input("Remote repository URL (must already exist and be empty): ").strip()
        if remote:
            return remote
        print("A remote repository URL is required.")


def plan_project_submodule(
    invocation_dir: Path,
    destination: Path,
    *,
    interactive: bool,
    submodule: bool | None,
    remote: str | None,
) -> SubmodulePlan | None:
    context = discover_repository_context(invocation_dir)
    if remote is not None:
        if submodule is False:
            raise ProjectRepositoryError("--remote cannot be combined with --no-submodule")
        submodule = True
    if submodule is None:
        submodule = prompt_for_submodule(context) if interactive and context is not None else False
    if submodule and context is None:
        raise ProjectRepositoryError(
            f"cannot add a submodule because {invocation_dir} is not inside a Git worktree"
        )
    if submodule and remote is None:
        if not interactive:
            raise ProjectRepositoryError("--remote is required with --submodule")
        remote = prompt_for_remote()

    return (
        prepare_submodule_plan(context, destination, remote)
        if submodule and context is not None and remote is not None
        else None
    )


def prepare_submodule_plan(
    context: RepositoryContext,
    destination: Path,
    remote: str,
) -> SubmodulePlan:
    destination = destination.expanduser().resolve()
    remote = remote.strip()
    if not remote:
        raise ProjectRepositoryError("a remote repository URL is required for submodule creation")
    if destination.exists():
        raise ProjectRepositoryError(f"destination already exists: {destination}")

    try:
        relative_path = destination.relative_to(context.owner_root)
    except ValueError as exc:
        raise ProjectRepositoryError(
            f"destination {destination} is outside enclosing repository {context.owner_root}"
        ) from exc
    if relative_path == Path("."):
        raise ProjectRepositoryError("a repository cannot be added as a submodule of itself")

    _git(["rev-parse", "--verify", "HEAD^{commit}"], cwd=context.owner_root)

    relative_text = relative_path.as_posix()
    ignored = _git(
        ["check-ignore", "--quiet", "--", relative_text],
        cwd=context.owner_root,
        accepted_returncodes=(0, 1),
    )
    if ignored.returncode == 0:
        raise ProjectRepositoryError(
            f"submodule destination is ignored by {context.owner_root}: {relative_text}"
        )

    tracked = _git(
        ["ls-files", "--error-unmatch", "--", relative_text],
        cwd=context.owner_root,
        accepted_returncodes=(0, 1),
    )
    if tracked.returncode == 0:
        raise ProjectRepositoryError(f"repository already tracks destination: {relative_text}")

    remote_state = _git(["ls-remote", "--", remote], cwd=context.owner_root)
    if remote_state.stdout.strip():
        raise ProjectRepositoryError(
            "remote repository is not empty; use a new empty repository for generated projects"
        )

    return SubmodulePlan(
        context=context,
        destination=destination,
        relative_path=relative_path,
        remote=remote,
    )


def initialize_and_register_submodule(
    plan: SubmodulePlan,
    project_name: str,
    *,
    output: TextIO | None = None,
) -> str:
    phase = "initializing the project repository"
    try:
        if output is not None:
            print("Initializing repository", file=output)
            print("  Branch: main", file=output)
            print(f"  Remote: {plan.remote}", file=output)
            print(file=output)

        _git(["init", "-b", "main"], cwd=plan.destination)
        _git(["add", "--all"], cwd=plan.destination)

        staged = _git(
            ["diff", "--cached", "--quiet"],
            cwd=plan.destination,
            accepted_returncodes=(0, 1),
        )
        if staged.returncode == 0:
            raise ProjectRepositoryError("generated project contains no files to commit")

        phase = "creating the initial project commit"
        _git(["commit", "-m", f"Initial {project_name} scaffold"], cwd=plan.destination)
        _git(["remote", "add", "origin", plan.remote], cwd=plan.destination)

        phase = "publishing the initial project commit"
        _git(["push", "-u", "origin", "main"], cwd=plan.destination)

        phase = "registering the project with its enclosing repository"
        if output is not None:
            print("Registering submodule", file=output)
            print(f"  Repository: {plan.context.owner_root}", file=output)
            print(f"  Path:       {plan.relative_path.as_posix()}", file=output)
            print(file=output)

        _git(
            ["submodule", "add", plan.remote, plan.relative_path.as_posix()],
            cwd=plan.context.owner_root,
        )
        head = _git(["rev-parse", "HEAD"], cwd=plan.destination).stdout.strip()
    except ProjectRepositoryError as exc:
        raise ProjectRepositoryError(
            f"{phase} failed: {exc}\n"
            f"The generated project was preserved at {plan.destination}."
        ) from exc

    if output is not None:
        print("Git integration complete", file=output)
        print(f"  Project commit: {head}", file=output)
        print("  Parent changes: .gitmodules and the submodule gitlink are staged", file=output)
    return head
