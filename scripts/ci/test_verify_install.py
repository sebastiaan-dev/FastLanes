# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# scripts/ci/test_verify_install.py
# ────────────────────────────────────────────────────────
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from verify_install import missing_headers


class VerifyInstallTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source include"
        self.installed = self.root / "installed include"
        self.source.mkdir()
        self.installed.mkdir()
        for relative in ("fls/api.h", "fls/nested/type.hpp"):
            for root in (self.source, self.installed):
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("// header\n")

    def test_complete_install(self):
        self.assertEqual(missing_headers(self.source, self.installed), [])

    def test_missing_nested_header(self):
        (self.installed / "fls/nested/type.hpp").unlink()
        self.assertEqual(
            missing_headers(self.source, self.installed), [Path("fls/nested/type.hpp")]
        )

    def test_directory_does_not_count_as_header(self):
        path = self.installed / "fls/api.h"
        path.unlink()
        path.mkdir()
        self.assertEqual(missing_headers(self.source, self.installed), [Path("fls/api.h")])

    def test_non_headers_are_ignored(self):
        (self.source / "LICENSE").write_text("license\n")
        self.assertEqual(missing_headers(self.source, self.installed), [])

    def test_missing_include_directory(self):
        with self.assertRaises(ValueError):
            missing_headers(self.source, self.root / "missing")
        with self.assertRaises(ValueError):
            missing_headers(self.root / "missing", self.installed)

    def test_empty_source_is_an_error(self):
        empty = self.root / "empty"
        empty.mkdir()
        with self.assertRaises(ValueError):
            missing_headers(empty, self.installed)

    def test_cli_exit_status(self):
        command = [
            sys.executable,
            str(Path(__file__).with_name("verify_install.py")),
            str(self.source),
            str(self.installed),
        ]
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
        (self.installed / "fls/api.h").unlink()
        self.assertEqual(subprocess.run(command, capture_output=True).returncode, 1)


if __name__ == "__main__":
    unittest.main()
