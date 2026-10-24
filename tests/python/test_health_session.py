from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


BITLOOP_ROOT = Path(__file__).resolve().parents[2]
HEALTH_SCRIPT_DIRECTORY = BITLOOP_ROOT / ".agents" / "skills" / "bitloop-development" / "scripts"
sys.dont_write_bytecode = True
sys.path.insert(0, str(HEALTH_SCRIPT_DIRECTORY))

from health_session import (  # noqa: E402
    MARKER_NAME,
    HealthSessionError,
    clean_session,
    create_session,
    execute_session,
    load_session,
)


class HealthSessionTests(unittest.TestCase):
    def test_create_execute_and_clean_one_marked_session(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "build" / "codex" / "health"
            session = create_session(root, "pairing")

            self.assertEqual(session.parent, root.resolve())
            for child in ("monitors", "raw", "analysis"):
                self.assertTrue((session / child).is_dir())

            validated, marker = load_session(session)
            self.assertEqual(validated, session)
            self.assertEqual(marker["label"], "pairing")

            code = (
                "import os, sys; from pathlib import Path; "
                "p=Path(os.environ['BITLOOP_HEALTH_SESSION']); assert p == Path(sys.argv[1]); "
                "(p/'analysis'/'environment.txt').write_text(str(p), encoding='utf-8')"
            )
            self.assertEqual(execute_session(session, [sys.executable, "-c", code, "{session}"]), 0)
            self.assertEqual((session / "analysis" / "environment.txt").read_text(encoding="utf-8"), str(session))

            removed = clean_session(session)
            self.assertEqual(removed, session)
            self.assertFalse(session.exists())
            self.assertTrue(root.is_dir())

    def test_clean_rejects_unmarked_and_mismatched_directories(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            unmarked = root / "unmarked"
            unmarked.mkdir()
            sentinel = unmarked / "sentinel.txt"
            sentinel.write_text("keep\n", encoding="utf-8")

            with self.assertRaises(HealthSessionError):
                clean_session(unmarked)
            self.assertTrue(sentinel.is_file())

            forged = root / "forged"
            forged.mkdir()
            marker = {
                "schema_version": 1,
                "session_id": "fixture",
                "label": "fixture",
                "created_utc": "2026-01-01T00:00:00+00:00",
                "root": str(root / "different-root"),
                "session": str(forged),
            }
            (forged / MARKER_NAME).write_text(json.dumps(marker), encoding="utf-8")

            with self.assertRaises(HealthSessionError):
                clean_session(forged)
            self.assertTrue(forged.is_dir())

    def test_create_rejects_unsafe_label(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(HealthSessionError):
                create_session(Path(temporary), "../escape")


if __name__ == "__main__":
    unittest.main()
