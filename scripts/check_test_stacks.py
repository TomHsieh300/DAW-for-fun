#!/usr/bin/env python3
"""Check that no test program puts more than two engines on the stack in one function.

Usage: python3 scripts/check_test_stacks.py

From chapter 15 on, an Engine holds its whole pool of track slots in itself: some 190 KB. A Debug build of MSVC
gives every block of a function its own room on the stack, and Windows gives a program's main thread 1 MB: a test
with five engines in one function overflowed it now and then (CI #72 and #77, test_midi). Two are still far
inside; more go on the heap (std::make_unique<Engine>()). This script looks at the tests, solutions, exercises,
examples and steps of every chapter whose engine has the pool, and fails if a function declares more than two
Engines as local variables. Exits with 1 if one does. Needs only the standard library.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MOST = 2
FOLDERS = ["tests", "solutions", "exercises", "examples", "steps"]
# A line that starts a function: something, then ") {" or ") const {" at its end, at the top level.
FUNCTION_START = re.compile(r"\n(?=\S[^\n;]*\)\s*(?:const\s*)?\{)")
ENGINE_ON_STACK = re.compile(r"(?m)^\s+Engine\s+\w+\s*(?:\([^)]*\))?\s*;")
FUNCTION_NAME = re.compile(r"[^(]*?(\w+)\(")


def has_the_pool(chapter):
    constants = chapter / "engine" / "include" / "dawlab" / "engine" / "Constants.h"
    return constants.is_file() and "kMaxTracks = DAWLAB_ENGINE_MAX_TRACKS" in constants.read_text(encoding="utf-8")


def problems():
    found = []
    for chapter in sorted(p for p in (ROOT / "lessons").iterdir() if has_the_pool(p)):
        for folder in FOLDERS:
            for path in sorted((chapter / folder).rglob("*.cpp")):
                text = path.read_text(encoding="utf-8", errors="replace")
                for part in FUNCTION_START.split(text):
                    engines = len(ENGINE_ON_STACK.findall(part))
                    if engines > MOST:
                        name = FUNCTION_NAME.match(part)
                        found.append(f"{path.relative_to(ROOT).as_posix()}: {name.group(1) if name else '?'}() has {engines} engines on the stack")
    return found


def main():
    found = problems()
    for line in found:
        print(line)
    print(f"{len(found)} functions with more than {MOST} engines on the stack")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
