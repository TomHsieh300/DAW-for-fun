"""Tests for scripts/build.sh, scripts/build.ps1 and the DAWLAB_ONLY check of the root CMakeLists.txt.

Run: python3 -m unittest discover -s scripts/tests -v

build.sh is run with a fake cmake and ctest first on the PATH: they only write their arguments to a log, so
these tests need no compiler and take milliseconds. The tests of the CMake check itself need the real cmake
(configure only, nothing is built).
"""

import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
BUILD_SH = ROOT / "scripts" / "build.sh"
BUILD_PS1 = ROOT / "scripts" / "build.ps1"

FAKE_TOOL = """#!/bin/sh
echo "$(basename "$0") $*" >> "$FAKE_LOG"
"""


@unittest.skipUnless(shutil.which("bash"), "needs bash")
class BuildShTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.tmp = Path(self._tmp.name)
        self.fakes = self.tmp / "fakes"
        self.fakes.mkdir()
        for tool in ("cmake", "ctest"):
            path = self.fakes / tool
            path.write_text(FAKE_TOOL, encoding="utf-8", newline="\n")
            path.chmod(path.stat().st_mode | stat.S_IXUSR)
        self.log = self.tmp / "log.txt"

    def run_build(self, *args):
        env = dict(os.environ, FAKE_LOG=str(self.log), PATH=str(self.fakes) + os.pathsep + os.environ["PATH"])
        result = subprocess.run(["bash", str(BUILD_SH), "--build-dir", str(self.tmp / "build"), *args],
                                capture_output=True, encoding="utf-8", env=env)
        calls = self.log.read_text(encoding="utf-8").splitlines() if self.log.exists() else []
        return result.returncode, result.stdout + result.stderr, calls

    # ---- S5: a test run that finds no test fails ----

    def test_ctest_is_called_with_no_tests_error(self):
        code, out, calls = self.run_build("--only", "02-filter")
        self.assertEqual(code, 0, out)
        ctest = [call for call in calls if call.startswith("ctest ")]
        self.assertEqual(len(ctest), 1, calls)
        self.assertIn("--no-tests=error", ctest[0])

    # ---- S10: an option that needs a value says so ----

    def test_option_without_a_value_is_a_usage_error(self):
        for args in (["--only"], ["--juce", "--only"], ["--build-dir"], ["--config"], ["--test-label"], ["--sanitize"],
                     ["--only", "--juce"], ["--test-label", "--no-test"], ["--build-dir", ""]):
            with self.subTest(args=args):
                code, out, calls = self.run_build(*args)
                option = [arg for arg in args if arg.startswith("--") and arg in
                          ("--only", "--build-dir", "--config", "--test-label", "--sanitize")][-1]
                self.assertEqual(code, 2, out)
                self.assertIn(f"{option} needs a value", out)
                self.assertNotIn("unbound variable", out)
                self.assertEqual(calls, [], "nothing may be configured or built")

    def test_empty_only_still_means_every_chapter_like_build_ps1(self):
        code, out, calls = self.run_build("--only", "")
        self.assertEqual(code, 0, out)
        self.assertIn("-DDAWLAB_ONLY=", [arg for arg in calls[0].split() if arg.startswith("-DDAWLAB_ONLY")])

    def test_only_with_several_chapters_passes_them_to_cmake_with_semicolons(self):
        code, out, calls = self.run_build("--only", "02-filter,03-eq", "--juce")
        self.assertEqual(code, 0, out)
        self.assertIn("-DDAWLAB_ONLY=02-filter;03-eq", calls[0])
        self.assertIn("-DDAWLAB_WITH_JUCE=ON", calls[0])

    def test_build_ps1_calls_ctest_with_no_tests_error_too(self):
        # There is no PowerShell on the Linux runner: the Windows jobs run the real thing. This only keeps the
        # two scripts in step.
        self.assertIn("--no-tests=error", BUILD_PS1.read_text(encoding="utf-8"))


@unittest.skipUnless(shutil.which("cmake"), "needs cmake")
class OnlyNamesTest(unittest.TestCase):
    """DAWLAB_ONLY is checked in CMake, the one place that knows the chapters: build.sh, build.ps1 and a
    hand-typed cmake command all get the same answer."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)

    def configure(self, only):
        result = subprocess.run(["cmake", "-S", str(ROOT), "-B", self._tmp.name, f"-DDAWLAB_ONLY={only}",
                                 "-DDAWLAB_WITH_JUCE=OFF"], capture_output=True, encoding="utf-8")
        return result.returncode, result.stdout + result.stderr

    def test_a_mistyped_chapter_is_refused_and_the_valid_names_are_listed(self):
        code, out = self.configure("02-fliter")
        self.assertNotEqual(code, 0, out)
        self.assertIn("02-fliter", out)
        self.assertIn("02-filter", out)

    def test_one_wrong_name_among_right_ones_is_refused(self):
        code, out = self.configure("02-filter;03-eqq")
        self.assertNotEqual(code, 0, out)
        self.assertIn("03-eqq", out)

    def test_a_chapter_and_the_groups_are_accepted(self):
        for only in ("02-filter", "02-filter;engine", "juce", "engine"):
            with self.subTest(only=only):
                code, out = self.configure(only)
                self.assertEqual(code, 0, out)

    def test_a_value_that_names_no_chapter_is_refused(self):
        code, out = self.configure(";")
        self.assertNotEqual(code, 0, out)
        self.assertIn("names no chapter", out)

    def test_empty_means_every_chapter(self):
        code, out = self.configure("")
        self.assertEqual(code, 0, out)


if __name__ == "__main__":
    unittest.main()
