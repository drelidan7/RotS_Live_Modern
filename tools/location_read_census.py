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
production `src/` **for the four tracked tokens** (``->in_room`` /
``.in_room`` / ``world[`` / ``next_in_room``) -- not for every conceivable
form of raw representation access. `src/tests` is deliberately excluded via
``DEFERRED_DIRS`` below, pending wave LS-3a. Three more exclusions are
deliberate and this script cannot see them at all, since none is one of the
four tracked tokens: ``room_data::people``/``.people`` direct occupant-chain
access, ``char_data::was_in_room`` (a second parallel location store), and
the ``&world``/``get_world()`` singleton handoff (``db_boot.cpp``,
``src/singleton.h``). All three are named LS-3 inputs, not oversights --
see ``docs/superpowers/specs/2026-07-23-locationsystem-program-design.md``'s
own As-built "out of LS-2's charter" list for the full account (O-I8,
``ls2-wholebranch-review-opus.md``).
This census is the checked-in regression gate (LS-1 T3, widened by LS-2
T5): it flags any raw token outside the census-named allow-list file set or
an inline ``// LS1-ALLOW: <reason>`` annotation ("LS1" names LocationSystem
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


# LS-2 R2/A-2 (.superpowers/sdd/ls2-census.md): `src/tests` is a WRITES
# problem (fixture construction -- char-location writes, occupant-chain
# splices, world[] assignment targets), not a READS problem, and this wave
# is reads-only. Wave LS-3a owns the test-fixture helper that will let the
# test tier join the scan for real; until then it is a named, gate-visible
# deferral -- deliberately NOT a ledger row, since a ledger row would
# assert tests are a permanent representation owner, which is false.
# `--check` prints how many deferred-dir files went unscanned so the
# deferral stays visible rather than silent (see main()).
DEFERRED_DIRS = ("tests",)

# O-I7 (LS-2 whole-branch review, Opus): source_files() returns an empty
# scan for a nonexistent search path with no error of its own (`rglob` on a
# missing directory yields nothing), and main() had no floor check -- a bad
# `--root`, a moved/renamed directory, or a typo in a positional path
# argument silently turned this gate's fail-closed exit criterion GREEN
# instead of RED. 100 is well below the real production count at the time
# this floor was added (181 scanned files -- see AGENTS.md's LS-2 chain
# entry and ls2-wholebranch-review-opus.md), giving headroom for legitimate
# future file-count drift (deletions, moves, an LS-3a scope change) while
# remaining far above zero, so a genuine path break is still caught long
# before it could be mistaken for ordinary tree churn.
MINIMUM_SCANNED_FILE_COUNT = 100

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
# implementation itself, not a caller; see task-3 report -- and, as of LS-2
# T5, three more (ls2-census.md R9): `representation-decl`/
# `representation-impl` for header sites that ARE the representation (a
# struct field declaration; the occupant_range/const_occupant_range
# iterators' own operator++ bodies) rather than a call site, and
# `not-a-location` for an `in_room`-named field that isn't a char location
# at all (`shop_data::in_room` is a shop VNUM, not a character's room). A
# line's annotation must start with one of these -- an empty or off-list
# reason is still a violation, so the gate can't be defeated with a bare
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


# The ledger's canonical allow-list table must open with exactly this header
# row. Only rows belonging to THAT table become whole-file exemptions.
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
    allow_listed = set()
    row_pattern = re.compile(r"^\|\s*`([^`]+)`\s*\|")
    inside_canonical_table = False
    for line in exception_path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not inside_canonical_table:
            if stripped == ALLOW_LIST_TABLE_HEADER:
                inside_canonical_table = True
            continue
        if stripped.startswith("|") and set(stripped) <= set("|- :"):
            continue  # the header's own separator row
        if not stripped.startswith("|"):
            break  # table ended; anything past here is not an exemption
        row_match = row_pattern.match(stripped)
        if row_match is None:
            continue
        allow_listed.add(pathlib.PurePosixPath(row_match.group(1)).as_posix())
    return allow_listed


def is_under_deferred_dir(candidate_path, repository_root):
    """Return whether candidate_path sits under a `src/<DEFERRED_DIRS>` tree."""
    try:
        relative_path = candidate_path.relative_to(repository_root)
    except ValueError:
        return False
    return (
        len(relative_path.parts) >= 2
        and relative_path.parts[0] == "src"
        and relative_path.parts[1] in DEFERRED_DIRS
    )


def source_files(search_paths, repository_root):
    """Return (scanned, deferred) eligible C++ source/header files, recursively.

    LS-2 widened this from the LS-1 seven-library, `.cpp`-only,
    non-recursive sweep (`src/<dir>/*.cpp`) to a recursive `src/**` sweep
    over `SOURCE_SUFFIXES` -- `src/app` and every header come into scope
    for the first time, and no directory is ever a blind spot again absent
    an explicit, named deferral. `src/tests` is that one named deferral
    (`DEFERRED_DIRS`, R2/A-2): its matching files are collected separately
    and reported by the caller, never silently dropped from the sweep.
    """
    discovered = set()
    deferred = set()
    for search_path in search_paths:
        resolved = search_path if search_path.is_absolute() else repository_root / search_path
        candidates = [resolved] if resolved.is_file() else sorted(resolved.rglob("*"))
        for candidate in candidates:
            if not candidate.is_file() or candidate.suffix not in SOURCE_SUFFIXES:
                continue
            if is_under_deferred_dir(candidate, repository_root):
                deferred.add(candidate)
                continue
            discovered.add(candidate)
    return sorted(discovered), sorted(deferred)


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

| Path | Reason |
| --- | --- |
| `src/owner/owned.cpp` | representation owner |

Prose between tables.

| Path | Count | Note |
| --- | --- | --- |
| `src/app/appendix.cpp` | 5 | an informational appendix row, NOT an exemption |
"""

SELF_TEST_CASES = (
    ("unannotated", "int a = ch->in_room;\n", 1),
    ("bogus-reason", "int a = ch->in_room; // LS1-ALLOW: not-an-authorized-reason\n", 1),
    ("valid-trailing", "int a = ch->in_room; // LS1-ALLOW: write (a real one)\n", 0),
    ("valid-continuation", "#define M(ch) \\\n    (ch)->in_room /* LS1-ALLOW: write (macro) */\n", 0),
    # I1: an LS1-ALLOW living inside a STRING LITERAL must not exempt the line.
    ("string-literal-bypass", 'int a = ch->in_room; log("LS1-ALLOW: write");\n', 1),
    # I2: a reason that merely PREFIXES an authorized one must not pass.
    ("prefix-extension-bypass", "int a = ch->in_room; // LS1-ALLOW: writeable-anything\n", 1),
)


def run_self_test():
    """Prove the gate still fails in every direction it must. Returns exit code."""
    import tempfile

    failures = []
    with tempfile.TemporaryDirectory() as temporary_root:
        root = pathlib.Path(temporary_root)
        ledger = root / "ledger.md"
        ledger.write_text(SELF_TEST_LEDGER, encoding="utf-8")
        allow_listed = load_allow_listed_files(ledger, root)

        # M10: only the canonical table's row is an exemption.
        if "src/owner/owned.cpp" not in allow_listed:
            failures.append("M10: canonical allow-list row was NOT honored")
        if "src/app/appendix.cpp" in allow_listed:
            failures.append("M10: an appendix table row WAS promoted to a whole-file exemption")

        source_dir = root / "src" / "probe"
        source_dir.mkdir(parents=True)
        for name, body, expected_violations in SELF_TEST_CASES:
            probe = source_dir / f"{name}.cpp"
            probe.write_text(body, encoding="utf-8")
            found = len(findings_for_file(probe, root, allow_listed))
            if found != expected_violations:
                failures.append(
                    f"{name}: expected {expected_violations} violation(s), got {found}"
                )
            probe.unlink()

        # A whole-file exemption really does silence its file.
        owner_dir = root / "src" / "owner"
        owner_dir.mkdir(parents=True)
        owned = owner_dir / "owned.cpp"
        owned.write_text("int a = ch->in_room;\n", encoding="utf-8")
        if findings_for_file(owned, root, allow_listed):
            failures.append("allow-listed file was still flagged")
        # ...and removing it from the ledger un-silences it.
        if not findings_for_file(owned, root, set()):
            failures.append("removing the ledger row did NOT re-flag the file")

        # DEFERRED_DIRS files are collected separately, never scanned silently.
        deferred_dir = root / "src" / DEFERRED_DIRS[0]
        deferred_dir.mkdir(parents=True)
        (deferred_dir / "fixture.cpp").write_text("int a = ch->in_room;\n", encoding="utf-8")
        scanned, deferred = source_files([root / "src"], root)
        if not any(path.name == "fixture.cpp" for path in deferred):
            failures.append("a DEFERRED_DIRS file was not reported as deferred")
        if any(path.name == "fixture.cpp" for path in scanned):
            failures.append("a DEFERRED_DIRS file leaked into the scanned set")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("location_read_census self-test: all directions pass")
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

    scanned_files, deferred_files = source_files(search_paths, repository_root)

    # O-I7: fail closed, unconditionally (not just under --check), the
    # moment the scan itself looks broken -- a zero-or-near-zero scanned
    # count is far more likely to mean a bad --root/search path than a
    # genuine shrink of production src/, and printing only the (technically
    # true) "[deferred] 0 file(s)..." notice below would read as good news.
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

    deferred_dir_list = ", ".join(f"src/{name}" for name in DEFERRED_DIRS)
    print(
        f"[deferred] {len(deferred_files)} file(s) under {deferred_dir_list} unscanned "
        "this wave (LS-2 R2/A-2 -- retired by wave LS-3a)."
    )

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
