# ────────────────────────────────────────────────────────
# |                      FastLanes                       |
# ────────────────────────────────────────────────────────
# scripts/ci/test_dispatch_drivers.py
# ────────────────────────────────────────────────────────
"""Negative controls for the assertion-enabled CMake subprocess test drivers."""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform != "win32" and shutil.which("cmake"), "requires POSIX and CMake")
class DispatchDriverTest(unittest.TestCase):
    def check_rejected(self, driver, abort):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "fake_dispatch"
            # The normal-exit control supplies every diagnostic fragment. The
            # aborting control supplies the right signal but deliberately wrong
            # types. Neither is a valid assertion test result.
            types = "WRONG_OPERATOR and WRONG_COLUMN" if abort else (
                "monostate enc_fsst_opr dec_fsst_opr enc_scan_opr enc_analyze_opr "
                "dec_unffor_opr dec_rsum_opr dec_slpatch_opr dec_constant_opr "
                "TypedCol FLSStrColumn PhysicalExpr List Struct"
            )
            executable.write_text(
                f"#!{sys.executable}\n"
                "import os, resource, sys\n"
                "if sys.argv[1:] == ['--assertions']:\n"
                "    print('enabled', end='')\n"
                "    sys.exit(0)\n"
                "resource.setrlimit(resource.RLIMIT_CORE, (0, 0))\n"
                f"print('ABORTED UNREACHABLE with unexpected types: {types} "
                "rsum_operator.cpp slpatch_operator.cpp transpose_operator.cpp "
                "materializer.cpp', file=sys.stderr, flush=True)\n"
                + ("os.abort()\n" if abort else "sys.exit(1)\n")
            )
            executable.chmod(0o755)
            result = subprocess.run(
                ["cmake", f"-DTEST_EXECUTABLE={executable}", "-P",
                 str(root / "test/standalone" / driver)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Expected assertion", result.stderr)

    def test_normal_exit_is_not_an_assertion(self):
        for driver in ("variant_dispatch_invalid.cmake", "materializer_dispatch_invalid.cmake"):
            with self.subTest(driver=driver):
                self.check_rejected(driver, abort=False)

    def test_aborting_with_wrong_types_is_not_success(self):
        for driver in ("variant_dispatch_invalid.cmake", "materializer_dispatch_invalid.cmake"):
            with self.subTest(driver=driver):
                self.check_rejected(driver, abort=True)


if __name__ == "__main__":
    unittest.main()
