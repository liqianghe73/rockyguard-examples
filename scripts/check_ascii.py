#!/usr/bin/env python3
"""Enforce ASCII-only sources, and repair mojibake and BOMs.

Why this exists
---------------
Without /utf-8, MSVC reads source files in the system ANSI codepage. On a GBK- or
Shift-JIS-locale Windows machine a single em dash in a comment either mis-encodes
or raises C4819, and the error never mentions encoding. We pass /utf-8 in CMake
anyway, but a contributor's editor and toolchain are not ours to configure, so
sources stay ASCII and CI proves it.

The rule earned its keep immediately: a PowerShell Get-Content/Set-Content round
trip over a UTF-8 file silently mangled every em dash into cp1252 mojibake, and a
UTF-8 BOM landed at the top of a .cpp file. Both are invisible in a diff view and
both are exactly what this gate makes loud.

Note on implementation: every mapping below is written with \\u escapes, so THIS
FILE is itself pure ASCII. That is not decoration. An earlier version embedded the
literal characters, and running --fix rewrote its own tables into no-ops -- the
script quietly destroyed itself and then reported success.

Usage
-----
    python scripts/check_ascii.py          # check, exit 1 on any offence
    python scripts/check_ascii.py --fix    # rewrite offenders as ASCII
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUFFIXES = {".cpp", ".h", ".hpp", ".txt", ".cmake", ".md", ".yml", ".yaml", ".py", ".json"}
SKIP_DIRS = {".git", "build", "build-stub", "build-sdk", ".sdk", "__pycache__"}

BOM = "\ufeff"

# Genuine non-ASCII we are willing to transliterate. Written as \u escapes so
# THIS FILE stays pure ASCII and cannot rewrite its own tables into no-ops. An
# earlier version embedded the literal characters; running --fix then replaced
# each key with its own ASCII value, silently turning every rule into a no-op
# while still reporting success.
TRANSLITERATE = [
    ("\u2014", "--"),    # em dash
    ("\u2013", "-"),     # en dash
    ("\u2018", "'"),     # left single quote
    ("\u2019", "'"),     # right single quote
    ("\u201c", '"'),     # left double quote
    ("\u201d", '"'),     # right double quote
    ("\u2026", "..."),   # ellipsis
    ("\u2192", "->"),    # rightwards arrow
    ("\u2190", "<-"),    # leftwards arrow
    ("\u2265", ">="),    # greater-than or equal to
    ("\u2264", "<="),    # less-than or equal to
    ("\u00a0", " "),     # non-breaking space
    ("\u00d7", "x"),     # multiplication sign
    ("\u00b0", " deg"),  # degree sign
    ("\u2713", "OK"),    # check mark
    ("\u26a0", "!"),     # warning sign
    ("\u2022", "*"),     # bullet
]


def _mojibake_table():
    """Derive the cp1252 mojibake form of every character we transliterate.

    Mojibake happens when UTF-8 bytes get decoded as cp1252, so the mojibake
    form of a character is exactly utf8-encode-then-cp1252-decode. Deriving it
    beats hand-transcribing sequences like the three-codepoint mangling of an em
    dash, which is unreadable in source and easy to get subtly wrong.

    Sorted longest-first: these share two-character prefixes, so applying a
    short match before a long one would corrupt the longer sequences.
    """
    table = []
    for ch, repl in TRANSLITERATE:
        try:
            table.append((ch.encode("utf-8").decode("cp1252"), repl))
        except UnicodeDecodeError:
            # Not every UTF-8 byte sequence is valid cp1252, so some characters
            # cannot arrive as mojibake by this route at all.
            continue
    table.sort(key=lambda pair: len(pair[0]), reverse=True)
    return table


MOJIBAKE = _mojibake_table()


def normalize(text: str) -> str:
    # BOM is stripped, never transliterated. Replacing it with a placeholder puts
    # a stray character before the first token and breaks the compile.
    text = text.replace(BOM, "")
    for bad, good in MOJIBAKE:
        text = text.replace(bad, good)
    for bad, good in TRANSLITERATE:
        text = text.replace(bad, good)
    return text


def candidates() -> list[Path]:
    out: list[Path] = []
    for p in ROOT.rglob("*"):
        if not p.is_file() or p.suffix.lower() not in SUFFIXES:
            continue
        if any(part in SKIP_DIRS for part in p.relative_to(ROOT).parts):
            continue
        out.append(p)
    return sorted(out)


def offending_lines(text: str) -> list[tuple[int, str]]:
    return [
        (i, line)
        for i, line in enumerate(text.splitlines(), 1)
        if any(ord(ch) > 127 for ch in line)
    ]


def main() -> int:
    fix = "--fix" in sys.argv
    files = candidates()
    offenders: list[tuple[Path, list[tuple[int, str]]]] = []
    fixed_count = 0

    for path in files:
        raw = path.read_bytes()
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            # Not even valid UTF-8. Recover via cp1252 so --fix can still help.
            text = raw.decode("cp1252", errors="replace")

        cleaned = normalize(text)
        leftovers = offending_lines(cleaned)

        if cleaned == text and not leftovers:
            continue

        if fix:
            # Anything the tables did not cover becomes '?' rather than being
            # left to fail the next run. Deliberately lossy and deliberately
            # visible: a '?' in a comment gets noticed and corrected by hand.
            cleaned = "".join(ch if ord(ch) < 128 else "?" for ch in cleaned)
            # Newline-preserving write. newline="" stops Python translating "\n"
            # to "\r\n" on Windows, which would show every line as changed.
            with open(path, "w", encoding="ascii", newline="") as fh:
                fh.write(cleaned)
            print(f"fixed  {path.relative_to(ROOT)}")
            fixed_count += 1
        else:
            offenders.append((path, leftovers))

    if fix:
        print(f"check_ascii: normalized {fixed_count} file(s) of {len(files)}")
        return 0

    if not offenders:
        print(f"check_ascii: OK ({len(files)} files are ASCII-only, no BOMs)")
        return 0

    for path, leftovers in offenders:
        print(f"check_ascii: NON-ASCII in {path.relative_to(ROOT)}")
        for lineno, line in leftovers[:5]:
            snippet = "".join(ch if ord(ch) < 128 else f"<U+{ord(ch):04X}>" for ch in line)
            print(f"    {lineno}: {snippet.strip()[:110]}")
        if not leftovers:
            print("    (mojibake or a BOM, not a raw non-ASCII character)")
    print("\nRun: python scripts/check_ascii.py --fix")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
