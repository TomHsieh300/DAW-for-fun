"""Tests for scripts/check_copies.py. Run: python3 -m unittest discover -s scripts/tests -v

Each test builds a small tree of chapters in a temporary folder, copies the script into it (the script
finds its root from its own place) and runs it the way CI does.
"""

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "check_copies.py"
HEADER = "# test list\n"


class CheckCopiesTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        (self.root / "scripts").mkdir()
        shutil.copy(SCRIPT, self.root / "scripts" / "check_copies.py")
        self.list_file = self.root / "scripts" / "copied_files.txt"
        self.list_file.write_text(HEADER, encoding="utf-8")

    def chapter(self, name):
        for folder in ("engine", "app/Source", "tests"):
            (self.root / "lessons" / name / folder).mkdir(parents=True, exist_ok=True)

    def put(self, chapter, name, text):
        path = self.root / "lessons" / chapter / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8", newline="\n")

    def set_list(self, *lines):
        self.list_file.write_text(HEADER + "".join(line + "\n" for line in lines), encoding="utf-8", newline="\n")

    def run_script(self, *args):
        result = subprocess.run([sys.executable, str(self.root / "scripts" / "check_copies.py"), *args],
                                capture_output=True, encoding="utf-8")
        return result.returncode, result.stdout + result.stderr

    def three_chapters_sharing_meters(self):
        for chapter in ("14-a", "15-b", "16-c"):
            self.chapter(chapter)
            self.put(chapter, "app/Source/Meters.cpp", "int meters;\n")
        self.set_list("14 15 16 app/Source/Meters.cpp")

    # ---- S1: the check trusts the disk too, not only the list ----

    def test_listed_files_identical_passes(self):
        self.three_chapters_sharing_meters()
        code, out = self.run_script()
        self.assertEqual(code, 0, out)
        self.assertIn("0 differ", out)

    def test_identical_file_that_is_not_listed_fails(self):
        self.three_chapters_sharing_meters()
        for chapter in ("14-a", "15-b"):
            self.put(chapter, "app/Source/NewShared.h", "#pragma once\n")
        code, out = self.run_script()
        self.assertEqual(code, 1, out)
        self.assertIn("NewShared.h", out)
        self.assertIn("not in scripts/copied_files.txt", out)

    def test_new_chapter_that_is_a_copy_of_a_listed_file_fails(self):
        self.three_chapters_sharing_meters()
        self.chapter("17-d")
        self.put("17-d", "app/Source/Meters.cpp", "int meters;\n")
        code, out = self.run_script()
        self.assertEqual(code, 1, out)
        self.assertIn("Meters.cpp", out)
        self.assertIn("not in scripts/copied_files.txt", out)

    def test_drift_of_a_listed_file_reports_only_the_difference(self):
        self.three_chapters_sharing_meters()
        self.put("14-a", "app/Source/Meters.cpp", "int changed;\n")
        code, out = self.run_script()
        self.assertEqual(code, 1, out)
        self.assertIn("differs between", out)
        self.assertNotIn("not in scripts/copied_files.txt", out)


if __name__ == "__main__":
    unittest.main()
