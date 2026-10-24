from __future__ import annotations

import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BITLOOP_ROOT / "scripts"))

from instantiate_template import create_project  # noqa: E402
from project_repository import (  # noqa: E402
    ProjectRepositoryError,
    discover_repository_context,
    prepare_submodule_plan,
    prompt_for_submodule,
)


IDENTITY_ENV = {
    "GIT_AUTHOR_NAME": "Bitloop Tests",
    "GIT_AUTHOR_EMAIL": "bitloop-tests@example.invalid",
    "GIT_COMMITTER_NAME": "Bitloop Tests",
    "GIT_COMMITTER_EMAIL": "bitloop-tests@example.invalid",
}


def git(cwd: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=str(cwd),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        env={**os.environ, **IDENTITY_ENV},
        check=False,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr or result.stdout)
    return result.stdout.strip()


def make_parent(root: Path, *, ignored_projects: bool = False) -> Path:
    parent = root / "Gallery"
    parent.mkdir()
    git(parent, "init", "-b", "main")
    (parent / ".bitloop-workspace").write_text("root=TRUE\n", encoding="utf-8")
    (parent / "README.md").write_text("fixture gallery\n", encoding="utf-8")
    if ignored_projects:
        (parent / ".gitignore").write_text("projects/\n", encoding="utf-8")
    (parent / "projects").mkdir()
    git(parent, "add", ".")
    git(parent, "commit", "-m", "Initial gallery")
    return parent


def make_bare_remote(root: Path, name: str) -> Path:
    remote = root / f"{name}.git"
    remote.mkdir()
    git(remote, "init", "--bare", "-b", "main")
    return remote


def populate_remote(root: Path, remote: Path) -> None:
    seed = root / "RemoteSeed"
    seed.mkdir()
    git(seed, "init", "-b", "main")
    (seed / "README.md").write_text("existing remote content\n", encoding="utf-8")
    git(seed, "add", ".")
    git(seed, "commit", "-m", "Seed remote")
    git(seed, "remote", "add", "origin", str(remote))
    git(seed, "push", "-u", "origin", "main")


class ProjectRepositoryTests(unittest.TestCase):
    def test_repository_owner_is_nearest_git_worktree_not_workspace_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            direct = discover_repository_context(gallery / "projects")
            self.assertIsNotNone(direct)
            self.assertEqual(direct.owner_root, gallery.resolve())
            self.assertEqual(direct.workspace_root, gallery.resolve())
            self.assertTrue(direct.owner_is_workspace_root)

            child = gallery / "projects" / "Child"
            child.mkdir()
            git(child, "init", "-b", "main")
            (child / ".bitloop-workspace").write_text("root=FALSE\n", encoding="utf-8")
            (child / "README.md").write_text("fixture child\n", encoding="utf-8")
            git(child, "add", ".")
            git(child, "commit", "-m", "Initial child")
            nested_parent = child / "subprojects"
            nested_parent.mkdir()

            nested = discover_repository_context(nested_parent)
            self.assertIsNotNone(nested)
            self.assertEqual(nested.owner_root, child.resolve())
            self.assertEqual(nested.workspace_root, gallery.resolve())
            self.assertFalse(nested.owner_is_workspace_root)

    def test_prompt_defaults_to_yes_at_workspace_root_and_no_when_nested(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            direct = discover_repository_context(gallery / "projects")
            self.assertIsNotNone(direct)

            child = gallery / "projects" / "Child"
            child.mkdir()
            git(child, "init", "-b", "main")
            (child / "README.md").write_text("fixture child\n", encoding="utf-8")
            git(child, "add", ".")
            git(child, "commit", "-m", "Initial child")
            nested_parent = child / "subprojects"
            nested_parent.mkdir()
            nested = discover_repository_context(nested_parent)
            self.assertIsNotNone(nested)

            with mock.patch("builtins.input", return_value=""), contextlib.redirect_stdout(io.StringIO()):
                self.assertTrue(prompt_for_submodule(direct))
                self.assertFalse(prompt_for_submodule(nested))

    def test_ignored_destination_is_rejected_before_generation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root, ignored_projects=True)
            remote = make_bare_remote(root, "IgnoredFixture")
            context = discover_repository_context(gallery / "projects")
            self.assertIsNotNone(context)

            with self.assertRaisesRegex(ProjectRepositoryError, "destination is ignored"):
                prepare_submodule_plan(context, gallery / "projects" / "IgnoredFixture", str(remote))

    def test_nonempty_remote_is_rejected_before_generation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            remote = make_bare_remote(root, "ExistingFixture")
            populate_remote(root, remote)
            context = discover_repository_context(gallery / "projects")
            self.assertIsNotNone(context)

            destination = gallery / "projects" / "ExistingFixture"
            with self.assertRaisesRegex(ProjectRepositoryError, "remote repository is not empty"):
                prepare_submodule_plan(context, destination, str(remote))
            self.assertFalse(destination.exists())

    def test_explicit_submodule_requires_an_enclosing_repository(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            remote = make_bare_remote(root, "StandaloneFixture")
            generation_root = root / "Generation"
            generation_root.mkdir()
            original_cwd = Path.cwd()
            try:
                os.chdir(generation_root)
                with self.assertRaisesRegex(ProjectRepositoryError, "not inside a Git worktree"):
                    create_project(
                        BITLOOP_ROOT,
                        "StandaloneFixture",
                        minimal=True,
                        include_ai=False,
                        submodule=True,
                        remote=str(remote),
                    )
            finally:
                os.chdir(original_cwd)
            self.assertFalse((generation_root / "StandaloneFixture").exists())

    def test_generated_project_is_pushed_and_registered_without_absorption(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            remote = make_bare_remote(root, "SubmoduleFixture")
            parent_head = git(gallery, "rev-parse", "HEAD")
            original_cwd = Path.cwd()

            try:
                os.chdir(gallery / "projects")
                with mock.patch.dict(os.environ, IDENTITY_ENV, clear=False):
                    with contextlib.redirect_stdout(io.StringIO()):
                        create_project(
                            BITLOOP_ROOT,
                            "SubmoduleFixture",
                            minimal=True,
                            include_ai=False,
                            submodule=True,
                            remote=str(remote),
                        )
            finally:
                os.chdir(original_cwd)

            project = gallery / "projects" / "SubmoduleFixture"
            project_head = git(project, "rev-parse", "HEAD")
            self.assertEqual(git(remote, "rev-parse", "refs/heads/main"), project_head)
            self.assertEqual(git(project, "status", "--porcelain"), "")
            self.assertEqual(git(project, "remote", "get-url", "origin"), str(remote))
            self.assertTrue((project / ".git").is_dir())

            gitlink = git(gallery, "ls-files", "--stage", "--", "projects/SubmoduleFixture")
            self.assertTrue(gitlink.startswith(f"160000 {project_head} 0\t"), gitlink)
            self.assertEqual(
                git(gallery, "config", "--file", ".gitmodules", "--get", "submodule.projects/SubmoduleFixture.url"),
                str(remote),
            )
            self.assertEqual(git(gallery, "rev-parse", "HEAD"), parent_head)
            generated_ignore = (project / ".gitignore").read_text(encoding="utf-8")
            self.assertIn("/.codex/plans/", generated_ignore)
            self.assertIn("/.codex/runs/", generated_ignore)

    def test_interactive_root_flow_uses_clean_prompts_and_defaults_to_submodule(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            remote = make_bare_remote(root, "InteractiveFixture")
            output = io.StringIO()
            original_cwd = Path.cwd()

            try:
                os.chdir(gallery / "projects")
                answers = ["InteractiveFixture", "2", "n", "", str(remote)]
                with mock.patch.dict(os.environ, IDENTITY_ENV, clear=False):
                    with mock.patch("builtins.input", side_effect=answers):
                        with contextlib.redirect_stdout(output):
                            create_project(BITLOOP_ROOT)
            finally:
                os.chdir(original_cwd)

            report = output.getvalue()
            self.assertIn("New Bitloop project", report)
            self.assertIn("Project variant", report)
            self.assertIn("Git integration", report)
            self.assertIn(f"Workspace root:       {gallery.resolve()}", report)
            self.assertIn(f"Enclosing repository: {gallery.resolve()}", report)
            self.assertIn("Creating project", report)
            self.assertIn("Initializing repository", report)
            self.assertIn("Registering submodule", report)
            self.assertIn("Git integration complete", report)
            self.assertTrue((gallery / "projects" / "InteractiveFixture" / ".git").is_dir())


if __name__ == "__main__":
    unittest.main()
