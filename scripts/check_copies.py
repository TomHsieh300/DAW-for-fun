#!/usr/bin/env python3
"""Check that the files the chapters copy from each other are still the same.

Usage: python3 scripts/check_copies.py            check against scripts/copied_files.txt
       python3 scripts/check_copies.py --write    write that list from what is in the chapters now

Chapters 14 and up each start from a copy of the engine and the application of the chapter before,
and changing the copy is the lesson. The files a chapter did not change stay in the list below. If
one of them changes in only one chapter, for example a fix that never reached the other copies,
this script fails. It also fails for a file that is the same in several chapters but is not in the list
(a new shared file, or a new chapter that starts as a copy), so that nothing is shared without being
checked. A difference that is meant to be there leaves the list with --write, where a reviewer sees it
in the diff. Exits with 1 on a difference. Needs only the standard library.
"""

import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIST = ROOT / "scripts" / "copied_files.txt"
# The chapters with an engine and an app of their own: each starts from a copy of the one before. Found from the
# folders, so that a new chapter is checked without anyone adding it here.
CHAPTERS = sorted(p.name for p in (ROOT / "lessons").iterdir() if (p / "engine").is_dir() and (p / "app").is_dir())
COPIED_FOLDERS = ["engine", "app", "tests"]
HEADER = """\
# Files that are the same in several chapters, written by scripts/check_copies.py --write.
# Every line: the chapters, then the path inside each chapter.
"""


def content_of(path):
    """The file's bytes, with the line ends a checkout on Windows may have changed undone."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def current_groups():
    """Every set of two or more chapters holding the same file, as (chapters, path)."""
    versions = defaultdict(lambda: defaultdict(list))
    for chapter in CHAPTERS:
        chapter_root = ROOT / "lessons" / chapter
        for folder in COPIED_FOLDERS:
            for path in sorted((chapter_root / folder).rglob("*")):
                if path.is_file():
                    versions[path.relative_to(chapter_root).as_posix()][content_of(path)].append(chapter)
    groups = []
    for name, by_content in sorted(versions.items()):
        groups += [(tuple(chapters), name) for chapters in by_content.values() if len(chapters) > 1]
    return groups


def format_line(chapters, name):
    return " ".join(chapter.split("-")[0] for chapter in chapters) + " " + name


def read_list():
    numbers = {chapter.split("-")[0]: chapter for chapter in CHAPTERS}
    entries = []
    for line in LIST.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            *chapters, name = line.split()
            entries.append(([numbers[number] for number in chapters], name))
    return entries


def differences(chapters, name):
    """What is wrong with one list entry, or an empty string."""
    paths = [ROOT / "lessons" / chapter / name for chapter in chapters]
    missing = [chapter for chapter, path in zip(chapters, paths) if not path.is_file()]
    if missing:
        return "missing in " + ", ".join(missing)
    if len({content_of(path) for path in paths}) > 1:
        return "differs between " + ", ".join(chapters)
    return ""


def main():
    if "--write" in sys.argv[1:]:
        groups = current_groups()
        lines = [format_line(chapters, name) for chapters, name in groups]
        LIST.write_text(HEADER + "\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        print(f"{len(groups)} copied files written to {LIST.relative_to(ROOT).as_posix()}")
        return 0

    entries = read_list()
    failures = 0
    failing = set()
    for chapters, name in entries:
        problem = differences(chapters, name)
        if problem:
            print(f"{name}: {problem}")
            failing.add(name)
            failures += 1
    # The list says which files are shared, the folders say which ones are. A file that is the same in several
    # chapters must be in the list, or nobody notices when one copy is changed later. A name that already fails
    # above is not reported a second time.
    listed = {(tuple(chapters), name) for chapters, name in entries}
    unlisted = [(chapters, name) for chapters, name in current_groups() if (chapters, name) not in listed and name not in failing]
    for chapters, name in unlisted:
        print(f"{name}: identical in {', '.join(chapter.split('-')[0] for chapter in chapters)} but not in scripts/copied_files.txt")
    failures += len(unlisted)
    print(f"{len(entries)} copied files, {failures} differ or are not listed")
    if failures:
        print("Change every copy, or, if the difference is meant to be there: python3 scripts/check_copies.py --write")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
