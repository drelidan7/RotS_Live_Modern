#!/usr/bin/env python3
"""Inventory raw char-location representation access in the LS-1 library tier.

Wave LS-1 routes every raw ``->in_room`` / ``.in_room`` / ``world[`` /
``next_in_room`` READ inside the six source-bearing libraries (plus
``persist``) through the Stage-1 Placement API (``location_of``/
``room_by_id``/``room_by_id_total``/``occupants`` -- see ``src/handler.h``).
This census is the checked-in regression gate (T3 of the LS-1 plan): it
flags any raw token outside the census-named allow-list file set or an
inline ``// LS1-ALLOW: <reason>`` annotation. Modeled on
``tools/string_view_census.py`` (rglob discovery, comment/string masking,
``--check`` mode, non-zero exit on violation) -- see
``.superpowers/sdd/ls1-census.md`` Step 8 for the full design and
``docs/superpowers/location-read-allowlist.md`` for the allow-listed file
set this script reads via ``--exceptions``.
"""

import argparse
import pathlib
import re
import sys


LIBRARY_DIRS = ("entity", "persist", "world", "combat", "pathfind", "script", "olc")

# Token patterns, applied to COMMENT/STRING-MASKED text (so a token living
# inside a comment or a log()/mudlog() string literal never trips the gate).
# Each pattern anchors on the arrow/dot/bracket immediately preceding the
# field name -- this is what naturally excludes `next_in_room` from the
# in_room patterns (the character right after `->`/`.` is `n`, not `i`) and
# excludes `was_in_room` too (the character right after `.` is `w`, not
# `i`) without any special-cased substring denylist (Amendment 3).
TOKEN_PATTERNS = (
    ("->in_room", re.compile(r"->in_room\b")),
    (".in_room", re.compile(r"\.in_room\b")),
    ("world[", re.compile(r"\bworld\[")),
    ("next_in_room", re.compile(r"\bnext_in_room\b")),
)

ANNOTATION_MARKER = "LS1-ALLOW"
ANNOTATION_PATTERN = re.compile(r"LS1-ALLOW:\s*(.*?)\s*(?:\*/\s*)?$")

# Reason prefixes this census authorizes (ls1-census.md Step 8, plus
# `resolver-impl` -- a T3-added class for the two library files, db_world.cpp
# room_by_id_impl/room_by_id_total_impl and room_data::operator[]'s own
# recursive fallback, whose literal world[] access IS the Stage-1 resolver
# implementation itself, not a caller; see task-3 report). A line's
# annotation must start with one of these -- an empty or off-list reason is
# still a violation, so the gate can't be defeated with a bare
# `// LS1-ALLOW`.
ALLOWED_REASON_PREFIXES = (
    "save-next",
    "manual occupant-list splice",
    "peek-ahead",
    "manual first-match advance",
    "in_room used as mutable room cursor",
    "write",
    "obj-location",
    "resolver-impl",
)

RAW_STRING_PATTERN = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')


def mask_comments_and_string_literals(source_text):
    """Blank out comment and string/char-literal CONTENTS, keep newlines/length.

    Unlike string_view_census's masker (which skips over literals only to
    avoid misreading a comment-start inside one, leaving their contents
    intact), this census must also blank literal contents themselves --
    log()/mudlog() calls in this tree embed strings like
    "SYSERR: ch->in_room = NOWHERE ..." that must never trip the gate.
    Line/column positions are preserved 1:1 so line numbers stay valid.
    """
    n = len(source_text)
    masked = list(source_text)
    i = 0
    while i < n:
        if source_text.startswith("//", i):
            end = source_text.find("\n", i)
            end = n if end < 0 else end
            for k in range(i, end):
                masked[k] = " "
            i = end
            continue
        if source_text.startswith("/*", i):
            end = source_text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for k in range(i, end):
                if masked[k] != "\n":
                    masked[k] = " "
            i = end
            continue
        raw_match = RAW_STRING_PATTERN.match(source_text, i)
        if raw_match is not None:
            delimiter = raw_match.group(1)
            terminator = f'){delimiter}"'
            content_start = raw_match.end()
            terminator_start = source_text.find(terminator, content_start)
            end = n if terminator_start < 0 else terminator_start + len(terminator)
            for k in range(i, end):
                if masked[k] != "\n":
                    masked[k] = " "
            i = end
            continue
        character = source_text[i]
        if character in "\"'":
            quote = character
            j = i + 1
            while j < n:
                if source_text[j] == "\\":
                    j += 2
                    continue
                if source_text[j] == quote:
                    j += 1
                    break
                j += 1
            end = min(j, n)
            for k in range(i, end):
                if masked[k] != "\n":
                    masked[k] = " "
            i = end
            continue
        i += 1
    return "".join(masked)


def load_allow_listed_files(exception_path, repository_root):
    """Parse the doc-side allow-list table's Path column into a path set."""
    if not exception_path.exists():
        return set()
    allow_listed = set()
    row_pattern = re.compile(r"^\|\s*`([^`]+)`\s*\|")
    for line in exception_path.read_text(encoding="utf-8").splitlines():
        row_match = row_pattern.match(line.strip())
        if row_match is None:
            continue
        allow_listed.add(pathlib.PurePosixPath(row_match.group(1)).as_posix())
    return allow_listed


def source_files(search_paths, repository_root):
    """Yield eligible library .cpp files in deterministic path order.

    Non-recursive per library directory (`src/<dir>/*.cpp`, not `**/*.cpp`)
    -- the LS-1 plan's six libraries plus persist are each a flat directory
    of translation units; `src/app` is explicitly out of scope this wave
    (LS-2 extends the gate there), and headers are out of scope per the
    census's Step 8 ruling (a `.cpp`-only sweep).
    """
    discovered = set()
    for search_path in search_paths:
        resolved = search_path if search_path.is_absolute() else repository_root / search_path
        if resolved.is_file():
            if resolved.suffix == ".cpp":
                discovered.add(resolved)
            continue
        for candidate in sorted(resolved.glob("*.cpp")):
            if candidate.is_file():
                discovered.add(candidate)
    return sorted(discovered)


def findings_for_file(source_path, repository_root, allow_listed_files):
    """Return (line_number, token, raw_line) for every un-annotated raw hit."""
    try:
        relative_path = source_path.relative_to(repository_root).as_posix()
    except ValueError:
        relative_path = source_path.as_posix()

    if relative_path in allow_listed_files:
        return []

    raw_text = source_path.read_text(encoding="utf-8", errors="replace")
    masked_text = mask_comments_and_string_literals(raw_text)
    raw_lines = raw_text.split("\n")
    masked_lines = masked_text.split("\n")

    findings = []
    for line_index, masked_line in enumerate(masked_lines):
        matched_token = None
        for token_name, pattern in TOKEN_PATTERNS:
            if pattern.search(masked_line):
                matched_token = token_name
                break
        if matched_token is None:
            continue

        raw_line = raw_lines[line_index]
        annotation_match = ANNOTATION_PATTERN.search(raw_line) if ANNOTATION_MARKER in raw_line else None
        if annotation_match is not None:
            reason_text = annotation_match.group(1)
            if any(reason_text.startswith(prefix) for prefix in ALLOWED_REASON_PREFIXES):
                continue
            findings.append((line_index + 1, matched_token, raw_line.strip(), "invalid-reason"))
            continue

        findings.append((line_index + 1, matched_token, raw_line.strip(), "unannotated"))
    return findings


def parse_arguments():
    """Parse command-line census configuration."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).parents[1])
    parser.add_argument("--exceptions", type=pathlib.Path)
    return parser.parse_args()


def main():
    """Print every raw hit and, in --check mode, fail on un-annotated ones."""
    arguments = parse_arguments()
    repository_root = arguments.root.resolve()
    search_paths = arguments.paths or [repository_root / "src" / library for library in LIBRARY_DIRS]
    exception_path = (
        arguments.exceptions
        if arguments.exceptions is not None
        else repository_root / "docs/superpowers/location-read-allowlist.md"
    )
    allow_listed_files = load_allow_listed_files(exception_path, repository_root)

    violations = []
    for source_path in source_files(search_paths, repository_root):
        for line_number, token, raw_line, reason in findings_for_file(
            source_path, repository_root, allow_listed_files
        ):
            try:
                display_path = source_path.relative_to(repository_root)
            except ValueError:
                display_path = source_path
            print(f"{display_path}:{line_number}: [{token}] {raw_line}")
            violations.append((display_path, line_number, token, reason))

    if arguments.check and violations:
        for display_path, line_number, token, reason in violations:
            print(
                f"{reason}: {display_path}:{line_number}: raw {token} outside the allow-list "
                "(add an `// LS1-ALLOW: <reason>` annotation or route it through the Stage-1 "
                "Placement API)",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
