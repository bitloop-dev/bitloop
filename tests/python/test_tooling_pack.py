from __future__ import annotations

import contextlib
import io
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BITLOOP_ROOT / "scripts"))

from instantiate_template import create_project  # noqa: E402
from tooling_pack import (  # noqa: E402
    MANIFEST_NAME,
    ToolingPackConflict,
    ToolingPackError,
    canonical_pack_dir,
    inspect_destination,
    load_manifest,
    sync_tooling_pack,
    validate_pack,
    write_manifest,
)


def make_project(root: Path, name: str = "FixtureProject") -> Path:
    project = root / name
    project.mkdir()
    (project / "CMakeLists.txt").write_text(
        f"bitloop_new_project({name})\nbitloop_finalize()\n",
        encoding="utf-8",
    )
    scripts = project / "tooling" / "scripts"
    scripts.mkdir(parents=True)
    (scripts / "project-owned.py").write_text("keep = True\n", encoding="utf-8")
    return project


class ToolingPackTests(unittest.TestCase):
    def test_canonical_manifest_is_valid(self) -> None:
        source = canonical_pack_dir(BITLOOP_ROOT)
        manifest = load_manifest(source)
        self.assertEqual(validate_pack(source, manifest), ())

    def test_sync_installs_checks_and_preserves_project_tooling(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = make_project(Path(temporary))
            self.assertEqual(sync_tooling_pack(BITLOOP_ROOT, project), 0)
            self.assertEqual(sync_tooling_pack(BITLOOP_ROOT, project, check=True), 0)
            self.assertTrue((project / "tooling" / MANIFEST_NAME).is_file())
            self.assertEqual(
                (project / "tooling" / "scripts" / "project-owned.py").read_text(encoding="utf-8"),
                "keep = True\n",
            )

    def test_sync_refuses_local_edits_unless_forced(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = make_project(Path(temporary))
            sync_tooling_pack(BITLOOP_ROOT, project)
            finder = project / "tooling" / "findBitloop.cmake"
            finder.write_text(finder.read_text(encoding="utf-8") + "\n# local edit\n", encoding="utf-8")

            self.assertEqual(sync_tooling_pack(BITLOOP_ROOT, project, check=True), 1)
            with self.assertRaises(ToolingPackConflict):
                sync_tooling_pack(BITLOOP_ROOT, project)
            self.assertEqual(sync_tooling_pack(BITLOOP_ROOT, project, force=True), 0)
            self.assertNotIn("local edit", finder.read_text(encoding="utf-8"))

    def test_stale_clean_pack_updates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            project = make_project(temporary_path)
            sync_tooling_pack(BITLOOP_ROOT, project)
            destination = project / "tooling"

            source_copy = temporary_path / "framework"
            pack_copy = canonical_pack_dir(source_copy)
            shutil.copytree(canonical_pack_dir(BITLOOP_ROOT), pack_copy)
            finder = pack_copy / "findBitloop.cmake"
            finder.write_text(finder.read_text(encoding="utf-8") + "\n# fixture update\n", encoding="utf-8")
            write_manifest(source_copy, pack_revision="fixture-next")

            source_manifest = load_manifest(pack_copy)
            self.assertEqual(inspect_destination(destination, source_manifest).status, "stale")
            self.assertEqual(sync_tooling_pack(source_copy, project), 0)
            self.assertIn(
                "fixture update",
                (destination / "findBitloop.cmake").read_text(encoding="utf-8"),
            )

    def test_failed_forced_update_rolls_back_managed_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = make_project(Path(temporary))
            sync_tooling_pack(BITLOOP_ROOT, project)
            finder = project / "tooling" / "findBitloop.cmake"
            original_finder = finder.read_bytes()
            conflict = project / "tooling" / "toolchain" / "discovery_shims.cmake"
            conflict.unlink()
            conflict.mkdir()

            with self.assertRaises(ToolingPackError):
                sync_tooling_pack(BITLOOP_ROOT, project, force=True)

            self.assertEqual(finder.read_bytes(), original_finder)
            self.assertTrue(conflict.is_dir())

    def test_generator_contains_current_tooling(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            original_cwd = Path.cwd()
            try:
                os.chdir(temporary)
                with contextlib.redirect_stdout(io.StringIO()):
                    create_project(BITLOOP_ROOT, "GeneratedToolingFixture")
            finally:
                os.chdir(original_cwd)

            project = Path(temporary) / "GeneratedToolingFixture"
            manifest = load_manifest(project / "tooling")
            self.assertEqual(validate_pack(project / "tooling", manifest), ())
            cmake = (project / "CMakeLists.txt").read_text(encoding="utf-8")
            self.assertIn("project(GeneratedToolingFixture NONE)", cmake)
            self.assertIn("bitloop_enable_languages(LANGUAGES C CXX)", cmake)


if __name__ == "__main__":
    unittest.main()
