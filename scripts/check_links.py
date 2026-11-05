#!/usr/bin/env python3
"""Check the relative links and images in every tracked Markdown file.

Usage: python3 scripts/check_links.py

A link fails when its file does not exist or its #anchor is not a heading of that file. External
links are not checked: CI should not fail because someone else's site is down. Exits with 1 when
a link is broken. Needs only the standard library and git.
"""

import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parent.parent

# ![alt](target) and [text](target), with an optional "title" after the target
INLINE_LINK = re.compile(r'!?\[[^\]]*\]\(\s*<?([^)\s>]+)>?(?:\s+"[^"]*")?\s*\)')
HTML_SOURCE = re.compile(r'<(?:img|a)\b[^>]*\b(?:src|href)="([^"]+)"')
HEADING = re.compile(r"^#{1,6}\s+(.*?)\s*#*\s*$")
HTML_ID = re.compile(r'\b(?:id|name)="([^"]+)"')
FENCE = re.compile(r"^\s*(```|~~~)")
INLINE_CODE = re.compile(r"`[^`]*`")
EXTERNAL = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.-]*:|^//")


def prose_lines(text):
    """The lines outside fenced code blocks, as (line number, line without inline code)."""
    in_fence = False
    for number, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            in_fence = not in_fence
        elif not in_fence:
            yield number, INLINE_CODE.sub("", line)


def github_slug(heading):
    """The anchor GitHub generates: lower case, no punctuation, spaces become hyphens."""
    heading = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", heading)  # keep only a link's text
    slug = "".join(c for c in heading.lower() if c.isalnum() or c in " -_")
    return slug.replace(" ", "-")


def anchors_of(path, cache={}):
    if path not in cache:
        anchors = set()
        seen = {}
        for _, line in prose_lines(path.read_text(encoding="utf-8")):
            anchors.update(HTML_ID.findall(line))
            match = HEADING.match(line)
            if match:
                slug = github_slug(match.group(1))
                count = seen.get(slug, 0)
                seen[slug] = count + 1
                anchors.add(f"{slug}-{count}" if count else slug)
        cache[path] = anchors
    return cache[path]


def broken_links(path):
    for number, line in prose_lines(path.read_text(encoding="utf-8")):
        for target in INLINE_LINK.findall(line) + HTML_SOURCE.findall(line):
            if EXTERNAL.match(target):
                continue
            file_part, _, anchor = unquote(target).partition("#")
            linked = (path.parent / file_part).resolve() if file_part else path
            if not linked.exists():
                yield number, target, "no such file"
            elif anchor and linked.suffix == ".md" and anchor.lower() not in anchors_of(linked):
                yield number, target, "no such heading"


def main():
    tracked = subprocess.run(["git", "ls-files", "*.md"], cwd=ROOT, check=True,
                             capture_output=True, encoding="utf-8").stdout.splitlines()
    failures = 0
    for name in tracked:
        for number, target, reason in broken_links(ROOT / name):
            print(f"{name}:{number}: {target}: {reason}")
            failures += 1
    print(f"{len(tracked)} Markdown files, {failures} broken links")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
