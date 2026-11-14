"""Tests for scripts/check_links.py. Run: python3 -m unittest discover -s scripts/tests -v"""

import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / "check_links.py"
spec = importlib.util.spec_from_file_location("check_links", SCRIPT)
check_links = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_links)


class CheckLinksTest(unittest.TestCase):
    def setUp(self):
        # anchors_of keeps a cache keyed by path: every test works in a folder of its own
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)

    def write(self, name, text):
        path = self.root / name
        path.write_text(text, encoding="utf-8", newline="\n")
        return path

    def broken(self, links_text):
        """(target, reason) of every broken link in a file that holds links_text."""
        path = self.write("links.md", links_text)
        return [(target, reason) for _, target, reason in check_links.broken_links(path)]

    # ---- S3: a heading's anchor keeps the text of its inline code ----

    def test_anchor_keeps_the_text_of_inline_code(self):
        self.write("target.md", "# Target\n\n## The `process()` method\n\ntext\n\n## Plain heading\n")
        broken = self.broken("[a](target.md#the-process-method)\n"
                             "[b](target.md#the--method)\n"
                             "[c](target.md#plain-heading)\n"
                             "[d](target.md#nope)\n")
        self.assertEqual(broken, [("target.md#the--method", "no such heading"),
                                  ("target.md#nope", "no such heading")])

    def test_anchor_of_a_heading_that_is_only_code(self):
        self.write("target.md", "## `Engine::process`\n")
        self.assertEqual(self.broken("[a](target.md#engineprocess)\n"), [])

    def test_a_link_inside_an_inline_code_span_is_ignored(self):
        self.assertEqual(self.broken("Write `[x](nowhere.md)` like this.\n"), [])

    def test_a_link_inside_a_fenced_block_is_ignored(self):
        self.assertEqual(self.broken("```\n[x](nowhere.md)\n```\n"), [])

    def test_a_link_outside_code_on_a_line_with_code_is_found(self):
        self.assertEqual(self.broken("`code` and [x](nowhere.md)\n"), [("nowhere.md", "no such file")])

    def test_a_heading_inside_a_fenced_block_is_not_an_anchor(self):
        self.write("target.md", "```\n## Hidden\n```\n")
        self.assertEqual(self.broken("[a](target.md#hidden)\n"), [("target.md#hidden", "no such heading")])

    def test_duplicate_headings_get_a_numbered_suffix(self):
        self.write("target.md", "## Same\n\n## Same\n")
        self.assertEqual(self.broken("[a](target.md#same)\n[b](target.md#same-1)\n[c](target.md#same-2)\n"),
                         [("target.md#same-2", "no such heading")])

    def test_an_id_inside_a_code_span_is_not_an_anchor(self):
        self.write("target.md", 'Write `<a id="fake"></a>` and <a id="real"></a>.\n')
        self.assertEqual(self.broken("[a](target.md#real)\n[b](target.md#fake)\n"),
                         [("target.md#fake", "no such heading")])

    def test_missing_file_is_reported(self):
        self.assertEqual(self.broken("[a](missing.md)\n"), [("missing.md", "no such file")])

    def test_external_links_are_not_checked(self):
        self.assertEqual(self.broken("[a](https://example.invalid/x)\n"), [])


if __name__ == "__main__":
    unittest.main()
