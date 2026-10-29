from __future__ import annotations

import json
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
PROVIDER = BITLOOP_ROOT / "templates" / "default" / "tooling" / "toolchain" / "bitloop_provider.cmake"
MERGER = BITLOOP_ROOT / "templates" / "default" / "tooling" / "toolchain" / "merge_vcpkg_manifests.cmake"
FINDER = BITLOOP_ROOT / "templates" / "default" / "tooling" / "findBitloop.cmake"
CMAKE = shutil.which("cmake")


def cmake_path(path: Path) -> str:
    return path.resolve().as_posix()


def make_bitloop_source(path: Path) -> None:
    for relative in ("cmake/bitloopSimulation.cmake", "include/bitloop.h"):
        target = path / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text("# fixture\n", encoding="utf-8")
    (path / "CMakeLists.txt").write_text("# fixture\n", encoding="utf-8")
    (path / "vcpkg.json").write_text(
        '{"name":"bitloop","version-string":"0.0.0","dependencies":[]}\n',
        encoding="utf-8",
    )


@unittest.skipUnless(CMAKE, "cmake is required")
class CMakeProjectToolingTests(unittest.TestCase):
    def resolve(self, source: Path, *, provider: str = "AUTO", source_override: Path | None = None) -> tuple[str, str, str]:
        output = source.parent / f"resolved-{source.name}.txt"
        script = source.parent / f"resolve-{source.name}.cmake"
        override = cmake_path(source_override) if source_override else ""
        script.write_text(
            textwrap.dedent(
                f"""
                cmake_minimum_required(VERSION 3.21)
                set(BITLOOP_PROVIDER "{provider}" CACHE STRING "" FORCE)
                set(BITLOOP_SOURCE_DIR "{override}" CACHE PATH "" FORCE)
                include("{cmake_path(PROVIDER)}")
                bl_find_bitloop_workspace("{cmake_path(source)}" _workspace _is_root)
                bl_resolve_bitloop_provider("{cmake_path(source)}" "${{_workspace}}")
                file(WRITE "{cmake_path(output)}"
                  "${{BITLOOP_RESOLVED_PROVIDER}}|${{BITLOOP_RESOLVED_SOURCE_DIR}}|${{BITLOOP_RESOLVED_REASON}}")
                """
            ).strip()
            + "\n",
            encoding="utf-8",
        )
        subprocess.run([CMAKE, "-P", str(script)], check=True, capture_output=True, text=True)
        provider_value, resolved_source, reason = output.read_text(encoding="utf-8").split("|")
        return provider_value, resolved_source, reason

    def test_nested_project_uses_workspace_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            gallery = root / "gallery"
            project = gallery / "projects" / "Nested"
            project.mkdir(parents=True)
            (gallery / ".bitloop-workspace").write_text("root=TRUE\n", encoding="utf-8")
            (project / ".bitloop-workspace").write_text("root=FALSE\n", encoding="utf-8")
            make_bitloop_source(gallery / "bitloop")

            provider, source, reason = self.resolve(project)
            self.assertEqual(provider, "SOURCE")
            self.assertEqual(Path(source), (gallery / "bitloop").resolve())
            self.assertEqual(reason, "workspace source")

    def test_example_uses_enclosing_framework_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            framework = Path(temporary) / "bitloop"
            example = framework / "examples" / "Example"
            example.mkdir(parents=True)
            (example / ".bitloop-workspace").write_text("root=FALSE\n", encoding="utf-8")
            make_bitloop_source(framework)

            provider, source, reason = self.resolve(example)
            self.assertEqual(provider, "SOURCE")
            self.assertEqual(Path(source), framework.resolve())
            self.assertEqual(reason, "enclosing source")

    def test_detached_project_uses_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "Detached"
            project.mkdir()
            (project / ".bitloop-workspace").write_text("root=FALSE\n", encoding="utf-8")

            provider, source, reason = self.resolve(project)
            self.assertEqual((provider, source, reason), ("PACKAGE", "", "no source tree found"))

    def test_explicit_source_override_wins(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Project"
            explicit = root / "ExplicitBitloop"
            project.mkdir()
            make_bitloop_source(explicit)

            provider, source, reason = self.resolve(project, source_override=explicit)
            self.assertEqual(provider, "SOURCE")
            self.assertEqual(Path(source), explicit.resolve())
            self.assertEqual(reason, "explicit source override")

    def test_forced_source_requires_a_valid_tree(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Project"
            project.mkdir()
            script = root / "invalid-source.cmake"
            script.write_text(
                textwrap.dedent(
                    f"""
                    cmake_minimum_required(VERSION 3.21)
                    set(BITLOOP_PROVIDER SOURCE CACHE STRING "" FORCE)
                    include("{cmake_path(PROVIDER)}")
                    bl_resolve_bitloop_provider("{cmake_path(project)}" "")
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("no valid Bitloop source tree", result.stdout + result.stderr)

    def test_package_rejects_a_source_override(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            project = root / "Project"
            source = root / "Bitloop"
            project.mkdir()
            make_bitloop_source(source)
            script = root / "contradictory-provider.cmake"
            script.write_text(
                textwrap.dedent(
                    f"""
                    cmake_minimum_required(VERSION 3.21)
                    set(BITLOOP_PROVIDER PACKAGE CACHE STRING "" FORCE)
                    set(BITLOOP_SOURCE_DIR "{cmake_path(source)}" CACHE PATH "" FORCE)
                    include("{cmake_path(PROVIDER)}")
                    bl_resolve_bitloop_provider("{cmake_path(project)}" "")
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("cannot be combined", result.stdout + result.stderr)

    def test_provided_port_is_removed_before_conflict_handling(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            root_manifest = root / "root.json"
            child_manifest = root / "child.json"
            output = root / "out"
            root_manifest.write_text(
                '{"name":"root","version-string":"1","dependencies":["bitloop","cpr"]}\n',
                encoding="utf-8",
            )
            child_manifest.write_text(
                '{"name":"child","version-string":"1","dependencies":[{"name":"bitloop","features":["x"]},"zlib"]}\n',
                encoding="utf-8",
            )
            (root / "vcpkg-configuration.json").write_text(
                '{"default-registry":{"kind":"builtin","baseline":"fixture"}}\n',
                encoding="utf-8",
            )
            script = root / "merge.cmake"
            script.write_text(
                textwrap.dedent(
                    f"""
                    cmake_minimum_required(VERSION 3.21)
                    include("{cmake_path(MERGER)}")
                    bl_merge_vcpkg_manifests(
                      ROOT_MANIFEST "{cmake_path(root_manifest)}"
                      CHILD_MANIFESTS "{cmake_path(child_manifest)}"
                      PROVIDED_PORTS bitloop
                      OUT_DIR "{cmake_path(output)}"
                      MODE UNION)
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            dependencies = json.loads((output / "vcpkg.json").read_text(encoding="utf-8"))["dependencies"]
            self.assertEqual(dependencies, ["cpr", "zlib"])

    def merge_feature_fixture(self, *, source_mode: bool, request_webgpu: bool) -> tuple[dict, list[str]]:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "bitloop.json"
            (root / "vcpkg-configuration.json").write_text(
                '{"default-registry":{"kind":"builtin","baseline":"fixture"}}\n',
                encoding="utf-8",
            )
            child = root / "child.json"
            manifest = root / "root.json"
            dawn = {
                "name": "dawn", "default-features": False,
                "features": [{"name": "dxc", "platform": "windows"}],
            }
            source.write_text(json.dumps({
                "name": "bitloop", "dependencies": ["sdl3"],
                "features": {"webgpu": {"description": "compute", "dependencies": [dawn]}},
            }), encoding="utf-8")
            dependency = {"name": "bitloop", "features": ["webgpu"]} if request_webgpu else "bitloop"
            child.write_text(json.dumps({"dependencies": [dependency]}), encoding="utf-8")
            manifest.write_text(json.dumps({
                "name": "root", "dependencies": ["bitloop"],
                "features": {"tools": {"description": "tools", "dependencies": ["zlib"]}},
            }), encoding="utf-8")
            source_arguments = (
                f'PROVIDED_PORTS bitloop SOURCE_MANIFEST "{cmake_path(source)}"'
                if source_mode else ""
            )
            script = root / "merge.cmake"
            script.write_text(textwrap.dedent(f"""
                cmake_minimum_required(VERSION 3.21)
                include("{cmake_path(MERGER)}")
                bl_merge_vcpkg_manifests(
                    ROOT_MANIFEST "{cmake_path(manifest)}"
                    CHILD_MANIFESTS "{cmake_path(child)};{cmake_path(source) if source_mode else ''}"
                    {source_arguments}
                    OUT_DIR "{cmake_path(root / 'out')}")
                file(WRITE "{cmake_path(root / 'features.txt')}" "${{BL_SOURCE_FEATURES}}")
            """), encoding="utf-8")
            result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (
                json.loads((root / "out/vcpkg.json").read_text(encoding="utf-8")),
                (root / "features.txt").read_text().split(),
            )

    def test_plain_source_dependency_does_not_install_dawn(self) -> None:
        manifest, features = self.merge_feature_fixture(source_mode=True, request_webgpu=False)
        self.assertEqual(manifest["dependencies"], ["sdl3"])
        self.assertEqual(features, [])

    def test_source_feature_keeps_dawn_backend_selection(self) -> None:
        manifest, features = self.merge_feature_fixture(source_mode=True, request_webgpu=True)
        self.assertEqual(features, ["webgpu"])
        self.assertEqual(manifest["dependencies"], [
            {"name": "dawn", "default-features": False,
             "features": [{"name": "dxc", "platform": "windows"}]},
            "sdl3",
        ])

    def test_package_feature_is_left_for_vcpkg(self) -> None:
        manifest, features = self.merge_feature_fixture(source_mode=False, request_webgpu=True)
        self.assertEqual(manifest["dependencies"], [{"name": "bitloop", "features": ["webgpu"]}])
        self.assertEqual(features, [])

    def test_root_features_survive_manifest_merge(self) -> None:
        manifest, _ = self.merge_feature_fixture(source_mode=True, request_webgpu=False)
        self.assertEqual(manifest["features"]["tools"]["dependencies"], ["zlib"])

    def test_including_installed_helpers_does_not_build_webgpu(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            script = root / "include.cmake"
            helper = BITLOOP_ROOT / "cmake/bitloopWebGPU.cmake"
            script.write_text(textwrap.dedent(f"""
                cmake_minimum_required(VERSION 3.21)
                set(BITLOOP_WITH_WEBGPU ON)
                include("{cmake_path(helper)}")
                if(TARGET bitloop::webgpu)
                    message(FATAL_ERROR "Package helpers must not build source components")
                endif()
            """), encoding="utf-8")
            result = subprocess.run([CMAKE, "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_package_lookup_happens_after_languages_are_enabled(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            package_dir = root / "package"
            package_dir.mkdir()
            (package_dir / "bitloopConfig.cmake").write_text(
                textwrap.dedent(
                    """
                    include(CMakeFindDependencyMacro)
                    find_dependency(Threads)
                    add_library(bitloop::bitloop INTERFACE IMPORTED)
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            source = root / "source"
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                textwrap.dedent(
                    f"""
                    cmake_minimum_required(VERSION 3.21)
                    project(PackageOrdering NONE)
                    set(BITLOOP_PROVIDER PACKAGE CACHE STRING "" FORCE)
                    set(bitloop_DIR "{cmake_path(package_dir)}" CACHE PATH "" FORCE)
                    include("{cmake_path(FINDER)}")
                    bitloop_enable_languages(LANGUAGES C CXX)
                    """
                ).strip()
                + "\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [CMAKE, "-S", str(source), "-B", str(root / "build")],
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
