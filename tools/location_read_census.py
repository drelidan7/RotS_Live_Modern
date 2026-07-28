#!/usr/bin/env python3
"""Inventory raw char-location representation access across production `src/`.

Wave LS-1 routed every raw ``->in_room`` / ``.in_room`` / ``world[`` /
``next_in_room`` READ inside the six source-bearing libraries (plus
``persist``) through the Stage-1 Placement API (``location_of``/
``room_by_id``/``room_by_id_total``/``occupants`` -- see ``src/handler.h``).
Wave LS-2 widened the scan tree-wide -- recursive over ``src/**`` for
``.cpp``/``.h``/``.hpp``, headers included -- so the program's Stage-1 exit
criterion ("raw location representation access exists ONLY inside the
allow-listed representation-owner set") is mechanically true for all of
production `src/`. Wave LS-3a (T4) closed the last two structural gaps in
that claim: ``->people``/``.people`` -- direct occupant-chain-head access --
became the **fifth** tracked token, and the ``src/tests`` deferral was
retired, so the tree is now swept **whole**, tests included, with no
directory excluded for any reason. What remains untracked is one named
exclusion, and this script cannot see it at all since it is not one of the
five tokens: ``char_data::was_in_room``, a second parallel location store.
(The LS-2-era list named two more that no longer apply: ``.people`` is now
tracked, and ``get_world()`` was DELETED outright in LS-3a T2 tranche 2d as
a zero-caller dead-code rider -- only the ``&world`` array-address handoff
in ``db_boot.cpp``/``src/singleton.h`` survives it, and that hands over the
table as a whole rather than reading a character's location.)
``was_in_room`` is a named LS-3b input, not an oversight -- see
``docs/superpowers/specs/2026-07-23-locationsystem-program-design.md``'s
own As-built "out of LS-2's charter" list for the full account (O-I8,
``ls2-wholebranch-review-opus.md``).
This census is the checked-in regression gate (LS-1 T3, widened by LS-2
T5 and again by LS-3a T4): it flags any raw token outside the census-named
allow-list file set or an inline ``// LS1-ALLOW: <reason>`` annotation ("LS1" names LocationSystem
Stage 1, which spans both LS-1 and LS-2 -- see the ledger doc). Modeled on
``tools/string_view_census.py`` (rglob discovery, comment/string masking,
``--check`` mode, non-zero exit on violation) -- see
``.superpowers/sdd/ls1-census.md`` Step 8 and ``.superpowers/sdd/ls2-census.md``
/ ``ls2-census-b.md`` PART 3 for the full design, and
``docs/superpowers/location-read-allowlist.md`` for the allow-listed file
set this script reads via ``--exceptions``.
"""

import argparse
import pathlib
import re
import sys


# LS-2's `DEFERRED_DIRS = ("tests",)` constant is RETIRED (LS-3a T4, ruling
# R-B8). LS-2 R2/A-2 had deferred `src/tests` because the tier is 95% a
# WRITES problem (fixture construction -- char-location writes, occupant-
# chain splices, world[] assignment targets) and LS-2 was reads-only; the
# deferral was deliberately NOT a ledger row, since a ledger row would
# assert tests are a permanent representation owner, which is false. LS-3a
# is the mutation wave: T1 built the test-tier fixture helper
# (`src/tests/test_placement.h`'s ScopedRoomOccupants) and T3 migrated the
# whole tier onto it, so the deferral has nothing left to defer. There is
# no directory-exclusion mechanism in this script any more -- deliberately,
# so a future wave cannot re-introduce a blind spot by adding a name to a
# tuple. The three test-tier representation OWNERS take ordinary whole-file
# ledger rows instead, visible in the same table as the production owners.

# O-I7 (LS-2 whole-branch review, Opus): source_files() returns an empty
# scan for a nonexistent search path with no error of its own (`rglob` on a
# missing directory yields nothing), and main() had no floor check -- a bad
# `--root`, a moved/renamed directory, or a typo in a positional path
# argument silently turned this gate's fail-closed exit criterion GREEN
# instead of RED. 100 is well below the real production count at the time
# this floor was added (181 scanned files -- see AGENTS.md's LS-2 chain
# entry and ls2-wholebranch-review-opus.md), giving headroom for legitimate
# future file-count drift (deletions, moves, a scope change) while remaining
# far above zero, so a genuine path break is still caught long before it
# could be mistaken for ordinary tree churn. LS-3a T4 raised it 100 -> 250
# (R-B8) when retiring the `src/tests` deferral took the scanned count from
# 181 to 307: a floor left at 100 would have gone on passing even if the
# entire newly-added test tier silently dropped back out of the sweep, which
# is precisely the regression this wave must make impossible.
MINIMUM_SCANNED_FILE_COUNT = 250

SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".c", ".inl", ".ipp")
# Widened past the original (".cpp", ".h", ".hpp") after the LS-2 whole-branch
# review (Fable M4): the tuple was closed-world, so a future .cc/.cxx/.inl/.ipp
# file would have escaped the sweep SILENTLY -- the one failure mode this gate
# must never have. Zero such files exist under production src/ today, so this
# is pure future-proofing and changes no current result.

# Token patterns, applied to COMMENT/STRING-MASKED text (so a token living
# inside a comment or a log()/mudlog() string literal never trips the gate).
# Each pattern anchors on the arrow/dot/bracket immediately preceding the
# field name -- this is what naturally excludes `next_in_room` from the
# in_room patterns (the character right after `->`/`.` is `n`, not `i`) and
# excludes `was_in_room` too (the character right after `.` is `w`, not
# `i`) without any special-cased substring denylist (Amendment 3).
#
# `->people`/`.people` is the FIFTH token, added by LS-3a T4 (ruling R-B6).
# It is the occupant chain's HEAD -- the other half of the same intrusive
# list `next_in_room` walks -- and without it a green gate was misleading.
# Measured at the commit that added it: 56 production lines and 68 test-tier
# lines carried the token, and 29 production lines were neither annotated nor
# inside an allow-listed owner -- including the `occupant_range`/
# `const_occupant_range` constructors in src/handler.h, whose own
# `operator++` siblings had been annotated since LS-2 (ruling R-C7). Of the
# 29, twenty-four were chain-HEAD reads that converted outright to the
# Stage-1 `first_occupant()` accessor (ruling R-C6's own named set among
# them); the remaining five are two header API bodies and three writes.
#
# READ-VS-WRITE: this gate does NOT classify a hit as a read or a write, and
# never has -- it reports token PRESENCE on a line and requires either a
# whole-file ledger row or a per-line `LS1-ALLOW: <reason>` annotation whose
# reason carries that classification. That matters most for `.people`,
# because clang-format splits some assignments across lines: src/olc/
# shaperom.cpp:157/:1284 put `->room->people` on one line and its `= 0;` on
# the NEXT, so a classifier keying off `=` on the token's own line would
# call those reads. They are writes, they are annotated `write`, and the
# annotation -- not any inference by this script -- is what carries the
# truth. The gate stays strictly line-based on purpose (ruling AM-5
# WITHDREW a proposed multi-line matcher: it would have put the per-line
# annotation contract at risk to re-derive what the annotation already
# states, and the token alone is enough to make the split write visible).
TOKEN_PATTERNS = (
    ("->in_room", re.compile(r"->in_room\b")),
    (".in_room", re.compile(r"\.in_room\b")),
    ("world[", re.compile(r"\bworld\s*\[")),
    ("next_in_room", re.compile(r"\bnext_in_room\b")),
    ("people", re.compile(r"(?:->|\.)people\b")),
)

ANNOTATION_MARKER = "LS1-ALLOW"
ANNOTATION_PATTERN = re.compile(r"LS1-ALLOW:\s*(.*?)\s*(?:\*/\s*)?$")

# Reason prefixes this census authorizes (ls1-census.md Step 8, plus
# `resolver-impl` -- a T3-added class for the two library files, db_world.cpp
# room_by_id_impl/room_by_id_total_impl and room_data::operator[]'s own
# recursive fallback, whose literal world[] access IS the Stage-1 resolver
# implementation itself, not a caller; see task-3 report -- and, as of LS-2
# T5, three more (ls2-census.md R9): `representation-decl`/
# `representation-impl` for header sites that ARE the representation (a
# struct field declaration; the occupant_range/const_occupant_range
# iterators' own operator++ bodies) rather than a call site, and
# `not-a-location` for an `in_room`-named field that isn't a char location
# at all (`shop_data::in_room` is a shop VNUM, not a character's room). A
# line's annotation must start with one of these -- an empty or off-list
# reason is still a violation, so the gate can't be defeated with a bare
# `// LS1-ALLOW`. LS-3a T4 minted NO new prefix (ruling R-B7 -- the count
# stays ELEVEN); it widened what `representation-impl` covers to a third
# class its wording already fits, the occupant-chain SHAPE assertions in
# `src/tests/spec_pro_tests.cpp`/`load_room_placement_tests.cpp` that pin
# raw `next_in_room` links because no Stage-1 API expresses a tail walk.
# See docs/superpowers/location-read-allowlist.md for the full definition.
ALLOWED_REASON_PREFIXES = (
    "save-next",
    "manual occupant-list splice",
    "peek-ahead",
    "manual first-match advance",
    "in_room used as mutable room cursor",
    "write",
    "obj-location",
    "resolver-impl",
    "representation-decl",
    "representation-impl",
    "not-a-location",
)

RAW_STRING_PATTERN = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')


def mask_comments_and_string_literals(source_text, mask_comments=True):
    """Blank out comment and string/char-literal CONTENTS, keep newlines/length.

    Unlike string_view_census's masker (which skips over literals only to
    avoid misreading a comment-start inside one, leaving their contents
    intact), this census must also blank literal contents themselves --
    log()/mudlog() calls in this tree embed strings like
    "SYSERR: ch->in_room = NOWHERE ..." that must never trip the gate.
    Line/column positions are preserved 1:1 so line numbers stay valid.

    mask_comments=False switches to a STRINGS-ONLY mode (I1 fix,
    ls2-wholebranch-review-fable.md): comment SPANS are still recognized and
    skipped whole (so an apostrophe inside a comment is never mistaken for
    the start of a char literal), but their contents are left un-blanked --
    real `LS1-ALLOW:` annotations live in comments, so this is what the
    annotation search (findings_for_file) scans against instead of the raw
    line. String/char-literal contents are still blanked in both modes, so
    an `LS1-ALLOW:` marker sitting inside a string literal (rather than a
    comment) is blanked out here too and can no longer be mistaken for a
    real annotation.
    """
    n = len(source_text)
    masked = list(source_text)
    i = 0
    while i < n:
        if source_text.startswith("//", i):
            end = source_text.find("\n", i)
            end = n if end < 0 else end
            if mask_comments:
                for k in range(i, end):
                    masked[k] = " "
            i = end
            continue
        if source_text.startswith("/*", i):
            end = source_text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            if mask_comments:
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
            # C++14 digit separators (1'700'000'000) put bare apostrophes in
            # CODE. An apostrophe directly preceded by a digit is a separator,
            # not a char-literal opener -- treating it as one opened a silent,
            # unbounded blind window over following code (LS-3a review F1:
            # an odd separator count swallowed real tokens up to the next
            # apostrophe anywhere later in the file).
            if quote == "'" and i > 0 and source_text[i - 1].isdigit():
                i += 1
                continue
            j = i + 1
            terminated = False
            while j < n:
                if source_text[j] == "\\":
                    j += 2
                    continue
                if source_text[j] == "\n":
                    # A char/string literal cannot contain an unescaped
                    # newline (an escaped one was consumed above), so an
                    # unterminated scan is NOT a literal: abandon instead of
                    # blanking arbitrary following lines (review F1).
                    break
                if source_text[j] == quote:
                    j += 1
                    terminated = True
                    break
                j += 1
            if not terminated:
                i += 1
                continue
            end = min(j, n)
            for k in range(i, end):
                if masked[k] != "\n":
                    masked[k] = " "
            i = end
            continue
        i += 1
    return "".join(masked)


# The ledger's canonical allow-list table must be introduced by this exact
# marker line, and the marker must appear EXACTLY ONCE. A bare header row is
# not a sufficient anchor: the LS-2 follow-up's first attempt keyed off
# `| Path | Reason |` and was still defeatable by placing a table carrying
# that same header ABOVE the real one (first match wins), including inside a
# fenced code block used as documentation -- if such a table also carried the
# two genuine owner rows, the gate stayed GREEN with an extra file silently
# exempted. Found in adversarial review of the follow-up itself.
ALLOW_LIST_TABLE_MARKER = "<!-- LOCATION-READ-ALLOWLIST-TABLE -->"
ALLOW_LIST_TABLE_HEADER = "| Path | Reason |"


def load_allow_listed_files(exception_path, repository_root):
    """Parse ONLY the ledger's canonical allow-list table into a path set.

    Hardened after the LS-2 whole-branch review (Opus M10). The original
    loader promoted **any** markdown table row in the ledger whose first cell
    was backticked into a WHOLE-FILE exemption, regardless of which table it
    sat in -- so a future "informational appendix" table containing a row like
    ``| `src/app/victim.cpp` | 5 | write |`` would have silently exempted that
    file from the program's fail-closed exit criterion. The reviewer
    demonstrated exactly that. The doc is prose-heavy and gaining a table is a
    plausible edit, so this parses only rows inside the table introduced by
    ALLOW_LIST_TABLE_HEADER, and stops at the first line that is not a table
    row -- an appendix table elsewhere in the file is now inert.
    """
    if not exception_path.exists():
        return set()
    lines = exception_path.read_text(encoding="utf-8").splitlines()

    # Fenced code blocks are documentation, never data -- a worked example of
    # the table format must not be parsed as the table itself.
    in_fence = False
    live_lines = []
    for line in lines:
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            live_lines.append(line)

    marker_count = sum(1 for line in live_lines if line.strip() == ALLOW_LIST_TABLE_MARKER)
    if marker_count != 1:
        raise SystemExit(
            f"error: {exception_path} must contain the allow-list marker "
            f"{ALLOW_LIST_TABLE_MARKER!r} exactly once outside any fenced code block "
            f"(found {marker_count}). The marker is what makes the canonical table "
            "unambiguous; without it a second table could silently exempt files."
        )

    allow_listed = set()
    row_pattern = re.compile(r"^\|\s*`([^`]+)`\s*\|")
    state = "before-marker"
    for line in live_lines:
        stripped = line.strip()
        if state == "before-marker":
            if stripped == ALLOW_LIST_TABLE_MARKER:
                state = "awaiting-header"
            continue
        if state == "awaiting-header":
            if not stripped:
                continue
            if stripped != ALLOW_LIST_TABLE_HEADER:
                raise SystemExit(
                    f"error: {exception_path}: the line after "
                    f"{ALLOW_LIST_TABLE_MARKER!r} must be {ALLOW_LIST_TABLE_HEADER!r}, "
                    f"got {stripped!r}."
                )
            state = "in-table"
            continue
        if stripped.startswith("|") and set(stripped) <= set("|- :"):
            continue  # the header's own separator row
        if not stripped.startswith("|"):
            break  # table ended; anything past here is not an exemption
        row_match = row_pattern.match(stripped)
        if row_match is None:
            continue
        candidate = pathlib.PurePosixPath(row_match.group(1)).as_posix()
        if ".." in pathlib.PurePosixPath(candidate).parts:
            raise SystemExit(
                f"error: {exception_path}: allow-list path {candidate!r} escapes the tree."
            )
        allow_listed.add(candidate)
    return allow_listed


def source_files(search_paths, repository_root):
    """Return every eligible C++ source/header file under search_paths, recursively.

    LS-2 widened this from the LS-1 seven-library, `.cpp`-only,
    non-recursive sweep (`src/<dir>/*.cpp`) to a recursive `src/**` sweep
    over `SOURCE_SUFFIXES` -- `src/app` and every header came into scope
    for the first time -- leaving exactly one named deferral, `src/tests`.
    LS-3a T4 retired that deferral (R-B8): the sweep is now unconditional
    over everything it discovers, so there is no longer any way for a file
    with a matching suffix to sit inside the searched tree and go unread.
    """
    discovered = set()
    for search_path in search_paths:
        # .resolve() on BOTH sides or neither: repository_root is resolved by
        # main(), so an unresolved candidate here makes relative_to() raise and
        # silently defeats the allow-list lookup below. On macOS that fires
        # for any path under /var (-> /private/var), i.e. every
        # temp dir -- which is exactly how the gate's own self-test caught it.
        # It fails closed (flags an exempt file rather than exempting a real
        # one), but it is still wrong.
        resolved = (search_path if search_path.is_absolute() else repository_root / search_path).resolve()
        candidates = [resolved] if resolved.is_file() else sorted(resolved.rglob("*"))
        for candidate in candidates:
            if not candidate.is_file() or candidate.suffix not in SOURCE_SUFFIXES:
                continue
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
    # I1 fix (ls2-wholebranch-review-fable.md): the annotation search below
    # must run against a STRINGS-ONLY-masked variant (comments intact,
    # string/char-literal contents blanked) rather than the raw line -- a
    # bare raw-line search lets an `LS1-ALLOW:` marker sitting inside a
    # string-literal ARGUMENT (e.g. a mudlog() call) silence a real raw
    # token on the same line, since that marker is not actually an
    # annotation at all.
    annotation_source_text = mask_comments_and_string_literals(raw_text, mask_comments=False)
    raw_lines = raw_text.split("\n")
    masked_lines = masked_text.split("\n")
    annotation_source_lines = annotation_source_text.split("\n")

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
        # R4 (ls2-global-constraints.md): a `\`-continued macro line's
        # trailing backslash is stripped before matching. C++ translation
        # phase 2 (line splicing on `\`) runs before phase 3 (comment
        # stripping), so a `//` annotation on such a line would be a
        # compile error -- the required form there is a block comment
        # BEFORE the backslash (`/* LS1-ALLOW: ... */ \`). Stripping the
        # backslash first also keeps the captured reason text free of the
        # trailing `*/ \` tail.
        match_source = annotation_source_lines[line_index].rstrip()
        if match_source.endswith("\\"):
            match_source = match_source[:-1].rstrip()
        annotation_match = ANNOTATION_PATTERN.search(match_source) if ANNOTATION_MARKER in match_source else None
        if annotation_match is not None:
            reason_text = annotation_match.group(1)
            # I2 fix (ls2-wholebranch-review-fable.md): require the
            # authorized prefix to be followed by end-of-string, a space, or
            # an opening paren -- a bare `startswith` let any EXTENSION of an
            # authorized prefix through (e.g. "writeable-anything-i-like"
            # passing because it starts with "write").
            if any(
                re.match(rf"{re.escape(prefix)}($|[ (])", reason_text)
                for prefix in ALLOWED_REASON_PREFIXES
            ):
                continue
            findings.append((line_index + 1, matched_token, raw_line.strip(), "invalid-reason"))
            continue

        findings.append((line_index + 1, matched_token, raw_line.strip(), "unannotated"))
    return findings


# ---------------------------------------------------------------------------
# Permanent, checked-in self-test (LS-2 whole-branch review, Fable M3).
#
# T5 originally proved this gate's five directions by probe-and-revert: it
# injected violations into real tree files, ran --check, then reverted. That
# proved the gate worked THAT DAY and left nothing behind to re-prove it on
# regression -- while docs/BUILD.md described the self-test in the present
# tense, implying a standing check that did not exist. This mode is that
# standing check. It builds a synthetic tree in a temp directory and never
# touches the repository, so it is safe to run from any working state.
# ---------------------------------------------------------------------------

SELF_TEST_LEDGER = """# synthetic ledger

An example of the format, in a fence -- must NOT be parsed as the table:

```
<!-- LOCATION-READ-ALLOWLIST-TABLE -->
| Path | Reason |
| --- | --- |
| `src/app/fenced.cpp` | a documented example, not an exemption |
```

| Path | Reason |
| --- | --- |
| `src/app/decoy.cpp` | a table ABOVE the marker, carrying the canonical header |

<!-- LOCATION-READ-ALLOWLIST-TABLE -->
| Path | Reason |
| --- | --- |
| `src/owner/owned.cpp` | representation owner |

| Path | Count | Note |
| --- | --- | --- |
| `src/app/appendix.cpp` | 5 | an informational appendix row, NOT an exemption |
"""

# (source body, expected --check exit code). Each runs the REAL gate end to
# end via main(), not a predicate in isolation -- the follow-up review's I2
# found the first version never invoked main() at all, so sabotaging the
# `if arguments.check and violations:` line (a gate that can never fail) went
# undetected. Anything that breaks the path from "file on disk" to "non-zero
# exit" must now be caught here.
SELF_TEST_CASES = (
    ("unannotated", "int a = ch->in_room;\n", 1),
    ("bogus-reason", "int a = ch->in_room; // LS1-ALLOW: not-an-authorized-reason\n", 1),
    ("valid-trailing", "int a = ch->in_room; // LS1-ALLOW: write (a real one)\n", 0),
    ("valid-continuation", "#define M(ch) \\\n    (ch)->in_room /* LS1-ALLOW: write (macro) */\n", 0),
    # The trailing " ok" matters: with `log("LS1-ALLOW: write");` the reason text
    # ends `write");`, which the I2 prefix-boundary check rejects on its own --
    # so that probe passed whether or not the I1 string-masking fix was present,
    # i.e. it was vacuous against the one direction it names. `write ok` is a
    # VALID reason, so this line can only be flagged by I1's masking. (Found in
    # adversarial review of this very self-test -- F1.)
    ("string-literal-bypass", 'int a = ch->in_room; log("LS1-ALLOW: write ok");\n', 1),
    ("prefix-extension-bypass", "int a = ch->in_room; // LS1-ALLOW: writeable-anything\n", 1),
    # Pins R4's trailing-`*/` stripping specifically. With a space before the
    # `*/` the captured reason is "write (macro) */", which the I2 boundary
    # check accepts anyway (prefix + space) -- so the continuation probe above
    # does NOT pin the stripping. Here `*/` abuts the prefix, so without the
    # stripping the reason reads "write*/" and is rejected as invalid.
    ("r4-abutting-block-comment", "int a = ch->in_room; /* LS1-ALLOW: write*/\n", 0),
    # Pins R4's BACKSLASH stripping -- the other half of the R4 handling, and
    # until LS-3a T4 it was pinned by nothing. The `valid-continuation` case
    # above puts the token on the line AFTER the backslash, so the stripping
    # never runs for it; a `#define` whose annotation and trailing backslash
    # share the token's own line is the only shape that exercises it. The `*/`
    # must ABUT the reason for the same reason `r4-abutting-block-comment`
    # does: with a space before it the captured reason is "write */ \\", which
    # the I2 prefix-boundary check accepts anyway. Abutting, an unstripped
    # line captures "write*/ \\" and is rejected, so only the stripping can
    # make this exit 0. (Verified by sabotage: deleting the two stripping
    # lines turns this case, and only this case, red -- the same sabotage was
    # a no-op against the LS-2 self-test that first claimed to cover it.)
    ("r4-backslash-continuation",
     "#define M(ch) (ch)->in_room /* LS1-ALLOW: write*/ \\\n    + 1\n", 0),
    ("comment-masked-token", "// int a = ch->in_room; -- a comment, not code\n", 0),
    ("world-token", "room_data& r = world[3];\n", 1),
    ("next-in-room-token", "for (c = head; c; c = c->next_in_room) {}\n", 1),
    ("dot-access-token", "int a = character.in_room;\n", 1),
    # C++14 digit separators must not open a blind window over following
    # code (review F1: an odd apostrophe count swallowed everything to the
    # next apostrophe -- this case has THREE separators, the shape that was
    # live in-tree at act_wiz_format_tests.cpp:1410).
    ("digit-separator-blind-window",
     "long t = 1'700'000'000;\nint a = ch->in_room;\n", 1),
    ("digit-separator-benign", "long t = 1'700'000'000;\nint b = t;\n", 0),
    # A genuine char literal containing a token spelling must STAY masked
    # after the F1 fix (the newline-abandon must not unmask real literals).
    ("char-literal-still-masked", "char c = '.'; log(\"x.in_room y\");\n", 0),
    # Whitespace between world and [ must not defeat the subscript token
    # (review #2 F1).
    ("world-spaced-subscript", "room_data& r = world [3];\n", 1),
    # Suffix coverage: the same unannotated token in a HEADER must be
    # flagged -- proves .h files are scanned (review F2: undetected before).
    ("header-suffix-scanned", "int a = ch->in_room;\n", 1, "probe.h"),
    # --- the FIFTH token, LS-3a T4 (ruling R-B6). Both spellings must fire,
    # the masker and the annotation path must still apply to it, the two
    # over-match shapes must NOT fire, and -- the AM-5 claim, which is the
    # whole reason no multi-line matcher was written -- a clang-format-split
    # write must be visible from its TOKEN line alone (src/olc/shaperom.cpp:
    # 157/:1284 are exactly this shape).
    ("people-arrow-token", "char_data* head = room->people;\n", 1),
    ("people-dot-token", "char_data* head = room.people;\n", 1),
    ("people-annotated", "room->people = 0; // LS1-ALLOW: write\n", 0),
    ("people-comment-masked", "// room->people is the chain head -- prose, not code\n", 0),
    ("people-split-write-flagged", "SHAPE_ROOM(ch)\n    ->room->people\n    = 0;\n", 1),
    ("people-split-write-annotated",
     "SHAPE_ROOM(ch)\n    ->room->people // LS1-ALLOW: write (split)\n    = 0;\n", 0),
    # Over-match guards: the pattern anchors on the `->`/`.` immediately
    # before the field name and ends on a word boundary, so neither a longer
    # field name that merely ENDS in the token nor a longer word that starts
    # with it may trip the gate.
    ("people-longer-field-not-matched", "int n = tribe.peoples;\n", 0),
    ("people-prefixed-field-not-matched", "int n = clan->mypeople;\n", 0),
)


def _run_gate(root, ledger, scan_path=None):
    """Invoke the REAL gate (subprocess, full main()) against a synthetic tree.

    scan_path defaults to the synthetic tree's own `src`; the floor probes
    pass their own directory instead so they can control the scanned count
    exactly.
    """
    import subprocess

    completed = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).resolve()), "--check",
         "--root", str(root), "--exceptions", str(ledger),
         str(scan_path if scan_path is not None else root / "src")],
        capture_output=True, text=True,
    )
    return completed.returncode, completed.stdout + completed.stderr


def run_self_test():
    """Prove the gate still fails in every direction it must. Returns exit code."""
    import tempfile

    failures = []

    # The floor's VALUE, pinned as a literal on purpose (LS-3a T4). Every
    # other floor probe below spends MINIMUM_SCANNED_FILE_COUNT symbolically,
    # so all of them move with the constant and none of them notices it being
    # lowered -- the exact vacuity a floor check must not have, since lowering
    # it is how this gate would be quietly defeated. T4 set it to 250 against
    # a 307-file scan; raising it later is fine and needs this literal raised
    # with it, which is the deliberate second edit.
    if MINIMUM_SCANNED_FILE_COUNT < 250:
        failures.append(
            f"MINIMUM_SCANNED_FILE_COUNT is {MINIMUM_SCANNED_FILE_COUNT}, below the 250 that "
            "LS-3a T4 set when src/tests joined the scan -- a lowered floor lets a broken scan "
            "path pass as a clean tree."
        )

    with tempfile.TemporaryDirectory() as temporary_root:
        root = pathlib.Path(temporary_root)
        ledger = root / "ledger.md"
        ledger.write_text(SELF_TEST_LEDGER, encoding="utf-8")

        source_dir = root / "src" / "probe"
        source_dir.mkdir(parents=True)
        # The floor check (O-I7) requires a realistic file count, so pad with
        # clean files. This also proves the floor does not fire spuriously.
        for index in range(MINIMUM_SCANNED_FILE_COUNT + 5):
            (source_dir / f"pad{index}.cpp").write_text("int clean = 0;\n", encoding="utf-8")

        allow_listed = load_allow_listed_files(ledger, root)
        # M10, all three shapes: fenced example, table above the marker, and a
        # differently-headed appendix below it. Only the marked table counts.
        if "src/owner/owned.cpp" not in allow_listed:
            failures.append("M10: the marked allow-list row was NOT honored")
        for smuggled in ("src/app/fenced.cpp", "src/app/decoy.cpp", "src/app/appendix.cpp"):
            if smuggled in allow_listed:
                failures.append(f"M10: {smuggled} was smuggled in as a whole-file exemption")

        # Cases are (name, body, expected_exit[, filename]). The optional
        # filename lets a case probe suffix coverage (review F2: every probe
        # was probe.cpp, so dropping .h from SOURCE_SUFFIXES was undetected
        # except by the accidental floor trip).
        default_probe = source_dir / "probe.cpp"
        for case in SELF_TEST_CASES:
            name, body, expected_exit = case[0], case[1], case[2]
            probe = source_dir / (case[3] if len(case) > 3 else "probe.cpp")
            probe.write_text(body, encoding="utf-8")
            actual_exit, output = _run_gate(root, ledger, None)
            if actual_exit != expected_exit:
                failures.append(
                    f"{name}: expected gate exit {expected_exit}, got {actual_exit}\n{output}"
                )
            if probe != default_probe:
                probe.unlink()
            else:
                probe.write_text("", encoding="utf-8")
        if default_probe.exists():
            default_probe.unlink()

        # A whole-file exemption silences its file end to end...
        owner_dir = root / "src" / "owner"
        owner_dir.mkdir(parents=True)
        (owner_dir / "owned.cpp").write_text("int a = ch->in_room;\n", encoding="utf-8")
        exit_code, output = _run_gate(root, ledger, None)
        if exit_code != 0:
            failures.append(f"allow-listed file was still flagged by the gate\n{output}")
        # ...and an unmarked ledger un-silences it.
        bare = root / "bare.md"
        bare.write_text("<!-- LOCATION-READ-ALLOWLIST-TABLE -->\n| Path | Reason |\n| --- | --- |\n",
                        encoding="utf-8")
        exit_code, _ = _run_gate(root, bare, None)
        if exit_code == 0:
            failures.append("removing the ledger row did NOT re-flag the file")

        # The O-I7 floor must fire when the scan is broken.
        empty = root / "empty"
        empty.mkdir()
        exit_code, output = _run_gate(root, ledger, empty)
        if exit_code == 0:
            failures.append(f"the scanned-file floor did NOT fire on an empty scan\n{output}")

        # ...and it must sit at MINIMUM_SCANNED_FILE_COUNT *exactly* (LS-3a
        # T4). The empty-scan case above only proves SOME floor exists; it
        # passes just as happily with the constant left at any stale value.
        # This pins the boundary in both directions, so a future wave that
        # widens the scan has to move the constant deliberately, and a wave
        # that silently narrows the scan back trips it. Both probe files are
        # clean, so only the floor can decide these two exits.
        boundary = root / "boundary"
        boundary.mkdir()
        for index in range(MINIMUM_SCANNED_FILE_COUNT - 1):
            (boundary / f"pad{index}.cpp").write_text("int clean = 0;\n", encoding="utf-8")
        exit_code, output = _run_gate(root, ledger, boundary)
        if exit_code == 0:
            failures.append(
                f"the floor did NOT fire one file BELOW MINIMUM_SCANNED_FILE_COUNT "
                f"({MINIMUM_SCANNED_FILE_COUNT})\n{output}"
            )
        (boundary / "one_more.cpp").write_text("int clean = 0;\n", encoding="utf-8")
        exit_code, output = _run_gate(root, ledger, boundary)
        if exit_code != 0:
            failures.append(
                f"the floor fired AT MINIMUM_SCANNED_FILE_COUNT "
                f"({MINIMUM_SCANNED_FILE_COUNT}) -- it must be a minimum, not an exclusive "
                f"bound\n{output}"
            )

        # `src/tests` is IN SCOPE (LS-3a T4, ruling R-B8). This probe is the
        # direct inverse of the one it replaces: LS-2's self-test asserted a
        # `src/tests` file was deferred and its notice printed, and this one
        # asserts the same file is now scanned and flagged like any other.
        # It is what goes red if a future edit re-introduces ANY directory-
        # exclusion mechanism, and it deliberately probes a WRITE, since the
        # write half of the representation is exactly what the test tier is
        # made of and what LS-2's deferral was granted for.
        tests_dir = root / "src" / "tests"
        tests_dir.mkdir(parents=True)
        (tests_dir / "fixture.cpp").write_text("ch->in_room = 3;\n", encoding="utf-8")
        exit_code, output = _run_gate(root, ledger, None)
        if exit_code == 0:
            failures.append(
                f"a src/tests file went unscanned -- the retired deferral is back\n{output}"
            )
        if "[deferred]" in output:
            failures.append(f"the retired deferral notice is still printed\n{output}")
        # ...and the complement: bringing the tier into scope must not have
        # been done by blanket-failing it. The same file, annotated, passes,
        # so a test-tier author has a working escape hatch and this probe is
        # a real discriminator rather than a file that can never be green.
        (tests_dir / "fixture.cpp").write_text(
            "ch->in_room = 3; // LS1-ALLOW: write\n", encoding="utf-8")
        exit_code, output = _run_gate(root, ledger)
        if exit_code != 0:
            failures.append(f"an ANNOTATED src/tests file was still flagged\n{output}")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("location_read_census self-test: all directions pass (gate invoked end to end)")
    return 0


def parse_arguments():
    """Parse command-line census configuration."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true",
                        help="prove the gate still fails in every direction it must")
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).parents[1])
    parser.add_argument("--exceptions", type=pathlib.Path)
    return parser.parse_args()


def main():
    """Print every raw hit and, in --check mode, fail on un-annotated ones."""
    arguments = parse_arguments()
    if arguments.self_test:
        return run_self_test()
    repository_root = arguments.root.resolve()
    search_paths = arguments.paths or [repository_root / "src"]
    exception_path = (
        arguments.exceptions
        if arguments.exceptions is not None
        else repository_root / "docs/superpowers/location-read-allowlist.md"
    )
    allow_listed_files = load_allow_listed_files(exception_path, repository_root)

    scanned_files = source_files(search_paths, repository_root)

    # O-I7: fail closed, unconditionally (not just under --check), the
    # moment the scan itself looks broken -- a zero-or-near-zero scanned
    # count is far more likely to mean a bad --root/search path than a
    # genuine shrink of the tree, and printing only the (technically true)
    # "[scanned] 0 file(s)..." notice below would read as good news.
    if len(scanned_files) < MINIMUM_SCANNED_FILE_COUNT:
        print(
            f"error: only {len(scanned_files)} file(s) scanned under "
            f"{[str(path) for path in search_paths]!r} (root {repository_root}) -- expected at "
            f"least {MINIMUM_SCANNED_FILE_COUNT}. This almost always means a broken --root, a "
            "moved/renamed directory, or a typo'd positional path argument, not a genuine "
            "shrink of production src/ -- fix the invocation rather than lowering this floor.",
            file=sys.stderr,
        )
        return 1

    print(f"[scanned] {len(scanned_files)} file(s) under {[str(path) for path in search_paths]!r} "
          "-- the whole production tree plus src/tests; no directory is excluded.")

    violations = []
    for source_path in scanned_files:
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
