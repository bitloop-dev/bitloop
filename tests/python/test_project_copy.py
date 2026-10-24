from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BITLOOP_ROOT / "scripts"))

from test_project_repository import IDENTITY_ENV, git, make_bare_remote, make_parent, populate_remote  # noqa: E402


def make_source(parent: Path) -> Path:
    source = parent / "FluidLesson"
    (source / "FluidLesson").mkdir(parents=True)
    (source / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.21)\n'
        'project(FluidLesson NONE)\n'
        'include("${CMAKE_CURRENT_LIST_DIR}/tooling/findBitloop.cmake")\n'
        'bitloop_enable_languages(LANGUAGES C CXX)\n'
        'set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/FluidLesson")\n'
        'file(GLOB_RECURSE SIM_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.h")\n'
        'bitloop_new_project(FluidLesson ${SIM_SOURCES})\n'
        'bitloop_finalize()\n',
        encoding="utf-8",
    )
    (source / "FluidLesson" / "FluidLesson.h").write_text(
        'namespace FluidLesson {\n'
        'class FluidLesson_Project {};\n'
        'class FluidLesson_Scene {};\n'
        'static ProjectInfo info() { return ProjectInfo({ "New Projects", "FluidLesson" }); }\n'
        '}\n', encoding="utf-8",
    )
    (source / "FluidLesson" / "FluidLesson.cpp").write_bytes(
        b'\xef\xbb\xbf#include "FluidLesson.h"\r\n'
        b'#include <FluidLesson/FluidLesson.hpp>\r\n'
        b'void FluidLesson_Scene::sceneStart() {}\r\n'
        b'void FluidLesson_Project::projectPrepare() {}\r\n'
        b'namespace FluidLesson::UI {}\r\n'
        b'// MyFluidLesson FluidLessonExtra FluidLesson_SceneExtra FluidLesson_Project2\r\n'
        b'// FluidLesson_value My_FluidLesson XFluidLesson_Scene fluidlesson FLUIDLESSON\r\n'
        b'// FluidLesson_Scene_Extra FluidLesson_Project_Extra fluidlesson_Scene\r\n'
    )
    return source


def snapshot(root: Path) -> dict[str, bytes]:
    return {path.relative_to(root).as_posix(): path.read_bytes() for path in root.rglob("*") if path.is_file()}


class ProjectCopyTests(unittest.TestCase):
    def run_cli(self, cwd: Path, *arguments: str, input: str = "", expected: int = 0, env=None):
        result = subprocess.run(
            [sys.executable, str(BITLOOP_ROOT / "scripts" / "cli.py"), "copy", "project", *arguments],
            cwd=cwd, input=input, capture_output=True, text=True, encoding="utf-8",
            env={**os.environ, **IDENTITY_ENV, **(env or {})}, check=False,
        )
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        return result

    def test_renames_only_exact_names_and_preserves_source_and_binary_data(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = make_source(root)
            for extension in (".c", ".cc", ".cxx", ".hh", ".hpp", ".hxx"):
                (source / "FluidLesson" / ("FluidLesson" + extension)).write_text("// FluidLesson\n")
            for filename in ("FluidLessonExtra.cpp", "FluidLesson_Scene.cpp", "FluidLesson.txt"):
                (source / "FluidLesson" / filename).write_text("FluidLesson\n")
            (source / "other").mkdir()
            (source / "other" / "fluidlesson.h").write_text("FluidLesson\n")
            (source / "FluidLesson-notes.md").write_text("FluidLesson\n")
            (source / "image.bin").write_bytes(b"FluidLesson\0FluidLesson_Scene")
            (source / "other.bin").write_bytes(b"\xffFluidLesson")
            (source / "AGENTS.md").write_text("Project FluidLesson rules\n")
            (source / ".bitloop-workspace").write_text("root=FALSE\n")
            before = snapshot(source)

            result = self.run_cli(root, "FluidLesson", "FluidSim")

            destination = root / "FluidSim"
            self.assertEqual(snapshot(source), before)
            self.assertFalse((source / ".git").exists())
            self.assertFalse((destination / ".git").exists())
            self.assertFalse((destination / "FluidLesson").exists())
            for extension in (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"):
                self.assertTrue((destination / "FluidSim" / ("FluidSim" + extension)).is_file())
            for filename in ("FluidLessonExtra.cpp", "FluidLesson_Scene.cpp", "FluidLesson.txt"):
                self.assertTrue((destination / "FluidSim" / filename).is_file())
            self.assertTrue((destination / "other" / "fluidlesson.h").is_file())
            self.assertEqual((destination / "FluidLesson-notes.md").read_text(), "FluidSim\n")
            self.assertEqual((destination / "image.bin").read_bytes(), before["image.bin"])
            self.assertEqual((destination / "other.bin").read_bytes(), before["other.bin"])
            self.assertEqual((destination / "AGENTS.md").read_text(), "Project FluidSim rules\n")
            self.assertEqual((destination / ".bitloop-workspace").read_bytes(), before[".bitloop-workspace"])
            self.assertEqual(
                (destination / "CMakeLists.txt").read_bytes(), before["CMakeLists.txt"].replace(b"FluidLesson", b"FluidSim"),
            )
            self.assertEqual(
                (destination / "FluidSim" / "FluidSim.h").read_bytes(),
                before["FluidLesson/FluidLesson.h"].replace(b"FluidLesson", b"FluidSim"),
            )
            implementation = (destination / "FluidSim" / "FluidSim.cpp").read_bytes()
            original = before["FluidLesson/FluidLesson.cpp"]
            code, comments = original.split(b"//", 1)
            self.assertEqual(implementation, code.replace(b"FluidLesson", b"FluidSim") + b"//" + comments)
            self.assertNotIn("Project variant", result.stdout)
            self.assertNotIn("AI-assisted", result.stdout)
            self.assertFalse(list(root.glob(".bitloop-copy-*")))

    def test_ignore_rules_apply_without_a_repository_and_regardless_of_tracking(self) -> None:
        for repository in (False, True):
            with self.subTest(repository=repository), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = make_source(root)
                (source / ".gitignore").write_text(
                    "build/\n*.tmp\n!keep.tmp\n/root-only.txt\ncache/*\n!cache/keep.txt\nspace dir/\n", encoding="utf-8",
                )
                for directory in ("build", "nested", "cache", "space dir", "empty"):
                    (source / directory).mkdir()
                (source / "nested" / ".gitignore").write_text("*.log\n!important.log\n", encoding="utf-8")
                excluded = {"build/file.cpp", "drop.tmp", "root-only.txt", "cache/drop.txt", "nested/drop.log", "space dir/file"}
                included = {"keep.tmp", "nested/root-only.txt", "nested/important.log", "cache/keep.txt", "untracked.txt"}
                for filename in excluded | included:
                    (source / filename).write_text("fixture\n")
                if repository:
                    git(source, "init", "-b", "main")
                    git(source, "add", "--force", "drop.tmp")
                    (source / ".git" / "info" / "exclude").write_text("untracked.txt\n", encoding="utf-8")
                else:
                    # A worktree/submodule .git file must be excluded too, without reading its target.
                    (source / ".git").write_text("gitdir: missing-metadata\n", encoding="utf-8")

                self.run_cli(root, "FluidLesson", "FluidSim")

                destination = root / "FluidSim"
                for filename in included:
                    self.assertTrue((destination / filename).is_file(), filename)
                for filename in excluded:
                    self.assertFalse((destination / filename).exists(), filename)
                self.assertFalse((destination / ".git").exists())
                self.assertTrue((destination / "empty").is_dir())
                self.assertTrue((destination / ".gitignore").is_file())

    def test_source_directory_rename_is_case_sensitive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = make_source(root)
            (source / "FluidLesson").rename(source / "other")
            (source / "other").rename(source / "fluidlesson")
            self.run_cli(root, "FluidLesson", "FluidSim")
            self.assertTrue((root / "FluidSim" / "fluidlesson").is_dir())
            self.assertFalse((root / "FluidSim" / "FluidSim").exists())

    def test_parent_and_global_ignore_rules_do_not_hide_source_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root, ignored_projects=True)
            source = make_source(gallery / "projects")
            (source / "keep.txt").write_text("keep\n")
            global_ignore = root / "global-ignore"
            global_ignore.write_text("*.txt\n", encoding="utf-8")
            config = root / "gitconfig"
            config.write_text(f'[core]\n\texcludesFile = "{global_ignore.as_posix()}"\n', encoding="utf-8")
            self.run_cli(gallery / "projects", "FluidLesson", "FluidSim", env={"GIT_CONFIG_GLOBAL": str(config)})
            self.assertTrue((gallery / "projects" / "FluidSim" / "keep.txt").is_file())

    def test_interactive_copy_offers_existing_git_integration_without_template_questions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            source = make_source(gallery / "projects")
            before = snapshot(source)
            remote = make_bare_remote(root, "FluidSim")
            parent_head = git(gallery, "rev-parse", "HEAD")

            result = self.run_cli(gallery / "projects", input=f"FluidLesson\nFluidSim\n\n{remote}\n")

            self.assertIn("Source project name:", result.stdout)
            self.assertIn("New project name:", result.stdout)
            self.assertIn("Add project as a direct submodule of Gallery? [Y/n]:", result.stdout)
            self.assertIn(f"Workspace root:       {gallery.resolve()}", result.stdout)
            self.assertIn(f"Enclosing repository: {gallery.resolve()}", result.stdout)
            self.assertNotIn("Project variant", result.stdout)
            self.assertNotIn("AI-assisted", result.stdout)
            project = gallery / "projects" / "FluidSim"
            head = git(project, "rev-parse", "HEAD")
            self.assertEqual(git(remote, "rev-parse", "refs/heads/main"), head)
            self.assertEqual(git(project, "status", "--porcelain"), "")
            self.assertTrue(git(gallery, "ls-files", "--stage", "--", "projects/FluidSim").startswith(f"160000 {head} 0\t"))
            self.assertEqual(git(gallery, "rev-parse", "HEAD"), parent_head)
            self.assertEqual(snapshot(source), before)

    def test_no_submodule_suppresses_interactive_git_offer(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            gallery = make_parent(Path(temporary))
            make_source(gallery / "projects")
            result = self.run_cli(gallery / "projects", "--no-submodule", input="FluidLesson\nFluidSim\n")
            self.assertNotIn("Git integration", result.stdout)
            self.assertFalse((gallery / "projects" / "FluidSim" / ".git").exists())

    def test_remote_flag_publishes_non_interactively(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            make_source(gallery / "projects")
            remote = make_bare_remote(root, "FluidSim")
            self.run_cli(gallery / "projects", "FluidLesson", "FluidSim", "--remote", str(remote))
            project = gallery / "projects" / "FluidSim"
            self.assertEqual(git(project, "rev-parse", "HEAD"), git(remote, "rev-parse", "refs/heads/main"))

    def test_invalid_inputs_and_remote_fail_before_copying(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = make_parent(root)
            parent = gallery / "projects"
            source = make_source(parent)
            before = snapshot(source)
            existing = parent / "Existing"
            existing.mkdir()
            (existing / "keep").write_text("untouched")
            remote = make_bare_remote(root, "ExistingRemote")
            populate_remote(root, remote)
            cases = [
                (("Missing", "FluidSim"), "source project not found"),
                (("FluidLesson", "../FluidSim"), "must be a C++ identifier"),
                (("../FluidLesson", "FluidSim"), "must be a C++ identifier"),
                (("FluidLesson", "FluidLesson"), "destination already exists"),
                (("FluidLesson", "Existing"), "destination already exists"),
                (("FluidLesson", "FluidSim", "--submodule"), "--remote is required"),
                (("FluidLesson", "FluidSim", "--remote", str(remote), "--no-submodule"), "cannot be combined"),
                (("FluidLesson", "FluidSim", "--remote", str(remote)), "remote repository is not empty"),
            ]
            for arguments, error in cases:
                with self.subTest(arguments=arguments):
                    result = self.run_cli(parent, *arguments, expected=2)
                    self.assertIn(error, result.stderr)
                    self.assertFalse((parent / "FluidSim").exists())
                    self.assertEqual(snapshot(source), before)
            self.assertEqual((existing / "keep").read_text(), "untouched")

    def test_rename_collisions_leave_no_destination(self) -> None:
        for collision in ("FluidSim", "FluidLesson/FluidSim.cpp"):
            with self.subTest(collision=collision), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                source = make_source(root)
                (source / collision).write_text("existing content\n")
                before = snapshot(source)
                result = self.run_cli(root, "FluidLesson", "FluidSim", expected=2)
                self.assertIn("destination already exists", result.stderr)
                self.assertEqual(snapshot(source), before)
                self.assertFalse((root / "FluidSim").exists())
                self.assertFalse(list(root.glob(".bitloop-copy-*")))

    def test_symlinks_are_preserved_without_rewriting_their_targets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = make_source(root)
            external = root / "external.txt"
            external.write_text("FluidLesson\n")
            try:
                (source / "linked.txt").symlink_to(external)
                (source / "loop").symlink_to(source, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"symlink creation unavailable: {exc}")
            self.run_cli(root, "FluidLesson", "FluidSim")
            self.assertEqual(external.read_text(), "FluidLesson\n")
            self.assertTrue((root / "FluidSim" / "linked.txt").is_symlink())
            self.assertTrue((root / "FluidSim" / "loop").is_symlink())


if __name__ == "__main__":
    unittest.main()
