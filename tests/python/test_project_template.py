from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(BITLOOP_ROOT / "scripts"))

import cli as bitloop_cli  # noqa: E402
from instantiate_template import (  # noqa: E402
    AI_BEGIN,
    AI_ELSE,
    AI_END,
    GUIDE_BEGIN,
    GUIDE_ELSE,
    GUIDE_END,
    create_project,
    process_ai_scaffolding,
    process_template_guidance,
    prompt_for_ai_scaffolding,
    prompt_for_minimal_project,
)


class ProjectTemplateTests(unittest.TestCase):
    def generate(
        self,
        parent: Path,
        name: str | None,
        *,
        minimal: bool = False,
        include_ai: bool | None = None,
        inputs: list[str] | None = None,
    ) -> Path:
        original_cwd = Path.cwd()
        try:
            os.chdir(parent)
            input_mock = mock.patch(
                "builtins.input",
                side_effect=inputs if inputs is not None else AssertionError("unexpected prompt"),
            )
            with input_mock, contextlib.redirect_stdout(io.StringIO()):
                create_project(BITLOOP_ROOT, name, minimal=minimal, include_ai=include_ai)
        finally:
            os.chdir(original_cwd)

        generated_name = name if name is not None else inputs[0]
        return parent / generated_name

    def test_named_project_is_guided_and_non_interactive_by_default(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = self.generate(Path(temporary), "GuidedFixture")

            header = (project / "GuidedFixture" / "GuidedFixture.h").read_text(encoding="utf-8")
            source = (project / "GuidedFixture" / "GuidedFixture.cpp").read_text(encoding="utf-8")
            self.assertIn("double gravity = 9.8", header)
            self.assertIn("Initialize the Scene", source)
            self.assertIn("Example: Scene variable input", source)
            self.assertNotIn(GUIDE_BEGIN, source)
            self.assertNotIn(GUIDE_ELSE, source)
            self.assertNotIn(GUIDE_END, source)
            self.assertNotIn(AI_BEGIN, source)
            self.assertNotIn(AI_ELSE, source)
            self.assertNotIn(AI_END, source)
            self.assertIn("traceFrame", source)
            self.assertIn("captureTraceSnapshot", header)
            self.assertIn("captureTraceSnapshot", source)
            self.assertTrue((project / "AGENTS.md").is_file())
            self.assertTrue((project / ".agents" / "skills" / "bitloop-development" / "SKILL.md").is_file())

    def test_minimal_project_selects_minimal_content_without_markers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = self.generate(Path(temporary), "MinimalFixture", minimal=True)

            header = (project / "MinimalFixture" / "MinimalFixture.h").read_text(encoding="utf-8")
            source = (project / "MinimalFixture" / "MinimalFixture.cpp").read_text(encoding="utf-8")
            cmake = (project / "CMakeLists.txt").read_text(encoding="utf-8")

            self.assertIn("struct Config {};", header)
            self.assertNotIn("double gravity = 9.8", header)
            self.assertNotIn("Example: Scene variable input", source)
            generated_text = header + source + cmake
            self.assertNotIn(GUIDE_BEGIN, generated_text)
            self.assertNotIn(GUIDE_ELSE, generated_text)
            self.assertNotIn(GUIDE_END, generated_text)
            self.assertIn("requestRedraw(true);", source)
            self.assertIn("navigator.handleWorldNavigation(e, true);", source)
            self.assertIn("ctx->transform(camera);", source)
            self.assertIn("sceneUnmounted", source)
            self.assertNotIn("camera.setSurface(ctx)", source)
            self.assertNotIn("ctx->setCamera(camera)", source)
            self.assertIn("bitloop_finalize()", cmake)
            self.assertTrue((project / "tooling" / "bitloop-tooling.json").is_file())
            self.assertTrue((project / ".agents" / "skills" / "bitloop-development" / "SKILL.md").is_file())

    def test_non_ai_project_omits_agent_files_and_diagnostic_scaffolding(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = self.generate(Path(temporary), "PlainFixture", include_ai=False)

            header = (project / "PlainFixture" / "PlainFixture.h").read_text(encoding="utf-8")
            source = (project / "PlainFixture" / "PlainFixture.cpp").read_text(encoding="utf-8")
            cmake = (project / "CMakeLists.txt").read_text(encoding="utf-8")
            generated_text = header + source + cmake

            self.assertFalse((project / "AGENTS.md").exists())
            self.assertFalse((project / ".agents").exists())
            self.assertTrue((project / "tooling" / "bitloop-tooling.json").is_file())
            self.assertNotIn("BITLOOP_DEV_MODE", generated_text)
            self.assertNotIn("SimTrace", generated_text)
            self.assertNotIn("SimCheckpoint", generated_text)
            self.assertNotIn("SimSnapshot", generated_text)
            self.assertNotIn("Codex", generated_text)
            self.assertNotIn(AI_BEGIN, generated_text)
            self.assertNotIn(AI_ELSE, generated_text)
            self.assertNotIn(AI_END, generated_text)

    def test_default_template_is_the_only_source_for_both_variants(self) -> None:
        template_source = (
            BITLOOP_ROOT / "templates" / "default" / "SIM_NAME" / "SIM_NAME.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn(f"// {GUIDE_BEGIN}", template_source)
        self.assertIn(f"// {GUIDE_END}", template_source)
        template_header = (
            BITLOOP_ROOT / "templates" / "default" / "SIM_NAME" / "SIM_NAME.h"
        ).read_text(encoding="utf-8")
        self.assertIn(f"// {GUIDE_ELSE}", template_header)
        self.assertIn(f"// {AI_BEGIN}", template_header)
        self.assertIn(f"// {AI_END}", template_header)
        self.assertFalse((BITLOOP_ROOT / "templates" / "default-overlays").exists())

    def test_guide_processor_keeps_or_removes_only_marked_content(self) -> None:
        template = (
            "before\n"
            f"// {GUIDE_BEGIN}\n"
            "guided content\n"
            f"// {GUIDE_END}\n"
            "after\n"
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            guided = root / "guided.cpp"
            minimal = root / "minimal.cpp"
            guided.write_text(template, encoding="utf-8")
            minimal.write_text(template, encoding="utf-8")

            process_template_guidance(guided, guided=True)
            process_template_guidance(minimal, guided=False)

            self.assertEqual(guided.read_text(encoding="utf-8"), "before\nguided content\nafter\n")
            self.assertEqual(minimal.read_text(encoding="utf-8"), "before\nafter\n")

    def test_guide_processor_selects_the_matching_else_branch(self) -> None:
        template = (
            "before\n"
            f"// {GUIDE_BEGIN}\n"
            "guided content\n"
            f"// {GUIDE_ELSE}\n"
            "minimal content\n"
            f"// {GUIDE_END}\n"
            "after\n"
        )

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            guided = root / "guided.cpp"
            minimal = root / "minimal.cpp"
            guided.write_text(template, encoding="utf-8")
            minimal.write_text(template, encoding="utf-8")

            process_template_guidance(guided, guided=True)
            process_template_guidance(minimal, guided=False)

            self.assertEqual(guided.read_text(encoding="utf-8"), "before\nguided content\nafter\n")
            self.assertEqual(minimal.read_text(encoding="utf-8"), "before\nminimal content\nafter\n")

    def test_guide_processor_rejects_invalid_markers(self) -> None:
        invalid_templates = {
            "nested": (
                f"// {GUIDE_BEGIN}\n"
                f"// {GUIDE_BEGIN}\n"
                f"// {GUIDE_END}\n"
            ),
            "unmatched begin": f"// {GUIDE_BEGIN}\n",
            "else outside block": f"// {GUIDE_ELSE}\n",
            "duplicate else": (
                f"// {GUIDE_BEGIN}\n"
                f"// {GUIDE_ELSE}\n"
                f"// {GUIDE_ELSE}\n"
                f"// {GUIDE_END}\n"
            ),
            "unmatched end": f"// {GUIDE_END}\n",
            "malformed": f"// {GUIDE_BEGIN} trailing text\n",
        }

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "invalid.cpp"
            for case, template in invalid_templates.items():
                with self.subTest(case=case):
                    path.write_text(template, encoding="utf-8")
                    with self.assertRaisesRegex(RuntimeError, r"line [123]"):
                        process_template_guidance(path, guided=True)

    def test_ai_processor_removes_disabled_scaffolding_and_markers(self) -> None:
        template = (
            "before\n"
            f"// {AI_BEGIN}\n"
            "AI scaffold\n"
            f"// {AI_END}\n"
            "after\n"
        )

        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "plain.cpp"
            path.write_text(template, encoding="utf-8")
            process_ai_scaffolding(path, include_ai=False)
            self.assertEqual(path.read_text(encoding="utf-8"), "before\nafter\n")

    def test_interactive_generation_asks_for_project_variant(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = io.StringIO()
            original_cwd = Path.cwd()
            try:
                os.chdir(temporary)
                with mock.patch("builtins.input", side_effect=["InteractiveFixture", "2", "n"]):
                    with contextlib.redirect_stdout(output):
                        create_project(BITLOOP_ROOT)
            finally:
                os.chdir(original_cwd)

            source = (
                Path(temporary) / "InteractiveFixture" / "InteractiveFixture" / "InteractiveFixture.cpp"
            ).read_text(encoding="utf-8")
            self.assertIn("Project variant", output.getvalue())
            self.assertIn("Creating project", output.getvalue())
            self.assertFalse((Path(temporary) / "InteractiveFixture" / "AGENTS.md").exists())
            self.assertNotIn("initialize scene", source)

    def test_interactive_variant_defaults_to_guided(self) -> None:
        with mock.patch("builtins.input", return_value=""):
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(prompt_for_minimal_project())

    def test_interactive_ai_scaffolding_defaults_to_enabled(self) -> None:
        with mock.patch("builtins.input", return_value=""):
            self.assertTrue(prompt_for_ai_scaffolding())

    def test_cli_forwards_minimal_flag(self) -> None:
        with mock.patch.object(sys, "argv", ["bitloop", "new", "project", "CliFixture", "--minimal"]):
            with mock.patch.object(bitloop_cli, "instantiate_template") as instantiate:
                bitloop_cli.main()

        instantiate.assert_called_once_with(
            bitloop_root=BITLOOP_ROOT,
            type="project",
            name="CliFixture",
            minimal=True,
            include_ai=None,
            submodule=None,
            remote=None,
        )

    def test_cli_forwards_no_ai_flag(self) -> None:
        with mock.patch.object(sys, "argv", ["bitloop", "new", "project", "CliFixture", "--no-ai"]):
            with mock.patch.object(bitloop_cli, "instantiate_template") as instantiate:
                bitloop_cli.main()

        instantiate.assert_called_once_with(
            bitloop_root=BITLOOP_ROOT,
            type="project",
            name="CliFixture",
            minimal=False,
            include_ai=False,
            submodule=None,
            remote=None,
        )

    def test_cli_forwards_submodule_remote(self) -> None:
        argv = [
            "bitloop",
            "new",
            "project",
            "CliFixture",
            "--submodule",
            "--remote",
            "https://example.invalid/CliFixture.git",
        ]
        with mock.patch.object(sys, "argv", argv):
            with mock.patch.object(bitloop_cli, "instantiate_template") as instantiate:
                bitloop_cli.main()

        instantiate.assert_called_once_with(
            bitloop_root=BITLOOP_ROOT,
            type="project",
            name="CliFixture",
            minimal=False,
            include_ai=None,
            submodule=True,
            remote="https://example.invalid/CliFixture.git",
        )


if __name__ == "__main__":
    unittest.main()
