from __future__ import annotations

import contextlib
import io
import json
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BITLOOP_ROOT / "scripts"))

import cli as bitloop_cli  # noqa: E402
import agent_pack as agent_pack_module  # noqa: E402
from agent_pack import (  # noqa: E402
    AgentPackConflict,
    AgentPackNotInitialized,
    PACK_RELATIVE_PATH,
    canonical_pack_dir,
    discover_managed_projects,
    init_agent_pack,
    inspect_destination,
    load_manifest,
    sync_agent_pack,
    sync_agent_packs_recursive,
    validate_pack,
    write_manifest,
)
from instantiate_template import create_project  # noqa: E402


def make_project(root: Path, name: str = "FixtureProject") -> Path:
    project = root / name
    project.mkdir()
    (project / ".bitloop-workspace").write_text("root=FALSE\n", encoding="utf-8")
    (project / "vcpkg.json").write_text(
        '{"name":"fixture","version-string":"0.1.0","dependencies":["bitloop"]}\n',
        encoding="utf-8",
    )
    (project / "CMakeLists.txt").write_text(
        f"bitloop_new_project({name})\nbitloop_finalize()\n",
        encoding="utf-8",
    )
    (project / "AGENTS.md").write_text("project-owned instructions\n", encoding="utf-8")
    (project / "ARCHITECTURE.md").write_text("project architecture\n", encoding="utf-8")
    (project / ".codex" / "runs").mkdir(parents=True)
    (project / ".codex" / "runs" / "sentinel.txt").write_text("keep\n", encoding="utf-8")
    return project


class AgentPackTests(unittest.TestCase):
    def test_canonical_pack_manifest_and_links_are_valid(self) -> None:
        pack = canonical_pack_dir(BITLOOP_ROOT)
        manifest = load_manifest(pack)
        self.assertEqual(validate_pack(pack, manifest), ())

        skill = (pack / "SKILL.md").read_text(encoding="utf-8")
        self.assertLess(len(skill.encode("utf-8")), 12 * 1024)
        self.assertLess((BITLOOP_ROOT / "AGENTS.md").stat().st_size, 16 * 1024)
        for reference in re.findall(r"\]\((references/[^)]+)\)", skill):
            self.assertTrue((pack / reference).is_file(), reference)

    def test_init_installs_then_sync_checks_and_preserves_project_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = make_project(Path(temporary))
            self.assertEqual(sync_agent_pack(BITLOOP_ROOT, project, check=True), 1)
            with self.assertRaises(AgentPackNotInitialized):
                sync_agent_pack(BITLOOP_ROOT, project)
            self.assertEqual(init_agent_pack(BITLOOP_ROOT, project), 0)
            destination = project / PACK_RELATIVE_PATH
            self.assertTrue((destination / "SKILL.md").is_file())
            self.assertEqual(sync_agent_pack(BITLOOP_ROOT, project, check=True), 0)
            self.assertEqual((project / "AGENTS.md").read_text(encoding="utf-8"), "project-owned instructions\n")
            self.assertEqual((project / "ARCHITECTURE.md").read_text(encoding="utf-8"), "project architecture\n")
            self.assertEqual(
                (project / ".codex" / "runs" / "sentinel.txt").read_text(encoding="utf-8"),
                "keep\n",
            )

    def test_sync_refuses_local_edits_unless_forced(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = make_project(Path(temporary))
            init_agent_pack(BITLOOP_ROOT, project)
            destination = project / PACK_RELATIVE_PATH
            skill = destination / "SKILL.md"
            skill.write_text(skill.read_text(encoding="utf-8") + "\nlocal edit\n", encoding="utf-8")

            self.assertEqual(sync_agent_pack(BITLOOP_ROOT, project, check=True), 1)
            with self.assertRaises(AgentPackConflict):
                sync_agent_pack(BITLOOP_ROOT, project)
            self.assertEqual(sync_agent_pack(BITLOOP_ROOT, project, force=True), 0)
            self.assertNotIn("local edit", skill.read_text(encoding="utf-8"))

    def test_stale_clean_pack_updates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            project = make_project(temporary_path)
            init_agent_pack(BITLOOP_ROOT, project)
            destination = project / PACK_RELATIVE_PATH

            source_copy = temporary_path / "framework"
            pack_copy = source_copy / PACK_RELATIVE_PATH
            shutil.copytree(canonical_pack_dir(BITLOOP_ROOT), pack_copy)
            reference = pack_copy / "references" / "framework-map.md"
            reference.write_text(reference.read_text(encoding="utf-8") + "\nUpdated fixture.\n", encoding="utf-8")
            write_manifest(
                pack_copy,
                pack_revision="fixture-next",
                source_revision="fixture",
                compatibility="fixture",
            )

            source_manifest = load_manifest(pack_copy)
            self.assertEqual(inspect_destination(destination, source_manifest).status, "stale")
            self.assertEqual(sync_agent_pack(source_copy, project), 0)
            self.assertIn("Updated fixture.", reference.read_text(encoding="utf-8"))
            self.assertIn("Updated fixture.", (destination / "references" / "framework-map.md").read_text(encoding="utf-8"))

    def test_sync_does_not_rename_an_existing_pack_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = make_project(Path(temporary))
            init_agent_pack(BITLOOP_ROOT, project)
            destination = project / PACK_RELATIVE_PATH
            obsolete = destination / "obsolete.md"
            obsolete.write_text("remove me\n", encoding="utf-8")
            write_manifest(
                destination,
                pack_revision="fixture-old",
                source_revision="fixture",
                compatibility="fixture",
            )

            real_replace = os.replace

            def reject_directory_rename(source: str | bytes | os.PathLike, target: str | bytes | os.PathLike) -> None:
                if Path(source).is_dir():
                    raise PermissionError(5, "fixture denies renaming an open directory", str(source))
                real_replace(source, target)

            with mock.patch.object(agent_pack_module.os, "replace", side_effect=reject_directory_rename):
                self.assertEqual(sync_agent_pack(BITLOOP_ROOT, project), 0)

            self.assertFalse(obsolete.exists())
            self.assertEqual(sync_agent_pack(BITLOOP_ROOT, project, check=True), 0)

    def test_failed_file_update_rolls_back_the_existing_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            project = make_project(temporary_path)
            init_agent_pack(BITLOOP_ROOT, project)
            destination = project / PACK_RELATIVE_PATH
            before = {
                path.relative_to(destination): path.read_bytes()
                for path in destination.rglob("*")
                if path.is_file()
            }

            source_copy = temporary_path / "framework"
            pack_copy = source_copy / PACK_RELATIVE_PATH
            shutil.copytree(canonical_pack_dir(BITLOOP_ROOT), pack_copy)
            for name in ("build-cmake.md", "framework-map.md"):
                reference = pack_copy / "references" / name
                reference.write_text(reference.read_text(encoding="utf-8") + "\nUpdated fixture.\n", encoding="utf-8")
            write_manifest(
                pack_copy,
                pack_revision="fixture-next",
                source_revision="fixture",
                compatibility="fixture",
            )

            real_replace_file = agent_pack_module._replace_file
            replacement_count = 0

            def fail_second_replacement(source: Path, target: Path) -> None:
                nonlocal replacement_count
                if agent_pack_module._files_equal(source, target):
                    return
                replacement_count += 1
                if replacement_count == 2:
                    raise OSError("fixture update failure")
                real_replace_file(source, target)

            with mock.patch.object(agent_pack_module, "_replace_file", side_effect=fail_second_replacement):
                with self.assertRaisesRegex(agent_pack_module.AgentPackError, "could not replace managed agent pack"):
                    sync_agent_pack(source_copy, project)

            after = {
                path.relative_to(destination): path.read_bytes()
                for path in destination.rglob("*")
                if path.is_file()
            }
            self.assertEqual(after, before)
            self.assertEqual(list(destination.parent.glob(".bitloop-development.backup-*")), [])

    def test_recursive_check_audits_only_managed_projects_without_writing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            current = make_project(root, "CurrentProject")
            stale = make_project(root, "StaleProject")
            unmanaged = make_project(root, "UnmanagedProject")
            init_agent_pack(BITLOOP_ROOT, current)
            init_agent_pack(BITLOOP_ROOT, stale)

            stale_manifest_path = stale / PACK_RELATIVE_PATH / "agent-pack.json"
            stale_manifest = json.loads(stale_manifest_path.read_text(encoding="utf-8"))
            stale_manifest["pack_revision"] = "fixture-old"
            stale_manifest_path.write_text(json.dumps(stale_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

            output = io.StringIO()
            self.assertEqual(sync_agent_packs_recursive(BITLOOP_ROOT, root, check=True, output=output), 1)
            report = output.getvalue()
            self.assertIn(str(current.resolve()), report)
            self.assertIn(str(stale.resolve()), report)
            self.assertNotIn(str(unmanaged.resolve()), report)
            self.assertEqual(load_manifest(stale / PACK_RELATIVE_PATH)["pack_revision"], "fixture-old")
            self.assertFalse((unmanaged / PACK_RELATIVE_PATH).exists())

    def test_recursive_sync_updates_managed_projects_without_installing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stale = make_project(root, "StaleProject")
            unmanaged = make_project(root, "UnmanagedProject")
            init_agent_pack(BITLOOP_ROOT, stale)

            stale_manifest_path = stale / PACK_RELATIVE_PATH / "agent-pack.json"
            stale_manifest = json.loads(stale_manifest_path.read_text(encoding="utf-8"))
            stale_manifest["pack_revision"] = "fixture-old"
            stale_manifest_path.write_text(json.dumps(stale_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

            self.assertEqual(sync_agent_packs_recursive(BITLOOP_ROOT, root), 0)
            self.assertEqual(load_manifest(stale / PACK_RELATIVE_PATH), load_manifest(canonical_pack_dir(BITLOOP_ROOT)))
            self.assertFalse((unmanaged / PACK_RELATIVE_PATH).exists())

    def test_recursive_sync_continues_after_a_modified_pack(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            modified = make_project(root, "AModifiedProject")
            stale = make_project(root, "BStaleProject")
            init_agent_pack(BITLOOP_ROOT, modified)
            init_agent_pack(BITLOOP_ROOT, stale)

            modified_skill = modified / PACK_RELATIVE_PATH / "SKILL.md"
            modified_skill.write_text(modified_skill.read_text(encoding="utf-8") + "\nlocal edit\n", encoding="utf-8")
            stale_manifest_path = stale / PACK_RELATIVE_PATH / "agent-pack.json"
            stale_manifest = json.loads(stale_manifest_path.read_text(encoding="utf-8"))
            stale_manifest["pack_revision"] = "fixture-old"
            stale_manifest_path.write_text(json.dumps(stale_manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

            output = io.StringIO()
            self.assertEqual(sync_agent_packs_recursive(BITLOOP_ROOT, root, output=output), 1)
            self.assertIn("local changes exist", output.getvalue())
            self.assertIn("local edit", modified_skill.read_text(encoding="utf-8"))
            self.assertEqual(load_manifest(stale / PACK_RELATIVE_PATH), load_manifest(canonical_pack_dir(BITLOOP_ROOT)))

    def test_recursive_discovery_prunes_build_and_cache_trees(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            managed = make_project(root, "ManagedProject")
            build_root = root / "build"
            build_root.mkdir()
            ignored = make_project(build_root, "GeneratedCopy")
            init_agent_pack(BITLOOP_ROOT, managed)
            init_agent_pack(BITLOOP_ROOT, ignored)

            self.assertEqual(discover_managed_projects(root), (managed.resolve(),))

    def test_cli_routes_init_and_recursive_check(self) -> None:
        project = Path("C:/fixture/project")
        search_root = Path("C:/fixture/root")

        with mock.patch.object(sys, "argv", ["bitloop", "agents", "init", "--project", str(project)]):
            with mock.patch.object(bitloop_cli, "resolve_project_root", return_value=project):
                with mock.patch.object(bitloop_cli, "init_agent_pack", return_value=0) as initialize:
                    with self.assertRaises(SystemExit) as exit_state:
                        bitloop_cli.main()
        self.assertEqual(exit_state.exception.code, 0)
        initialize.assert_called_once_with(BITLOOP_ROOT, project, output=sys.stdout)

        argv = ["bitloop", "agents", "sync", "--recursive", "--check", "--project", str(search_root)]
        with mock.patch.object(sys, "argv", argv):
            with mock.patch.object(bitloop_cli, "resolve_recursive_root", return_value=search_root):
                with mock.patch.object(bitloop_cli, "sync_agent_packs_recursive", return_value=0) as recursive_sync:
                    with self.assertRaises(SystemExit) as exit_state:
                        bitloop_cli.main()
        self.assertEqual(exit_state.exception.code, 0)
        recursive_sync.assert_called_once_with(
            BITLOOP_ROOT,
            search_root,
            check=True,
            force=False,
            output=sys.stdout,
        )

    def test_generator_creates_self_contained_agent_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination_parent = Path(temporary)
            original_cwd = Path.cwd()
            try:
                os.chdir(destination_parent)
                with contextlib.redirect_stdout(io.StringIO()):
                    create_project(BITLOOP_ROOT, "GeneratedAgentFixture")
            finally:
                os.chdir(original_cwd)

            project = destination_parent / "GeneratedAgentFixture"
            self.assertTrue((project / "AGENTS.md").is_file())
            self.assertNotIn("{SIM_NAME}", (project / "AGENTS.md").read_text(encoding="utf-8"))
            pack = project / PACK_RELATIVE_PATH
            manifest = load_manifest(pack)
            self.assertEqual(validate_pack(pack, manifest), ())
            self.assertTrue((pack / "references" / "simulation-health-diagnostics.md").is_file())
            self.assertTrue((pack / "assets" / "health_probe_support.hpp").is_file())
            self.assertTrue((pack / "scripts" / "health_session.py").is_file())

            text_files = [project / "AGENTS.md", *pack.rglob("*.md"), *pack.rglob("*.yaml")]
            combined = "\n".join(path.read_text(encoding="utf-8") for path in text_files)
            self.assertNotIn("C:\\dev\\cpp\\bitloop-workspace", combined)
            self.assertNotIn("CodexProjects", combined)
            self.assertNotIn("bitloop-gallery", combined)


if __name__ == "__main__":
    unittest.main()
