#!/usr/bin/env python3
"""Scanner core for the room-resolve retirement census (Wave R1 deliverable).

See ``docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md``
for the full program design; this module is the sibling of
``tools/location_read_census.py`` for a DIFFERENT question. `room_data::
operator[]` (``src/world/db_world.cpp``) is a *total* function over invalid
input, with five degrade paths (spec section 1):

1. ``i < 0`` -- mudlog + return ``world[0]``.
2. ``i >= BASE_LENGTH``, beyond all extensions -- mudlog + return
   ``world[r_immort_start_room]``.
3. same, but ``i == r_immort_start_room`` -- mudlog + ``exit(0)`` (a
   recursion guard on the fallback itself, unreachable by construction
   today).
4. the allocated-but-uncreated window -- a silent dummy-room read, no
   mudlog, no fallback arm (the commonest corrupt-range silent read).
5. ``BASE_WORLD == nullptr`` -- ``abort()`` (already a tripwire; not part
   of this campaign).

The classification question this census asks is NOT the LocationSystem
program's question. That program's gate (``location_read_census.py``)
licenses *representation access* -- whether a raw ``->in_room``/``world[``/
etc. spelling is allowed to exist outside the Stage-1 Placement API at all.
This census asks *input validity* -- whether the id handed to a
resolver-reaching spelling can be proven in-range before it is dereferenced.
These are different questions with different ledgers: an ``LS1-ALLOW``
annotation on a line is NOT a proof of validity here, and never stands in
for one (spec section 1, the ``world[`` bullet -- review O-1/F-1, both
reviews' top finding). A site can be simultaneously ``LS1-ALLOW``'d
(representation access is fine) and an unclassified ``TODO`` here (its
input validity is still unproven).

This is Task 1 of Wave R1: the scanner core only (tokens, masking, macro-
family derivation, and per-line function attribution). Task 2 (this
revision) builds the ledger parser, reconciliation, the ``MAXIMUM_TODO_COUNT``
ratchet, and the ``--check``/``--self-test`` gate on top of those exact
interfaces. Task 3 implements ``--generate-ledger``/``--advise`` and generates
``docs/superpowers/room-resolve-ledger.md`` against the real tree.

Task 3's first pass hit a real blocker (see
``.superpowers/sdd/2026-07-31-rr1-census/task-3-report.md`` for the full
account): nine keys (75 sites, three files) were real production call sites
that two Task 1 scanner attribution gaps mis-keyed to file-scope -- an
unrecognized third function-definer macro, ``ASPELL`` (``src/spells.h:401``,
alongside ``ACMD``/``SPECIAL``), and a function-pointer-parameter header that
defeated the generic name-extraction heuristic's ``rfind("(")`` logic. Both
were fixed and re-review-verified (commits ``20b41fa7``/``98babc93``): the
recognized function-definer set is now proven exactly
``{ACMD, SPECIAL, ASPELL}`` tree-wide, and name extraction uses the FIRST
top-level ``(`` rather than the last. With the fix landed, Task 3 resumed:
the real ledger reconciles completely (zero unclassified sites),
``MAXIMUM_TODO_COUNT``/``MINIMUM_LEDGER_ROW_COUNT`` are seeded from the
measured real-tree totals, the ceiling-pin self-test direction is active, and
``--check`` against the real tree exits 0.

Token shape note (Task 2, self-test direction 16; Task 3: carry this into
the ledger prose). Every ``STATIC_TOKEN_PATTERNS``/``MACRO_FAMILY`` pattern
anchors on the callee name immediately followed by ``(``, matched per PHYSICAL
LINE (``scan_file`` iterates ``masked.splitlines()``), so a call whose
ARGUMENTS wrap to a following line (``room_of(\\n    ch)``) still hits --
only the name-then-open-paren pair has to share a line, not the whole call --
while a genuinely split spelling (the callee name and its ``(`` on two
different physical lines, e.g. ``room_of\\n    (ch)``) cannot, structurally,
by this line-based design. A one-time real-tree grep for that second shape
(``grep -rnE "room_of\\s*$" src --include="*.cpp"``) found zero matches at
this commit, confirming the shape does not exist in production today.
"""

import argparse
import os
import pathlib
import re
import sys


# Copied verbatim from tools/location_read_census.py:144-145 -- copy, not
# shared: each gate's self-test must prove its own copy, and a shared module
# would let one refactor weaken both gates at once.
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".c", ".inl", ".ipp",
                   ".hxx", ".h++", ".tcc", ".inc", ".ixx", ".cppm")

RAW_STRING_PATTERN = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')


def mask_comments_and_string_literals(source_text, mask_comments=True):
    """Blank out comment and string/char-literal CONTENTS, keep newlines/length.

    Copied verbatim from tools/location_read_census.py:342-430 -- copy, not
    shared: each gate's self-test must prove its own copy, and a shared
    module would let one refactor weaken both gates at once.

    mask_comments=False switches to a STRINGS-ONLY mode: comment SPANS are
    still recognized and skipped whole (so an apostrophe inside a comment is
    never mistaken for the start of a char literal), but their contents are
    left un-blanked. String/char-literal contents are still blanked in both
    modes. Line/column positions are preserved 1:1 so line numbers stay
    valid.
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
            # CODE. An apostrophe directly preceded by a digit is a
            # separator, not a char-literal opener.
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
                    # blanking arbitrary following lines.
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


# RR Wave R3 Task 5-fix (review-1 B-3b). The masker above understands
# comments and literals but not the preprocessor, so wrapping a registered
# guard in `#if 0 ... #endif` leaves its literal textually present: the
# registry's DOWNWARD check passes while the guard is compiled out. This
# blanks every INACTIVE `#if 0` region -- line count and every line's length
# preserved, so line numbers stay valid -- before either R3 check reads the
# text.
#
# "Simple balanced scan", deliberately: a never-true `#if` opens a region,
# any nested `#if`/`#ifdef`/`#ifndef` deepens it, `#endif` closes it, and an
# `#else` or `#elif` at the region's OWN depth ends the inactive part (that
# branch is live code and must keep being scanned). Anything cleverer would
# be a C preprocessor, which this gate is not; a literally-never-true
# CONSTANT condition is the only inactive form it claims to see, and there
# are ZERO such regions in the tree today (verified by grep at this commit),
# so nothing real changes.
#
# Task 5-fix2 (re-verification M-3): the original pattern recognized only the
# literal spelling `#if 0`, and the re-verifier walked a registered guard out
# of the build past it six other ways -- `#if 00`, `#if (0)`, `#if 0L`,
# `#if !1`, `#if 1 - 1`, `#if 0 - 0` -- each `--check` exit 0 with the guard
# literal still textually present. The condition is now NORMALIZED (comment
# stripped, all whitespace removed, whole-expression parentheses peeled) and
# matched against the never-true constant forms.
#
# RESIDUAL, recorded rather than papered over (ledger blind-spot list,
# playbook): `#ifdef ROTS_NEVER_DEFINED` and `#if defined(ROTS_NEVER)` are
# NOT decidable textually -- whether they are never-true depends on what the
# translation unit defined, which only a preprocessor knows. A guard hidden
# behind one of those is still `--check` exit 0, and is caught by review and
# by the per-entry discriminator tests, not by this gate.
INACTIVE_IF_LINE_PATTERN = re.compile(r"^\s*#\s*if\s+(?P<condition>.*)$")
NEVER_TRUE_CONDITION_PATTERN = re.compile(
    r"^(?:0+[uUlL]*|0[xX]0+[uUlL]*|!1+[uUlL]*|1-1|0-0)$")
PREPROCESSOR_IF_PATTERN = re.compile(r"^\s*#\s*(if|ifdef|ifndef)\b")
PREPROCESSOR_ENDIF_PATTERN = re.compile(r"^\s*#\s*endif\b")
PREPROCESSOR_ELSE_PATTERN = re.compile(r"^\s*#\s*(else|elif)\b")


def _condition_is_never_true(condition):
    """Is this `#if` condition a literally-never-true CONSTANT expression?

    Normalizes first -- trailing comment dropped, ALL whitespace removed,
    parentheses wrapping the WHOLE expression peeled -- so `#if ( 0 )`,
    `#if 00` and `#if 1 - 1` read the same as `#if 0`. Anything with an
    identifier in it (`#if defined(X)`, `#if DEBUG_LEVEL - 1`) is left alone:
    deciding those needs a preprocessor, and a gate that GUESSED would either
    blank live code or claim a closure it does not have."""
    text = condition.split("//", 1)[0].split("/*", 1)[0]
    text = "".join(text.split())
    while len(text) > 2 and text.startswith("(") and text.endswith(")"):
        inner = text[1:-1]
        depth = 0
        for character in inner:
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth < 0:
                    break
        if depth != 0:
            # The outer parens are not a single wrapping pair (`(a)+(b)`).
            break
        text = inner
    return bool(text) and NEVER_TRUE_CONDITION_PATTERN.match(text) is not None


def line_opens_inactive_region(line):
    """Does this physical line open a never-true `#if` region?"""
    match = INACTIVE_IF_LINE_PATTERN.match(line)
    return match is not None and _condition_is_never_true(match.group("condition"))


def strip_inactive_preprocessor_blocks(masked_text):
    """Blank never-true `#if ... #endif` regions in already-masked text.

    Returns text with the same number of lines, each of the same length, so
    every line number and column the callers already computed stays valid."""
    lines = masked_text.split("\n")
    depth = 0            # nesting depth INSIDE an inactive region (0 = active)
    for index, line in enumerate(lines):
        if depth == 0:
            if line_opens_inactive_region(line):
                depth = 1
                lines[index] = " " * len(line)
            continue
        # Inside an inactive region: blank this line, then adjust the depth.
        blanked = " " * len(line)
        if PREPROCESSOR_IF_PATTERN.match(line):
            depth += 1
        elif PREPROCESSOR_ENDIF_PATTERN.match(line):
            depth -= 1
        elif depth == 1 and PREPROCESSOR_ELSE_PATTERN.match(line):
            # The `#else`/`#elif` arm of an `#if 0` is LIVE code: end the
            # inactive region here (the directive line itself is blanked, the
            # code after it is not).
            depth = 0
        lines[index] = blanked
    return "\n".join(lines)


# The six non-macro resolver-reaching spellings (spec section 1). world[ is
# the operator[] invocation itself -- ~30 live production sites survive the
# LS waves under LS1-ALLOW annotations, which license REPRESENTATION access,
# not input validity; they classify here like every other site (review
# O-1/F-1, the dual spec review's convergent blocker). world_room_vnum/
# dispatch_room_vnum are the one hook-dispatch alias pair at HEAD (F-2),
# pinned while the set is closed.
#
# review-1 W-3/F-4 (the m-13 paste-evasion class, LS-3b's own gate-hardening
# precedent -- AGENTS.md's "token-paste" prefix): the ## preprocessor paste
# operator can assemble a tracked identifier (`room##_of(` -> `room_of(`
# after expansion) across two macro operands, defeating every intact-token
# pattern above AND derive_macro_family's own seeding (which only matches
# STATIC token patterns against a macro BODY's literal text, never simulated
# expansion). Tracking the bare `##` token itself, unconditionally, closes
# this the same way LS-3b's location gate closed it: any physical line
# containing a paste operator becomes a site needing its own ledger row,
# whatever it assembles. Verified at this commit: a real-tree grep
# (`grep -rn '##' src --include='*.h' --include='*.cpp'`) finds exactly two
# hits tree-wide, both inside string literals (`act_wiz.cpp:4207`'s
# `"Usage: top ## ..."` and its test's expectation string) and therefore
# masked before the scan ever sees them -- zero real production sites, hence
# the token-counts table's `| \`##\` | 0 |` row.
STATIC_TOKEN_PATTERNS = (
    ("room_of(", re.compile(r"\broom_of\s*\(")),
    ("room_by_id_total(", re.compile(r"\broom_by_id_total\s*\(")),
    ("world[", re.compile(r"(?:\b|::)world\s*\[")),
    ("world_room_vnum(", re.compile(r"\bworld_room_vnum\s*\(")),
    ("dispatch_room_vnum(", re.compile(r"\bdispatch_room_vnum\s*\(")),
    ("##", re.compile(r"##")),
)

# The resolver-expanding macro family, PINNED as a literal (spec section
# 1.4). --check re-derives the family from all scanned SOURCE files' #define
# bodies -- headers AND .cpp files, not headers alone -- and fails on any
# mismatch (the closed-world direction, Task 2): a future resolver-reaching
# macro anywhere forces this literal AND its sites' classification rows to
# land together. VALID_EDGE is the proof this closed-world direction works
# end to end: it is .cpp-LOCAL (src/pathfind/graph.cpp:70, never exported
# via a header) and reaches room_by_id_total( through TOROOM(x, y); this
# task's own real-tree --derive-macros sweep found it as an eleventh name
# (a STOP-gate hit, ruled a genuine family member in commit 61332054) --
# had the sweep been scoped to headers only, its two call sites
# (graph.cpp:133/:146) would have stayed silently invisible to the token
# scan, so the sweep stays whole-tree by design (see the CLI wiring below).
MACRO_FAMILY = (
    "EXIT", "OUTSIDE", "SUN_PENALTY", "IS_DARK", "IS_LIGHT", "IS_SUNLIT",
    "IS_SUNLIT_EXIT", "IS_SHADOWY_EXIT", "IS_WATER", "ASSIGNROOM",
    "VALID_EDGE",
)


def token_patterns():
    """Static tokens plus one bare-word call pattern per family macro."""
    macro = tuple((f"{name}(", re.compile(r"\b" + re.escape(name) + r"\s*\("))
                  for name in MACRO_FAMILY)
    return STATIC_TOKEN_PATTERNS + macro


def collect_define_bodies(header_text):
    """Map macro name -> full body text (continuation lines joined), from
    MASKED header text (a commented-out #define must not derive -- mask
    first, exactly as the scan does)."""
    bodies = {}
    lines = mask_comments_and_string_literals(header_text).splitlines()
    i = 0
    define_re = re.compile(r"^\s*#\s*define\s+(\w+)")
    while i < len(lines):
        m = define_re.match(lines[i])
        if m:
            name, body = m.group(1), lines[i]
            while body.rstrip().endswith("\\") and i + 1 < len(lines):
                i += 1
                body = body.rstrip()[:-1] + "\n" + lines[i]
            bodies[name] = body
        i += 1
    return bodies


def derive_macro_family(source_texts):
    """Transitive closure: macros whose bodies reach a resolver spelling.

    Seeds: any body matching a STATIC token pattern (this includes world[,
    which is how ASSIGNROOM joins -- review F-3/O-1). Closure: any body
    invoking an already-in-family macro name.

    `source_texts` (review-1 W-7 rename, from `header_texts`): maps path ->
    full file text for every SCANNED SOURCE file, headers AND .cpp files --
    never headers alone. VALID_EDGE (`src/pathfind/graph.cpp:70`) is the
    proof this matters: it is a .cpp-LOCAL macro, never exported via a
    header, so a header_texts-only sweep would silently miss it and its two
    call sites (graph.cpp:133/:146) would stay invisible to the token scan
    (see the CLI wiring below, and the module docstring's whole-tree-sweep
    note). The OLD parameter name (`header_texts`) said the opposite of
    what every caller actually passes; renamed here rather than left as a
    misleading label a future reader could take literally.
    """
    bodies = {}
    for text in source_texts.values():
        bodies.update(collect_define_bodies(text))
    family = {name for name, body in bodies.items()
              if any(p.search(body) for _, p in STATIC_TOKEN_PATTERNS)}
    changed = True
    while changed:
        changed = False
        for name, body in bodies.items():
            if name in family:
                continue
            if any(re.search(r"\b" + re.escape(f) + r"\s*\(", body) for f in family):
                family.add(name)
                changed = True
    return frozenset(family)


# Function headers defined via macros (spec section 4; 635 production
# functions use these -- interpre.h:35/:49). Extend ONLY alongside a
# self-test direction proving the new definer attributes correctly.
#
# FIX ROUND 2: both definer regexes are now FULLY anchored (trailing
# ``\s*$``) and matched against the ACCUMULATED, brace-stripped header
# (``head`` -- see attribute_lines) rather than the single current raw
# line. Round 1 matched only the single current line, which (a) never
# checked whether that same line also carried the body-opening ``{`` --
# so the repo's DOMINANT gtest style, a brace-attached one-liner
# (``TEST(Suite, Name) {``, 536 such headers), silently attributed its
# whole body to file-scope -- and (b) could never recognize a definer
# whose two arguments wrap onto a following line (live at
# occupant_order_tests.cpp:744-745 / db_loader_tests.cpp:1823-1824),
# which left a STALE ``pending_header`` from an earlier, already-opened
# test to leak forward and cross-contaminate the next one (confirmed
# live: occupant_order_tests.cpp:746 keyed to an unrelated prior test's
# name). Matching against the growing ``head`` fixes both: a definer is
# recognized the moment its argument list balances, independent of
# whether ``{`` is present yet, and the SAME iteration's open-brace check
# (shared with the generic path) opens the function immediately when it
# is.
#
# FIX ROUND 3, GAP 1 (Task 3's real-tree run hit this fail-closed):
# ``ASPELL`` (``src/spells.h:401``) is a THIRD single-argument definer of
# the identical ACMD/SPECIAL shape -- ``#define ASPELL(castname)`` expands
# to a fixed ``void castname(...)`` spell-handler signature, exactly the
# ACMD/SPECIAL pattern -- and its ~70 call sites (``src/combat/mage.cpp``/
# ``mystic.cpp``) fell to file-scope with no recognized definer for them.
# ENUMERATION METHOD (so this set is reproducible, not hand-picked): every
# ``#define NAME(single_param)`` across all scanned ``.h`` files whose
# (backslash-joined, comment/string-masked) body both (1) uses that single
# parameter as a callable name -- ``\bPARAM\s*\(`` OR the wrapped-name idiom
# ACMD/SPECIAL themselves use, ``\(PARAM\)\s*\(`` -- and (2) ends the body
# at a bare ``)`` with no trailing ``;`` (a declarator, not a full
# statement -- this is what excludes the ``ASSIGNMOB``/``ASSIGNOBJ``/
# ``ASSIGNROOM``-family macros, which expand to a `{ ... }` statement
# block, not a function header). Run against the real tree at this fix's
# commit, this enumeration finds EXACTLY THREE macros tree-wide: ``ACMD``,
# ``SPECIAL`` (both already recognized), and ``ASPELL`` -- no fourth
# exists. Re-run this same method before ever adding a new name here.
FUNCTION_DEFINER_RE = re.compile(r"^\s*(?:ACMD|SPECIAL|ASPELL)\s*\(\s*(\w+)\s*\)\s*$")
# GoogleTest's own definer family (fix round 1, CRITICAL): TEST/TEST_F/TEST_P
# each take TWO comma-separated arguments (suite, name) and key as
# "Suite.Name" -- gtest's own convention, and the shape the ledger's test-
# tier rows must match. Measured before round 1's fix: 244 of 1177
# tree-wide token hits (~21%, 15 test files) silently mis-keyed to the
# literal macro name "TEST"/"TEST_F"/"TEST_P" via the generic heuristic
# below, which is blind to the second comma-separated argument.
GTEST_DEFINER_RE = re.compile(r"^\s*(?:TEST|TEST_F|TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*$")
# The recognized macro-definer names -- anything matching an all-caps
# macro-invocation shape that is NOT one of these is an unrecognized
# definer, not a function name (see ALL_CAPS_MACRO_INVOCATION_RE below).
RECOGNIZED_DEFINER_NAMES = frozenset({"ACMD", "SPECIAL", "ASPELL", "TEST", "TEST_F", "TEST_P"})
# Hardens the generic heuristic against the same mis-keying class recurring
# for any FUTURE all-caps macro this scanner doesn't yet recognize (fix
# round 1, CRITICAL): a header whose very start is an all-caps identifier
# immediately followed by '(' is a macro-invocation shape, not an ordinary
# function definition. If that identifier isn't in RECOGNIZED_DEFINER_NAMES,
# the site is UNRECOGNIZED and must fail closed to file-scope rather than
# keying on the macro's own name (which silently invents a wrong function
# key, exactly like the TEST/TEST_F/TEST_P case above).
ALL_CAPS_MACRO_INVOCATION_RE = re.compile(r"^([A-Z_][A-Z0-9_]*)\s*\(")
CONTROL_KEYWORDS = frozenset({"if", "while", "for", "switch", "return", "sizeof", "catch"})
# Fix round 1, IMPORTANT (struct-return-type false rejection): the ORIGINAL
# guard blanket-rejected any header starting with struct/class/enum/union/
# typedef, which also rejected legitimate C-style function DEFINITIONS whose
# return type happens to be struct-prefixed (`struct room_data*
# get_char_room(...)`) -- ~30 production functions share this shape
# (get_char_room, the get_obj family, read_mobile/read_object, visibility.cpp
# getters, unequip_char...). This regex instead recognizes only a bare type
# DEFINITION head -- a tag keyword, a name, and an optional base-list, with
# NO parenthesized argument list anywhere -- so `struct foo {` / `struct foo
# : bar {` still reject, but a struct-return function header (which has a
# '(' for its argument list) does not match this pattern and is accepted by
# the ordinary name-extraction path below.
TYPE_DEFINITION_HEAD_RE = re.compile(r"^(?:struct|class|enum|union)\s+\w+\s*(?::\s*[\w,\s:<>]*)?$")
# FIX ROUND 3 HARDENING (re-review IMPORTANT): a real function/method name
# can never BE a C/C++ type or storage keyword -- if the generic extractor
# ever produces one, that is proof it grabbed a return-type/storage-
# specifier word rather than a name (the fn-ptr-returning shape, `int
# (*get_fn())(int)`, is the confirmed way this happens; the check here is
# belt-and-braces against any OTHER shape reaching the same wrong result).
TYPE_OR_STORAGE_KEYWORDS = frozenset({
    "int", "void", "char", "bool", "short", "long", "unsigned", "signed",
    "float", "double", "const", "static", "inline", "struct", "class",
    "enum", "union", "extern", "register", "volatile",
})


def attribute_lines(masked_lines):
    """Per-line (kind, name): ('macro', NAME) inside #define bodies;
    ('function', qualified_name) inside a function body; ('file-scope', '')
    otherwise. Namespace names qualify; braces are counted on masked text.

    CORRECTION (Task 1, from the brief's starting algorithm): header
    detection is gated on ``depth == base_depth`` rather than the literal
    ``depth == 0`` -- ``base_depth`` is the depth level immediately inside
    the innermost open namespace (0 with no namespace open). A namespace's
    own opening brace increments ``depth`` past 0, so a literal ``depth ==
    0`` gate never re-armed header detection for any function declared
    inside a ``namespace { ... }`` block -- confirmed by running the
    dev-selftest's ``rots::helper`` case against the unmodified brief code
    (it misattributed every line of that function's body as file-scope
    instead of ``rots::helper``). Tracking ``base_depth`` from
    ``namespace_stack`` fixes this without weakening any other case: at
    file scope with no namespace open, ``base_depth`` is still 0.

    FIX ROUND 1 (task review, one CRITICAL + two IMPORTANT findings):

    - CRITICAL: ``TEST``/``TEST_F``/``TEST_P`` (gtest's own definer family)
      fell to the generic heuristic below, which extracted the macro name
      itself -- every gtest body silently mis-keyed to the literal function
      name ``"TEST"``/``"TEST_F"``/``"TEST_P"`` (244 of 1177 tree-wide token
      hits, ~21%, across 15 test files). Fixed two ways: ``GTEST_DEFINER_RE``
      recognizes the real two-argument shape and keys ``"Suite.Name"``; the
      generic heuristic additionally fails closed (to file-scope) for ANY
      other all-caps macro-invocation shape not in
      ``RECOGNIZED_DEFINER_NAMES``, so a FUTURE unrecognized macro definer
      can no longer mis-key on its own name either.
    - IMPORTANT: the C++17 nested-namespace form ``namespace rots::world {``
      was invisible to the old ``namespace\\s+(\\w+)?\\s*\\{`` regex (``\\w+``
      cannot match ``::``), so header detection never re-armed inside one --
      confirmed live at ``src/world/db_world.cpp:198/257/348``, misattributing
      the resolver ``_impl`` functions themselves. Fixed by widening the
      capture to ``[\\w:]+`` and pushing one ``namespace_stack`` entry per
      ``::``-separated segment (all sharing the same ``depth_at_open``, since
      there is only one physical ``{``/``}`` pair) -- the existing
      ``qualifier = "::".join(...)`` join already produces the right
      dotted-in-``::`` name from either a nested or a compound form.
    - IMPORTANT: the blanket ``not head.startswith(("struct ", "class ", ...))``
      guard rejected legitimate C-style function definitions with a
      struct-prefixed return type (``struct room_data* get_char_room(...)``,
      ~30 production functions). Fixed by replacing the blanket prefix
      rejection with ``TYPE_DEFINITION_HEAD_RE``, which matches only a bare
      type-definition head (tag + name + optional base-list, no parenthesized
      argument list) -- a struct-return function's head always has a ``(``
      for its argument list and so never matches, while a plain ``struct foo
      {`` / ``struct foo : bar {`` still does.
    """
    attributions = [("file-scope", "")] * len(masked_lines)

    # Pass 1: macro-definition ranges (before brace logic -- a macro body's
    # braces must not perturb depth).
    in_macro = None
    define_re = re.compile(r"^\s*#\s*define\s+(\w+)")
    macro_lines = set()
    for i, line in enumerate(masked_lines):
        if in_macro is not None:
            attributions[i] = ("macro", in_macro)
            macro_lines.add(i)
            if not line.rstrip().endswith("\\"):
                in_macro = None
            continue
        m = define_re.match(line)
        if m:
            attributions[i] = ("macro", m.group(1))
            macro_lines.add(i)
            if line.rstrip().endswith("\\"):
                in_macro = m.group(1)

    depth = 0
    namespace_stack = []      # (name, depth_at_open)
    function_name = None
    function_open_depth = None
    pending_header = None     # candidate name seen at base depth, awaiting '{'
    header_accum = ""         # multi-line header accumulation at base depth

    for i, line in enumerate(masked_lines):
        if i in macro_lines:
            continue
        stripped = line.strip()
        base_depth = (namespace_stack[-1][1] + 1) if namespace_stack else 0
        if depth == base_depth and function_name is None:
            # A bare scope-closing line (just '}', optionally with a
            # trailing ';') can never be part of a header -- discard any
            # accumulated fragment instead of letting it survive into the
            # NEXT line. Found via the dev harness itself (fix round 2):
            # an enclosing namespace's own closing '}' otherwise leaked
            # into header_accum and glued onto the following, unrelated
            # header (e.g. a bare '}' immediately before `ACMD(do_look)`
            # produced the accumulated head "} ACMD(do_look)", which the
            # generic heuristic then misread as a function literally named
            # "ACMD" with argument list "(do_look)").
            #
            # review-1 W-9 (cheap/riding fix): the SAME bug shape recurs when
            # the stray close and the FOLLOWING header share one PHYSICAL
            # line (`} ACMD(do_x) {`) rather than sitting on separate lines
            # -- the leading '}' still glues into header_accum and mis-keys
            # the site as literally "ACMD". Stripping a leading run of
            # '}'/';' (mirroring the discipline this file already applies to
            # a bare '}'/';' LINE, just now generalized to a leading RUN
            # within the line) before computing `stripped` for the rest of
            # this block closes it: the run is consumed as a scope-close (so
            # any stale accumulation is discarded exactly as a bare-close
            # line already discards it) and the REMAINDER of the line is
            # what header recognition actually sees. Zero live occurrences
            # at this commit -- a fresh whole-tree grep
            # (`grep -rnE '\}\s*while\s*\(\s*0\s*\)' src`) finds 36 `}
            # while(0)` hits (not the 96 this wave's own gitignored
            # progress notes carried -- corrected here since progress.md
            # itself cannot carry a reviewable fix), and every one of the 36
            # lives inside a `#define` body (the do-while(0) macro idiom),
            # which pass 1's `macro_lines` skip (above) already routes
            # around before this loop ever reaches this block for that
            # line -- so this is a hardening against a shape this tree does
            # not currently produce, not an active production mis-key.
            leading_close_match = re.match(r"^[\}\;]+", stripped)
            if leading_close_match:
                header_accum = ""
                pending_header = None
                stripped = stripped[leading_close_match.end():].lstrip()
            is_bare_scope_close = (stripped == "")
            ns = None if is_bare_scope_close else re.match(r"^\s*namespace\s+([\w:]+)?\s*\{", line)
            if ns:
                raw_name = ns.group(1)
                segments = raw_name.split("::") if raw_name else ["<anon>"]
                for segment in segments:
                    namespace_stack.append((segment, depth))
                depth += line.count("{") - line.count("}")
                continue
            if not is_bare_scope_close and stripped and not stripped.startswith("#"):
                # FIX ROUND 2, hygiene (c): a fresh accumulation beginning
                # while pending_header is still set is necessarily stale --
                # a real completed header always consumes and clears
                # pending_header at its own '{' (below). Never let an
                # earlier header's candidate name leak into a new one.
                if header_accum == "" and pending_header is not None:
                    pending_header = None
                header_accum = (header_accum + " " + stripped).strip()
                # A ';' at base depth ends any declaration/statement: no header.
                if ";" in stripped:
                    header_accum = ""
                    pending_header = None
                open_here = "{" in stripped
                # The header accumulated so far, brace-stripped -- valid
                # whether or not '{' has appeared yet (FIX ROUND 2 (b)): a
                # definer's argument list can complete on an EARLIER line
                # than its '{', so this must be checked every accumulation
                # step, not only once open_here is true.
                head = header_accum.split("{")[0].strip()
                if pending_header is None:
                    dm = FUNCTION_DEFINER_RE.match(head)
                    gm = None if dm else GTEST_DEFINER_RE.match(head)
                    if dm:
                        pending_header = dm.group(1)
                    elif gm:
                        pending_header = f"{gm.group(1)}.{gm.group(2)}"
                    # An '=' before '{' means initializer, not a function
                    # body; the generic path only fires once '{' is seen,
                    # since only then is `head` known to be complete.
                    elif open_here and "=" not in head and head.endswith("(") is False:
                        first_word = re.match(r"\s*(\w+)", head)
                        all_caps = ALL_CAPS_MACRO_INVOCATION_RE.match(head)
                        if not (all_caps and all_caps.group(1) not in RECOGNIZED_DEFINER_NAMES):
                            # FIX ROUND 3, GAP 2: slice at the FIRST '(' (the
                            # argument list's own opener), not the LAST. A
                            # function-pointer PARAMETER -- `void room_target(
                            # char_data* ch, void (*skill_damage)(char_data*
                            # character, char_data* victim))`
                            # (src/combat/olog_hai.cpp:374) -- has THREE '('s;
                            # `rfind` landed inside the fn-ptr parameter's own
                            # argument list, examining the wrong text entirely
                            # and never finding a name. The function's real
                            # name always precedes the argument list's own
                            # opening '(', which is the FIRST '(' in `head`
                            # for every live shape in this tree (ACMD/SPECIAL/
                            # ASPELL bypass this generic path entirely, so
                            # their own "(name)(...)" idiom never reaches
                            # here).
                            first_paren_index = head.find("(")
                            # FIX ROUND 3 HARDENING (re-review IMPORTANT): a
                            # function RETURNING a function pointer -- `int
                            # (*get_fn())(int)` -- has its first '(' open the
                            # RETURN TYPE's own declarator, immediately
                            # followed by '*' (modulo whitespace); slicing
                            # there put the return type's leading word
                            # ("int"/"void"/...) into name_part's tail, and
                            # the original round-3 fix SILENTLY MIS-KEYED to
                            # that word instead of failing closed (measured:
                            # ('function', 'int') for `int (*get_fn())(int)`,
                            # and likewise 'void'/'special_function_t' for
                            # the other two reviewer-supplied variants) --
                            # the exact silent-wrong-key failure mode this
                            # scanner's own stated principle forbids, a
                            # regression from the PRE-round-3 code (which
                            # gave file-scope for this shape, since rfind
                            # landed even deeper and found no identifier at
                            # all). Detect the shape directly and refuse to
                            # extract from it.
                            is_fn_ptr_returning = (
                                first_paren_index != -1
                                and re.match(r"\s*\*", head[first_paren_index + 1:]) is not None
                            )
                            name_part = (
                                head[:first_paren_index]
                                if first_paren_index != -1 and not is_fn_ptr_returning
                                else ""
                            )
                            nm = re.search(r"([A-Za-z_][\w:~]*(?:::operator\s*\S+|)|operator\s*\S+)\s*$",
                                           name_part)
                            # Belt-and-braces (re-review IMPORTANT): a real
                            # function name can NEVER be a C/C++ type or
                            # storage keyword. If extraction ever produces
                            # one anyway (a shape the shape-check above
                            # didn't anticipate), that is proof the
                            # candidate is a return-type/storage-specifier
                            # word, not a name -- reject and fail closed
                            # rather than key on it.
                            if (nm and "(" in head and not is_fn_ptr_returning
                                    and nm.group(1) not in TYPE_OR_STORAGE_KEYWORDS
                                    and (first_word is None or first_word.group(1)
                                         not in CONTROL_KEYWORDS)
                                    and not TYPE_DEFINITION_HEAD_RE.match(head)):
                                pending_header = nm.group(1)
                # FIX ROUND 2 (a): open the function on THIS SAME iteration
                # whenever '{' and a resolved pending_header coincide --
                # this is what a brace-attached one-liner definer
                # (`TEST(Suite, Name) {`, `ACMD(do_x) {`, the dominant gtest
                # style, 536 headers) needs: round 1's separate dm/gm
                # branches set pending_header but never reached this check
                # on the SAME line, so the body silently fell to file-scope.
                if open_here and pending_header is not None:
                    qualifier = "::".join(n for n, _ in namespace_stack)
                    function_name = (qualifier + "::" + pending_header) if qualifier else pending_header
                    function_open_depth = depth
                    pending_header = None
                    header_accum = ""
                elif open_here:
                    # struct/class/enum/initializer body at base depth:
                    # contents stay file-scope; depth tracking still applies.
                    header_accum = ""
        if function_name is not None:
            attributions[i] = ("function", function_name)
        depth += line.count("{") - line.count("}")
        if function_name is not None and depth <= function_open_depth:
            function_name = None
            function_open_depth = None
        while namespace_stack and depth <= namespace_stack[-1][1]:
            namespace_stack.pop()
    return attributions


def _brace_depth_before(line, index):
    """Brace nesting depth at `index` on one physical (masked) line."""
    prefix = line[:index]
    return prefix.count("{") - prefix.count("}")


def file_scope_occurrence_is_admissible(line, match_start):
    """Is a pinned-token occurrence on a FILE-SCOPE line a declaration head?

    RR Wave R3 Task 5-fix, review-1 B-1. Both R3 checks used to SKIP every
    `file-scope`-attributed line outright, on the (documented, and for the
    real tree correct) grounds that such lines are declarations and
    non-brace-attached definition headers, never calls. The reviewer
    demonstrated the hole: a ONE-LINE function definition --

        void rogue(struct char_data* ch, char* arg) { ((*cmd_info[5].command_pointer)(ch, ...)); }

    -- puts the entire body on a line `attribute_lines` reports as
    `file-scope` (there is no separate brace line to open a function scope),
    so an unregistered dispatcher, or an 87th `CAN_SEE(` caller, passed both
    checks silently. This is the same brace-boundary attribution fragility
    the sibling census's W-1 `} ACMD(do_x) {` hardening dealt with.

    The discriminator is the occurrence's own BRACE DEPTH on its line. A
    declaration (`int CAN_SEE(char_data* sub);`), a wrapped definition header
    (`int activate_char_special(char_data* character,`), a struct-member
    fn-ptr (`void (*command_pointer)(struct char_data* ch,`) and a brace-init
    definition (`std::array<...> g_command_table {};`) all read the token at
    depth 0. An inline body puts it at depth >= 1. Verified against the real
    tree at this commit: all 8 file-scope dispatch-spelling occurrences and
    all 5 file-scope pinned-caller occurrences sit at depth 0."""
    return _brace_depth_before(line, match_start) == 0


def scan_file_full(source_path, repository_root):
    """One masking+attribution pass over a file, yielding all three of the
    gate's per-file measurements:

      * `sites` -- every resolver-token site, keyed by its enclosing
        function/macro (the ledger's own unit; see `scan_file`).
      * `dispatch_occurrences` -- every DISPATCH_SPELLING_TOKENS occurrence
        in PRODUCTION code on a non-file-scope line (R3 Task 1a, direction
        (b) of the dispatch-entry registry's closed world).
      * `caller_counts` -- per-token occurrence counts for the R3-O-3
        caller-count pins, measured by the method recorded verbatim beside
        `PINNED_CALLER_COUNTS`.
      * `attribution_errors` -- Task 5-fix (review-1 B-1): file-scope lines
        carrying a pinned token at brace depth >= 1, i.e. an inline function
        body masquerading as a declaration. Both checks used to SKIP these.

    All four ride the SAME pass deliberately: `attribute_lines` is the
    expensive step, and re-walking 316 files twice more would triple the
    gate's `ctest` runtime for measurements that need exactly the same masked
    text and attributions the site scan already computed."""
    text = source_path.read_text(encoding="utf-8", errors="replace")
    masked = strip_inactive_preprocessor_blocks(
        mask_comments_and_string_literals(text)).splitlines()
    attributions = attribute_lines(masked)
    sites = []
    dispatch_occurrences = []
    caller_counts = {}
    attribution_errors = []
    rel = source_path.relative_to(repository_root).as_posix()
    is_test_tier = rel == "src/tests" or rel.startswith("src/tests/")
    for i, line in enumerate(masked):
        kind, name = attributions[i]
        for token_name, pattern in token_patterns():
            for _ in pattern.finditer(line):
                # Two hits of one token on one line are two sites -- the
                # per-key COUNT is the anti-inheritance mechanism and must
                # not collapse them.
                sites.append({"file": rel, "function": name,
                              "attribution": kind, "token": token_name})
        # The dispatch registry and the caller-count pins are both
        # PRODUCTION-scoped claims.
        if is_test_tier:
            continue
        # A file-scope line is admitted for either check only when the
        # occurrence sits at brace depth 0 -- a declaration or a definition
        # HEAD. At depth >= 1 it is an inline function body, which is
        # review-1 B-1's demonstrated bypass of BOTH checks, and is an ERROR
        # rather than a skip. See file_scope_occurrence_is_admissible.
        if kind == "file-scope":
            for token_name, pattern in DISPATCH_SPELLING_TOKENS:
                for match in pattern.finditer(line):
                    if file_scope_occurrence_is_admissible(line, match.start()):
                        continue
                    attribution_errors.append(
                        f"file-scope dispatch spelling {rel}:{i + 1} -- token `{token_name}` "
                        "occurs inside braces on a line the scanner attributes to FILE SCOPE, "
                        "which means an inline (one-line) function body, not a declaration or "
                        "definition head. The dispatch-entry registry's closed world cannot see "
                        "such a site: split the definition across lines so the body attributes "
                        "to its function, then register it (with a guard) or pin it in "
                        "DISPATCH_TOKEN_EXEMPT_SITES."
                    )
            for token_name, pattern in PINNED_CALLER_PATTERNS.items():
                for match in pattern.finditer(line):
                    if file_scope_occurrence_is_admissible(line, match.start()):
                        continue
                    attribution_errors.append(
                        f"file-scope pinned caller {rel}:{i + 1} -- token `{token_name}` occurs "
                        "inside braces on a line the scanner attributes to FILE SCOPE, which "
                        "means an inline (one-line) function body, not a declaration. "
                        "PINNED_CALLER_COUNTS would not count it, so the R3-O-3 pin would not "
                        "see the new caller: split the definition across lines."
                    )
            continue
        for token_name, pattern in DISPATCH_SPELLING_TOKENS:
            for _ in pattern.finditer(line):
                dispatch_occurrences.append(
                    {"file": rel, "function": name, "attribution": kind,
                     "token": token_name, "line": i + 1})
        for token_name, pattern in PINNED_CALLER_PATTERNS.items():
            hits = len(pattern.findall(line))
            if hits:
                caller_counts[token_name] = caller_counts.get(token_name, 0) + hits
    return sites, dispatch_occurrences, caller_counts, attribution_errors


def scan_file(source_path, repository_root):
    """Every resolver-token site in one file, keyed by its enclosing
    function/macro. Returns a list of Site dicts: {"file": rel_posix,
    "function": name_or_macro, "attribution": kind, "token": token_name}.

    Thin projection of `scan_file_full` (R3 Task 1a), kept as its own name
    because `--generate-ledger`, `--advise` and several self-test directions
    want only the site list."""
    return scan_file_full(source_path, repository_root)[0]


def source_files(root):
    """Every SOURCE_SUFFIXES file under root, sorted for determinism."""
    return sorted(p for p in root.rglob("*") if p.is_file() and p.suffix in SOURCE_SUFFIXES)


# ---------------------------------------------------------------------------
# Task 2: ledger parsing, reconciliation, ratchets, and --check/--self-test.
#
# The ledger is docs/superpowers/room-resolve-ledger.md (Task 3 generates it
# against the exact format documented above; this task only teaches the
# parser to read it and the gate to enforce it). Several structural idioms
# below are copied -- not shared -- from tools/location_read_census.py:
# marker-anchored tables (Opus M10: a look-alike table elsewhere in the doc,
# or one sitting inside a fenced example, must be inert), a row floor per
# table, and a subprocess-driven self-test that proves the REAL gate end to
# end rather than a predicate in isolation. Copy, not import: each gate's
# self-test must prove its own copy, so one refactor can never weaken both
# at once.
# ---------------------------------------------------------------------------

CLASSES = ("TODO", "PROVEN", "GUARDED", "TEST-FIXTURE", "RESOLVER-IMPL", "DECL")

# Closed proof-kind vocabulary (spec section 3; exact-match, prefix-boundary-
# checked the way ALLOWED_REASON_PREFIXES is in the sibling census -- a row's
# Kind must equal a vocabulary entry EXACTLY, so "entry-guardish" fails even
# though it starts with a real entry). Only a row whose Kind is not
# LEDGER_EMPTY_MARKER is checked against this vocabulary and against
# MINIMUM_PROOF_TEXT_LENGTH/CITATION_RE below -- a TODO row's Kind/Proof are
# both the empty marker and need neither.
#
# RR Wave R3 (owner ruling R3-O-1) mints the SIXTH kind, `dispatch-invariant`:
# a row whose actor id is proven not by a guard in its OWN body but by a
# tripwire guard at the DISPATCH ENTRY POINT that handed the actor to that
# body (`if (location_of(ch) == NOWHERE) { mudlog(...); return; }`). Such a
# proof is only as strong as the claim that the entry set is complete, which
# is why the kind arrives together with the marker-anchored dispatch-entry
# registry below (`LEDGER_DISPATCH_ENTRIES_MARKER`) and its closed-world
# token check: an eighth dispatcher cannot silently inherit the proof.
PROOF_KINDS = ("entry-guard", "caller-contract", "occupant-chain",
               "loop-bound", "dominating-resolve", "dispatch-invariant")

# Kinds whose Proof text MUST carry a file:line citation (review F-4/O-6).
# `dispatch-invariant` joins the set because its FIRST mandatory citation
# part IS a file:line -- the registry entry whose guard covers the actor
# (design doc `2026-08-21-rr3-combat-design.md` section 2, "Proof kind").
CITATION_REQUIRED_KINDS = frozenset({"entry-guard", "dominating-resolve", "caller-contract",
                                     "dispatch-invariant"})
CITATION_RE = re.compile(r"\b[\w./-]+\.(?:cpp|h|hpp|cc|inl)\s*:\s*\d+")
MINIMUM_PROOF_TEXT_LENGTH = 20

# Seeded (Task 3) from the real ledger's measured TODO total: the SUM of
# every TODO row's Count cell -- since reconcile()'s own ceiling check sums
# `row["count"]` (site occurrences), not the number of TODO-classified rows.
# Both `--todo-ceiling-override` (self-test only) and the ceiling-pin
# direction in run_self_test() key off this same literal -- the brief's F-10
# mirror of MINIMUM_SCANNED_FILE_COUNT's own pin below. Each drain wave
# lowers this in the same commit that adds its proofs (design doc section
# 4/O-10). RR Wave R2 Task 1 (entity/pathfind/olc small tiers) drained 40 of
# its 48 scanned TODO sites to PROVEN -- 8 stayed TODO per controller ruling
# (the char_to_room F17/RR-O-1 site, the four ACMD-dispatched-only-against-
# a-placed-character OLC rows, and obj_to_room/CAN_GO's un-completable
# HIGH-fan-in caller enumeration) -- taking the ceiling 788 -> 748. RR Wave
# R2 Task 2 (src/world small tier) drained 32 of its 35 scanned TODO sites
# to PROVEN/GUARDED -- 3 stayed TODO (weather_to_char's OUTSIDE(/room_of(
# rows, the same ACMD-dispatched-only-against-a-placed-character deferral
# class Task 1 already applied to the OLC tier) -- taking the ceiling
# 748 -> 716. Value re-derived programmatically from the ledger's own TODO
# rows at land time, per the "never hand math" rule, not estimated.
#
# RR Wave R3 Task 1a RAISES it, once, 716 -> 717 -- the only raise this
# program has taken. Coordinator ruling R3-C-3 REOPENED
# `src/world/db_world.cpp · report_zone_power · room_of(` (1 site), a Wave R2
# PROVEN row whose `caller-contract` proof enumerated "the SOLE FOUR"
# tree-wide `skills[].spell_pointer` dispatch doors. There are SIX: the row's
# own grep pattern (`spell_pointer)(`) structurally cannot match the
# unparenthesised `.spell_pointer(` spelling, so `entity_lifecycle.cpp:2440`/
# `:2442` were invisible to it -- and `:2440` runs inside `affect_modify`'s
# APPLY_SPELL arm at NOWHERE on the login path (`db_players.cpp:1403` ->
# `affect_total` `:1405` -> `modify_affects` -> `affect_modify`), so the row
# is NOWHERE-reachable for any character wearing an `APPLY_SPELL`/109 item at
# login. A wrong proof is worse than no proof: the row goes back to TODO and
# is re-dispositioned under the R3 dispatch policy alongside every other
# spell-reachable row. Both this ceiling and `run_self_test`'s pin were
# `--check`-DERIVED at the flip (the gate reported "TODO total 717 exceeds
# the ceiling of 716"), never hand-computed.
MAXIMUM_TODO_COUNT = 579

# Same floor tools/location_read_census.py's own MINIMUM_SCANNED_FILE_COUNT
# uses, at the same value: measured at 315 files under src/ at this commit
# (python3 -c a rglob-and-count over SOURCE_SUFFIXES), 15 files of headroom.
# The self-test's own symbolic pin (run_self_test) fails loudly if this is
# ever lowered below 300 rather than silently tracking a weakened floor.
MINIMUM_SCANNED_FILE_COUNT = 300

# Seeded (Task 3) to the real ledger's measured row count (461) minus 10 --
# small headroom for legitimate future row merges (e.g. two TODO rows for the
# same key-shape later proven together under one PROVEN row), not a floor an
# ordinary edit should ever need to touch. The self-test's floor direction
# spends this constant SYMBOLICALLY (MINIMUM_LEDGER_ROW_COUNT - 1 / + 0), the
# same discipline MINIMUM_SCANNED_FILE_COUNT's own boundary probe uses, so a
# future edit that raises the literal keeps exercising the real edge rather
# than a stale one.
MINIMUM_LEDGER_ROW_COUNT = 451

# review-1 W-1(b): the RESOLVER-IMPL class is pinned by (file, function) --
# same discipline as MACRO_FAMILY's own pinned literal -- so extending it is
# a review-visible SCRIPT edit, not a ledger-only one (a ledger-only "class"
# flip could otherwise launder ANY site into the no-proof-required
# RESOLVER-IMPL bucket, review-1's demonstrated blocker: a TODO row flipped
# straight to RESOLVER-IMPL passed --check with zero other changes). This
# set is enumerated directly from the real ledger's RESOLVER-IMPL rows as
# they stand AFTER this commit's own W-5 reclassification (below) --
# `renum_world`/`setup_dir` left the RESOLVER-IMPL class in the SAME commit
# that introduced this pin, so the pin reflects the post-W-5 state, not a
# transient 8/10-pair snapshot that would need a second edit immediately
# after.
RESOLVER_IMPL_KEYS = frozenset({
    ("src/entity/placement.cpp", "room_of"),
    ("src/world/db_world.cpp", "load_rooms"),
    ("src/world/db_world.cpp", "room_data::create_room"),
    ("src/world/db_world.cpp", "room_data::operator[]"),
    ("src/world/db_world.cpp", "rots::world::room_by_id_impl"),
    ("src/world/db_world.cpp", "rots::world::room_by_id_total_impl"),
})

# review-1 W-1(c): restores spec section 4's fail-closed rule for the DECL
# class's "#decl" (file-scope, unattributable) bucket in `.cpp` files only --
# a header's `#decl` key and any `#NAME` macro-body key (in a header OR a
# `.cpp` file) stay open/legal, since those are structurally derived from a
# real, named macro definition the scanner can always re-derive; a `.cpp`
# `#decl` key, in contrast, means the scanner's attribute_lines() could NOT
# attach the site to any real function or macro at all, which must not
# silently auto-launder into a no-proof-required class as production code
# changes shape around it. Enumerated directly from the real ledger's four
# such rows (entity/placement.cpp x2 -- room_of/room_by_id_total, two
# resolver-impl DECLs -- persist/db_players.cpp, world/db_world.cpp), plus
# one self-test fixture entry (`src/app/probe.cpp`, this file's own
# PROBE_SOURCE_OK/DECL_ROW forward declaration, reused across ~15 --self-test
# directions) -- a real "src/app/probe.cpp" never exists in production, so
# the fixture entry is inert against the real tree and needs no per-call-site
# override threaded through parse_ledger()/`_run_gate()` (see run_self_test's
# "new-cpp-decl-key" direction, which uses a STANDALONE fixture file outside
# this set to prove the check still fires).
PINNED_DECL_KEYS = frozenset({
    ("src/entity/placement.cpp", "room_by_id_total("),
    ("src/entity/placement.cpp", "room_of("),
    ("src/persist/db_players.cpp", "dispatch_room_vnum("),
    ("src/world/db_world.cpp", "world_room_vnum("),
    ("src/app/probe.cpp", "room_of("),  # self-test fixture only, see above
})

# Key = backticked `file · function · token`, KEY_SEPARATOR-joined (spec
# format). U+00B7 MIDDLE DOT, space-padded on both sides, is illegal in a
# path or a C identifier, so splitting on it is unambiguous and pipe-free by
# construction. LEDGER_EMPTY_MARKER (U+2014 EM DASH) is the literal cell
# content meaning "no Kind" / "no Proof" (the TODO row shape in the format
# example above).
KEY_SEPARATOR = " · "
LEDGER_EMPTY_MARKER = "—"

LEDGER_CLASSIFICATION_MARKER = "<!-- ROOM-RESOLVE-CLASSIFICATION -->"
LEDGER_CLASSIFICATION_HEADER = "| Key | Count | Class | Kind | Proof |"
LEDGER_TOKEN_COUNTS_MARKER = "<!-- ROOM-RESOLVE-TOKEN-COUNTS -->"
LEDGER_TOKEN_COUNTS_HEADER = "| Token | Sites |"

# ---------------------------------------------------------------------------
# RR Wave R3 Task 1a: the dispatch-entry registry (design doc
# `docs/superpowers/specs/2026-08-21-rr3-combat-design.md` section 2,
# "Closure"; owner ruling R3-O-1).
#
# The `dispatch-invariant` proof kind above lets a row inherit its actor's
# placement from a tripwire guard at the DISPATCH ENTRY POINT rather than
# from anything in the row's own function. That inheritance is only sound
# while the entry set is CLOSED -- an eighth dispatcher, or a new
# fn-ptr-dispatch call site inside some unrelated function, would otherwise
# hand an unguarded actor to a body whose row already claims to be proven.
# Two checks close it, mirroring `tools/location_read_census.py`'s
# location-state registry (`parse_registry`/`check_registry_consistency`,
# the stated precedent):
#
#   (a) DOWNWARD -- every registry row with a GUARDED/GUARDED-PRIOR status
#       names a real file and a real function whose (comment/string-masked)
#       body still contains that row's guard literal. Deleting the guard from
#       production is a gate failure, not a silent proof rot.
#   (b) UPWARD -- every occurrence of a pinned DISPATCH-SPELLING token in
#       PRODUCTION code lies inside a registered entry's function. A new
#       `.spell_pointer(` (or `command_pointer)(`, or ...) call anywhere else
#       is a gate ERROR naming the unregistered site.
#
# The registry lives in the ledger (marker-anchored, exactly once, outside
# any fence -- the M10 rule) rather than in this file, for the same reason
# the location-state registry does: the rows are prose-adjacent facts a
# reviewer reads beside the proofs that cite them. What IS pinned here, as a
# review-visible script edit, is the token surface (below) and the small,
# named exemption set -- the `RESOLVER_IMPL_KEYS`/`PINNED_DECL_KEYS`
# discipline.
LEDGER_DISPATCH_ENTRIES_MARKER = "<!-- ROOM-RESOLVE-DISPATCH-ENTRIES -->"
LEDGER_DISPATCH_ENTRIES_HEADER = (
    "| Entry point (file · function) | Actor parameter | Guard literal | Status |"
)

# The closed status vocabulary. EVERY status in it requires a guard literal;
# there is no exempt status any more.
#
# `PENDING-T1b` USED to be a third member: a transitional spelling for an
# entry whose guard Task 1a had SPECIFIED but Task 1b had not yet LANDED, so
# the row legitimately carried no guard literal and was exempt from check (a),
# with `--check` printing a loud WARNING while any such row remained.
# **RETIRED** at the Task 5-fix round, on the convergent finding of both
# whole-branch reviews (review-2 B-1 / review-1 B-2), each of which
# DEMONSTRATED the same evasion: append an entirely unguarded dispatcher to
# production, add one `PENDING-T1b` registry row naming it, and `--check`
# exits 0 -- because the status satisfied the UPWARD closed-world check
# (`key in registered` is status-blind) while being exempt from the DOWNWARD
# literal check, and the warning was printed but never counted as an error.
# `dispatch-invariant` is sound only while every registered entry actually
# guards its actor, so a status that turns off the only mechanism enforcing
# that is a hole, not a convenience. T1b landed long ago and every live row is
# `GUARDED`/`GUARDED-PRIOR`, so retiring it costs nothing; an unknown status
# is now the ordinary closed-vocabulary SystemExit in
# `_parse_dispatch_entry_row`, which is what the reviews asked for.
#
# `GUARDED-PRIOR` is `GUARDED` for an entry whose guard predates this program
# entirely (`special()`, `one_mobile_activity`, `affect_update_room`): check
# (a) treats the two identically; the distinct spelling is what keeps the
# ledger honest about which guards this wave actually wrote.
DISPATCH_ENTRY_STATUSES = ("GUARDED", "GUARDED-PRIOR")

# RR Wave R3 Task 1d (coordinator ruling R3-C-7): an entry may carry MORE
# THAN ONE guard literal, because one tripwire per entry point is not always
# enough. R3-C-7's adjacency rule says a `dispatch-invariant` row inherits an
# entry's guard only if nothing between that guard and the dispatch can run
# code with the actor as a participant; where a function must ALSO resolve or
# parse the actor earlier (`do_cast`'s room_of(ch)/target_from_word,
# `command_interpreter`'s target_parser), the honest shape is TWO tripwires --
# an early one licensing the early sites, and one immediately before the
# dispatch licensing everything reached through it. Each row then lists both,
# and every `dispatch-invariant` proof cites the specific guard LINE it rests
# on.
#
# Cell grammar: one or more backtick-wrapped literals joined by ` || `
# (SPACES around the bars, and the bars themselves written `\|\|` in the
# markdown -- `_split_row_cells` unescapes them before this runs). The
# separator is matched STRUCTURALLY, never by a bare `split(" || ")`: the
# `one_mobile_activity` entry's single literal contains a real C `||`
# operator, and a naive split would tear it in half. Anchoring on the
# backticks keeps a literal's own contents opaque.
DISPATCH_GUARD_SEPARATOR = " || "
DISPATCH_GUARD_CELL_PATTERN = r"`[^`]*`(?: \|\| `[^`]*`)*"

# Floor pinned AT the real registry's row count (12 = the nine R3 entries
# plus the three already-guarded ones), zero headroom -- the same reasoning
# `MINIMUM_REGISTRY_ROWS_A` records in the sibling census: the row-count
# floor is the only backstop against a single-row deletion whose token
# occurrences happen to sit inside ANOTHER registered entry's function, so it
# must trip on any deletion at all. `--minimum-dispatch-entries-override`
# (self-test only, RR_SELF_TEST-gated like every other override here) lets
# the hermetic fixtures run against their own tiny tables.
MINIMUM_DISPATCH_ENTRY_ROWS = 12

# The dispatch spellings themselves (design doc section 2's "Closure"
# bullet). Every one of these is a fn-ptr invocation through a dispatch
# table or a `skills[]`/`cmd_info[]`/`mob_index[]`/`obj_index[]` slot, a
# COPY of such a slot into a local (the call then happens through a name the
# token scan cannot recognise), or a direct call to one of the two SPECIAL
# invokers -- i.e. the exact syntactic shapes by which a body is entered with
# an actor it never validated. Matched per PHYSICAL LINE against
# comment/string-MASKED text, exactly like `token_patterns()` above.
#
# WIDENED at the Task 5-fix round (review-2 B-2 / review-1 B-3, convergent).
# The original seven spellings were the ones THIS WAVE happened to write, and
# both reviewers demonstrated the same class of evasion against them: an
# arrow-form member call (`sk->spell_pointer(...)`), a plain member call with
# no `(*...)` wrapper (`cmd_info[cmd].command_pointer(...)`), a hoisted copy
# (`auto fp = skills[i].spell_pointer;` then `fp(...)`), a table alias
# (`auto& tbl = g_command_table;`), and the `->funct)(` / `.func)(` family
# that `special()` and the mob-AI driver already use in production. All of
# those now have tokens. Two residual line-based limits remain and are
# DOCUMENTED rather than papered over (ledger, "The line-split-call note"):
# a call split across two physical lines at the `)`/`(` boundary, and a copy
# taken in an expression position this pattern set does not spell.
#
# Occurrences attributed to FILE SCOPE are NOT skipped any more (review-1
# B-1): they are admitted only when the occurrence itself sits at brace
# depth 0 on its physical line, which is what a declaration, a
# (non-brace-attached) definition header and a brace-init definition all look
# like -- `interpre.h:157`'s `command_pointer` struct member, `spells.h:360`'s
# `spell_pointer` member, `protos.h:229`/`interpre.h:139`'s prototypes,
# `interpre.cpp:1263`/`:1314`/`shapemob.cpp:2364`'s own definition heads, and
# `combat_hooks.cpp:39`'s `g_command_table {};`. A ONE-LINE function
# definition puts its whole body on a file-scope line, so its tokens sit at
# depth >= 1 and are an ERROR -- that was the demonstrated bypass. Everything
# else (a function body OR a macro body) must be registered or exempt.
DISPATCH_SPELLING_TOKENS = (
    ("command_pointer)(", re.compile(r"command_pointer\s*\)\s*\(")),
    ("g_command_table[", re.compile(r"\bg_command_table\s*\[")),
    ("spell_pointer)(", re.compile(r"spell_pointer\s*\)\s*\(")),
    (".spell_pointer(", re.compile(r"\.\s*spell_pointer\s*\(")),
    ("activate_char_special(", re.compile(r"\bactivate_char_special\s*\(")),
    ("activate_obj_special(", re.compile(r"\bactivate_obj_special\s*\(")),
    ("shape_center(", re.compile(r"\bshape_center\s*\(")),
    # Task 5-fix additions. Member-call forms (no `(*...)` wrapper):
    ("->spell_pointer(", re.compile(r"->\s*spell_pointer\s*\(")),
    ("->command_pointer(", re.compile(r"->\s*command_pointer\s*\(")),
    (".command_pointer(", re.compile(r"\.\s*command_pointer\s*\(")),
    # Bare-identifier forms: the slot's ADDRESS is being read rather than
    # called, which is the hoisted-copy / table-alias evasion. `(`, `[` and
    # `)(` are excluded because those spellings are the call forms already
    # tokenised above (and the struct-member declarations).
    ("command_pointer", re.compile(r"\bcommand_pointer\b(?!\s*(?:\)\s*\(|\(|\[))")),
    ("spell_pointer", re.compile(r"\bspell_pointer\b(?!\s*(?:\)\s*\(|\(|\[))")),
    ("g_command_table", re.compile(r"\bg_command_table\b(?!\s*(?:\)\s*\(|\(|\[))")),
    # The SPECIAL fn-ptr slots the two invokers, `complete_delay_impl` and
    # the mob-AI driver already dispatch through in production.
    (".func)(", re.compile(r"\.\s*func\s*\)\s*\(")),
    ("->func)(", re.compile(r"->\s*func\s*\)\s*\(")),
    (".funct)(", re.compile(r"\.\s*funct\s*\)\s*\(")),
    ("->funct)(", re.compile(r"->\s*funct\s*\)\s*\(")),
    # ... and the same two slots read as an ADDRESS. BOTH call forms are
    # excluded from the address-read tokens -- the `(*...)` wrapper form
    # (`.func)(` above) and the plain member call (`.func(` below) -- so a
    # presence TEST and a real dispatch are never the SAME token. That
    # separation is load-bearing: `DISPATCH_TOKEN_EXEMPT_SITES` is keyed on
    # the token, and 15 of its keys sit on one of these two address-read
    # spellings, so while a member call shared their token an exemption for
    # `if (mob_index[ch->nr].func)` also exempted
    # `mob_index[ch->nr].func(ch, ...)` added to the same function
    # (re-verification BLOCKER B-1, demonstrated in `do_block` and
    # `load_mobiles` at `--check` exit 0).
    ("mob_index[].func", re.compile(r"\bmob_index\s*\[[^\]]*\]\s*\.\s*func\b(?!\s*(?:\)\s*\(|\())")),
    ("obj_index[].func", re.compile(r"\bobj_index\s*\[[^\]]*\]\s*\.\s*func\b(?!\s*(?:\)\s*\(|\())")),
    # The member-call forms the split above carves out. Zero occurrences in
    # the tree today (measured): every real dispatch through these two slots
    # is written with the `(*...)` wrapper, which is why the original
    # patterns only excluded that one.
    ("mob_index[].func(", re.compile(r"\bmob_index\s*\[[^\]]*\]\s*\.\s*func\s*\(")),
    ("obj_index[].func(", re.compile(r"\bobj_index\s*\[[^\]]*\]\s*\.\s*func\s*\(")),
    # Task 5-fix2 (re-verification M-5): the remaining SPECIAL-body doors the
    # tree ALREADY uses in production and no spelling above named. The first
    # four return a spec-proc ADDRESS that the caller immediately calls
    # through a local (the hoisted-copy shape, one line later at every real
    # site); `intelligent(` is a direct call of a SPECIAL body with an actor
    # the callee never validated. All five were `--check` exit 0 in a brand
    # new unregistered function before this round. `\b` keeps
    # `virt_program_number(` out of `dispatch_virt_program_number(` (`_` is a
    # word character), so the three are genuinely distinct tokens.
    ("virt_program_number(", re.compile(r"\bvirt_program_number\s*\(")),
    ("virt_obj_program_number(", re.compile(r"\bvirt_obj_program_number\s*\(")),
    ("dispatch_virt_program_number(", re.compile(r"\bdispatch_virt_program_number\s*\(")),
    ("get_special_function(", re.compile(r"\bget_special_function\s*\(")),
    ("intelligent(", re.compile(r"\bintelligent\s*\(")),
)

# The closed category vocabulary every exemption below is filed under. It
# exists so the exemption CENSUS is re-derivable by import rather than by
# hand -- the re-verification's M-1 found the same census stated four
# different ways (24 / ten / eleven / 12) with no shipped figure right, in
# the very commit whose job was to repair that class. `--self-test` pins the
# vocabulary closed, and the split is
# `collections.Counter(category for _, category, _ in
# DISPATCH_TOKEN_EXEMPT_SITES.values())`.
DISPATCH_EXEMPTION_CATEGORIES = (
    "design-exclusion",     # a real dispatch this program deliberately does not guard
    "registration-write",   # the slot is WRITTEN; no actor exists at the statement
    "registration-lookup",  # the slot's value is FETCHED for a later registration write
    "presence-test",        # the slot is read as a boolean or compared; nothing is called
    "STOP",                 # a real, unguarded dispatch recorded as R4 design input
)

# The sites that carry a dispatch spelling but are NOT dispatch entry
# points, pinned as a `(file, function, token)` -> `(count, category,
# reason)` map so adding one is a review-visible SCRIPT edit rather than a
# ledger-only one (the `RESOLVER_IMPL_KEYS` precedent). Every one was
# enumerated directly from the real tree by a masked scan; none can be
# expressed as a registry row without a category error.
#
# Two of them were the original pair. The rest arrived with the Task 5-fix
# token widening above, which deliberately admits shapes that are NOT calls
# (a slot's address read as a value) precisely so a hoisted-copy evasion
# cannot hide -- the price is that every ordinary presence TEST, comparison
# and registration WRITE of the same slots now surfaces too, and each is
# dispositioned here.
#
# THE COUNT IS PART OF THE PIN (re-verification BLOCKER B-1, the
# `PINNED_CALLER_COUNTS` precedent). A key used to exempt its
# (file, function, token) triple with no line, count or shape restriction,
# so a SECOND occurrence of the same token added to an exempted function was
# invisible -- and for `affect_modify`, whose exemption covers the two REAL
# `.spell_pointer(` calls of the APPLY_SPELL arm, a third dispatch arm added
# beside them would have been absorbed silently while the mage/mystic
# `dispatch-invariant` rows' "the APPLY_SPELL door is provably blocked"
# argument rotted. `check_dispatch_entries` now compares the MEASURED
# occurrence count per key against the pinned one and fails closed on drift
# in EITHER direction: a new occurrence is the case the pin exists to catch,
# and a vanished one means the pinned key is stale and would stop catching
# the next new occurrence (the same both-directions rule
# `check_caller_counts` states). A key that matches nothing at all is a
# STALE-KEY error rather than a silent no-op (re-verification m-1: a
# pre-planted key for a function that did not exist passed, and so did
# creating that function later with a real unguarded dispatch in it).
#
# Category `STOP` marks the load-bearing exemptions rather than category
# errors -- the direct SPECIAL doors review-1's B-3 named
# (`complete_delay_impl` and `delayed_command_interpreter::run`). They really
# do dispatch a spec proc with an actor nothing on that path validated. They
# are NOT registered here because registering an entry asserts it HAS a
# guard, and this wave adds none for them (the wave's scope ends at the
# twelve entries in the ledger's registry). They are R4 design input,
# recorded in the spec's section 11 alongside shop.cpp's direct ACMD host
# calls.
DISPATCH_TOKEN_EXEMPT_SITES = {
    ("src/combat/combat_hooks.cpp", "rots::combat::set_combat_command", "g_command_table["):
        (1, "registration-write",
         "registration WRITE (`g_command_table[i] = handler;`, combat_hooks.cpp:56), not a "
         "dispatch -- no actor exists at this statement"),
    ("src/entity/entity_lifecycle.cpp", "affect_modify", ".spell_pointer("):
        (2, "design-exclusion",
         "the APPLY_SPELL arm (entity_lifecycle.cpp:2440/:2442) -- design doc section 2 "
         "EXCLUDES it deliberately: it runs at NOWHERE by design inside the login/rent-load "
         "window (producer P1), so a tripwire there would fire on every login of a character "
         "carrying an APPLY_SPELL affect. The rows reachable through it stay TODO under the "
         "`APPLY_SPELL-window` category (owner ruling R3-O-2). The count of 2 is the pin that "
         "keeps a THIRD dispatch arm added here from inheriting the exclusion"),

    # --- Task 5-fix: the three direct SPECIAL doors (M-4 direct door, R4
    # design input). Real dispatches, deliberately unregistered because this
    # wave lands no guard for them.
    ("src/app/comm.cpp", "complete_delay_impl", ".func)("):
        (1, "STOP",
         "M-4 direct door, R4 design input -- `(*mob_index[ch->nr].func)(...)` at comm.cpp:2830 "
         "dispatches a mob spec proc directly, outside every registered entry"),
    ("src/app/comm.cpp", "complete_delay_impl", "mob_index[].func"):
        (1, "STOP",
         "M-4 direct door, R4 design input -- the presence test at comm.cpp:2829 guarding the "
         "direct dispatch on the next line"),
    ("src/app/delayed_command_interpreter.cpp",
     "game_types::delayed_command_interpreter::run", "mob_index[].func"):
        (1, "STOP",
         "M-4 direct door, R4 design input -- `func_pointer = mob_index[index].func;` at "
         "delayed_command_interpreter.cpp:45, then called at :47 (the hoisted-copy shape)"),

    # --- Task 5-fix2 (M-5): the SAME two direct-door functions' OTHER arms.
    # The Task 5-fix round recorded each of them by its `mob_index[].func`
    # arm alone, but both dispatch a SPECIAL body two more ways on the
    # `store_prog_number`/`special_prog_number` fall-throughs, with the same
    # unvalidated actor. Completing the R4 STOP inventory is the point: it
    # ships as R4 design input, so a partial list is a defect in the
    # deliverable, not only in the gate.
    ("src/app/comm.cpp", "complete_delay_impl", "virt_program_number("):
        (1, "STOP",
         "M-4 direct door, R4 design input -- `tmpfunc = (SPECIAL(*))virt_program_number("
         "ch->specials.store_prog_number);` at comm.cpp:2832, called on the next line at :2833"),
    ("src/app/comm.cpp", "complete_delay_impl", "intelligent("):
        (1, "STOP",
         "M-4 direct door, R4 design input -- `intelligent(ch, 0, -1, ..., SPECIAL_DELAY, ...)` "
         "at comm.cpp:2835 calls a SPECIAL body directly on the special_prog_number arm"),
    ("src/app/delayed_command_interpreter.cpp",
     "game_types::delayed_command_interpreter::run", "get_special_function("):
        (1, "STOP",
         "M-4 direct door, R4 design input -- `func_pointer = get_special_function("
         "function_number);` at delayed_command_interpreter.cpp:50, called at :51"),
    ("src/app/delayed_command_interpreter.cpp",
     "game_types::delayed_command_interpreter::run", "intelligent("):
        (1, "STOP",
         "M-4 direct door, R4 design input -- `intelligent(m_character, victim, -1, ..., "
         "SPECIAL_DELAY, delay)` at delayed_command_interpreter.cpp:53, the same "
         "special_prog_number arm as comm.cpp:2835"),

    # --- Task 5-fix2 (M-5): registration LOOKUPs. The fetched address is
    # stored into the mob/obj index by the ASSIGN* macro on the next line;
    # nothing is dispatched and no actor participates. (`virt_assignmob`'s
    # `mob` is a prototype being assigned a spec proc at boot, not an actor.)
    ("src/script/spec_ass.cpp", "virt_assignmob", "virt_program_number("):
        (1, "registration-lookup",
         "registration LOOKUP -- `tmpptr = virt_program_number(mob->specials.store_prog_number);`"
         " (spec_ass.cpp:516), cast and handed to ASSIGNREALMOB at :518; no call is made"),
    ("src/script/spec_ass.cpp", "virt_assignobj", "virt_obj_program_number("):
        (1, "registration-lookup",
         "registration LOOKUP -- the object twin, `tmpptr = virt_obj_program_number("
         "obj->obj_flags.prog_number);` (spec_ass.cpp:530), handed to ASSIGNREALOBJ at :532"),

    # --- Task 5-fix: registration WRITEs. No actor exists at any of these
    # statements; they populate the tables the entries later dispatch through
    # (the combat_hooks.cpp:56 exemption above is the same category).
    ("src/app/interpre.cpp", "COMMANDO", "command_pointer"):
        (1, "registration-write",
         "registration WRITE -- `cmd_info[(number)].command_pointer = (pointer);`, the COMMANDO "
         "macro body (interpre.cpp:59)"),
    ("src/app/interpre.cpp", "assign_command_pointers", "command_pointer"):
        (1, "registration-write",
         "registration WRITE -- `cmd_info[position].command_pointer = 0;` (interpre.cpp:1467)"),
    ("src/combat/spell_pa.cpp", "assign_spell_pointers", "spell_pointer"):
        (69, "registration-write",
         "registration WRITE -- the 69 `skills[N].spell_pointer = spell_x;` lines that populate "
         "the spell table (spell_pa.cpp:1120-1188)"),
    ("src/interpre.h", "ASSIGNMOB", "mob_index[].func"):
        (1, "registration-write",
         "registration WRITE -- `mob_index[real_mobile(mob)].func = fname;` (interpre.h:66)"),
    ("src/interpre.h", "ASSIGNREALMOB", "mob_index[].func"):
        (1, "registration-write",
         "registration WRITE -- `mob_index[mob->nr].func = fname;` (interpre.h:72)"),
    ("src/interpre.h", "ASSIGNOBJ", "obj_index[].func"):
        (1, "registration-write",
         "registration WRITE -- `obj_index[real_object(obj)].func = fname;` (interpre.h:84)"),
    ("src/interpre.h", "ASSIGNREALOBJ", "obj_index[].func"):
        (1, "registration-write",
         "registration WRITE -- `obj_index[obj->item_number].func = fname;` (interpre.h:78)"),
    ("src/world/db_world.cpp", "load_mobiles", "mob_index[].func"):
        (1, "registration-write",
         "registration WRITE -- `mob_index[i].func = 0;`, the boot-time table clear "
         "(db_world.cpp:1209)"),
    ("src/world/db_world.cpp", "load_objects", "obj_index[].func"):
        (1, "registration-write",
         "registration WRITE -- `obj_index[i].func = 0;`, the boot-time table clear "
         "(db_world.cpp:1426)"),

    # --- Task 5-fix: presence TESTS and comparisons. The slot is read as a
    # boolean or compared against a known function; no call is made and no
    # copy escapes the expression.
    ("src/app/act_info.cpp", "show_char_to_char", "mob_index[].func"):
        (1, "presence-test",
         "presence TEST -- `!mob_index[i->nr].func && MOB_FLAGGED(...)` (act_info.cpp:993)"),
    ("src/app/act_info.cpp", "sort_commands", "command_pointer"):
        (2, "presence-test",
         "presence TEST + comparison -- the table-length loop condition (act_info.cpp:3181) and "
         "`... .command_pointer == do_action` (:3183); neither calls anything"),
    ("src/app/act_othe.cpp", "do_block", "mob_index[].func"):
        (1, "presence-test",
         "presence TEST -- `IS_NPC(ch) && mob_index[ch->nr].func` (act_othe.cpp:1934)"),
    ("src/app/act_wiz.cpp", "do_stat_character", "mob_index[].func"):
        (1, "presence-test",
         "presence TEST -- the `stat`-output ternary that prints whether a spec proc exists "
         "(act_wiz.cpp:976)"),
    ("src/app/act_wiz.cpp", "do_stat_object", "obj_index[].func"):
        (1, "presence-test",
         "presence TEST -- the same ternary for objects (act_wiz.cpp:587)"),
    ("src/app/objsave.cpp", "gen_receptionist", "mob_index[].func"):
        (1, "presence-test",
         "comparison -- `mob_index[tch->nr].func == receptionist` (objsave.cpp:1307)"),
    ("src/combat/fight.cpp", "exp_with_modifiers", "mob_index[].func"):
        (1, "presence-test",
         "presence TEST -- `mob_index[dead_man->nr].func || dead_man->special_prog_number` "
         "(fight.cpp:1227)"),
    ("src/combat/spell_pa.cpp", "<anon>::can_cast_spell", "spell_pointer"):
        (1, "presence-test",
         "presence TEST -- `!spell.spell_pointer` rejects an unimplemented spell "
         "(spell_pa.cpp:396)"),
    ("src/combat/spell_pa.cpp", "do_prepare", "spell_pointer"):
        (1, "presence-test",
         "presence TEST -- `!skills[spl].spell_pointer` (spell_pa.cpp:1060)"),
    ("src/entity/entity_lifecycle.cpp", "affect_modify", "spell_pointer"):
        (1, "presence-test",
         "presence TEST -- `!skills[tmp].spell_pointer` at entity_lifecycle.cpp:2436, the guard "
         "for the APPLY_SPELL arm already exempted above"),
    ("src/script/spec_ass.cpp", "virt_assignmob", "mob_index[].func"):
        (1, "presence-test",
         "presence TEST -- refuses to overwrite an already-assigned mob spec (spec_ass.cpp:512)"),
    ("src/script/spec_ass.cpp", "virt_assignobj", "obj_index[].func"):
        (1, "presence-test",
         "presence TEST -- the same refusal for objects (spec_ass.cpp:527)"),
}


# ---------------------------------------------------------------------------
# RR Wave R3 Task 1a: the caller-count pins (owner ruling R3-O-3).
#
# `CAN_SEE(` and `get_char_room_vis(` are the two scale-flagged rows R3 does
# NOT drain: both stay TODO, and both are id-TAKING in the sense the ledger's
# "caller-contract token-pinning policy" blind spot describes -- their safety
# claim would rest on "every caller is accounted for." The policy's own
# remedy is to pin the name as a SCANNED TOKEN so a new caller surfaces as
# its own site; here that remedy is unaffordable, because these two names
# have 86 + 41 = 127 production call sites and pinning them as
# `token_patterns()` entries would mint ~127 new ledger rows and raise
# `MAXIMUM_TODO_COUNT` by the same amount, for zero proof value.
#
# R3-O-3's deliberate deviation: pin the COUNTS instead. A new caller of
# either name changes the measured count and fails `--check` with a message
# naming the two-edit fix -- so the scale-flagged rows cannot silently grow
# a caller while they wait for TASK-005, and the ledger's stated policy is
# satisfied without the row explosion.
#
# MEASUREMENT METHOD (re-run this exact method before ever changing a
# literal here -- these are NOT hand counts):
#   * scan every `SOURCE_SUFFIXES` file under `<root>/src`, EXCLUDING
#     `src/tests/` (production only -- both names have zero occurrences in
#     `src/tests/` today, so the exclusion is scope discipline, not a
#     concession);
#   * mask comments and string literals with this module's own
#     `mask_comments_and_string_literals`;
#   * attribute every line with this module's own `attribute_lines`;
#   * count every regex occurrence (per occurrence, not per line) on a line
#     whose attribution is NOT `file-scope`.
# The file-scope exclusion is what drops the declarations and definition
# heads -- `utils.h:669`/`:670` and `visibility.cpp:107`/`:527` for
# `CAN_SEE`, `handler.h:509` and `visibility.cpp:617` for
# `get_char_room_vis` -- while KEEPING real calls that live inside a macro
# body (`spec_pro.cpp:2345` is one such `CAN_SEE(` call, attributed
# `macro`; counting only `function` lines would have silently lost it).
# Measured at this commit: `CAN_SEE(` 86 (85 function + 1 macro),
# `get_char_room_vis(` 41 (41 function + 0 macro).
PINNED_CALLER_COUNTS = {
    "CAN_SEE(": 86,
    "get_char_room_vis(": 41,
}
PINNED_CALLER_PATTERNS = {
    "CAN_SEE(": re.compile(r"\bCAN_SEE\s*\("),
    "get_char_room_vis(": re.compile(r"\bget_char_room_vis\s*\("),
}


def _strip_fences(text):
    """Drop fenced code blocks -- documentation, never data (M10 precedent).

    Copied verbatim from tools/location_read_census.py:563-573."""
    live_lines = []
    in_fence = False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            live_lines.append(line)
    return live_lines


def _split_row_cells(stripped_line):
    """Split a `| a | b | c |` table row into unescaped cell strings.

    Splits on '|' NOT preceded by a backslash, so an escaped pipe (`\\|`,
    the ledger's own Proof-text escaping contract) stays inside its cell
    instead of prematurely ending it. The leading/trailing empty strings
    from the row's own bounding pipes are dropped."""
    raw_cells = re.split(r"(?<!\\)\|", stripped_line)[1:-1]
    return [cell.strip().replace("\\|", "|") for cell in raw_cells]


def _parse_marked_table(live_lines, marker, header, minimum_rows, label):
    """Parse the cell rows of the single marker-anchored table. Fails closed
    on: the marker missing or duplicated, a wrong header line, or a row
    count below the floor. Copied structure from location_read_census.py's
    own `_parse_marked_table` (marker-then-header-then-rows state machine)."""
    marker_count = sum(1 for line in live_lines if line.strip() == marker)
    if marker_count != 1:
        raise SystemExit(
            f"error: ledger: {label} marker {marker!r} must appear exactly once "
            f"outside any fenced code block (found {marker_count})."
        )
    rows = []
    state = "before-marker"
    for line in live_lines:
        stripped = line.strip()
        if state == "before-marker":
            if stripped == marker:
                state = "awaiting-header"
            continue
        if state == "awaiting-header":
            if not stripped:
                continue
            if stripped != header:
                raise SystemExit(
                    f"error: ledger: the line after {marker!r} must be {header!r}, "
                    f"got {stripped!r}."
                )
            state = "in-table"
            continue
        if stripped.startswith("|") and set(stripped) <= set("|- :"):
            continue  # the header's own separator row
        if not stripped.startswith("|"):
            break  # table ended
        rows.append(_split_row_cells(stripped))
    if len(rows) < minimum_rows:
        raise SystemExit(
            f"error: ledger: {label} has {len(rows)} row(s), below the floor of "
            f"{minimum_rows} -- a truncated ledger must not pass."
        )
    return rows


def _parse_classification_row(cells):
    """One `| Key | Count | Class | Kind | Proof |` row -> a row dict, or a
    fail-closed SystemExit (malformed key, non-positive count, unknown
    class/kind, proof-shape violations, TEST-FIXTURE path illegality, a
    non-'#'-prefixed DECL key, a RESOLVER-IMPL/pinned-DECL key outside its
    closed set)."""
    if len(cells) != 5:
        raise SystemExit(
            f"error: ledger: classification row has {len(cells)} cell(s), expected 5: {cells!r}"
        )
    key_cell, count_cell, cls, kind, proof = cells

    key_match = re.fullmatch(r"`([^`]*)`", key_cell)
    if key_match is None:
        raise SystemExit(f"error: ledger: malformed key cell (must be backtick-wrapped): {key_cell!r}")
    key_text = key_match.group(1)
    parts = key_text.split(KEY_SEPARATOR)
    if len(parts) != 3:
        raise SystemExit(
            f"error: ledger: malformed key {key_text!r} -- expected exactly three "
            f"{KEY_SEPARATOR!r}-separated parts (file, function, token)"
        )
    key_file, key_function, key_token = parts

    try:
        count = int(count_cell)
    except ValueError:
        raise SystemExit(f"error: ledger: non-integer count {count_cell!r} for key {key_text!r}")

    # review-1 W-2: a zero or negative Count can mask real sites while
    # keeping a per-key SUM exact against some OTHER row -- demonstrated
    # live: a -500 TODO row paired with a +500 DECL row leaves every
    # cross-check reconcile() runs untouched while collapsing the TODO
    # ceiling's real total. A ledger row always describes one-or-more real
    # scanned occurrences; it can never legitimately claim zero or fewer.
    if count < 1:
        raise SystemExit(
            f"error: ledger: row count {count} for key {key_text!r} must be >= 1 -- a "
            "zero or negative count can mask real sites while leaving a per-key sum "
            "exact (review-1 W-2)"
        )

    if cls not in CLASSES:
        raise SystemExit(f"error: ledger: unknown class {cls!r} for key {key_text!r}")

    # review-1 W-1(a): a DECL row must key on a '#'-prefixed macro-body or
    # file-scope-decl name -- never an ordinary function name, which can
    # never start with '#' in real C++. Verified 0 violations in the real
    # ledger at this commit; this closes the class off from ever silently
    # keying a DECL row onto a real function going forward.
    if cls == "DECL" and not key_function.startswith("#"):
        raise SystemExit(
            f"error: ledger: DECL row for key {key_text!r} has key_function "
            f"{key_function!r}, which does not start with '#' -- a DECL row must key "
            "on a macro-body ('#NAME') or file-scope-decl ('#decl') name, never an "
            "ordinary function name (review-1 W-1(a))"
        )

    # review-1 W-1(b): RESOLVER-IMPL is pinned by (file, function) -- see
    # RESOLVER_IMPL_KEYS above for the full rationale and enumeration.
    if cls == "RESOLVER-IMPL" and (key_file, key_function) not in RESOLVER_IMPL_KEYS:
        raise SystemExit(
            f"error: ledger: RESOLVER-IMPL row for key {key_text!r} is outside the "
            "pinned RESOLVER_IMPL_KEYS set -- extending the resolver-impl set is a "
            "review-visible script edit (review-1 W-1(b)), not a ledger-only one"
        )

    # review-1 W-1(c): a '.cpp' file's "#decl" (unattributable, file-scope)
    # key is pinned closed -- see PINNED_DECL_KEYS above. A header's "#decl"
    # key and any "#NAME" macro-body key (header or .cpp) stay legal and
    # open, since both are re-derivable from a real, named construct.
    if cls == "DECL" and key_function == "#decl" and key_file.endswith(".cpp"):
        if (key_file, key_token) not in PINNED_DECL_KEYS:
            raise SystemExit(
                f"error: ledger: new '.cpp' '#decl' key {key_text!r} is outside the "
                "pinned PINNED_DECL_KEYS set -- an unattributable production .cpp site "
                "must not auto-launder into DECL (review-1 W-1(c), restoring spec "
                "section 4's fail-closed rule); review and either fix the scanner's "
                "attribution or give the site a real proof row"
            )

    # review-1 F-1: PROVEN/GUARDED rows must carry BOTH a real Kind and a
    # real Proof -- the empty marker on EITHER previously bypassed every
    # shape check below wholesale (kind == LEDGER_EMPTY_MARKER short-
    # circuited the whole `if kind != LEDGER_EMPTY_MARKER:` block).
    # Demonstrated live: a real TODO row hand-edited to `| PROVEN | — | — |`
    # passed --check unchanged.
    if cls in ("PROVEN", "GUARDED"):
        if kind == LEDGER_EMPTY_MARKER:
            raise SystemExit(
                f"error: ledger: {cls} row for key {key_text!r} must carry a non-empty "
                f"Kind -- the empty marker {LEDGER_EMPTY_MARKER!r} bypasses every "
                "proof-shape check (review-1 F-1)"
            )
        if proof == LEDGER_EMPTY_MARKER:
            raise SystemExit(
                f"error: ledger: {cls} row for key {key_text!r} must carry a non-empty "
                f"Proof -- the empty marker {LEDGER_EMPTY_MARKER!r} bypasses every "
                "proof-shape check (review-1 F-1)"
            )

    # PROVEN's Kind is the closed PROOF_KINDS vocabulary; GUARDED's Kind is
    # free text citing its wave/test (spec section 3) and is NOT held to
    # that vocabulary -- every other class's Kind, if non-empty, still is
    # (unchanged from the original design: only GUARDED gets the carve-out).
    if kind != LEDGER_EMPTY_MARKER and kind not in PROOF_KINDS and cls != "GUARDED":
        raise SystemExit(f"error: ledger: unknown proof kind {kind!r} for key {key_text!r}")

    if kind != LEDGER_EMPTY_MARKER:
        if len(proof) < MINIMUM_PROOF_TEXT_LENGTH:
            raise SystemExit(
                f"error: ledger: proof text too short ({len(proof)} < "
                f"{MINIMUM_PROOF_TEXT_LENGTH} characters) for key {key_text!r}: {proof!r}"
            )
        if kind in CITATION_REQUIRED_KINDS and not CITATION_RE.search(proof):
            raise SystemExit(
                f"error: ledger: kind {kind!r} requires a file:line citation in the proof "
                f"text for key {key_text!r}: {proof!r}"
            )

    if cls == "TEST-FIXTURE" and not (key_file == "src/tests" or key_file.startswith("src/tests/")):
        raise SystemExit(
            f"error: ledger: TEST-FIXTURE row for {key_file!r} is outside src/tests/ "
            f"(key {key_text!r})"
        )

    return {"key_file": key_file, "key_function": key_function, "key_token": key_token,
            "count": count, "cls": cls, "kind": kind, "proof": proof}


def _parse_token_count_row(cells):
    """One `| Token | Sites |` row -> (token_name, count), or SystemExit."""
    if len(cells) != 2:
        raise SystemExit(
            f"error: ledger: token-counts row has {len(cells)} cell(s), expected 2: {cells!r}"
        )
    token_cell, count_cell = cells
    token_match = re.fullmatch(r"`([^`]*)`", token_cell)
    if token_match is None:
        raise SystemExit(f"error: ledger: malformed token cell (must be backtick-wrapped): {token_cell!r}")
    token_name = token_match.group(1)
    valid_tokens = {name for name, _ in token_patterns()}
    if token_name not in valid_tokens:
        raise SystemExit(f"error: ledger: unknown token {token_name!r} in token-counts table")
    try:
        count = int(count_cell)
    except ValueError:
        raise SystemExit(f"error: ledger: non-integer sites count {count_cell!r} for token {token_name!r}")
    return token_name, count


_UNSET = object()  # sentinel: "no override was passed" (see reconcile()/parse_ledger())


def parse_ledger(text, *, minimum_rows=_UNSET):
    """Parse the ledger's two marker-anchored tables.

    Returns (rows, token_counts): `rows` is a list of dicts with keys
    key_file/key_function/key_token/count/cls/kind/proof; `token_counts` maps
    token name -> declared site count. Fails closed (SystemExit) on any
    marker/header/row-shape/vocabulary/proof-shape violation -- see the
    per-row parsers above for the exact conditions.

    `minimum_rows` (the classification table's row floor) defaults to the
    LIVE module-level `MINIMUM_LEDGER_ROW_COUNT`, resolved at CALL time via
    the same `_UNSET`-sentinel, late-bound-default idiom `reconcile()`'s
    `todo_ceiling` parameter uses below (the review-1 IMPORTANT fix for
    exactly this early-bound-default footgun) -- a plain
    `minimum_rows=MINIMUM_LEDGER_ROW_COUNT` default would freeze whatever the
    constant held at `def` time. Ordinary callers (`main()`'s real `--check`
    path) never pass this kwarg and transparently see the real, seeded
    floor (451, Task 3); `--self-test`'s own tiny synthetic ledgers (4 rows)
    pass a small override (`--minimum-ledger-rows-override`, gated the same
    way as `--todo-ceiling-override`/`--minimum-files-override`) so the real
    floor's scale never swamps the hermetic fixtures."""
    if minimum_rows is _UNSET:
        minimum_rows = MINIMUM_LEDGER_ROW_COUNT
    live_lines = _strip_fences(text)
    classification_cells = _parse_marked_table(
        live_lines, LEDGER_CLASSIFICATION_MARKER, LEDGER_CLASSIFICATION_HEADER,
        minimum_rows, "classification table")
    token_count_cells = _parse_marked_table(
        live_lines, LEDGER_TOKEN_COUNTS_MARKER, LEDGER_TOKEN_COUNTS_HEADER,
        0, "token-counts table")
    rows = [_parse_classification_row(cells) for cells in classification_cells]
    token_counts = dict(_parse_token_count_row(cells) for cells in token_count_cells)
    return rows, token_counts


def _normalize_whitespace(text):
    """Collapse every run of whitespace to a single space, and strip.

    Guard-literal containment is checked "verbatim up to whitespace
    normalization": a registry row's Guard literal cell cannot carry a
    newline (it lives in a markdown table cell), while the real guards it
    pins routinely wrap across two or three physical lines and carry
    file-specific indentation. Normalizing both sides is what lets ONE
    single-line literal in the ledger match the real, wrapped statement
    without the row degenerating into a substring so short it would match
    almost anything."""
    return " ".join(text.split())


def _parse_dispatch_entry_row(cells):
    """One `| Entry point | Actor parameter | Guard literal | Status |` row ->
    a dict, or a fail-closed SystemExit."""
    if len(cells) != 4:
        raise SystemExit(
            f"error: ledger: dispatch-entry row has {len(cells)} cell(s), expected 4: {cells!r}"
        )
    entry_cell, actor_cell, guard_cell, status = cells

    entry_match = re.fullmatch(r"`([^`]*)`", entry_cell)
    if entry_match is None:
        raise SystemExit(
            f"error: ledger: malformed dispatch-entry cell (must be backtick-wrapped): "
            f"{entry_cell!r}"
        )
    entry_text = entry_match.group(1)
    parts = entry_text.split(KEY_SEPARATOR)
    if len(parts) != 2:
        raise SystemExit(
            f"error: ledger: malformed dispatch entry {entry_text!r} -- expected exactly two "
            f"{KEY_SEPARATOR!r}-separated parts (file, function)"
        )
    entry_file, entry_function = parts

    if status not in DISPATCH_ENTRY_STATUSES:
        raise SystemExit(
            f"error: ledger: unknown dispatch-entry status {status!r} for {entry_text!r} -- "
            f"the closed vocabulary is {list(DISPATCH_ENTRY_STATUSES)}"
        )

    actor_match = re.fullmatch(r"`([^`]*)`", actor_cell)
    if actor_match is None or not actor_match.group(1).strip():
        raise SystemExit(
            f"error: ledger: dispatch entry {entry_text!r} must name its actor parameter as a "
            f"backtick-wrapped, non-empty cell (got {actor_cell!r}) -- an actor-inherited "
            "proof that does not say WHICH argument it covers is unreviewable (design doc "
            "section 2, mandatory citation part (ii))"
        )
    actor = actor_match.group(1).strip()

    # Every surviving status requires a guard literal (the PENDING-T1b
    # exemption was retired at the Task 5-fix round -- see
    # DISPATCH_ENTRY_STATUSES), so there is no `else` arm any more: a row that
    # leaves the cell at the empty marker fails the grammar below, which is
    # exactly the "registered but unguarded" shape both reviews demonstrated.
    if re.fullmatch(DISPATCH_GUARD_CELL_PATTERN, guard_cell) is None:
        raise SystemExit(
            f"error: ledger: {status} dispatch entry {entry_text!r} must carry one or more "
            f"backtick-wrapped guard literals, separated by {DISPATCH_GUARD_SEPARATOR!r} "
            f"(got {guard_cell!r})"
        )
    guards = re.findall(r"`([^`]*)`", guard_cell)
    for literal in guards:
        if not literal.strip():
            raise SystemExit(
                f"error: ledger: {status} dispatch entry {entry_text!r} carries an EMPTY "
                f"guard literal in {guard_cell!r} -- every listed literal must be a real, "
                "non-empty statement the downward check can look for"
            )

    return {"file": entry_file, "function": entry_function, "actor": actor,
            "guards": guards, "status": status}


def parse_dispatch_entries(text, *, minimum_rows=_UNSET):
    """Parse the ledger's marker-anchored dispatch-entry registry.

    Deliberately NOT folded into `parse_ledger`: the location census's own
    registry (`parse_registry`) is likewise a separate entry point called by
    `main()`, and keeping it separate means the ~20 existing hermetic
    `parse_ledger` fixtures in `run_self_test` stay judged on the violation
    each one actually targets instead of incidentally tripping this table's
    absence.

    `minimum_rows` is late-bound through the same `_UNSET` sentinel
    `parse_ledger`/`reconcile` use, so a monkeypatched or self-test-overridden
    floor is honored at CALL time rather than frozen at `def` time."""
    if minimum_rows is _UNSET:
        minimum_rows = MINIMUM_DISPATCH_ENTRY_ROWS
    live_lines = _strip_fences(text)
    cells = _parse_marked_table(
        live_lines, LEDGER_DISPATCH_ENTRIES_MARKER, LEDGER_DISPATCH_ENTRIES_HEADER,
        minimum_rows, "dispatch-entry registry")
    return [_parse_dispatch_entry_row(row_cells) for row_cells in cells]


def check_dispatch_entries(entries, dispatch_occurrences, repository_root,
                           exempt_sites=_UNSET):
    """The registry's two closed-world directions. Returns (errors, warnings).

    Direction (a), DOWNWARD: every entry names a real file and a real,
    scanner-findable function whose MASKED body still contains EVERY guard
    literal that entry lists (whitespace-normalized). No status is exempt --
    the PENDING-T1b exemption was retired at the Task 5-fix round.

    Direction (b), UPWARD: every production occurrence of a
    DISPATCH_SPELLING_TOKENS spelling (already filtered to non-file-scope
    lines by the scanner) lies inside a registered entry's function, or is
    one of the pinned DISPATCH_TOKEN_EXEMPT_SITES -- and there, the pinned
    OCCURRENCE COUNT must match exactly (re-verification B-1/m-1). Drift in
    either direction is an error, and so is a key that matches nothing.

    `exempt_sites` is late-bound through the `_UNSET` sentinel, like every
    other overridable pin here, so `--dispatch-exempt-sites-override` (and a
    monkeypatched module global) is honored at CALL time."""
    if exempt_sites is _UNSET:
        exempt_sites = DISPATCH_TOKEN_EXEMPT_SITES
    errors = []
    # Retained as the second return value even though the PENDING-T1b
    # retirement (Task 5-fix) left it always empty: it is the shape `main()`
    # prints through, and a future advisory belongs here rather than in the
    # error list. An empty warnings list is the correct state today.
    warnings = []

    seen = set()
    registered = set()
    for entry in entries:
        key = (entry["file"], entry["function"])
        if key in seen:
            errors.append(
                f"dispatch-entry registry: duplicate entry {KEY_SEPARATOR.join(key)} -- one row "
                "per entry point, so a second row cannot quietly relax the first's status"
            )
        seen.add(key)
        registered.add(key)

        source_path = repository_root / entry["file"]
        if not source_path.is_file():
            errors.append(
                f"dispatch-entry registry: {KEY_SEPARATOR.join(key)} names a file that does not "
                f"exist under {repository_root}"
            )
            continue
        masked = strip_inactive_preprocessor_blocks(mask_comments_and_string_literals(
            source_path.read_text(encoding="utf-8", errors="replace"))).splitlines()
        attributions = attribute_lines(masked)
        body_lines = [line for line, attribution in zip(masked, attributions)
                      if attribution == ("function", entry["function"])]
        if not body_lines:
            errors.append(
                f"dispatch-entry registry: {KEY_SEPARATOR.join(key)} names a function the "
                "scanner cannot find in that file (renamed, moved, or mis-spelled)"
            )
            continue
        # EVERY listed literal must still be there. An entry with two
        # tripwires (Task 1d's adjacency rule, R3-C-7) is only as strong as
        # its weakest one: deleting the pre-dispatch guard while leaving the
        # entry guard behind would restore exactly the hole adjacency exists
        # to close, so a partial match is a gate failure, not a pass.
        masked_body = _normalize_whitespace("\n".join(body_lines))
        for literal in entry["guards"]:
            if _normalize_whitespace(literal) not in masked_body:
                errors.append(
                    f"dispatch-entry registry: {KEY_SEPARATOR.join(key)} is {entry['status']}, "
                    f"but its guard literal {literal!r} no longer appears in that function's "
                    "(comment/string-masked) body -- either the guard was removed/rewritten in "
                    "production, or the registry row is stale. Every `dispatch-invariant` row "
                    "citing this entry rests on that literal."
                )

    measured_exempt = {}
    for occurrence in dispatch_occurrences:
        key = (occurrence["file"], occurrence["function"])
        if key in registered:
            continue
        exempt_key = (occurrence["file"], occurrence["function"], occurrence["token"])
        if exempt_key in exempt_sites:
            measured_exempt[exempt_key] = measured_exempt.get(exempt_key, 0) + 1
            continue
        errors.append(
            f"unregistered dispatch site {occurrence['file']}:{occurrence['line']} -- token "
            f"`{occurrence['token']}` inside `{occurrence['function']}`, which is not a "
            "registered dispatch entry and is not in DISPATCH_TOKEN_EXEMPT_SITES. A new "
            "dispatcher cannot inherit the `dispatch-invariant` proof: either register it (with "
            "a guard) in the ledger's dispatch-entry registry, or pin it as a reviewed "
            "exemption in the script."
        )

    # The exemption COUNT pins (re-verification B-1) and the stale-key check
    # (m-1). An exemption is a statement about a KNOWN set of occurrences, so
    # both directions of drift are errors: one more occurrence is a new,
    # unreviewed site hiding behind a reviewed one, and one fewer means the
    # key no longer describes the tree and would absorb the next new site
    # silently.
    for exempt_key in sorted(exempt_sites):
        pinned = exempt_sites[exempt_key][0]
        actual = measured_exempt.get(exempt_key, 0)
        if actual == pinned:
            continue
        location = KEY_SEPARATOR.join(exempt_key)
        if actual == 0:
            errors.append(
                f"STALE dispatch-token exemption `{location}` -- DISPATCH_TOKEN_EXEMPT_SITES "
                f"pins {pinned} occurrence(s), the production scan found NONE. The exempted "
                "function, token or file was renamed, moved or deleted: an exemption that "
                "matches nothing silently pre-authorizes whatever is written there next. "
                "Delete the key, or re-point it at the site it is meant to cover."
            )
            continue
        errors.append(
            f"dispatch-token exemption count `{location}`: DISPATCH_TOKEN_EXEMPT_SITES pins "
            f"{pinned}, the production scan found {actual}. An exemption covers a REVIEWED set "
            "of occurrences, not the (file, function, token) triple forever: a new occurrence "
            "must be read and dispositioned on its own merits (it may be a real dispatch -- "
            "then register it with a guard), and a vanished one must be un-pinned so the key "
            "stops standing in for a site that no longer exists."
        )

    return errors, warnings


def check_pinned_caller_macro_aliases(source_texts, pinned_counts):
    """Refuse a `#define` that ALIASES a pinned caller name. Error list.

    RR Wave R3 Task 5-fix (review-2 m-5). `PINNED_CALLER_COUNTS` pins the
    number of times a literal spelling appears, so `#define RR_CS CAN_SEE`
    followed by `RR_CS(a, b)` adds a caller the pin cannot see: the alias
    definition mentions `CAN_SEE` once (raising nothing, because the
    definition is not a call) and the call site spells `RR_CS`. Reviewer
    demonstrated `--check` exit 0 on exactly that.

    The rule is narrow on purpose: a `#define` body containing a pinned name
    NOT followed by `(` is an alias (`#define RR_CS CAN_SEE`), while a body
    that CALLS the name (`#define VIS(a, b) CAN_SEE(a, b)`) is an ordinary
    macro whose call the scanner already counts on its own line, under its
    `macro` attribution -- which is exactly why the measurement method keeps
    macro-body lines. Measured at this commit: zero `#define` bodies
    anywhere under `src/` mention either pinned name at all."""
    errors = []
    for path_text in sorted(source_texts):
        for macro_name, body in sorted(collect_define_bodies(source_texts[path_text]).items()):
            for pinned in sorted(pinned_counts):
                bare = pinned.rstrip("(")
                if re.search(r"\b" + re.escape(bare) + r"\b(?!\s*\()", body):
                    errors.append(
                        f"pinned-caller alias macro `{macro_name}` (in {path_text}) -- its body "
                        f"names `{bare}` without calling it, which makes every use of "
                        f"`{macro_name}` a caller PINNED_CALLER_COUNTS cannot count (owner "
                        "ruling R3-O-3's pin is a literal-spelling count). Call the name "
                        "directly, or retire the pin in favour of a real ledger row."
                    )
    return errors


def check_caller_counts(measured_counts, pinned_counts):
    """The R3-O-3 caller-count pins. Returns a list of error strings.

    Drift in EITHER direction is an error: a new caller (count up) is the
    case the pin exists to catch, and a removed caller (count down) means the
    pinned literal is now stale and would stop catching the next new one."""
    errors = []
    for token_name in sorted(pinned_counts):
        expected = pinned_counts[token_name]
        actual = measured_counts.get(token_name, 0)
        if actual == expected:
            continue
        errors.append(
            f"caller-count pin `{token_name}`: PINNED_CALLER_COUNTS says {expected}, the "
            f"production scan found {actual} (owner ruling R3-O-3). These two names stay TODO "
            "under the scale-flagged category, so a caller change must be seen, not absorbed. "
            "The fix is TWO edits made together: update PINNED_CALLER_COUNTS in "
            "tools/room_resolve_census.py, AND the measured figure in the ledger's "
            '"Caller-count pins (R3-O-3)" section.'
        )
    return errors


def reconcile(sites, rows, token_counts, *, todo_ceiling=_UNSET):
    """Cross-check scanned sites against ledger rows and the token-counts
    table (spec section 3). Returns a list of human-readable error strings;
    an empty list means the tree and the ledger agree completely.

    Every scanned key must have ledger row(s) summing EXACTLY to its scanned
    count (unclassified-site / count-mismatch, both directions -- the
    anti-inheritance property, spec section 3/O-5: a stale higher OR a stale
    lower ledger count are both errors, so a site can never silently inherit
    an unrelated count). Every ledger row's key must exist among the scanned
    sites (stale-row). Per-token totals recomputed from the scan must match
    the declared token-counts table (O-10). TODO rows are ratcheted against
    `todo_ceiling`, which defaults to the LIVE module-level MAXIMUM_TODO_COUNT
    -- resolved at CALL time, inside this function body, not baked in as an
    ordinary keyword default -- so ordinary callers matching the documented
    `reconcile(sites, rows, token_counts)` three-positional-argument interface
    transparently see whatever MAXIMUM_TODO_COUNT the module currently holds,
    including a monkeypatched or Task-3-raised value, without needing to know
    the self-test-only `--todo-ceiling-override` mechanism exists (main()
    supplies that override here explicitly when present).

    A plain `todo_ceiling=MAXIMUM_TODO_COUNT` default would be WRONG here:
    Python evaluates a default argument expression once, at `def` time, so it
    would freeze whatever MAXIMUM_TODO_COUNT held at import time and silently
    ignore any later change to the module constant -- a footgun for exactly
    the callers this default exists to serve. The `_UNSET` sentinel plus an
    explicit `if todo_ceiling is _UNSET: todo_ceiling = MAXIMUM_TODO_COUNT`
    below re-reads the module global on every call instead.
    """
    if todo_ceiling is _UNSET:
        todo_ceiling = MAXIMUM_TODO_COUNT

    errors = []

    # Macro attributions key as "#NAME"; file-scope (declarations, and any
    # other line the scanner could not attribute to a real function or
    # macro body) key as "#decl" -- neither spelling can collide with a real
    # C++ function name, which can never start with '#'.
    scanned = {}
    for site in sites:
        kind, name = site["attribution"], site["function"]
        if kind == "macro":
            function_key = f"#{name}"
        elif kind == "file-scope":
            function_key = "#decl"
        else:
            function_key = name
        key = (site["file"], function_key, site["token"])
        scanned[key] = scanned.get(key, 0) + 1

    ledger_totals = {}
    row_keys = set()
    for row in rows:
        key = (row["key_file"], row["key_function"], row["key_token"])
        row_keys.add(key)
        ledger_totals[key] = ledger_totals.get(key, 0) + row["count"]

    for key, scanned_count in scanned.items():
        display = KEY_SEPARATOR.join(key)
        ledger_count = ledger_totals.get(key)
        if ledger_count is None:
            errors.append(f"{display}: unclassified site (no ledger row for this key)")
        elif ledger_count != scanned_count:
            errors.append(
                f"{display}: ledger row(s) total {ledger_count}, scan found "
                f"{scanned_count} site(s) (count mismatch)"
            )

    for key in sorted(row_keys - set(scanned)):
        display = KEY_SEPARATOR.join(key)
        errors.append(f"{display}: stale ledger row (no matching scanned site)")

    scanned_token_totals = {}
    for site in sites:
        scanned_token_totals[site["token"]] = scanned_token_totals.get(site["token"], 0) + 1
    all_token_names = {name for name, _ in token_patterns()} | set(token_counts)
    for token_name in sorted(all_token_names):
        expected = token_counts.get(token_name, 0)
        actual = scanned_token_totals.get(token_name, 0)
        if expected != actual:
            errors.append(
                f"token `{token_name}`: token-counts table says {expected}, scan found {actual}"
            )

    if todo_ceiling is not None:
        todo_total = sum(row["count"] for row in rows if row["cls"] == "TODO")
        if todo_total > todo_ceiling:
            errors.append(
                f"TODO total {todo_total} exceeds the ceiling of {todo_ceiling} "
                "(MAXIMUM_TODO_COUNT or --todo-ceiling-override)"
            )

    return errors


def _resolve_scan_targets(paths, repository_root):
    """Every eligible file under the given search paths (repository_root /
    'src' when none given), resolving both the search path and comparing
    against an already-resolved repository_root -- mirrors
    tools/location_read_census.py's source_files() resolve-both-sides
    discipline (O-I7/F-14), needed so a symlinked --root (macOS's /tmp ->
    /private/tmp, or a deliberately symlinked probe directory) never makes
    scan_file()'s relative_to(repository_root) raise."""
    search_paths = paths or [repository_root / "src"]
    discovered = set()
    for search_path in search_paths:
        resolved = (search_path if search_path.is_absolute()
                    else repository_root / search_path).resolve()
        if resolved.is_file():
            if resolved.suffix in SOURCE_SUFFIXES:
                discovered.add(resolved)
            continue
        discovered.update(source_files(resolved))
    return sorted(discovered)


def dev_selftest():
    """Task 1's own TDD harness (temporary): proves derive_macro_family and
    attribute_lines against small inline fixtures before Task 2 builds the
    ledger gate on top. Superseded by a real --self-test in a later task."""
    failures = []
    # Macro-family derivation: direct, transitive, and world[-reaching macros.
    # "graph.cpp" is a .cpp-local-macro case (real-tree precedent: VALID_EDGE,
    # src/pathfind/graph.cpp:70) -- the dict key is just a name string, so a
    # non-header-suffixed key exercises the WHOLE-TREE scope decision (ruled
    # in commit 61332054: --derive-macros sweeps headers AND .cpp files, not
    # headers alone) with a standing assertion, not only the real-tree sweep.
    headers = {
        "a.h": "#define OUTSIDE(ch) (!IS_SET(room_of((ch))->room_flags, INDOORS))\n"
               "#define IS_LIGHT(room) (!IS_DARK(room))\n"
               "#define IS_DARK(room) (room_by_id_total(room)->light < 1)\n",
        "b.h": "#define ASSIGNROOM(room, fname) \\\n"
               "    do { world[real_room(room)].funct = fname; } while (0)\n"
               "#define UNRELATED(x) ((x) + 1)\n",
        "graph.cpp": "#define VALID_EDGE(x, y) \\\n"
                     "    ((x)->dir_option[(y)] && !IS_MARKED(room_by_id_total(TOROOM(x, y))))\n",
    }
    derived = derive_macro_family(headers)
    if derived != {"OUTSIDE", "IS_LIGHT", "IS_DARK", "ASSIGNROOM", "VALID_EDGE"}:
        failures.append(f"derive_macro_family: got {sorted(derived)}")

    # Attribution: plain function, ACMD header, class-qualified method,
    # namespace, macro body, file-scope decl, file-scope array (not a
    # function), fail-closed unattributable is Task 2's gate concern --
    # here attribute_lines must label every line. Line indices below are
    # derived by counting text.splitlines() for THIS exact fixture (see
    # task-1-report.md); they are not the brief's illustrative numbers.
    text = (
        "int r_x = 0;\n"
        "struct room_data* room_of(const struct char_data* ch);\n"   # decl: file-scope
        "int table[] = {\n    1, 2,\n};\n"                            # array: file-scope
        "#define EXIT(ch, door) (room_of((ch))->dir_option[door])\n"  # macro
        "namespace rots {\n"
        "static int helper(int a)\n{\n    return room_of(x);\n}\n"    # rots::helper
        "}\n"
        "ACMD(do_look)\n{\n    int a = room_of(ch)->number;\n}\n"     # do_look
        "room_data& room_data::operator[](int i)\n{\n    return *(BASE_WORLD + i);\n}\n"
    )
    attributions = attribute_lines(text.splitlines())
    expected = {
        1: ("file-scope", ""),                       # decl line
        9: ("function", "rots::helper"),              # namespaced function
        14: ("function", "do_look"),                  # ACMD function
        18: ("function", "room_data::operator[]"),    # qualified operator
        5: ("macro", "EXIT"),                         # macro-body line
    }
    for line_index, want in expected.items():
        if attributions[line_index] != want:
            failures.append(f"line {line_index}: got {attributions[line_index]}, want {want}")

    # Fix round 1, CRITICAL: gtest's own TEST/TEST_F/TEST_P definer keys as
    # "Suite.Name"; an unrecognized all-caps macro-invocation shape (the same
    # class of bug) must fail closed to file-scope rather than key on its own
    # macro name. Line indices derived by counting this fixture's
    # splitlines().
    gtest_text = (
        "TEST(LoadRoomChain, CalcLoadRoomLeavesTheChannelHoldingTheRawPersistedValue)\n"
        "{\n"
        "    int a = room_of(ch)->number;\n"
        "}\n"
        "SOMEMACRO(a, b)\n"
        "{\n"
        "    int b = room_of(ch)->number;\n"
        "}\n"
    )
    gtest_attributions = attribute_lines(gtest_text.splitlines())
    gtest_expected = {
        2: ("function", "LoadRoomChain.CalcLoadRoomLeavesTheChannelHoldingTheRawPersistedValue"),
        6: ("file-scope", ""),  # unrecognized macro-invocation shape: fail closed
    }
    for line_index, want in gtest_expected.items():
        if gtest_attributions[line_index] != want:
            failures.append(f"gtest line {line_index}: got {gtest_attributions[line_index]}, want {want}")

    # Fix round 1, IMPORTANT: the C++17 nested-namespace form
    # `namespace rots::world {` must re-arm header detection and qualify
    # with the full path.
    nested_ns_text = (
        "namespace rots::world {\n"
        "static int helper2(int a)\n"
        "{\n"
        "    return room_of(x);\n"
        "}\n"
        "}\n"
    )
    nested_ns_attributions = attribute_lines(nested_ns_text.splitlines())
    if nested_ns_attributions[3] != ("function", "rots::world::helper2"):
        failures.append(
            f"nested-namespace line 3: got {nested_ns_attributions[3]}, "
            "want ('function', 'rots::world::helper2')")

    # Fix round 1, IMPORTANT: a struct-prefixed return type must not be
    # mistaken for a type DEFINITION -- `struct room_data* get_char_room(...)`
    # keys as a function, while a plain `struct foo {` / `struct foo : bar {`
    # stays file-scope.
    struct_text = (
        "struct room_data* get_char_room(int i)\n"
        "{\n"
        "    return room_of(i);\n"
        "}\n"
        "struct foo {\n"
        "    int x;\n"
        "};\n"
        "struct foo2 : bar {\n"
        "    int y;\n"
        "};\n"
    )
    struct_attributions = attribute_lines(struct_text.splitlines())
    struct_expected = {
        2: ("function", "get_char_room"),
        5: ("file-scope", ""),
        8: ("file-scope", ""),
    }
    for line_index, want in struct_expected.items():
        if struct_attributions[line_index] != want:
            failures.append(f"struct line {line_index}: got {struct_attributions[line_index]}, want {want}")

    # Fix round 2, CRITICAL re-review: round 1's dm/gm branches set
    # pending_header without ever checking whether the SAME line also
    # carried the body-opening '{', so the repo's DOMINANT gtest style --
    # a brace-attached one-liner header, 536 such headers tree-wide --
    # silently fell to file-scope, and a multi-line-wrapped header could
    # inherit a STALE pending_header from an earlier, already-opened test
    # (cross-contamination). Four shapes, each with its own case, plus the
    # regression fixture proving no leakage between two adjacent tests.

    # (1) Brace-attached one-liner -- the DOMINANT real-tree shape.
    case1_text = "TEST(SuiteA, NameA) {\n    room_of(x);\n}\n"
    case1_attributions = attribute_lines(case1_text.splitlines())
    if case1_attributions[1] != ("function", "SuiteA.NameA"):
        failures.append(f"case1 (brace-attached) line 1: got {case1_attributions[1]}, "
                         "want ('function', 'SuiteA.NameA')")

    # (2) Multi-line args, '{' on its own following line.
    case2_text = "TEST(SuiteB,\n    NameB)\n{\n    room_of(x);\n}\n"
    case2_attributions = attribute_lines(case2_text.splitlines())
    if case2_attributions[3] != ("function", "SuiteB.NameB"):
        failures.append(f"case2 (multi-line, brace-next-line) line 3: got {case2_attributions[3]}, "
                         "want ('function', 'SuiteB.NameB')")

    # (3) Multi-line args, '{' attached to the closing-paren line.
    case3_text = "TEST(SuiteC,\n    NameC) {\n    room_of(x);\n}\n"
    case3_attributions = attribute_lines(case3_text.splitlines())
    if case3_attributions[2] != ("function", "SuiteC.NameC"):
        failures.append(f"case3 (multi-line, brace-attached) line 2: got {case3_attributions[2]}, "
                         "want ('function', 'SuiteC.NameC')")

    # (4) Cross-contamination regression: a brace-attached test immediately
    # followed by a wrapped-header test. The second body must key to ITS
    # OWN name; the first test's name must appear nowhere in the second's
    # range (the live bug: occupant_order_tests.cpp:746 keyed to an
    # unrelated prior test's name).
    case4_text = (
        "TEST(ReportZonePowerTest, EvilRaceDoesSomething) {\n"
        "    room_of(x);\n"
        "}\n"
        "TEST(RecountLightRoomTest,\n"
        "     AbsoluteDarknessBlocksLight) {\n"
        "    room_of(y);\n"
        "}\n"
    )
    case4_attributions = attribute_lines(case4_text.splitlines())
    first_name = ("function", "ReportZonePowerTest.EvilRaceDoesSomething")
    second_name = ("function", "RecountLightRoomTest.AbsoluteDarknessBlocksLight")
    if case4_attributions[1] != first_name:
        failures.append(f"case4 line 1 (first test body): got {case4_attributions[1]}, want {first_name}")
    for line_index in (4, 5, 6):
        if case4_attributions[line_index] != second_name:
            failures.append(f"case4 line {line_index} (second test body): got "
                             f"{case4_attributions[line_index]}, want {second_name} "
                             "(cross-contamination: leaked the first test's name)")

    # (5) ACMD/SPECIAL brace-attached one-liner with an inline body
    # statement: `ACMD(do_x) { room_of(ch); }` all on one physical line.
    # This still lands file-scope -- the inline ';' inside the body (not a
    # declaration-ending ';') fires the SAME statement-end reset that
    # correctly discards a real declaration, since the current line-based
    # accumulation cannot distinguish "the body's own ';'" from
    # "terminated before reaching '{'" within a single physical line.
    # ACCEPTABLE only because it is fail-closed (surfaces as an
    # unattributed-function site for Task 2's gate to catch, not a wrong
    # function key) AND because the real tree was swept for this exact
    # shape: exactly two lines tree-wide match it
    # (src/app/act_othe.cpp:150, src/script/spec_pro.cpp:2887), and
    # NEITHER carries a resolver token -- see task-1-report.md Sec 11.5.
    case5_text = "ACMD(do_x) { room_of(ch); }\n"
    case5_attributions = attribute_lines(case5_text.splitlines())
    if case5_attributions[0] != ("file-scope", ""):
        failures.append(f"case5 (ACMD one-liner) line 0: got {case5_attributions[0]}, "
                         "want ('file-scope', '') (known, verified-safe fail-closed gap)")

    # Fix round 3, GAP 1: ASPELL (src/spells.h:401) is a third single-arg
    # definer of the ACMD/SPECIAL shape -- ~70 spell functions in
    # src/combat/{mage,mystic}.cpp fell to file-scope with no recognized
    # definer for them (Task 3's real-tree run hit this fail-closed).
    aspell_text = "ASPELL(spell_fireball)\n{\n    room_of(caster);\n}\n"
    aspell_attributions = attribute_lines(aspell_text.splitlines())
    if aspell_attributions[2] != ("function", "spell_fireball"):
        failures.append(f"aspell line 2: got {aspell_attributions[2]}, "
                         "want ('function', 'spell_fireball')")

    # Fix round 3, GAP 2: a function-pointer PARAMETER defeats the generic
    # name-extraction heuristic, which sliced at head.rfind("(") -- the
    # LAST paren, landing inside the fn-ptr parameter's own argument list
    # (src/combat/olog_hai.cpp:374's real `room_target` defeats it this
    # way). Fixed by slicing at the FIRST '(' instead.
    fnptr_param_text = (
        "void room_target(char_data* ch, void (*skill_damage)(char_data* character, char_data* victim))\n"
        "{\n"
        "    room_of(ch);\n"
        "}\n"
    )
    fnptr_param_attributions = attribute_lines(fnptr_param_text.splitlines())
    if fnptr_param_attributions[2] != ("function", "room_target"):
        failures.append(f"fnptr-param line 2: got {fnptr_param_attributions[2]}, "
                         "want ('function', 'room_target')")

    # Fix round 3 HARDENING (re-review IMPORTANT): a function RETURNING a
    # function pointer must fail closed to file-scope, not silently key on
    # its return type's leading word. The original round-3 fix (slice at
    # the FIRST '(') introduced exactly this regression -- measured before
    # this hardening: ('function', 'int') / ('function', 'void') /
    # ('function', 'special_function_t') for the three variants below,
    # none of which is a real function name. Verified not live anywhere in
    # this tree (no `(*name(args))(` shape exists) but must never silently
    # mis-key if one is ever added.
    fnptr_returning_cases = (
        ("int (*get_fn())(int)\n{\n    room_of(x);\n}\n", 2),
        ("void (*get_handler(int i))(char_data*)\n{\n    room_of(x);\n}\n", 2),
        ("special_function_t (*find_special(int vnum))(void)\n{\n    room_of(x);\n}\n", 2),
    )
    for case_text, check_line in fnptr_returning_cases:
        case_attributions = attribute_lines(case_text.splitlines())
        if case_attributions[check_line] != ("file-scope", ""):
            failures.append(
                f"fnptr-returning {case_text.splitlines()[0]!r} line {check_line}: "
                f"got {case_attributions[check_line]}, want ('file-scope', '')")

    # review-1 W-9: a stray leading run of '}'/';' -- closing an enclosing
    # scope -- sharing a PHYSICAL LINE with the very next header
    # (`} ACMD(do_x) {`) must not glue into the header text and mis-key the
    # site as literally "ACMD" (the macro-definer's own name). An empty
    # namespace whose OWN closing brace shares this line with the new
    # header is the smallest reproducible shape: `depth == base_depth`
    # already holds at the top of this line (nothing happened between the
    # namespace's opening and this line, so depth never moved away from
    # "just inside namespace"), so header recognition actually runs on it --
    # unlike a FUNCTION body's own close sharing a line with the next
    # header, which this architecture's per-line lumped brace-delta cannot
    # detect within one iteration (a known, separate limitation, matching
    # case5's already-documented inline-body gap above).
    w9_text = "namespace rots {\n} ACMD(do_x) {\n    room_of(ch);\n}\n"
    w9_attributions = attribute_lines(w9_text.splitlines())
    if w9_attributions[1] != ("function", "rots::do_x"):
        failures.append(
            f"w9 (stray-close-then-header) line 1: got {w9_attributions[1]}, want "
            "('function', 'rots::do_x') -- the leading '}' must not glue into the "
            "header text and mis-key this as literally 'ACMD'")

    for failure in failures:
        print(f"dev-selftest FAILED: {failure}", file=sys.stderr)
    return 1 if failures else 0


# ---------------------------------------------------------------------------
# Hermetic self-test fixtures. Everything below is synthetic: a
# TemporaryDirectory tree plus these literal strings, never the repository's
# own docs or src/ (the real-tree --check run is a separate, one-off manual
# invocation -- see the Task 2 report -- not part of --self-test).
# ---------------------------------------------------------------------------

# A four-site probe file: one pinned declaration (file-scope, keys "#decl"),
# one plain TODO call, one PROVEN call with a citation, and a second TODO
# call written as a genuinely multi-line invocation (self-test direction 16
# -- room_of( and its ')' on different physical lines still hits, since the
# token pattern only needs the callee name immediately followed by '(' on
# ONE line; see the module docstring's token-shape note).
PROBE_SOURCE_OK = """struct room_data* room_of(const struct char_data* ch);

int do_light(char_data* ch)
{
    room_data* r = world[5];
    return 0;
}

void save_char(char_data* ch)
{
    if (location_of(ch) != NOWHERE)
    {
        dispatch_room_vnum(ch);
    }
}

void recalc_zone(char_data* ch)
{
    room_data* r = room_of(
        ch);
    return;
}
"""

# A variant proving masking (self-test direction 15): a token inside a
# comment, a token inside a string literal, and a real site with a trailing
# `//` comment -- same four real sites as PROBE_SOURCE_OK (so SELF_TEST_LEDGER_OK
# reconciles against it unchanged), plus decoys that must NOT add to any count.
PROBE_SOURCE_MASKING = """struct room_data* room_of(const struct char_data* ch);

int do_light(char_data* ch)
{
    // room_of(ch) mentioned in a comment -- must not count
    const char* s = "world[999] also not a real site";
    room_data* r = world[5]; // trailing comment after the real site
    return 0;
}

void save_char(char_data* ch)
{
    if (location_of(ch) != NOWHERE)
    {
        dispatch_room_vnum(ch);
    }
}

void recalc_zone(char_data* ch)
{
    room_data* r = room_of(
        ch);
    return;
}
"""

DECL_ROW = "| `src/app/probe.cpp · #decl · room_of(` | 1 | DECL | " + LEDGER_EMPTY_MARKER + " | " + LEDGER_EMPTY_MARKER + " |"
TODO_ROW_DO_LIGHT = "| `src/app/probe.cpp · do_light · world[` | 1 | TODO | " + LEDGER_EMPTY_MARKER + " | " + LEDGER_EMPTY_MARKER + " |"
PROVEN_ROW = (
    "| `src/app/probe.cpp · save_char · dispatch_room_vnum(` | 1 | PROVEN | entry-guard | "
    "guarded two lines above by `location_of(ch) != NOWHERE` — src/app/probe.cpp:11 |"
)
TODO_ROW_RECALC_ZONE = "| `src/app/probe.cpp · recalc_zone · room_of(` | 1 | TODO | " + LEDGER_EMPTY_MARKER + " | " + LEDGER_EMPTY_MARKER + " |"

# Every hermetic fixture ledger driven through the REAL gate (`_run_gate`)
# needs the dispatch-entry marker present, since `main()` now parses that
# table too and the M10 rule fails closed on a missing marker. The fixtures
# that only exercise `parse_ledger` directly (the ~18
# `expect_parse_ledger_systemexit` directions and direction 13's synthetic
# row-floor ledgers) do NOT need it -- `parse_dispatch_entries` is a separate
# entry point precisely so those stay judged on the violation each one
# actually targets.
EMPTY_DISPATCH_TABLE = f"""
{LEDGER_DISPATCH_ENTRIES_MARKER}
{LEDGER_DISPATCH_ENTRIES_HEADER}
| --- | --- | --- | --- |
"""

SELF_TEST_LEDGER_OK = f"""<!-- ROOM-RESOLVE-CLASSIFICATION -->
| Key | Count | Class | Kind | Proof |
| --- | --- | --- | --- | --- |
{DECL_ROW}
{TODO_ROW_DO_LIGHT}
{PROVEN_ROW}
{TODO_ROW_RECALC_ZONE}

<!-- ROOM-RESOLVE-TOKEN-COUNTS -->
| Token | Sites |
| --- | --- |
| `room_of(` | 2 |
| `world[` | 1 |
| `dispatch_room_vnum(` | 1 |
{EMPTY_DISPATCH_TABLE}"""

# Task 3 raised MINIMUM_LEDGER_ROW_COUNT (above) to the real ledger's scale
# (451) -- far larger than any hermetic self-test fixture in this file should
# ever need to be. This is the small, fixed floor the self-test's own tiny
# synthetic ledgers (SELF_TEST_LEDGER_OK and its variants, all 4 rows) are
# built against; `_run_gate`'s `minimum_ledger_rows_override` parameter
# defaults to this constant so every EXISTING self-test call site keeps
# working unchanged (only the directions that deliberately probe the REAL
# floor -- 13, below -- opt out of the override).
SELF_TEST_LEDGER_ROW_FLOOR = 4

# M10 sabotage fixture (marker sabotage, direction 12): a decoy classification
# table sitting inside a FENCE, above the one real marker, carrying the exact
# same header -- must be inert (parse_ledger must still find and use only the
# real, unfenced table).
SELF_TEST_LEDGER_FENCED_DECOY = f"""# synthetic ledger with a fenced decoy

```
<!-- ROOM-RESOLVE-CLASSIFICATION -->
| Key | Count | Class | Kind | Proof |
| --- | --- | --- | --- | --- |
| `src/decoy.cpp · decoy_fn · world[` | 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |
```

{SELF_TEST_LEDGER_OK}
"""

# An ACMD-defined function and a macro-body site, exercising self-test
# direction 16's attribution claims end to end (ACMD keys as its bare name;
# a macro body keys as "#NAME") plus two plain-function filler sites so the
# ledger clears MINIMUM_LEDGER_ROW_COUNT. The macro-body site deliberately
# reuses ASSIGNROOM -- an ALREADY-pinned MACRO_FAMILY member -- rather than a
# fresh name: a fresh macro name whose body reaches room_of( would ALSO trip
# the (unrelated) macro-family-closed-world check this fixture is not
# testing, since derive_macro_family's seed rule is exactly "any body
# matching a STATIC token pattern." Reusing a pinned name sidesteps that
# without weakening the attribution claim (the "#NAME" keying is identical
# either way). The #define line itself doubles as a site for BOTH tokens on
# one line: "ASSIGNROOM(" (the definer's own parameter list, matching its
# MACRO_FAMILY call pattern) and "room_of(" (its body) -- both correctly
# attributed to ("macro", "ASSIGNROOM").
ATTRIBUTION_PROBE_SOURCE = """ACMD(do_glance)
{
    room_data* r = room_of(
        ch);
}

#define ASSIGNROOM(ch) (room_of(ch)->number)

int helper_fn(char_data* ch)
{
    room_data* r = world[2];
    return 0;
}

void another_fn(char_data* ch)
{
    room_data* r2 = room_by_id_total(3);
    return;
}
"""

ATTRIBUTION_LEDGER = f"""<!-- ROOM-RESOLVE-CLASSIFICATION -->
| Key | Count | Class | Kind | Proof |
| --- | --- | --- | --- | --- |
| `src/app/attrib.cpp · do_glance · room_of(` | 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |
| `src/app/attrib.cpp · #ASSIGNROOM · ASSIGNROOM(` | 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |
| `src/app/attrib.cpp · #ASSIGNROOM · room_of(` | 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |
| `src/app/attrib.cpp · helper_fn · world[` | 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |
| `src/app/attrib.cpp · another_fn · room_by_id_total(` | 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |

<!-- ROOM-RESOLVE-TOKEN-COUNTS -->
| Token | Sites |
| --- | --- |
| `room_of(` | 2 |
| `ASSIGNROOM(` | 1 |
| `world[` | 1 |
| `room_by_id_total(` | 1 |
{EMPTY_DISPATCH_TABLE}"""


# RR Wave R3 Task 1a fixtures: one synthetic dispatcher carrying a real
# dispatch spelling (`.spell_pointer(`) behind a real entry guard, plus a
# second, deliberately UNREGISTERED function carrying the same spelling. The
# file has NO resolver token at all, so it needs no classification row and
# SELF_TEST_LEDGER_OK reconciles against the shared probe.cpp unchanged --
# which keeps every dispatch direction isolated to the dispatch check.
DISPATCH_PROBE_SOURCE = """void dispatcher_fn(char_data* ch)
{
    if (location_of(ch) == NOWHERE) {
        return;
    }
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""

DISPATCH_PROBE_SOURCE_GUARD_DELETED = """void dispatcher_fn(char_data* ch)
{
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""

DISPATCH_PROBE_SOURCE_SECOND_DISPATCHER = DISPATCH_PROBE_SOURCE + """
void sneaky_new_dispatcher(char_data* ch)
{
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""

DISPATCH_ENTRY_ROW_GUARDED = (
    "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
    "`if (location_of(ch) == NOWHERE) { return; }` | GUARDED |"
)

# RR Wave R3 Task 1d (R3-C-7): the TWO-tripwire shape. An early guard
# licenses whatever the function resolves before it dispatches, and a second
# one sits IMMEDIATELY before the dispatch so nothing that ran in between
# (here `parse_the_target(ch)`, standing in for `target_parser`/
# `complete_delay`) can have relocated the actor unobserved. The second guard
# uses the if/else refusal shape rather than an early return, exactly as the
# real `command_interpreter`/`do_cast` sites do, so the probe also exercises a
# literal whose text ends in `} else {`.
DISPATCH_PROBE_SOURCE_TWO_GUARDS = """void dispatcher_fn(char_data* ch)
{
    if (location_of(ch) == NOWHERE) {
        return;
    }

    parse_the_target(ch);

    if (location_of(ch) == NOWHERE) {
        mudlog("dispatcher_fn: dispatch refused", NRM, LEVEL_IMPL, TRUE);
    } else {
        skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
    }
}
"""

# The two-literal cell. The separator's bars are written escaped, exactly as
# the ledger's markdown must write them, so this fixture drives
# `_split_row_cells`'s unescaping too and not merely the grammar behind it.
DISPATCH_ENTRY_ROW_TWO_GUARDS = (
    "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
    "`if (location_of(ch) == NOWHERE) { return; }` \\|\\| "
    "`if (location_of(ch) == NOWHERE) { mudlog( , NRM, LEVEL_IMPL, TRUE); } else {` | GUARDED |"
)

# A single literal that CONTAINS a real C `||` operator -- the shape the live
# `one_mobile_activity` entry carries. It must parse as ONE literal; a naive
# `split(" || ")` would tear it into two unmatchable halves and fail a row
# that is perfectly well formed.
DISPATCH_PROBE_SOURCE_DISJUNCTIVE_GUARD = """void dispatcher_fn(char_data* ch)
{
    if ((location_of(ch) < 0) || (location_of(ch) > top_of_world)) {
        return;
    }
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""

DISPATCH_ENTRY_ROW_DISJUNCTIVE_GUARD = (
    "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
    "`if ((location_of(ch) < 0) \\|\\| (location_of(ch) > top_of_world)) { return; }` | GUARDED |"
)

# The same entry after somebody REWROTE its disjunctive guard into two
# separate, weaker tests. Each of the two halves a naive `split(" || ")`
# would produce is still a substring of this body -- `if ((location_of(ch) <
# 0)` and `(location_of(ch) > top_of_world)) { return; }` both appear -- but
# the guard the registry row names does not exist any more. Only a checker
# that matches the WHOLE literal catches it.
DISPATCH_PROBE_SOURCE_DISJUNCTIVE_GUARD_TORN = """void dispatcher_fn(char_data* ch)
{
    if ((location_of(ch) < 0) && allow_negative_rooms) {
        return;
    }
    if (false && (location_of(ch) > top_of_world)) { return; }
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""


# RR Wave R3 Task 5-fix fixtures (review-1 B-1/B-3/B-3b, review-2 B-2).
#
# Every one of these is a spelling of the SAME unregistered dispatch that the
# original seven-token surface let through. They are separate constants (not
# one probe with six functions) so each self-test direction names exactly the
# spelling it pins.
DISPATCH_PROBE_SOURCE_ONE_LINE_DEFINITION = DISPATCH_PROBE_SOURCE + """
void rogue_one_liner(char_data* ch, char* arg) { ((*cmd_info[5].command_pointer)(ch, arg, 0)); }
"""

# The admissible file-scope shapes, all in one probe: a prototype, a
# struct-member fn-ptr declaration, a WRAPPED definition header, and a
# brace-init definition. Every pinned token here sits at brace depth 0 and
# must NOT raise -- this is the direction that keeps the B-1 fix from simply
# banning file scope.
DISPATCH_PROBE_SOURCE_FILE_SCOPE_DECLARATIONS = DISPATCH_PROBE_SOURCE + """
void shape_center(char_data* ch, char* argument);
void (*command_pointer)(char_data* ch, char* argument, int cmd);
void (*spell_pointer)(char_data* caster, char* arg, int type);
std::array<acmd_fn, 3> g_command_table {};
int activate_char_special(char_data* host, char_data* victim, int cmd,
    char* argument, int callflag);
"""

DISPATCH_PROBE_SOURCE_ARROW_MEMBER_CALL = DISPATCH_PROBE_SOURCE + """
void rogue_arrow(char_data* ch, skill_data* sk)
{
    sk->spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""

DISPATCH_PROBE_SOURCE_HOISTED_COPY = DISPATCH_PROBE_SOURCE + """
void rogue_hoist(char_data* ch, char* arg)
{
    auto fp = cmd_info[5].command_pointer;
    fp(ch, arg, 0);
}
"""

DISPATCH_PROBE_SOURCE_TABLE_ALIAS = DISPATCH_PROBE_SOURCE + """
void rogue_alias(char_data* ch, char* arg)
{
    auto& tbl = g_command_table;
    tbl[3](ch, arg, 0);
}
"""

DISPATCH_PROBE_SOURCE_FUNCT_SLOT = DISPATCH_PROBE_SOURCE + """
void rogue_funct(char_data* ch, int room)
{
    ((*room_by_id_total(room)->funct)(ch, ch, 0, mutable_arg(""), 0, 0));
}
"""

# review-1 B-3b: the guard is still TEXTUALLY present, so the downward
# literal check used to pass -- while the compiler saw no guard at all.
DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO = """void dispatcher_fn(char_data* ch)
{
#if 0
    if (location_of(ch) == NOWHERE) {
        return;
    }
#endif
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""

# Task 5-fix2 (re-verification M-3): the same probe in two of the six other
# never-true spellings the reviewer walked a guard out of the build with.
DISPATCH_PROBE_SOURCE_GUARD_IF_PAREN_ZERO = DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO.replace(
    "#if 0", "#if (0)", 1)
DISPATCH_PROBE_SOURCE_GUARD_IF_NOT_ONE = DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO.replace(
    "#if 0", "#if !1", 1)

# ... and the control that keeps the widening from becoming a blanket
# "blank every #if region": a LIVE `#if 1` leaves the guard standing.
DISPATCH_PROBE_SOURCE_GUARD_IF_ONE = DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO.replace(
    "#if 0", "#if 1", 1)

# ... and the mirror: an `#if 0` whose `#else` arm carries the REAL guard.
# That arm is live code, so the region must end at the `#else` and the gate
# must still find the literal.
DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO_ELSE = """void dispatcher_fn(char_data* ch)
{
#if 0
    ignore_placement_entirely(ch);
#else
    if (location_of(ch) == NOWHERE) {
        return;
    }
#endif
    skills[tmp].spell_pointer(ch, mutable_arg(""), 0, ch, 0, 0, 1);
}
"""


# RR Wave R3 Task 5-fix2 (re-verification M-5): the SPECIAL-body doors the
# tree already uses and no earlier spelling named. Each probe is the exact
# shape the reviewer ran against the real tree at `--check` exit 0.
DISPATCH_PROBE_SOURCE_SPECIAL_DOORS = DISPATCH_PROBE_SOURCE + """
void rogue_prog_number(char_data* ch, char* arg)
{
    SPECIAL(*tmpfunc);
    tmpfunc = (SPECIAL(*))virt_program_number(ch->specials.store_prog_number);
    tmpfunc(ch, ch, 0, arg, SPECIAL_COMMAND, 0);
}

void rogue_special_function(char_data* ch, char* arg, int number)
{
    special_func_ptr fp = get_special_function(number);
    fp(ch, ch, 0, arg, SPECIAL_COMMAND, 0);
}

void rogue_dispatch_prog_number(char_data* ch, char* arg)
{
    SPECIAL(*tmpfunc) = (SPECIAL(*))rots::script::dispatch_virt_program_number(7);
    tmpfunc(ch, ch, 0, arg, SPECIAL_COMMAND, 0);
}

void rogue_intelligent(char_data* ch, char* arg)
{
    intelligent(ch, ch, 0, arg, SPECIAL_COMMAND, 0);
}
"""

# RR Wave R3 Task 5-fix2 (re-verification B-1/m-1): the exemption-shaped
# probes. `exempt_fn` is the shape the 15 `*_index[].func` exemptions have --
# an ordinary presence TEST that reads the slot's address and calls nothing.
# The gate is driven with a matching one-key override
# (`--dispatch-exempt-sites-override`), so these four sources differ from one
# another only in the way a future edit could abuse that key.
DISPATCH_PROBE_SOURCE_EXEMPT_PRESENCE_TEST = DISPATCH_PROBE_SOURCE + """
void exempt_fn(char_data* ch, char* arg)
{
    if (mob_index[ch->nr].func) {
        send_to_char("busy", ch);
    }
}
"""

# The BLOCKER itself: the identical statement rewritten as a member CALL.
# Before the token split, `mob_index[ch->nr].func(...)` carried the SAME
# token as the presence test above, so the exemption covered both and this
# whole function dispatched a spec proc at `--check` exit 0.
DISPATCH_PROBE_SOURCE_EXEMPT_MEMBER_CALL = DISPATCH_PROBE_SOURCE + """
void exempt_fn(char_data* ch, char* arg)
{
    if (mob_index[ch->nr].func) {
        mob_index[ch->nr].func(ch, ch, 0, arg, 0, 0);
    }
}
"""

# The count direction: a SECOND presence test of the same token in the same
# exempted function. Same token, same key, one more occurrence than the pin
# describes -- which is how `affect_modify` could have grown a third
# `.spell_pointer(` arm beside its two exempted ones.
DISPATCH_PROBE_SOURCE_EXEMPT_SECOND_OCCURRENCE = DISPATCH_PROBE_SOURCE + """
void exempt_fn(char_data* ch, char* arg)
{
    if (mob_index[ch->nr].func) {
        send_to_char("busy", ch);
    }
    if (mob_index[ch->nr].func) {
        send_to_char("still busy", ch);
    }
}
"""

# The stale direction: the exempted function exists no longer (renamed), so
# the pinned key matches nothing and would silently pre-authorize whatever
# is written into a function of that name next.
DISPATCH_PROBE_SOURCE_EXEMPT_RENAMED = DISPATCH_PROBE_SOURCE + """
void exempt_fn_renamed(char_data* ch, char* arg)
{
    if (mob_index[ch->nr].func) {
        send_to_char("busy", ch);
    }
}
"""


def _dispatch_ledger(*entry_rows):
    """SELF_TEST_LEDGER_OK with the empty dispatch table filled in."""
    filled = EMPTY_DISPATCH_TABLE.rstrip("\n") + "".join(
        "\n" + row for row in entry_rows) + "\n"
    return SELF_TEST_LEDGER_OK.replace(EMPTY_DISPATCH_TABLE, filled, 1)


# RR Wave R3 Task 1a: a caller-count probe. The DECLARATION must not count
# (file-scope attribution); the two calls inside a function body must.
CALLER_COUNT_PROBE_SOURCE = PROBE_SOURCE_OK + """
int CAN_SEE(char_data* sub);

void looks_around(char_data* ch, char_data* other)
{
    if (CAN_SEE(ch, other)) {
        return;
    }
}
"""

CALLER_COUNT_PROBE_SOURCE_DECLARATION_ONLY = PROBE_SOURCE_OK + """
int CAN_SEE(char_data* sub);
"""

# RR Wave R3 Task 5-fix (review-1 B-1), the caller-pin half of the one-line
# bypass: the call lives in a brace-attached one-line definition, so its line
# attributes to FILE SCOPE and the pin never counted it.
CALLER_COUNT_PROBE_SOURCE_ONE_LINE_DEFINITION = PROBE_SOURCE_OK + """
int peek_at(char_data* a, char_data* b) { return CAN_SEE(a, b); }
"""

# review-2 m-5: an alias macro. `RR_CS(a, b)` is a CAN_SEE caller the pinned
# literal-spelling count cannot see.
CALLER_COUNT_PROBE_SOURCE_ALIAS_MACRO = PROBE_SOURCE_OK + """
#define RR_CS CAN_SEE

void looks_around(char_data* ch, char_data* other)
{
    if (RR_CS(ch, other)) {
        return;
    }
}
"""

# ... and the shape the alias rule must NOT reject: a macro that CALLS the
# pinned name. Its call site spells `VIS(`, but the macro BODY's `CAN_SEE(`
# is itself on a `macro`-attributed line and is already counted, which is
# exactly why the measurement method keeps macro lines.
CALLER_COUNT_PROBE_SOURCE_CALLING_MACRO = PROBE_SOURCE_OK + """
#define VIS(a, b) CAN_SEE(a, b)
"""


def _run_gate(root, ledger=None, scan_path=None, *,
              todo_ceiling_override=None, minimum_files_override=None,
              minimum_ledger_rows_override=SELF_TEST_LEDGER_ROW_FLOOR,
              minimum_dispatch_entries_override=0,
              caller_count_pins_override="",
              dispatch_exempt_sites_override=""):
    """Invoke the REAL gate (subprocess, full main()) against a synthetic
    tree. `ledger=None` omits --ledger entirely, exercising main()'s own
    default-path computation (self-test direction 17). `scan_path=None`
    scans <root>/src (main()'s own default when no positional path is
    given).

    `minimum_ledger_rows_override` defaults to `SELF_TEST_LEDGER_ROW_FLOOR`
    (4) -- every `_run_gate` call in this file targets a small, hand-built
    synthetic ledger, never the real repo's, so applying the small floor
    HERE (rather than at each of the ~20 call sites) is what lets Task 3's
    real MINIMUM_LEDGER_ROW_COUNT (451) coexist with the hermetic fixtures
    unchanged. Pass `None` explicitly to test the REAL floor instead
    (direction 13's boundary probes).

    `minimum_dispatch_entries_override` (R3 Task 1a) defaults to 0, and
    `caller_count_pins_override`/`dispatch_exempt_sites_override` to the
    empty string, for the same reason: a synthetic tree has neither the real
    registry's 12 entry points, nor any of the 127 real
    `CAN_SEE(`/`get_char_room_vis(` callers, nor any of the 26 real exempted
    dispatch-token sites, so the real floor, the real pins and the real
    exemption map would fail every unrelated direction. The directions that
    PROBE those three checks pass their own values."""
    import subprocess

    command = [sys.executable, str(pathlib.Path(__file__).resolve()),
               "--check", "--root", str(root)]
    if ledger is not None:
        command += ["--ledger", str(ledger)]
    if scan_path is not None:
        command.append(str(scan_path))
    env = dict(os.environ)
    if todo_ceiling_override is not None:
        command += ["--todo-ceiling-override", str(todo_ceiling_override)]
        env["RR_SELF_TEST"] = "1"
    if minimum_files_override is not None:
        command += ["--minimum-files-override", str(minimum_files_override)]
        env["RR_SELF_TEST"] = "1"
    if minimum_ledger_rows_override is not None:
        command += ["--minimum-ledger-rows-override", str(minimum_ledger_rows_override)]
        env["RR_SELF_TEST"] = "1"
    if minimum_dispatch_entries_override is not None:
        command += ["--minimum-dispatch-entries-override", str(minimum_dispatch_entries_override)]
        env["RR_SELF_TEST"] = "1"
    if caller_count_pins_override is not None:
        command += ["--caller-count-pins-override", caller_count_pins_override]
        env["RR_SELF_TEST"] = "1"
    if dispatch_exempt_sites_override is not None:
        command += ["--dispatch-exempt-sites-override", dispatch_exempt_sites_override]
        env["RR_SELF_TEST"] = "1"
    completed = subprocess.run(command, capture_output=True, text=True, env=env)
    return completed.returncode, completed.stdout + completed.stderr


def run_self_test():
    """Prove the gate still fails in every direction it must. Returns exit code."""
    import tempfile

    failures = []

    # Task 1's own TDD harness runs first, as additional standing checks --
    # nothing it proved is lost now that dev_selftest() is no longer reachable
    # from a bare invocation.
    if dev_selftest() != 0:
        failures.append("dev_selftest() (Task 1's own harness) reported failures -- see stderr above")

    # The floor's VALUE, pinned as a literal on purpose (mirrors
    # location_read_census.py's own MINIMUM_SCANNED_FILE_COUNT pin): every
    # boundary probe below spends the constant SYMBOLICALLY, so none of them
    # would notice it being silently lowered.
    if MINIMUM_SCANNED_FILE_COUNT < 300:
        failures.append(
            f"MINIMUM_SCANNED_FILE_COUNT is {MINIMUM_SCANNED_FILE_COUNT}, below the 300 measured "
            "against a 315-file scan -- a lowered floor lets a broken scan path pass as clean."
        )

    # Ceiling pin (F-10 mirror of MINIMUM_SCANNED_FILE_COUNT's pin, T2 brief
    # direction 5), ACTIVATED (Task 3, the tracked review obligation): pinned
    # at 579 -- the exact MAXIMUM_TODO_COUNT the module now holds (RR Wave R2
    # Task 1 lowered it 788 -> 748; Task 2 lowered it further, 748 -> 716; RR
    # Wave R3 Task 1a raised it 716 -> 717 for the R3-C-3 reopening, the
    # program's only raise -- see MAXIMUM_TODO_COUNT's own comment; R3's
    # T2p+T3p integration lowered it 717 -> 636 and T2d+T3d 636 -> 585, then
    # T1c+T3e 585 -> 579, all `--check`-derived), measured
    # against the real ledger's TODO total. The two literals move together, in
    # one commit, always. An accidental RAISE (an
    # edit that loosens the ratchet without a deliberate, reviewed
    # drain-wave decision) fails `--self-test`, not just review; a LOWER
    # value (a real drain wave) keeps passing `<=`, and that wave updates
    # this literal in the same commit that lowers MAXIMUM_TODO_COUNT itself
    # (the two literals are set together, never independently).
    if MAXIMUM_TODO_COUNT is not None and MAXIMUM_TODO_COUNT > 579:
        failures.append(
            f"MAXIMUM_TODO_COUNT is {MAXIMUM_TODO_COUNT}, above the pinned ceiling of 579 -- "
            "an accidental raise must not silently loosen the ratchet."
        )

    # review-1 W-6: the row-count floor's own VALUE, pinned the same way as
    # the two literals immediately above -- without this pin, the docs'
    # "both floors self-test-pinned" claim was false (only the file-count
    # and TODO-ceiling floors actually were). W-5's reclassification of
    # renum_world/setup_dir moves rows BETWEEN classes, not into or out of
    # the ledger, so it does not change the real row count and this pin
    # stays at 451 unchanged by this same commit.
    if MINIMUM_LEDGER_ROW_COUNT < 451:
        failures.append(
            f"MINIMUM_LEDGER_ROW_COUNT is {MINIMUM_LEDGER_ROW_COUNT}, below the 451 seeded "
            "from the real ledger's measured row count -- a lowered floor lets a "
            "truncated ledger pass as clean."
        )

    # Review-1 IMPORTANT finding 1: reconcile()'s todo_ceiling default must
    # be resolved at CALL time (the LIVE MAXIMUM_TODO_COUNT), not baked in as
    # an ordinary keyword default evaluated once at `def` time -- a plain
    # `todo_ceiling=MAXIMUM_TODO_COUNT` default would freeze whatever the
    # constant held at import time and silently ignore any later change
    # (Task 3 raising it, or a test monkeypatching it). Proven in-process,
    # no subprocess needed -- this is a pure Python calling-convention
    # property, not something the gate's file-scanning path affects.
    original_maximum_todo_count = MAXIMUM_TODO_COUNT
    try:
        globals()["MAXIMUM_TODO_COUNT"] = 1
        live_ceiling_sites = [
            {"file": "x.cpp", "function": "f", "attribution": "function", "token": "world["},
            {"file": "x.cpp", "function": "f", "attribution": "function", "token": "world["},
        ]
        live_ceiling_rows = [{
            "key_file": "x.cpp", "key_function": "f", "key_token": "world[",
            "count": 2, "cls": "TODO", "kind": LEDGER_EMPTY_MARKER, "proof": LEDGER_EMPTY_MARKER,
        }]
        live_ceiling_errors = reconcile(live_ceiling_sites, live_ceiling_rows, {"world[": 2})
        if not any("exceeds the ceiling of 1" in error for error in live_ceiling_errors):
            failures.append(
                "reconcile()'s todo_ceiling default did not honor a live-monkeypatched "
                f"MAXIMUM_TODO_COUNT=1 when called without the kwarg (early-bound-default "
                f"regression): {live_ceiling_errors}"
            )
    finally:
        globals()["MAXIMUM_TODO_COUNT"] = original_maximum_todo_count

    with tempfile.TemporaryDirectory() as temporary_root:
        root = pathlib.Path(temporary_root).resolve()
        app_dir = root / "src" / "app"
        app_dir.mkdir(parents=True)
        probe = app_dir / "probe.cpp"
        ledger = root / "ledger.md"

        # The floor (O-I7-style) needs a realistic file count; pad with
        # clean files exactly as location_read_census.py's own self-test does.
        pad_dir = root / "src" / "pad"
        pad_dir.mkdir(parents=True)
        for index in range(MINIMUM_SCANNED_FILE_COUNT + 5):
            (pad_dir / f"pad{index}.cpp").write_text("int clean = 0;\n", encoding="utf-8")

        def reset():
            probe.write_text(PROBE_SOURCE_OK, encoding="utf-8")
            ledger.write_text(SELF_TEST_LEDGER_OK, encoding="utf-8")

        reset()

        def run_case(name, expected_exit, *, probe_text=None, ledger_text=None,
                     scan_path=None, todo_ceiling_override=None, minimum_files_override=None):
            probe.write_text(probe_text if probe_text is not None else PROBE_SOURCE_OK,
                              encoding="utf-8")
            ledger.write_text(ledger_text if ledger_text is not None else SELF_TEST_LEDGER_OK,
                               encoding="utf-8")
            exit_code, output = _run_gate(
                root, ledger, scan_path,
                todo_ceiling_override=todo_ceiling_override,
                minimum_files_override=minimum_files_override)
            if exit_code != expected_exit:
                failures.append(f"{name}: expected gate exit {expected_exit}, got {exit_code}\n{output}")
            reset()

        # 1. clean.
        run_case("clean", 0)

        # 2. unclassified-site: a brand-new function with a real token and no
        # matching ledger row.
        run_case(
            "unclassified-site", 1,
            probe_text=PROBE_SOURCE_OK + (
                "\nvoid untracked_fn(char_data* ch)\n{\n"
                "    room_data* r = room_of(ch);\n}\n"
            ),
        )

        # 3. stale-row: an extra classification row naming a key nothing scans to.
        run_case(
            "stale-row", 1,
            ledger_text=SELF_TEST_LEDGER_OK.replace(
                TODO_ROW_RECALC_ZONE,
                TODO_ROW_RECALC_ZONE + "\n| `src/app/probe.cpp · nonexistent_fn · room_of(` "
                f"| 1 | TODO | {LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |"
            ),
        )

        # 4. count-mismatch-low: scan finds MORE sites than the ledger row claims.
        run_case(
            "count-mismatch-low", 1,
            probe_text=PROBE_SOURCE_OK.replace(
                "    room_data* r = room_of(\n        ch);\n    return;\n}\n",
                "    room_data* r = room_of(\n        ch);\n"
                "    room_data* r2 = room_of(ch);\n    return;\n}\n"
            ),
        )
        # 4. count-mismatch-high: scan finds FEWER sites than the ledger row claims
        # (the anti-inheritance direction, spec section 3/O-5 -- a stale HIGHER
        # count must fail exactly like a stale lower one).
        run_case(
            "count-mismatch-high", 1,
            ledger_text=SELF_TEST_LEDGER_OK.replace(
                TODO_ROW_RECALC_ZONE,
                "| `src/app/probe.cpp · recalc_zone · room_of(` | 2 | TODO | "
                f"{LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |"
            ),
        )

        # 5. todo-over-ceiling: two TODO rows total count 2; a ceiling of 1 must fail.
        run_case("todo-over-ceiling", 1, todo_ceiling_override=1)
        # ...and a ceiling AT the real total must pass (boundary, not exclusive).
        run_case("todo-at-ceiling", 0, todo_ceiling_override=2)

        # The overrides refuse to parse without RR_SELF_TEST=1 -- _run_gate
        # always sets it when passing one, so drive the subprocess directly
        # here to prove the refusal itself.
        import subprocess
        env_without_flag = dict(os.environ)
        env_without_flag.pop("RR_SELF_TEST", None)
        completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--check",
             "--root", str(root), "--ledger", str(ledger), "--todo-ceiling-override", "1"],
            capture_output=True, text=True, env=env_without_flag,
        )
        if completed.returncode == 0 or "RR_SELF_TEST" not in (completed.stdout + completed.stderr):
            failures.append(
                "todo-ceiling-override-refused-outside-self-test: expected an argparse "
                f"error naming RR_SELF_TEST, got exit {completed.returncode}\n"
                f"{completed.stdout}{completed.stderr}"
            )

        # 6. bad-class / bad-kind / kind-prefix-extension: pure ledger-format
        # violations, checked directly against parse_ledger() (no real sites
        # needed -- these never reach reconcile()).
        def expect_parse_ledger_systemexit(name, ledger_text, minimum_rows=SELF_TEST_LEDGER_ROW_FLOOR):
            # minimum_rows=None probes the REAL (live MINIMUM_LEDGER_ROW_COUNT)
            # floor instead of the small synthetic-fixture one (direction 13's
            # boundary probes, below) -- every other caller keeps the small
            # default so these small (4-row) fixtures are judged on the
            # violation each direction actually targets, not incidentally
            # tripping the real, much larger real-ledger floor.
            try:
                if minimum_rows is None:
                    parse_ledger(ledger_text)
                else:
                    parse_ledger(ledger_text, minimum_rows=minimum_rows)
                failures.append(f"{name}: parse_ledger() did NOT raise SystemExit")
            except SystemExit:
                pass

        try:
            parse_ledger(SELF_TEST_LEDGER_OK, minimum_rows=SELF_TEST_LEDGER_ROW_FLOOR)
        except SystemExit as exc:
            failures.append(f"baseline-ledger-parses: SELF_TEST_LEDGER_OK raised SystemExit: {exc}")

        expect_parse_ledger_systemexit(
            "bad-class", SELF_TEST_LEDGER_OK.replace("| DECL |", "| BOGUS-CLASS |", 1))
        expect_parse_ledger_systemexit(
            "bad-kind", SELF_TEST_LEDGER_OK.replace("| entry-guard |", "| entry-guardish |", 1))
        expect_parse_ledger_systemexit(
            "kind-prefix-extension",
            SELF_TEST_LEDGER_OK.replace("| entry-guard |", "| entry-guard-extended |", 1))

        # 7. proof-too-short: a PROVEN row's proof text under MINIMUM_PROOF_TEXT_LENGTH.
        short_proof_row = PROVEN_ROW.replace(
            "guarded two lines above by `location_of(ch) != NOWHERE` — src/app/probe.cpp:11",
            "ok")
        expect_parse_ledger_systemexit(
            "proof-too-short", SELF_TEST_LEDGER_OK.replace(PROVEN_ROW, short_proof_row))

        # 8. citation-missing: a PROVEN/entry-guard row with no file:line in the proof.
        no_citation_row = PROVEN_ROW.replace(
            "guarded two lines above by `location_of(ch) != NOWHERE` — src/app/probe.cpp:11",
            "guarded by a runtime nil-check with no citation text at all here")
        expect_parse_ledger_systemexit(
            "citation-missing", SELF_TEST_LEDGER_OK.replace(PROVEN_ROW, no_citation_row))

        # review-1 F-1: PROVEN/GUARDED rows must carry a non-empty Kind AND
        # a non-empty Proof -- demonstrated exploit: a real TODO row
        # hand-edited to class PROVEN with BOTH Kind and Proof left at the
        # empty marker previously passed unchanged.
        proven_empty_kind_row = (
            f"| `src/app/probe.cpp · recalc_zone · room_of(` | 1 | PROVEN | "
            f"{LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |"
        )
        expect_parse_ledger_systemexit(
            "proven-empty-kind",
            SELF_TEST_LEDGER_OK.replace(TODO_ROW_RECALC_ZONE, proven_empty_kind_row))
        guarded_empty_proof_row = (
            f"| `src/app/probe.cpp · recalc_zone · room_of(` | 1 | GUARDED | "
            f"landed in wave rr1, red-first test RecalcZoneGuardTest | {LEDGER_EMPTY_MARKER} |"
        )
        expect_parse_ledger_systemexit(
            "guarded-empty-proof",
            SELF_TEST_LEDGER_OK.replace(TODO_ROW_RECALC_ZONE, guarded_empty_proof_row))

        # review-1 W-2: a ledger row's Count must be >= 1 -- a zero or
        # negative count can mask real sites while some OTHER row's count
        # keeps the per-key SUM exact (Opus's demonstrated evasion: a -500
        # TODO row paired with a +500 DECL row).
        negative_count_row = TODO_ROW_RECALC_ZONE.replace("` | 1 | TODO", "` | -1 | TODO")
        expect_parse_ledger_systemexit(
            "negative-count",
            SELF_TEST_LEDGER_OK.replace(TODO_ROW_RECALC_ZONE, negative_count_row))
        zero_count_row = TODO_ROW_RECALC_ZONE.replace("` | 1 | TODO", "` | 0 | TODO")
        expect_parse_ledger_systemexit(
            "zero-count",
            SELF_TEST_LEDGER_OK.replace(TODO_ROW_RECALC_ZONE, zero_count_row))

        # review-1 W-1(a): a DECL row's key_function must start with '#' --
        # a plain function name (no '#') keyed as DECL must fail even before
        # reconcile() ever compares it against a scanned site.
        expect_parse_ledger_systemexit(
            "decl-class-on-a-function-key",
            SELF_TEST_LEDGER_OK.replace(DECL_ROW, DECL_ROW.replace("#decl", "declared_thing")))

        # review-1 W-1(b): a RESOLVER-IMPL row outside the pinned
        # RESOLVER_IMPL_KEYS set must fail -- "src/app/probe.cpp"/"do_light"
        # is not, and never will be, a resolver-implementation function.
        resolver_impl_outside_pinned_row = TODO_ROW_DO_LIGHT.replace("TODO", "RESOLVER-IMPL")
        expect_parse_ledger_systemexit(
            "resolver-impl-outside-pinned-set",
            SELF_TEST_LEDGER_OK.replace(TODO_ROW_DO_LIGHT, resolver_impl_outside_pinned_row))

        # review-1 W-1(c): a NEW '.cpp' file's "#decl" key outside
        # PINNED_DECL_KEYS must fail -- a standalone 1-row fixture (not
        # SELF_TEST_LEDGER_OK, whose own probe.cpp/#decl/room_of( entry IS
        # one of the pinned self-test keys, see PINNED_DECL_KEYS's own
        # comment) so this direction actually exercises an UNPINNED file.
        new_cpp_decl_key_ledger = (
            f"{LEDGER_CLASSIFICATION_MARKER}\n"
            f"{LEDGER_CLASSIFICATION_HEADER}\n"
            "| --- | --- | --- | --- | --- |\n"
            f"| `src/app/notpinned.cpp · #decl · room_of(` | 1 | DECL | "
            f"{LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |\n"
            "\n"
            f"{LEDGER_TOKEN_COUNTS_MARKER}\n"
            f"{LEDGER_TOKEN_COUNTS_HEADER}\n"
            "| --- | --- |\n"
        )
        expect_parse_ledger_systemexit(
            "new-cpp-decl-key", new_cpp_decl_key_ledger, minimum_rows=1)

        # 9. test-fixture-outside-tests: a TEST-FIXTURE row for a src/app/ key.
        expect_parse_ledger_systemexit(
            "test-fixture-outside-tests",
            SELF_TEST_LEDGER_OK.replace("| DECL |", "| TEST-FIXTURE |", 1))
        # ...and the path legality holds under a symlinked --root (F-14/PR-#23):
        # the clean pair must still pass end to end when accessed through a
        # symlink, proving the check is computed on the resolved tree.
        symlink_root = root.parent / "rr1-symlink-root"
        symlink_root.symlink_to(root)
        try:
            exit_code, output = _run_gate(symlink_root, ledger)
            if exit_code != 0:
                failures.append(f"symlinked --root broke an otherwise-clean gate run\n{output}")
        finally:
            symlink_root.unlink()

        # 10. decl-outside-pinned-set: the file-scope declaration's site is
        # mis-keyed under a plain function name instead of "#decl" -- the
        # scanned key and the ledger row key no longer match at all, so the
        # site is unclassified AND the row is stale.
        run_case(
            "decl-outside-pinned-set", 1,
            ledger_text=SELF_TEST_LEDGER_OK.replace(
                "src/app/probe.cpp · #decl · room_of(", "src/app/probe.cpp · room_of · room_of("),
        )

        # 11. macro-family-closed-world: a new header defines a resolver-
        # reaching macro with no MACRO_FAMILY entry.
        #
        # review-1 F-3 FIX: the macro's own #define BODY line is ITSELF a
        # room_of( site (attributed to macro "SNEAKY", keying "#SNEAKY") --
        # with no ledger row for that key, the ORIGINAL fixture ALWAYS also
        # produced an unclassified-site error whose OWN text happens to
        # contain the substring "SNEAKY" (the key itself), so the original
        # assertion (`"SNEAKY" not in output`) passed for the WRONG reason:
        # deleting the closed-world check entirely left this direction
        # passing unchanged (verified below by scratch-copy sabotage). Fixed
        # by giving the fixture ledger a `#SNEAKY` DECL row (closing the
        # unclassified-site path) and bumping the `room_of(` token-counts
        # row by 1 (closing the token-total cross-check path too) -- the
        # closed-world violation becomes the ONLY possible error, and the
        # assertion checks for ITS OWN text ("MACRO_FAMILY"/"closed-world"),
        # never the macro's name.
        sneaky_header = app_dir / "sneaky.h"
        sneaky_header.write_text(
            "#define SNEAKY(ch) (room_of(ch)->number)\n", encoding="utf-8")
        sneaky_decl_row = (
            f"| `src/app/sneaky.h · #SNEAKY · room_of(` | 1 | DECL | "
            f"{LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |"
        )
        sneaky_ledger_text = SELF_TEST_LEDGER_OK.replace(
            TODO_ROW_RECALC_ZONE, TODO_ROW_RECALC_ZONE + "\n" + sneaky_decl_row
        ).replace("| `room_of(` | 2 |", "| `room_of(` | 3 |")
        ledger.write_text(sneaky_ledger_text, encoding="utf-8")
        try:
            exit_code, output = _run_gate(root, ledger)
            if exit_code == 0 or not any(marker in output for marker in ("MACRO_FAMILY", "closed-world")):
                failures.append(
                    "macro-family-closed-world: expected a closed-world failure naming "
                    f"MACRO_FAMILY/closed-world, got exit {exit_code}\n{output}"
                )
            if "unclassified site" in output:
                failures.append(
                    "macro-family-closed-world: the #SNEAKY DECL row did not neutralize "
                    f"the unclassified-site path -- the fixture is not isolating the "
                    f"closed-world check\n{output}"
                )
        finally:
            sneaky_header.unlink()
            ledger.write_text(SELF_TEST_LEDGER_OK, encoding="utf-8")

        # review-1 W-3/F-4: the ## preprocessor paste operator is now a
        # tracked (17th) static token -- a paste-assembled identifier in a
        # PRODUCTION file (not masked by a comment/string, and not inside a
        # #define body the macro_lines pass already skips) must surface as
        # an ordinary unclassified site, exactly like any other untracked
        # room_of(/world[ call would.
        paste_probe = app_dir / "paste_probe.cpp"
        paste_probe.write_text(
            "void paste_fn(char_data* ch)\n{\n    int a##b = 0;\n}\n", encoding="utf-8")
        try:
            exit_code, output = _run_gate(root, ledger)
            if exit_code == 0 or "##" not in output or "unclassified site" not in output:
                failures.append(
                    "token-paste-evasion: expected an unclassified-site failure naming "
                    f"'##', got exit {exit_code}\n{output}"
                )
        finally:
            paste_probe.unlink()

        # 12. marker sabotage (both markers; the M10 copies).
        expect_parse_ledger_systemexit(
            "marker-missing-classification",
            SELF_TEST_LEDGER_OK.replace(LEDGER_CLASSIFICATION_MARKER, ""))
        expect_parse_ledger_systemexit(
            "marker-duplicate-classification",
            SELF_TEST_LEDGER_OK.replace(
                LEDGER_CLASSIFICATION_MARKER,
                LEDGER_CLASSIFICATION_MARKER + "\n" + LEDGER_CLASSIFICATION_MARKER, 1))
        expect_parse_ledger_systemexit(
            "marker-missing-token-counts",
            SELF_TEST_LEDGER_OK.replace(LEDGER_TOKEN_COUNTS_MARKER, ""))
        expect_parse_ledger_systemexit(
            "wrong-header-classification",
            SELF_TEST_LEDGER_OK.replace(LEDGER_CLASSIFICATION_HEADER, "| Wrong | Header |"))
        # A marker+table INSIDE a fence is inert -- the real table (unfenced)
        # still parses correctly, and the fenced decoy row is never seen.
        try:
            fenced_rows, _ = parse_ledger(
                SELF_TEST_LEDGER_FENCED_DECOY, minimum_rows=SELF_TEST_LEDGER_ROW_FLOOR)
            if any(row["key_file"] == "src/decoy.cpp" for row in fenced_rows):
                failures.append("marker-inside-fence: the fenced decoy row was NOT inert")
            if len(fenced_rows) != 4:
                failures.append(
                    f"marker-inside-fence: expected 4 real rows, parsed {len(fenced_rows)}"
                )
        except SystemExit as exc:
            failures.append(f"marker-inside-fence: the real (unfenced) table failed to parse: {exc}")

        # 13. floor sabotage: the ledger-row floor (direct parse_ledger call).
        # Built PROGRAMMATICALLY off MINIMUM_LEDGER_ROW_COUNT itself (not by
        # dropping a row from the fixed 4-row SELF_TEST_LEDGER_OK fixture),
        # the same symbolic discipline MINIMUM_SCANNED_FILE_COUNT's own
        # boundary probe uses -- so Task 3 raising this literal keeps this
        # probe exercising the real edge rather than a value that was only
        # ever true for THIS commit's specific fixture size.
        def _synthetic_classification_ledger(row_count):
            lines = [LEDGER_CLASSIFICATION_MARKER, LEDGER_CLASSIFICATION_HEADER,
                     "| --- | --- | --- | --- | --- |"]
            for index in range(row_count):
                lines.append(
                    f"| `src/synthetic{index}.cpp · fn{index} · world[` | 1 | TODO | "
                    f"{LEDGER_EMPTY_MARKER} | {LEDGER_EMPTY_MARKER} |"
                )
            lines += ["", LEDGER_TOKEN_COUNTS_MARKER, LEDGER_TOKEN_COUNTS_HEADER, "| --- | --- |"]
            return "\n".join(lines) + "\n"

        expect_parse_ledger_systemexit(
            "ledger-row-floor-below",
            _synthetic_classification_ledger(MINIMUM_LEDGER_ROW_COUNT - 1),
            minimum_rows=None)  # probe the REAL floor, not the small fixture default
        try:
            parse_ledger(_synthetic_classification_ledger(MINIMUM_LEDGER_ROW_COUNT))
        except SystemExit as exc:
            failures.append(
                f"ledger-row-floor-at-boundary: exactly {MINIMUM_LEDGER_ROW_COUNT} rows "
                f"(AT the floor, a minimum not an exclusive bound) raised SystemExit: {exc}"
            )
        # ...and the scanned-file floor, via --minimum-files-override so the
        # boundary can be probed cheaply rather than by padding hundreds of
        # files per sub-case. A SEPARATE synthetic --root (not the shared
        # `root`/`ledger` fixture) whose own src/app/probe.cpp mirrors the
        # real layout exactly, so the ledger's "src/app/probe.cpp"-keyed rows
        # still reconcile once the floor itself passes.
        floor_root = root / "floor_root"
        floor_app_dir = floor_root / "src" / "app"
        floor_app_dir.mkdir(parents=True)
        (floor_app_dir / "probe.cpp").write_text(PROBE_SOURCE_OK, encoding="utf-8")
        for index in range(3):
            (floor_app_dir / f"pad{index}.cpp").write_text("int clean = 0;\n", encoding="utf-8")
        # 4 files total (probe + 3 pad): one BELOW an override of 5.
        exit_code, output = _run_gate(floor_root, ledger, minimum_files_override=5)
        if exit_code == 0:
            failures.append(f"scanned-file floor did NOT fire one file below the override\n{output}")
        (floor_app_dir / "pad3.cpp").write_text("int clean = 0;\n", encoding="utf-8")
        # 5 files total: AT the override -- must pass (a minimum, not an
        # exclusive bound), and the ledger reconciles exactly against probe.cpp.
        exit_code, output = _run_gate(floor_root, ledger, minimum_files_override=5)
        if exit_code != 0:
            failures.append(f"scanned-file floor fired AT the override (must be inclusive)\n{output}")

        # 14. token-counts-table: the declared table disagrees with the scan.
        run_case(
            "token-counts-table", 1,
            ledger_text=SELF_TEST_LEDGER_OK.replace("| `world[` | 1 |", "| `world[` | 2 |"),
        )

        # 15. masking: comment/string-literal tokens are not counted; a
        # `//`-trailed real site still is (an otherwise-clean pair -> exit 0).
        run_case("masking", 0, probe_text=PROBE_SOURCE_MASKING)

        # 16. attribution directions: ACMD keys as its bare name, a macro body
        # keys as "#NAME", and a multi-line call still hits (both ATTRIBUTION_
        # PROBE_SOURCE's do_glance body and PROBE_SOURCE_OK's recalc_zone
        # already exercise the multi-line-call shape used throughout this
        # self-test). A one-time real-tree grep confirmed the genuinely SPLIT
        # shape (callee name and '(' on different lines) does not exist in
        # production (see the module docstring's token-shape note).
        # A separate synthetic --root (same reasoning as floor_root above):
        # the ledger's keys are "src/app/attrib.cpp"-relative, so the file
        # must actually sit at <root>/src/app/attrib.cpp under whichever
        # directory is passed as --root.
        attrib_root = root / "attrib_root"
        attrib_app_dir = attrib_root / "src" / "app"
        attrib_app_dir.mkdir(parents=True)
        (attrib_app_dir / "attrib.cpp").write_text(ATTRIBUTION_PROBE_SOURCE, encoding="utf-8")
        attrib_ledger = root / "attrib_ledger.md"
        attrib_ledger.write_text(ATTRIBUTION_LEDGER, encoding="utf-8")
        exit_code, output = _run_gate(attrib_root, attrib_ledger, minimum_files_override=1)
        if exit_code != 0:
            failures.append(f"attribution-directions: expected exit 0, got {exit_code}\n{output}")

        # 17. ctest-invocation-shape: an explicit --ledger naming the exact
        # default-relative location must behave identically to omitting
        # --ledger (the W-1 direction).
        default_ledger_dir = root / "docs" / "superpowers"
        default_ledger_dir.mkdir(parents=True)
        default_ledger = default_ledger_dir / "room-resolve-ledger.md"
        default_ledger.write_text(SELF_TEST_LEDGER_OK, encoding="utf-8")
        exit_explicit, output_explicit = _run_gate(root, default_ledger)
        exit_default, output_default = _run_gate(root, None)
        if exit_explicit != 0 or exit_default != 0:
            failures.append(
                "ctest-invocation-shape: explicit-default-path and omitted --ledger disagreed "
                f"on a clean pair (explicit={exit_explicit}, default={exit_default})\n"
                f"{output_explicit}\n{output_default}"
            )
        default_ledger.write_text(
            SELF_TEST_LEDGER_OK.replace("| `world[` | 1 |", "| `world[` | 2 |"), encoding="utf-8")
        exit_explicit, _ = _run_gate(root, default_ledger)
        exit_default, _ = _run_gate(root, None)
        if exit_explicit == 0 or exit_default == 0 or exit_explicit != exit_default:
            failures.append(
                "ctest-invocation-shape: explicit-default-path and omitted --ledger disagreed "
                f"on a sabotaged pair (explicit={exit_explicit}, default={exit_default})"
            )

        # Review-1 IMPORTANT finding 2: the positional `paths` argument and
        # _resolve_scan_targets()'s multi-path/single-file branches had zero
        # self-test coverage -- the reviewer drove them independently and
        # found the logic sound, but an unverified code path is exactly the
        # wave-coverage-gap class this repository's own conventions flag.
        # At this point in the run, app_dir holds exactly one file
        # (probe.cpp, PROBE_SOURCE_OK) -- reset() ran after "masking" (the
        # last run_case call before direction 17) and nothing since has
        # touched app_dir, so "[scanned] 1 file(s)" is the real expected count.

        # (a) a positional RELATIVE directory path -- exercises the
        # not-absolute branch and the `source_files(resolved)` (non-file)
        # branch, and narrows the scan enough that the notice must show 1.
        exit_code, output = _run_gate(
            root, ledger, pathlib.Path("src/app"), minimum_files_override=1)
        if exit_code != 0:
            failures.append(
                f"positional-relative-directory-path: expected exit 0, got {exit_code}\n{output}"
            )
        if "[scanned] 1 file(s)" not in output:
            failures.append(
                "positional-relative-directory-path: expected the scanned-file notice to "
                f"reflect the narrowed scope (1 file), got:\n{output}"
            )

        # (b) a positional SINGLE-FILE path, also relative -- exercises both
        # the not-absolute branch AND resolved.is_file() in one case.
        exit_code, output = _run_gate(
            root, ledger, pathlib.Path("src/app/probe.cpp"), minimum_files_override=1)
        if exit_code != 0:
            failures.append(
                f"positional-single-file-path: expected exit 0, got {exit_code}\n{output}"
            )
        if "[scanned] 1 file(s)" not in output:
            failures.append(
                "positional-single-file-path: expected the scanned-file notice to reflect "
                f"the narrowed scope (1 file), got:\n{output}"
            )

        # (c) MULTIPLE positional paths -- exercises _resolve_scan_targets'
        # own for-loop actually iterating more than once. src/app (the one
        # real, reconciling file) plus src/pad (all clean pad files, zero
        # sites) together clear the real MINIMUM_SCANNED_FILE_COUNT floor
        # with no override needed.
        import subprocess
        multi_path_completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--check",
             "--root", str(root), "--ledger", str(ledger), "src/app", "src/pad",
             "--minimum-ledger-rows-override", str(SELF_TEST_LEDGER_ROW_FLOOR),
             "--minimum-dispatch-entries-override", "0",
             "--caller-count-pins-override", "",
             "--dispatch-exempt-sites-override", ""],
            capture_output=True, text=True,
            env={**os.environ, "RR_SELF_TEST": "1"},
        )
        if multi_path_completed.returncode != 0:
            failures.append(
                "positional-multiple-paths: expected exit 0, got "
                f"{multi_path_completed.returncode}\n"
                f"{multi_path_completed.stdout}{multi_path_completed.stderr}"
            )

        # (d) --derive-macros silently ignored positional paths before this
        # review; now it must be an explicit argparse error.
        derive_macros_completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--derive-macros",
             "--root", str(root), "src/app"],
            capture_output=True, text=True,
        )
        if derive_macros_completed.returncode == 0 or "positional paths are not supported" not in (
                derive_macros_completed.stdout + derive_macros_completed.stderr):
            failures.append(
                "derive-macros-rejects-positional-paths: expected an argparse error naming "
                f"'positional paths are not supported', got exit "
                f"{derive_macros_completed.returncode}\n"
                f"{derive_macros_completed.stdout}{derive_macros_completed.stderr}"
            )

        # 18. generate-ledger-round-trip (Task 3): --generate-ledger against a
        # fresh synthetic tree must produce a file parse_ledger() accepts,
        # AND that same file must make --check pass end to end (the
        # generator's mechanical TODO/TEST-FIXTURE/DECL classification is
        # exactly what the real scan's key set needs -- no hand-pinning is
        # needed for a freshly generated, entirely-mechanical ledger).
        generate_root = root / "generate_root"
        generate_app_dir = generate_root / "src" / "app"
        generate_app_dir.mkdir(parents=True)
        (generate_app_dir / "probe.cpp").write_text(PROBE_SOURCE_OK, encoding="utf-8")
        generate_pad_dir = generate_root / "src" / "pad"
        generate_pad_dir.mkdir(parents=True)
        for index in range(3):
            (generate_pad_dir / f"pad{index}.cpp").write_text("int clean = 0;\n", encoding="utf-8")
        generated_ledger = generate_root / "generated_ledger.md"
        generate_completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--generate-ledger",
             "--root", str(generate_root), "--ledger", str(generated_ledger),
             "--minimum-files-override", "4"],
            capture_output=True, text=True, env={**os.environ, "RR_SELF_TEST": "1"},
        )
        if generate_completed.returncode != 0:
            failures.append(
                "generate-ledger-round-trip: --generate-ledger itself failed\n"
                f"{generate_completed.stdout}{generate_completed.stderr}"
            )
        else:
            try:
                parse_ledger(generated_ledger.read_text(encoding="utf-8"),
                             minimum_rows=SELF_TEST_LEDGER_ROW_FLOOR)
            except SystemExit as exc:
                failures.append(
                    f"generate-ledger-round-trip: parse_ledger() rejected the generated file: {exc}"
                )
            check_completed = subprocess.run(
                [sys.executable, str(pathlib.Path(__file__).resolve()), "--check",
                 "--root", str(generate_root), "--ledger", str(generated_ledger),
                 "--minimum-files-override", "4",
                 "--minimum-ledger-rows-override", str(SELF_TEST_LEDGER_ROW_FLOOR),
                 "--minimum-dispatch-entries-override", "0",
                 "--caller-count-pins-override", "",
                 "--dispatch-exempt-sites-override", ""],
                capture_output=True, text=True, env={**os.environ, "RR_SELF_TEST": "1"},
            )
            if check_completed.returncode != 0:
                failures.append(
                    "generate-ledger-round-trip: --check against the freshly generated ledger "
                    f"did not pass\n{check_completed.stdout}{check_completed.stderr}"
                )

        # 19. generate-ledger-refuses-without-force: a second --generate-ledger
        # against the same (now-existing) ledger path must refuse; the SAME
        # invocation with --force-regenerate must succeed (and overwrite).
        refuse_completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--generate-ledger",
             "--root", str(generate_root), "--ledger", str(generated_ledger),
             "--minimum-files-override", "4"],
            capture_output=True, text=True, env={**os.environ, "RR_SELF_TEST": "1"},
        )
        if refuse_completed.returncode == 0 or "--force-regenerate" not in (
                refuse_completed.stdout + refuse_completed.stderr):
            failures.append(
                "generate-ledger-refuses-without-force: expected a refusal naming "
                f"--force-regenerate, got exit {refuse_completed.returncode}\n"
                f"{refuse_completed.stdout}{refuse_completed.stderr}"
            )
        force_completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--generate-ledger",
             "--force-regenerate",
             "--root", str(generate_root), "--ledger", str(generated_ledger),
             "--minimum-files-override", "4"],
            capture_output=True, text=True, env={**os.environ, "RR_SELF_TEST": "1"},
        )
        if force_completed.returncode != 0:
            failures.append(
                "generate-ledger-refuses-without-force: --force-regenerate did not succeed\n"
                f"{force_completed.stdout}{force_completed.stderr}"
            )

        # 20. advise-emits-suggestions-writes-nothing: a synthetic function
        # guarded by a `location_of(ch) != NOWHERE` entry check, immediately
        # followed by a resolver-token call, must produce an entry-guard?
        # suggestion on stdout -- and --advise must create/modify NOTHING
        # (no ledger involved at all; the probe directory's own file listing
        # must be byte-for-byte unchanged).
        advise_root = root / "advise_root"
        advise_app_dir = advise_root / "src" / "app"
        advise_app_dir.mkdir(parents=True)
        advise_probe = advise_app_dir / "advise_probe.cpp"
        advise_probe.write_text(
            "void guarded_fn(char_data* ch)\n{\n"
            "    if (location_of(ch) != NOWHERE)\n    {\n"
            "        room_data* r = room_of(ch);\n    }\n}\n",
            encoding="utf-8",
        )
        before_listing = sorted(p.name for p in advise_app_dir.iterdir())
        before_bytes = advise_probe.read_bytes()
        advise_completed = subprocess.run(
            [sys.executable, str(pathlib.Path(__file__).resolve()), "--advise",
             "--root", str(advise_root), "src/app"],
            capture_output=True, text=True,
        )
        if advise_completed.returncode != 0 or "entry-guard?" not in advise_completed.stdout:
            failures.append(
                "advise-emits-suggestions-writes-nothing: expected exit 0 with an entry-guard? "
                f"suggestion, got exit {advise_completed.returncode}\n"
                f"{advise_completed.stdout}{advise_completed.stderr}"
            )
        after_listing = sorted(p.name for p in advise_app_dir.iterdir())
        after_bytes = advise_probe.read_bytes()
        if before_listing != after_listing or before_bytes != after_bytes:
            failures.append(
                "advise-emits-suggestions-writes-nothing: --advise modified the filesystem "
                f"(listing {before_listing} -> {after_listing})"
            )

        # ------------------------------------------------------------------
        # RR Wave R3 Task 1a, direction 21: the `dispatch-invariant` proof
        # kind's own vocabulary pins. Cheap, in-process, and the thing a
        # future edit is most likely to undo by accident.
        # ------------------------------------------------------------------
        if "dispatch-invariant" not in PROOF_KINDS:
            failures.append(
                "PROOF_KINDS no longer contains 'dispatch-invariant' -- owner ruling R3-O-1 "
                "mints it; removing it silently invalidates every R3 row that cites it."
            )
        if "dispatch-invariant" not in CITATION_REQUIRED_KINDS:
            failures.append(
                "CITATION_REQUIRED_KINDS no longer contains 'dispatch-invariant' -- its FIRST "
                "mandatory citation part IS a file:line (the registry entry), so a row without "
                "one cites nothing."
            )
        dispatch_kind_uncited_row = (
            f"| `src/app/probe.cpp · recalc_zone · room_of(` | 1 | PROVEN | dispatch-invariant | "
            "actor ch arrives through the command dispatcher and is therefore placed |"
        )
        expect_parse_ledger_systemexit(
            "dispatch-invariant-requires-a-citation",
            SELF_TEST_LEDGER_OK.replace(TODO_ROW_RECALC_ZONE, dispatch_kind_uncited_row))
        dispatch_kind_cited_row = (
            f"| `src/app/probe.cpp · recalc_zone · room_of(` | 1 | PROVEN | dispatch-invariant | "
            "actor `ch`, guarded at the registered entry src/app/probe.cpp:11; sole direct "
            "caller is the dispatcher itself |"
        )
        try:
            parse_ledger(
                SELF_TEST_LEDGER_OK.replace(TODO_ROW_RECALC_ZONE, dispatch_kind_cited_row),
                minimum_rows=SELF_TEST_LEDGER_ROW_FLOOR)
        except SystemExit as exc:
            failures.append(
                f"dispatch-invariant-accepts-a-cited-proof: parse_ledger() rejected a "
                f"well-formed dispatch-invariant row: {exc}"
            )

        # ------------------------------------------------------------------
        # RR Wave R3 Task 1a, direction 22: the dispatch-entry registry, driven
        # END TO END through the real gate (its own synthetic --root, the
        # floor_root/attrib_root pattern). Every sub-direction below is a
        # SABOTAGE direction: the fixture is clean first, then broken one way
        # at a time, and the clean case is re-asserted by construction (each
        # sub-case rewrites both files from the constants).
        # ------------------------------------------------------------------
        dispatch_root = root / "dispatch_root"
        dispatch_app_dir = dispatch_root / "src" / "app"
        dispatch_app_dir.mkdir(parents=True)
        (dispatch_app_dir / "probe.cpp").write_text(PROBE_SOURCE_OK, encoding="utf-8")
        dispatch_probe = dispatch_app_dir / "dispatch_probe.cpp"
        dispatch_ledger = root / "dispatch_ledger.md"

        def run_dispatch_case(name, expected_exit, *, probe_text=DISPATCH_PROBE_SOURCE,
                              ledger_text=None, expected_output=(), forbidden_output=(),
                              minimum_dispatch_entries_override=0,
                              dispatch_exempt_sites_override=""):
            dispatch_probe.write_text(probe_text, encoding="utf-8")
            dispatch_ledger.write_text(
                ledger_text if ledger_text is not None
                else _dispatch_ledger(DISPATCH_ENTRY_ROW_GUARDED),
                encoding="utf-8")
            exit_code, output = _run_gate(
                dispatch_root, dispatch_ledger, minimum_files_override=2,
                minimum_dispatch_entries_override=minimum_dispatch_entries_override,
                dispatch_exempt_sites_override=dispatch_exempt_sites_override)
            if expected_exit == 0 and exit_code != 0:
                failures.append(f"{name}: expected gate exit 0, got {exit_code}\n{output}")
            if expected_exit != 0 and exit_code == 0:
                failures.append(f"{name}: expected a NON-zero gate exit, got 0\n{output}")
            for needle in expected_output:
                if needle not in output:
                    failures.append(f"{name}: expected {needle!r} in the gate output\n{output}")
            for needle in forbidden_output:
                if needle in output:
                    failures.append(
                        f"{name}: did NOT expect {needle!r} in the gate output\n{output}")

        # (a) clean: a GUARDED entry whose guard literal really is in the body,
        # and whose dispatch token really is inside that registered function.
        run_dispatch_case("dispatch-clean", 0, forbidden_output=("PENDING-T1b",))

        # (b) SABOTAGE: the guard literal is deleted from production while the
        # registry row still claims GUARDED -- the proof-rot direction.
        run_dispatch_case(
            "dispatch-guard-literal-deleted", 1,
            probe_text=DISPATCH_PROBE_SOURCE_GUARD_DELETED,
            expected_output=("guard literal",))

        # (c) SABOTAGE: the entry row is removed while its dispatch token
        # remains in production -- the row-deletion direction.
        run_dispatch_case(
            "dispatch-entry-row-removed", 1,
            ledger_text=SELF_TEST_LEDGER_OK,
            expected_output=("unregistered dispatch site", "dispatcher_fn"))

        # (d) SABOTAGE: a NEW `.spell_pointer(` call appears in a function
        # nobody registered -- the eighth-dispatcher direction, the whole
        # reason the closed world exists.
        run_dispatch_case(
            "dispatch-new-unregistered-dispatcher", 1,
            probe_text=DISPATCH_PROBE_SOURCE_SECOND_DISPATCHER,
            expected_output=("unregistered dispatch site", "sneaky_new_dispatcher"))

        # (e)/(f) SABOTAGE: the M10 marker rules -- duplicated, and the whole
        # table buried inside a fence (leaving zero LIVE markers).
        run_dispatch_case(
            "dispatch-marker-duplicated", 1,
            ledger_text=_dispatch_ledger(DISPATCH_ENTRY_ROW_GUARDED).replace(
                LEDGER_DISPATCH_ENTRIES_MARKER,
                LEDGER_DISPATCH_ENTRIES_MARKER + "\n" + LEDGER_DISPATCH_ENTRIES_MARKER, 1),
            expected_output=("must appear exactly once",))
        # The fenced variant strips the ONE live table out of the fixture and
        # re-adds it inside a fence, so the live marker count is 0 -- a
        # dispatch registry that exists only as documentation must not be
        # mistaken for the real thing.
        fenced_dispatch_ledger = (
            SELF_TEST_LEDGER_OK.replace(EMPTY_DISPATCH_TABLE, "\n", 1)
            + "\n```\n"
            + LEDGER_DISPATCH_ENTRIES_MARKER + "\n"
            + LEDGER_DISPATCH_ENTRIES_HEADER + "\n"
            + "| --- | --- | --- | --- |\n"
            + DISPATCH_ENTRY_ROW_GUARDED + "\n"
            + "```\n"
        )
        run_dispatch_case(
            "dispatch-marker-fenced", 1,
            ledger_text=fenced_dispatch_ledger,
            expected_output=("must appear exactly once",))

        # (g) Task 5-fix (review-2 B-1 / review-1 B-2): `PENDING-T1b` is
        # RETIRED. It used to pass with a warning, which is exactly how both
        # reviewers walked an entirely unguarded dispatcher through the gate:
        # the status satisfied the UPWARD closed-world check while being
        # exempt from the DOWNWARD literal one. It is now an unknown status.
        run_dispatch_case(
            "dispatch-pending-status-retired", 1,
            probe_text=DISPATCH_PROBE_SOURCE_SECOND_DISPATCHER,
            ledger_text=_dispatch_ledger(
                DISPATCH_ENTRY_ROW_GUARDED,
                "| `src/app/dispatch_probe.cpp · sneaky_new_dispatcher` | `ch` | "
                f"{LEDGER_EMPTY_MARKER} | PENDING-T1b |"),
            expected_output=("unknown dispatch-entry status", "PENDING-T1b"))

        # (h) ... and with the status word gone, a registry row can no longer
        # leave its guard cell at the empty marker under ANY status: the cell
        # grammar rejects it. This is the "registered but unguarded" shape.
        run_dispatch_case(
            "dispatch-entry-with-an-empty-guard-cell", 1,
            ledger_text=_dispatch_ledger(
                "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
                f"{LEDGER_EMPTY_MARKER} | GUARDED |"),
            expected_output=("must carry one or more",))

        # (i) the status vocabulary is closed.
        run_dispatch_case(
            "dispatch-unknown-status", 1,
            ledger_text=_dispatch_ledger(
                DISPATCH_ENTRY_ROW_GUARDED.replace("| GUARDED |", "| GUARDED-ISH |")),
            expected_output=("unknown dispatch-entry status",))

        # (j) a registry row naming a function that does not exist fails
        # closed, even with a perfectly well-formed guard literal.
        run_dispatch_case(
            "dispatch-entry-names-a-missing-function", 1,
            ledger_text=_dispatch_ledger(
                DISPATCH_ENTRY_ROW_GUARDED.replace("dispatcher_fn", "no_such_function"),
                DISPATCH_ENTRY_ROW_GUARDED),
            expected_output=("cannot find",))

        # (k) the registry's own row-count floor: one row, floor of two.
        run_dispatch_case(
            "dispatch-entry-row-floor", 1,
            minimum_dispatch_entries_override=2,
            expected_output=("below the floor",))

        # ------------------------------------------------------------------
        # RR Wave R3 Task 1d (R3-C-7): the MULTI-LITERAL guard cell. An entry
        # with two tripwires is only as strong as its weakest one, so the
        # downward check must demand EVERY listed literal, and the cell's
        # grammar must fail closed on a malformed separator rather than
        # silently dropping half of it.
        # ------------------------------------------------------------------

        # (l) clean: BOTH literals really are in the body.
        run_dispatch_case(
            "dispatch-two-guards-both-present", 0,
            probe_text=DISPATCH_PROBE_SOURCE_TWO_GUARDS,
            ledger_text=_dispatch_ledger(DISPATCH_ENTRY_ROW_TWO_GUARDS))

        # (m) SABOTAGE: the SECOND literal (the pre-dispatch tripwire) is gone
        # from production while the first survives. This is the exact
        # regression R3-C-7 exists to catch -- an entry guard left standing
        # while the adjacent one is deleted restores the relocation hole -- and
        # a one-of-two check would pass it.
        run_dispatch_case(
            "dispatch-two-guards-second-deleted", 1,
            probe_text=DISPATCH_PROBE_SOURCE,
            ledger_text=_dispatch_ledger(DISPATCH_ENTRY_ROW_TWO_GUARDS),
            expected_output=("guard literal", "} else {"))

        # (n) SABOTAGE: the FIRST literal is gone and only the pre-dispatch one
        # survives -- the mirror direction, so neither position is privileged.
        run_dispatch_case(
            "dispatch-two-guards-first-deleted", 1,
            probe_text=DISPATCH_PROBE_SOURCE_TWO_GUARDS.replace(
                "    if (location_of(ch) == NOWHERE) {\n        return;\n    }\n\n", "", 1),
            ledger_text=_dispatch_ledger(DISPATCH_ENTRY_ROW_TWO_GUARDS),
            expected_output=("guard literal", "{ return; }"))

        # (o) SABOTAGE: a stray trailing separator with no literal after it.
        run_dispatch_case(
            "dispatch-guard-cell-stray-separator", 1,
            probe_text=DISPATCH_PROBE_SOURCE_TWO_GUARDS,
            ledger_text=_dispatch_ledger(
                "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
                "`if (location_of(ch) == NOWHERE) { return; }` \\|\\| | GUARDED |"),
            expected_output=("separated by",))

        # (p) SABOTAGE: an EMPTY second literal (``) -- grammatically a literal,
        # but one that would match every body ever written.
        run_dispatch_case(
            "dispatch-guard-cell-empty-literal", 1,
            probe_text=DISPATCH_PROBE_SOURCE_TWO_GUARDS,
            ledger_text=_dispatch_ledger(
                "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
                "`if (location_of(ch) == NOWHERE) { return; }` \\|\\| `` | GUARDED |"),
            expected_output=("EMPTY guard literal",))

        # (q) SABOTAGE: a separator without its surrounding spaces. The cell
        # grammar is exact; a `A`||`B` cell is a typo, not a two-literal row
        # the checker may guess at.
        run_dispatch_case(
            "dispatch-guard-cell-unspaced-separator", 1,
            probe_text=DISPATCH_PROBE_SOURCE_TWO_GUARDS,
            ledger_text=_dispatch_ledger(
                "| `src/app/dispatch_probe.cpp · dispatcher_fn` | `ch` | "
                "`if (location_of(ch) == NOWHERE) { return; }`\\|\\|"
                "`if (location_of(ch) == NOWHERE) { mudlog( , NRM, LEVEL_IMPL, TRUE); } else {`"
                " | GUARDED |"),
            expected_output=("separated by",))

        # (r) a SINGLE literal containing a real C `||` operator still parses
        # as one literal and passes -- the `one_mobile_activity` shape, and the
        # regression a bare `split(" || ")` implementation would introduce.
        run_dispatch_case(
            "dispatch-guard-cell-disjunctive-single-literal", 0,
            probe_text=DISPATCH_PROBE_SOURCE_DISJUNCTIVE_GUARD,
            ledger_text=_dispatch_ledger(DISPATCH_ENTRY_ROW_DISJUNCTIVE_GUARD))

        # (s) SABOTAGE, and the direction that makes (r) worth having: that
        # same disjunctive guard rewritten in production into two separate,
        # weaker tests. Both halves a naive `split(" || ")` would produce are
        # still substrings of the body, so a split-then-match checker passes
        # this happily; the whole-literal checker fails it, which is the
        # correct answer -- the named guard is gone.
        run_dispatch_case(
            "dispatch-guard-cell-disjunctive-torn-apart", 1,
            probe_text=DISPATCH_PROBE_SOURCE_DISJUNCTIVE_GUARD_TORN,
            ledger_text=_dispatch_ledger(DISPATCH_ENTRY_ROW_DISJUNCTIVE_GUARD),
            expected_output=("guard literal",))

        # ------------------------------------------------------------------
        # RR Wave R3 Task 5-fix, direction 24: the two whole-branch reviews'
        # demonstrated evasions of the UPWARD closed-world check. Each
        # sub-direction below is a spelling of the SAME unregistered
        # dispatcher; before this round every one of them exited 0.
        # ------------------------------------------------------------------

        # (t) review-1 B-1: a ONE-LINE function definition. Its whole body
        # sits on a line attributed to FILE SCOPE, which both R3 checks used
        # to skip outright.
        run_dispatch_case(
            "dispatch-one-line-definition-is-an-error", 1,
            probe_text=DISPATCH_PROBE_SOURCE_ONE_LINE_DEFINITION,
            expected_output=("file-scope dispatch spelling", "command_pointer)("))

        # (u) ... and the direction that keeps (t) from being a blanket ban on
        # file scope: the REAL declarations, wrapped definition headers,
        # struct-member fn-ptrs and brace-init definitions must still pass.
        run_dispatch_case(
            "dispatch-file-scope-declarations-still-pass", 0,
            probe_text=DISPATCH_PROBE_SOURCE_FILE_SCOPE_DECLARATIONS)

        # (v)-(y) review-2 B-2 / review-1 B-3: four ordinary re-spellings of a
        # fn-ptr dispatch that the original seven tokens did not cover.
        run_dispatch_case(
            "dispatch-arrow-member-call", 1,
            probe_text=DISPATCH_PROBE_SOURCE_ARROW_MEMBER_CALL,
            expected_output=("unregistered dispatch site", "->spell_pointer("))
        run_dispatch_case(
            "dispatch-hoisted-fn-ptr-copy", 1,
            probe_text=DISPATCH_PROBE_SOURCE_HOISTED_COPY,
            expected_output=("unregistered dispatch site", "rogue_hoist"))
        run_dispatch_case(
            "dispatch-table-alias", 1,
            probe_text=DISPATCH_PROBE_SOURCE_TABLE_ALIAS,
            expected_output=("unregistered dispatch site", "rogue_alias"))
        run_dispatch_case(
            "dispatch-room-funct-slot", 1,
            probe_text=DISPATCH_PROBE_SOURCE_FUNCT_SLOT,
            expected_output=("unregistered dispatch site", "->funct)("))

        # (z) review-1 B-3b: a registered guard wrapped in `#if 0`. The
        # literal is still textually present, so the downward check passed
        # while the compiler saw no guard at all.
        run_dispatch_case(
            "dispatch-guard-compiled-out-by-if-zero", 1,
            probe_text=DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO,
            expected_output=("guard literal",))

        # (aa) ... and the mirror: an `#if 0` whose `#else` arm carries the
        # real guard. That arm is LIVE, so the stripper must stop at the
        # `#else` and the gate must still find the literal.
        run_dispatch_case(
            "dispatch-guard-in-the-else-arm-of-if-zero", 0,
            probe_text=DISPATCH_PROBE_SOURCE_GUARD_IF_ZERO_ELSE)

        # (aa2)/(aa3) Task 5-fix2 (M-3): two of the six other never-true
        # spellings. Both were `--check` exit 0 before the condition was
        # normalized rather than string-matched.
        run_dispatch_case(
            "dispatch-guard-compiled-out-by-parenthesised-zero", 1,
            probe_text=DISPATCH_PROBE_SOURCE_GUARD_IF_PAREN_ZERO,
            expected_output=("guard literal",))
        run_dispatch_case(
            "dispatch-guard-compiled-out-by-not-one", 1,
            probe_text=DISPATCH_PROBE_SOURCE_GUARD_IF_NOT_ONE,
            expected_output=("guard literal",))

        # (aa4) ... and the control: a LIVE `#if 1` region must keep its
        # guard, or the widening would be a blanket ban on `#if`.
        run_dispatch_case(
            "dispatch-guard-inside-a-live-if-one", 0,
            probe_text=DISPATCH_PROBE_SOURCE_GUARD_IF_ONE)

        # (aa5) the never-true condition vocabulary, in process: every
        # spelling the reviewer demonstrated is recognized, every live or
        # undecidable one is left alone. The last two rows are the RECORDED
        # RESIDUAL -- `#ifdef`/`#if defined(...)` cannot be decided without a
        # preprocessor, and the ledger's blind-spot list says so.
        for condition_text, expected_never_true in (
                ("0", True), ("00", True), ("(0)", True), ("0L", True),
                ("!1", True), ("1 - 1", True), ("0 - 0", True), ("0x0", True),
                ("1", False), ("0 && FOO", False), ("(a)+(b)", False),
                ("defined(ROTS_NEVER)", False), ("ROTS_NEVER_DEFINED", False)):
            if _condition_is_never_true(condition_text) != expected_never_true:
                failures.append(
                    f"inactive-if-condition-vocabulary: `#if {condition_text}` classified "
                    f"{not expected_never_true} -- expected never-true={expected_never_true}"
                )
        if line_opens_inactive_region("#ifdef ROTS_NEVER_DEFINED"):
            failures.append(
                "inactive-if-condition-vocabulary: `#ifdef` must NOT be treated as never-true "
                "(the recorded residual); blanking it would delete live code from the scan"
            )

        # ------------------------------------------------------------------
        # RR Wave R3 Task 5-fix2, direction 25: the exemption map's own
        # closed world (re-verification BLOCKER B-1 and m-1). Every case here
        # pins ONE synthetic exemption through the override and then varies
        # only what production writes under it.
        # ------------------------------------------------------------------
        exempt_key_pin = (
            "src/app/dispatch_probe.cpp::exempt_fn::mob_index[].func=1")

        # (ab) clean: exactly the pinned presence test, exactly once.
        run_dispatch_case(
            "dispatch-exemption-at-the-pin", 0,
            probe_text=DISPATCH_PROBE_SOURCE_EXEMPT_PRESENCE_TEST,
            dispatch_exempt_sites_override=exempt_key_pin)

        # (ac) THE BLOCKER: the same statement written as a member CALL is a
        # different token now, so the presence-test exemption does not reach
        # it and the dispatch surfaces as unregistered.
        run_dispatch_case(
            "dispatch-exempted-presence-test-does-not-exempt-a-member-call", 1,
            probe_text=DISPATCH_PROBE_SOURCE_EXEMPT_MEMBER_CALL,
            dispatch_exempt_sites_override=exempt_key_pin,
            expected_output=("unregistered dispatch site", "mob_index[].func(", "exempt_fn"))

        # (ad) count drift UP: a second occurrence of the exempted token in
        # the exempted function is a new, unreviewed site.
        run_dispatch_case(
            "dispatch-exemption-count-drift-up", 1,
            probe_text=DISPATCH_PROBE_SOURCE_EXEMPT_SECOND_OCCURRENCE,
            dispatch_exempt_sites_override=exempt_key_pin,
            expected_output=("dispatch-token exemption count", "pins 1", "found 2"))

        # (ae) count drift DOWN: the pin claims two, production has one. The
        # key no longer describes the tree, so it would absorb the next new
        # occurrence silently.
        run_dispatch_case(
            "dispatch-exemption-count-drift-down", 1,
            probe_text=DISPATCH_PROBE_SOURCE_EXEMPT_PRESENCE_TEST,
            dispatch_exempt_sites_override=(
                "src/app/dispatch_probe.cpp::exempt_fn::mob_index[].func=2"),
            expected_output=("dispatch-token exemption count", "pins 2", "found 1"))

        # (af) m-1: a key matching NOTHING is an error, not a no-op. Before
        # this round a pre-planted key for a function that did not exist
        # passed, and so did creating that function later with a real
        # unguarded dispatch inside it.
        run_dispatch_case(
            "dispatch-exemption-stale-key", 1,
            probe_text=DISPATCH_PROBE_SOURCE_EXEMPT_RENAMED,
            dispatch_exempt_sites_override=exempt_key_pin,
            expected_output=("STALE dispatch-token exemption", "exempt_fn"))

        # (ah) Task 5-fix2 (M-5): four more SPECIAL-body dispatch shapes the
        # tree already uses in production. All four were exit 0 before this
        # round; each names its own token in the failure message.
        run_dispatch_case(
            "dispatch-special-body-doors", 1,
            probe_text=DISPATCH_PROBE_SOURCE_SPECIAL_DOORS,
            expected_output=("virt_program_number(", "get_special_function(",
                             "dispatch_virt_program_number(", "intelligent(",
                             "rogue_prog_number", "rogue_intelligent"))

        # (ag) the token SPLIT itself, pinned directly: a member CALL and an
        # address READ of the same slot must classify as DIFFERENT tokens.
        # The end-to-end direction above stays red without this split (the
        # call form's own token still fires), so nothing else in the suite
        # notices the lookahead being dropped -- which is exactly the silent
        # regression that re-opens B-1's key-sharing.
        for slot_name in ("mob_index", "obj_index"):
            call_line = f"    {slot_name}[ch->nr].func(ch, ch, 0, arg, 0, 0);"
            address_line = f"    if ({slot_name}[ch->nr].func) {{"
            wrapped_call_line = f"    ((*{slot_name}[ch->nr].func)(ch, ch, 0, arg, 0, 0));"
            call_tokens = {token_name for token_name, pattern in DISPATCH_SPELLING_TOKENS
                           if pattern.search(call_line)}
            address_tokens = {token_name for token_name, pattern in DISPATCH_SPELLING_TOKENS
                              if pattern.search(address_line)}
            wrapped_tokens = {token_name for token_name, pattern in DISPATCH_SPELLING_TOKENS
                              if pattern.search(wrapped_call_line)}
            if call_tokens != {f"{slot_name}[].func("}:
                failures.append(
                    f"dispatch-index-slot-call-is-its-own-token: {call_line.strip()!r} matched "
                    f"{sorted(call_tokens)}, expected exactly ['{slot_name}[].func('] -- while "
                    "the member call shares the address-read token, every exemption pinned on "
                    "that token exempts a real dispatch too (re-verification B-1)"
                )
            if address_tokens != {f"{slot_name}[].func"}:
                failures.append(
                    f"dispatch-index-slot-address-is-its-own-token: {address_line.strip()!r} "
                    f"matched {sorted(address_tokens)}, expected exactly "
                    f"['{slot_name}[].func']"
                )
            if wrapped_tokens != {".func)("}:
                failures.append(
                    f"dispatch-index-slot-wrapped-call-is-the-funct-token: "
                    f"{wrapped_call_line.strip()!r} matched {sorted(wrapped_tokens)}, expected "
                    "exactly ['.func)('] -- the `(*...)` form the tree really uses"
                )

        # (ag) the exemption CATEGORY vocabulary is closed, and the shipped
        # map only uses spellings from it -- the property the exemption
        # census (AGENTS.md/BUILD.md/the ledger) is re-derived from.
        for exemption_key, exemption_value in sorted(DISPATCH_TOKEN_EXEMPT_SITES.items()):
            if len(exemption_value) != 3:
                failures.append(
                    f"dispatch-exemption-value-shape: {exemption_key} maps to "
                    f"{exemption_value!r}, expected (count, category, reason)"
                )
                continue
            exemption_count, exemption_category, exemption_reason = exemption_value
            if not isinstance(exemption_count, int) or exemption_count < 1:
                failures.append(
                    f"dispatch-exemption-count-shape: {exemption_key} pins "
                    f"{exemption_count!r}; an exemption covers at least one real occurrence"
                )
            if exemption_category not in DISPATCH_EXEMPTION_CATEGORIES:
                failures.append(
                    f"dispatch-exemption-category-closed-world: {exemption_key} is filed under "
                    f"{exemption_category!r}, which is not in DISPATCH_EXEMPTION_CATEGORIES "
                    f"{list(DISPATCH_EXEMPTION_CATEGORIES)}"
                )
            if not exemption_reason.strip():
                failures.append(
                    f"dispatch-exemption-reason-non-empty: {exemption_key} carries an empty "
                    "reason; every exemption is a reviewed disposition, not a silent skip"
                )

        # ------------------------------------------------------------------
        # RR Wave R3 Task 1a, direction 23: the R3-O-3 caller-count pins, both
        # drift directions plus the declaration-does-not-count claim the
        # measurement method rests on.
        # ------------------------------------------------------------------
        caller_root = root / "caller_root"
        caller_app_dir = caller_root / "src" / "app"
        caller_app_dir.mkdir(parents=True)
        caller_probe = caller_app_dir / "probe.cpp"
        caller_ledger = root / "caller_ledger.md"
        caller_ledger.write_text(SELF_TEST_LEDGER_OK, encoding="utf-8")

        def run_caller_case(name, expected_exit, probe_text, pins, *, expected_output=()):
            caller_probe.write_text(probe_text, encoding="utf-8")
            exit_code, output = _run_gate(
                caller_root, caller_ledger, minimum_files_override=1,
                caller_count_pins_override=pins)
            if expected_exit == 0 and exit_code != 0:
                failures.append(f"{name}: expected gate exit 0, got {exit_code}\n{output}")
            if expected_exit != 0 and exit_code == 0:
                failures.append(f"{name}: expected a NON-zero gate exit, got 0\n{output}")
            for needle in expected_output:
                if needle not in output:
                    failures.append(f"{name}: expected {needle!r} in the gate output\n{output}")

        # The probe has exactly ONE CAN_SEE( call in a function body (plus a
        # declaration that must not count): pinning 1 passes.
        run_caller_case("caller-count-at-the-pin", 0, CALLER_COUNT_PROBE_SOURCE, "CAN_SEE(=1")
        # DRIFT UP (a new caller appeared): the pin says 0, the scan finds 1.
        run_caller_case(
            "caller-count-drift-up", 1, CALLER_COUNT_PROBE_SOURCE, "CAN_SEE(=0",
            expected_output=("caller-count pin", "TWO edits"))
        # DRIFT DOWN (a caller was removed, leaving the pin stale): the pin
        # says 1, the scan finds 0.
        run_caller_case(
            "caller-count-drift-down", 1, PROBE_SOURCE_OK, "CAN_SEE(=1",
            expected_output=("caller-count pin", "TWO edits"))
        # The DECLARATION-does-not-count claim the measurement method rests
        # on: a file-scope `int CAN_SEE(char_data* sub);` and nothing else
        # measures 0, not 1.
        run_caller_case(
            "caller-count-ignores-declarations", 0,
            CALLER_COUNT_PROBE_SOURCE_DECLARATION_ONLY, "CAN_SEE(=0")

        # Task 5-fix, review-1 B-1's caller-pin half: a ONE-LINE definition
        # hides its call on a file-scope line, so the pin never counted it.
        # It is now an ERROR rather than an invisible miss -- note the pin is
        # set to 0 here, which the OLD behaviour would have satisfied.
        run_caller_case(
            "caller-count-one-line-definition-is-an-error", 1,
            CALLER_COUNT_PROBE_SOURCE_ONE_LINE_DEFINITION, "CAN_SEE(=0",
            expected_output=("file-scope pinned caller", "CAN_SEE("))
        # Task 5-fix, review-2 m-5: an ALIAS macro. `RR_CS(a, b)` is a real
        # CAN_SEE caller that the literal-spelling count cannot see -- the pin
        # of 0 below would otherwise be satisfied.
        run_caller_case(
            "caller-alias-macro-is-an-error", 1,
            CALLER_COUNT_PROBE_SOURCE_ALIAS_MACRO, "CAN_SEE(=0",
            expected_output=("alias macro", "RR_CS"))
        # ... and the shape the alias rule must NOT reject: a macro that CALLS
        # the pinned name. Its body line is `macro`-attributed and counted, so
        # pinning 1 passes.
        run_caller_case(
            "caller-macro-that-calls-the-pinned-name-is-fine", 0,
            CALLER_COUNT_PROBE_SOURCE_CALLING_MACRO, "CAN_SEE(=1")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("room_resolve_census self-test: all directions pass (gate invoked end to end)")
    return 0


# ---------------------------------------------------------------------------
# Task 3: --generate-ledger and --advise.
#
# --generate-ledger performs ONLY the mechanical classification a scan can
# derive without judgment: every src/tests/ key -> TEST-FIXTURE, every
# #decl/#NAME (macro-body or file-scope) key -> DECL, everything else ->
# TODO. The task-3 brief's hand-pinning step (RESOLVER-IMPL rows, the two
# PROVEN rows, the closed DECL set) is applied BY HAND afterward, directly
# editing the generated file -- this generator never emits PROVEN/GUARDED/
# RESOLVER-IMPL itself (spec section 4/F-12: "advisory only; no machine-
# minted PROVEN rows").
# ---------------------------------------------------------------------------

LEDGER_PROSE = """\
# Room-resolve retirement ledger

Generated by `tools/room_resolve_census.py --generate-ledger`, then hand-refined
per Wave R1's task-3 brief (`.superpowers/sdd/2026-07-31-rr1-census/task-3-brief.md`).
Program design: `docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md`
(section numbers cited below refer to that document).

## Program contract

This ledger is the proof-obligation table for the room-resolve retirement
program ("RR"): every resolver-reaching site in production code (`room_of(`,
`room_by_id_total(`, `world[`/`::world[`, the `world_room_vnum(`/
`dispatch_room_vnum(` hook-dispatch alias pair, and the eleven-member
resolver-expanding macro family) must carry a row classifying whether its
input id can be proven in-range before `room_data::operator[]` degrades it
(design doc section 1). A row's key is `` `file · function · token` `` (the
middle dot is U+00B7, illegal in a path or C identifier, so the split is
unambiguous); its count must equal the scanner's own count for that exact key
(the anti-inheritance rule, section 3/O-5) -- adding a new site to an
already-classified function fails the gate as a count mismatch rather than
silently inheriting the existing proof.

## The standing rule

`--check` fails closed on: an unclassified scanned site, a stale ledger row
matching nothing in the scan, a per-key count mismatch (either direction), a
per-token total mismatch against the token-counts table below, a `TODO` total
over the checked-in `MAXIMUM_TODO_COUNT` ceiling, and a resolver-reaching
macro whose name is not in the pinned `MACRO_FAMILY` literal. Each drain wave
lowers `MAXIMUM_TODO_COUNT` in the same commit that adds its proofs --
**a new resolver site anywhere in production is therefore a new `TODO` row,
which exceeds the ceiling: unclassified new code is a build failure from R1
onward** (design doc section 4/O-10). A row `Count` must be at least 1 -- a
zero or negative count can mask real sites while some other row's count
keeps a per-key SUM exact (review-1 W-2).

## The closed, pinned classes

Three classes are pinned closed, in addition to `MACRO_FAMILY`'s own pinned
literal (review-1 W-1, closing the dual whole-branch review's BLOCKER):

- **`DECL`** rows must key on a `#`-prefixed name -- either a macro-body key
  (`#NAME`, matching a real `#define`) or the file-scope-unattributable key
  `#decl`. A plain function name can never start with `#`; a `DECL` row
  keyed on one is rejected outright, closing off the demonstrated exploit of
  auto-laundering an unattributable site into the no-proof-required `DECL`
  class by naming it like a real function.
- **`RESOLVER-IMPL`** is closed by a `RESOLVER_IMPL_KEYS` frozenset of
  `(file, function)` pairs (the tool's own module-level constant) -- any
  `RESOLVER-IMPL` row outside that set is a gate error. Extending the
  resolver-impl set is therefore a review-visible SCRIPT edit, never a
  ledger-only one.
- **`.cpp`-file `#decl` keys** are additionally closed by a
  `PINNED_DECL_KEYS` frozenset -- a header's `#decl` key and any `#NAME`
  macro-body key (header OR `.cpp`) stay legal and open, since both are
  re-derivable from a real, named construct; only the `.cpp`-file
  "unattributable, file-scope" bucket is pinned, restoring design doc
  section 4's fail-closed rule that an unattributable production site must
  not silently auto-launder into `DECL` as the tree changes shape around it.

`PROVEN` and `GUARDED` rows must carry a genuinely non-empty `Kind` AND a
genuinely non-empty `Proof` -- the empty marker on either previously
short-circuited every shape check below wholesale (review-1 F-1,
demonstrated live: a real `TODO` row hand-flipped to
`` | PROVEN | — | — | `` passed `--check` unchanged). `PROVEN`'s `Kind` is
held to the closed `PROOF_KINDS` vocabulary above; `GUARDED`'s `Kind` is free
text citing its wave and red-first test (design doc section 3) and is
**not** held to that vocabulary.

## The gate verifies shape, reviews verify semantics

The gate is line-based and performs no reachability analysis of its own: it
checks that every `Class`/`Kind` is a recognized vocabulary word, that
`Kind`-bearing rows carry a `Proof` of at least `MINIMUM_PROOF_TEXT_LENGTH`
characters, and that `entry-guard`/`dominating-resolve`/`caller-contract` rows
cite a `file:line`. Those checks close the *trivially gameable* one-word-row
class -- they cannot tell a semantically correct proof from a
plausible-sounding wrong one. A `PROVEN` row with wrong reasoning is a
**review** finding, not a gate finding (design doc section 3/section 8): the
dual adversarial whole-branch reviews audit the `PROVEN` set by sampling, with
`caller-contract` rows (the hardest kind) checked citation-by-citation.

## Occupant-chain proof caveats

A row proved by the `occupant-chain` kind must state, in its own proof text,
which half of the occupant-chain invariant it relies on (design doc section 3):
the invariant's **contrapositive gives `!= NOWHERE` only** -- it does not by
itself prove the id is *in range*, only that it is not the sentinel. In-range-
ness instead follows from the WRITE side, in the wording ruling R3-C-2 fixes
below ("The in-range half's standing citation"): the location field's only
writers are `char_to_room` and `ScopedRenderLocation`, plus append-only room
allocation (a room id, once valid, stays valid for the process lifetime). The
invariant's second half -- that the occupant-chain-derived id still equals the
character's own stored location field -- does **not** hold inside a
`ScopedRenderLocation` window (the render cursor temporarily diverges from the
stored field by design); an `occupant-chain` proof taken from inside such a
window must say so explicitly, or it is incomplete.

## The location_of() guard proves only the sentinel half

The SAME two-half distinction applies to an `entry-guard` or `caller-contract`
proof built on a `location_of(ch) != NOWHERE` test, not only to
`occupant-chain` proofs (review-1 W-4): the guard establishes only that the
id is not the `NOWHERE` sentinel -- it does not, by itself, establish that the
id is *in range*. A row proved by
`entry-guard`/`caller-contract`/`dispatch-invariant` over a `location_of()`
test must therefore state BOTH halves in its own proof text -- the sentinel
exclusion AND the in-range justification -- not the sentinel half alone.

### The in-range half's standing citation (R3-C-2)

Wave R1/R2 rows spell the in-range half as "in-range via M-1
(`placement.cpp:369-395`) + append-only allocation". **That citation is wrong
about what it points at**: `src/entity/placement.cpp:369-395` is
`ScopedRenderLocation`'s own explanatory banner and precondition discussion,
not an in-range argument. Coordinator ruling R3-C-2 replaces it, from Wave R3
onward, with a citation that is honest about where in-range-ness actually
comes from -- the WRITE side, which this same program drains:

> in-range: the location field's only writers are `char_to_room`
> (`src/entity/placement.cpp:548`, whose own unconditional resolve at `:564`
> is the program's RR-O-1 site and catches every invalid id -- mudlog today,
> abort post-flip) and `ScopedRenderLocation` (which refuses NOWHERE-over-real
> in `would_break_the_absence_invariant`, `src/entity/placement.cpp:410-420`,
> enforced from the constructor at `:427` and from `retarget` at `:440`, and
> otherwise spoofs only ids its callers supply), plus append-only room
> allocation.

Two notes on this section's own citations. **First**, R3-C-2's drafted wording
cited the `ScopedRenderLocation` half as `:369-391`; every line above was
re-read at this commit, and the refusal is implemented at `:410-420` and
enforced at `:427`/`:440`, so the span is corrected here rather than copied
forward (the wave's standing "every line number re-read before citation"
rule). **Second**, this is an INHERITED proof, not a self-standing one: the
in-range half rests on the write side's own unconditional resolve, which is
exactly the site the program's final flip wave converts from mudlog to abort.
Saying so plainly is the point of the wording.

**R2's landed rows are NOT rewritten.** Every Wave R2 `PROVEN`/`GUARDED` row
still carries the `M-1 (placement.cpp:369-395)` boilerplate in its own `Proof`
cell. Those rows' CONCLUSIONS are correct; only the citation pointed at the
wrong lines, so R3-C-2 fixes the one prose section they all lean on instead of
editing dozens of `Proof` cells -- churn with no reviewable content, and a
diff that would bury the rows a reviewer actually needs to read. **This
section supersedes that boilerplate**: wherever an R2 row says "in-range via
M-1 (`placement.cpp:369-395`)", read the block-quoted wording above instead.
Rows landed from Wave R3 onward cite the new wording directly.

**Wave R3's OWN rows are uniform, as of Task 4.** The wave's two proof-only
classification tasks (T2p/T3p) branched before T1a's correction landed and
copied R3-C-2's DRAFTED `:369-391` span into 23 rows; T2d/T3d/T1c then wrote
the corrected span into 27 more, each carrying its own clause saying the
earlier spelling was stale. Task 4 re-pointed all 50 onto the single
block-quoted wording above -- 31 of them needed the sentence itself rewritten
(the 23 drafted-span rows plus T2d/T1c's 8, whose phrasing and file-prefix
differed), the other 19 only their trailing citation-provenance note. Together
with Task 3e's four OLC rows the ledger now carries exactly ONE spelling of the
in-range half across all **54** Wave R3 rows, and the drafted `:369-391` span
survives nowhere in the classification table (it appears exactly once in this
file, in the paragraph above that records what it was). **No Wave R1/R2 row's PROOF ARGUMENT was
edited** -- those still carry the superseded `M-1` boilerplate this section
fixes, deliberately (see the paragraph above). Three R1/R2 rows did have
their `file:line` CITATIONS re-derived, because this wave's own tripwire
insertions moved the lines they point at: `src/entity/placement.cpp ·
get_char_room` (whose proof argument was additionally repaired at the Task
5-fix round -- see review-1's M-3, recorded in that row), `src/entity/
containment.cpp · obj_from_room` and `src/entity/location_benchmark.cpp ·
measure_lookup`. Re-pointing a citation at unchanged code is not editing the
row's argument; the sentence above means the latter (whole-branch review-1,
finding m-6).

## `dispatch-invariant` proofs and the dispatch-entry registry

`dispatch-invariant` (Wave R3, owner ruling R3-O-1) is the sixth and newest
`PROOF_KINDS` entry, and the first whose evidence lives ENTIRELY OUTSIDE the
row's own function. It covers the pattern that dominates `src/combat/` and
`src/script/`: a body resolves the room of an actor (`ch`/`caster`) it never
validated, because the actor arrived through a dispatch mechanism -- an
`cmd_info[].command_pointer` ACMD dispatch, the `combat_command` table, a
`skills[].spell_pointer` door, one of the two SPECIAL invokers, or the mob-AI
driver. The proof is that the DISPATCHER guards the actor before dispatching:
one tripwire per entry point, on the actor argument, in the
`db_world.cpp:2083` idiom (`if (location_of(ch) == NOWHERE) { mudlog(<globally
unique string>); return; }`).

**A `dispatch-invariant` proof must carry three citation parts. All three, or
the row is not proven:**

1. **The registry entry** -- the `file:line` of the dispatch-entry row whose
   guard covers this row's actor. A `dispatch-invariant` row that cites no
   entry cites nothing; this is also why the kind is in
   `CITATION_REQUIRED_KINDS`.
2. **The actor parameter, by name.** The guard covers ONE argument. A proof
   that does not say which one is unreviewable, and silently over-claims for
   any `victim`/target-derived id -- ruling R3-C-5: the tree's
   `location_of(A) == location_of(B)` equality checks all pass when BOTH are
   NOWHERE, so a target id inherits the caster's status and nothing more.
   Rows whose id is target-derived are classified on their own merits.
3. **The body's exhaustive direct-caller list**, with every door that is NOT
   the registered dispatcher proven separately or the row split. A body called
   directly from elsewhere (`do_mental` from `do_hit`/`perform_violence`,
   `perform_drop` from `script.cpp`) does not inherit the entry's guard on
   that path.

The in-range half is cited exactly as "The in-range half's standing citation"
above states it -- the entry guard, like any `location_of()` test, excludes
only the sentinel.

**The closure, and exactly what enforces it.** The kind is sound only while
the entry set is CLOSED. **What closes a row is mandatory citation part (iii)
-- its exhaustive direct-caller list, read and re-grepped by a human.** The
mechanical check below is a BACKSTOP over that: it catches every occurrence of
the dispatch spellings pinned in `DISPATCH_SPELLING_TOKENS`, which is the set
of shapes this tree actually uses plus the re-spellings two adversarial
reviews demonstrated, and it is line-based. It is not, and cannot be, a proof
that no eighth dispatcher exists -- see "The line-split-call note" for the two
residual spellings it cannot see. Read the two directions below as "a new
dispatcher written in any of the pinned spellings is a build failure", not as
"a new dispatcher is impossible". With that said, the entry points are
enumerated in the marker-anchored **dispatch-entry registry** at the end of
this file (`<!-- ROOM-RESOLVE-DISPATCH-ENTRIES -->`, exactly
once, outside any fence -- the M10 rule the classification and token-counts
tables already follow). `tools/room_resolve_census.py --check` asserts two
directions over it, the same bidirectional shape
`tools/location_read_census.py`'s location-state registry uses:

- **Downward:** every `GUARDED`/`GUARDED-PRIOR` row names a real file and a
  real, scanner-findable function whose comment/string-MASKED body still
  contains EVERY guard literal that row lists (verbatim up to whitespace
  normalization, since a real guard wraps across physical lines and a table
  cell cannot). Deleting a guard from production is a gate failure, not
  silent proof rot -- and deleting ONE of an entry's two guards is the same
  failure, since a two-tripwire entry is only as strong as its weakest (see
  "An entry may carry MORE THAN ONE guard literal" below).
- **Upward:** every occurrence of a pinned dispatch spelling in production
  code lies inside a registered entry's function. An eighth dispatcher
  *written in one of those spellings* is a gate ERROR naming the unregistered
  site, not a free inheritance of the proof. The spellings are pinned in the
  script's `DISPATCH_SPELLING_TOKENS`; the Task 5-fix round widened them from
  the original seven (`command_pointer)(`, `g_command_table[`,
  `spell_pointer)(`, `.spell_pointer(`, `activate_char_special(`,
  `activate_obj_special(`, `shape_center(`) to **nineteen**, adding the
  member-call forms (`->spell_pointer(`, `->command_pointer(`,
  `.command_pointer(`), the bare address-read forms that a hoisted copy or a
  table alias produces (`command_pointer`, `spell_pointer`,
  `g_command_table`), and the SPECIAL fn-ptr slots the tree already dispatches
  through (`.func)(`, `->func)(`, `.funct)(`, `->funct)(`,
  `mob_index[].func`, `obj_index[].func`). Each of the six added shapes was a
  demonstrated evasion in one or both whole-branch reviews. A file-scope
  occurrence is admitted only when it sits at brace depth 0 on its own line --
  a declaration, a definition head or a brace-init definition; an occurrence
  inside braces on a file-scope line is a ONE-LINE function body (the other
  demonstrated bypass) and is an ERROR. The real sites that carry a pinned
  spelling without being an entry point are pinned as reviewed exemptions in
  the script's `DISPATCH_TOKEN_EXEMPT_SITES`, each with its own one-line
  reason (see below).

There is **no** exempt status. `PENDING-T1b` used to be one -- a transitional
spelling for an entry whose guard was specified but not yet landed, carrying
no guard literal and warning rather than failing. Both whole-branch reviews
independently demonstrated the same evasion through it (add an unguarded
dispatcher plus one `PENDING-T1b` row; `--check` exits 0), so it was retired
at the Task 5-fix round. Every registry row now carries at least one guard
literal, and an unknown status is a hard parse error.

**How a guard literal is SPELLED, and why it looks masked.** The downward
check runs against the function's comment/string-MASKED body, so a
literal that quotes a `mudlog()` call must quote it as the masker leaves
it -- the message's contents (and its quotes) are blanked, so
`mudlog("...", NRM, LEVEL_IMPL, TRUE);` normalizes to `mudlog( , NRM,
LEVEL_IMPL, TRUE);`. Wave R3 Task 1b's nine rows use that spelling
deliberately: a literal of just `if (location_of(ch) == NOWHERE) {` is
satisfied by ANY absence test anywhere in the function, so deleting the
tripwire and leaving some unrelated NOWHERE check behind would pass. The
full statement -- condition, `mudlog()` call, and the exit keyword, which
differs per site (`return` / `return 0` / `continue`) -- pins the
whole shape. (`command_interpreter`'s entry was an `if/else` refusal in Task
1b, because at that placement -- immediately above the ACMD dispatch -- an
early `return` would have skipped the parsed targets' cleanup. Task 1c moved
the guard to the top of the position-check `else` block so it also dominates
`target_parser`, whose `target_from_word` resolves `room_of(ch)`/`EXIT(ch, ..)`
from the same actor (T3d finding O-2); nothing is parsed yet at that point, so
the entry is now an ordinary `return`.) The globally unique message TEXT is the separate half of the
contract: it is what R-final's measured-zero sweep greps for, and it lives
in production, not here.

**An entry may carry MORE THAN ONE guard literal** (Wave R3 Task 1d,
coordinator ruling R3-C-7). One tripwire per entry point is not always
enough. R3-C-7's adjacency rule says a `dispatch-invariant` row inherits an
entry's guard ONLY if no statement between that guard and the dispatch the
row's site is reached through can run code with the actor as a participant --
a `special()` fan-out, a `complete_delay()`, an `affect_total()` APPLY_SPELL
walk, any `command_interpreter` re-entry. The structural way to satisfy it is
ADJACENCY: the tripwire sits IMMEDIATELY before the dispatch statement, with
only scalar/local work in between. But an entry function that must ALSO
resolve or parse the actor EARLIER than its dispatch -- `do_cast`'s
`room_of(ch)` at spell_pa.cpp:505 and its `target_from_word` call at :624,
`command_interpreter`'s `target_parser` at interpre.cpp:1130 -- still needs a
guard THERE, for those sites. So such an entry carries TWO tripwires: an
early one licensing the early sites, and a pre-dispatch one licensing
everything reached through the dispatch. Both share the entry's one globally
unique message string (the sweep counts entry points, not statements).

The `Guard literal` cell then lists both, separated by ` || ` -- spaces
around, and the bars written `\\|\\|` in the markdown so the row survives cell
splitting -- and `--check` requires EVERY listed literal to be present. The
separator is parsed STRUCTURALLY, anchored on the backticks that wrap each
literal, never by a bare split: `one_mobile_activity`'s single literal
contains a real C `||` operator, and a naive split would tear it into two
halves that each still match, silently weakening the check to nothing.

Because the two guards license DIFFERENT sets of sites, every
`dispatch-invariant` proof cites the specific guard LINE it rests on, not
merely the entry. A row whose site is reached through the dispatch cites the
pre-dispatch line; a row whose site sits above the first relocating call
cites the early one.


**The `special` entry's actor column is narrower than the others** (both
whole-branch reviews, review-2 M-1 / review-1 M-2). `special()`
(interpre.cpp:1342-1360) tests its SIXTH PARAMETER, not its actor: it sets
`remote_mode` from `in_room != location_of(ch)`, normalizes `in_room` to
`location_of(ch)` **only** when the caller passed `NOWHERE`, and then refuses
on `in_room`. Nine of the ten production call sites omit the argument (its
default is `NOWHERE`), so for those nine the literal IS an actor test. The
tenth -- `src/app/act_comm.cpp:660`, `do_gen_com`'s `SCMD_YELL` zone sweep --
passes a real room for every room in the zone, and in that remote mode an
unplaced `ch` passes the literal and reaches the room funct (`:1429`) and
every object spec in that room (`:1451`); only the mob arm is caught, by this
wave's own `activate_char_special` guard on `victim`. The row's actor column
says so explicitly rather than claiming `ch`. **No Wave R3 row cites this
entry** (checked: the entry citation of all 30 `dispatch-invariant` rows is
one of `interpre.cpp:1175`, `spell_pa.cpp:928`, `act_othe.cpp:831`/`:862`,
`shapemob.cpp:2374`, `combat_hooks.cpp:81`, `interpre.cpp:1119`), so nothing
landed rests on it -- but R4 will read this registry as a contract, and the
column must not over-claim. Closing the remote-mode half needs a real actor
tripwire in `special()`, which is R4 work.

**Deliberately NOT an entry: `affect_modify`'s APPLY_SPELL arm**
(`src/entity/entity_lifecycle.cpp:2440`/`:2442`). That arm runs a real
`ASPELL` with `caster == victim == ch` at NOWHERE **by design**, inside the
login/rent-load window (`src/persist/db_players.cpp:1403` sets NOWHERE and
calls `affect_total` on the next line; `src/app/objsave.cpp:506`'s
`Crash_load` runs the same way before placement). A tripwire there would fire
on every login of a character carrying an `APPLY_SPELL` affect. It is pinned
in `DISPATCH_TOKEN_EXEMPT_SITES` with that reason, and every row reachable
through it stays `TODO` under the `APPLY_SPELL-window` category below (owner
ruling R3-O-2). The original companion exemption is
`src/combat/combat_hooks.cpp:56`'s `g_command_table[i] = handler;` -- a
registration WRITE, at which no actor exists at all.

**The Task 5-fix exemptions.** Widening the token surface to catch
address-reads (above) necessarily surfaces every ordinary presence TEST,
comparison and registration WRITE of the same fn-ptr slots. Each is
dispositioned in `DISPATCH_TOKEN_EXEMPT_SITES`, once per
`(file, function, token)` key, with a one-line reason: nine registration
writes (the `COMMANDO`/`ASSIGNMOB`/`ASSIGNOBJ` families, `assign_spell_pointers`,
`assign_command_pointers`, `load_mobiles`/`load_objects`) and eleven presence
tests or comparisons. The key includes the TOKEN, so exempting a function's
address-reads does not exempt a real call newly added to it.

**Three exemptions are STOPs, not category errors** -- the direct SPECIAL
doors review-1's B-3 named: `src/app/comm.cpp:2829`/`:2830`
(`complete_delay_impl`'s `(*mob_index[ch->nr].func)(...)`) and
`src/app/delayed_command_interpreter.cpp:45` (`func_pointer =
mob_index[index].func;`, called at `:47`). Both really do dispatch a spec proc
with an actor nothing on that path validated. They are NOT registered, because
a registry row asserts the entry HAS a guard and Wave R3 lands none for them;
they are R4 design input, alongside `src/app/shop.cpp`'s direct ACMD host
calls, which are plain calls rather than dispatch spellings and are already
recorded there. No Wave R3 row cites either of them.

## Caller-count pins (R3-O-3)

`src/combat/visibility.cpp · CAN_SEE` and `· get_char_room_vis` are the two
scale-flagged rows Wave R3 does NOT drain: both stay `TODO`, waiting for the
same batch treatment `CAN_GO`/`obj_to_room` are queued for. Both are also
id-TAKING in the sense "The caller-contract token-pinning policy" blind spot
below describes -- any future proof of them rests on "every caller is
accounted for" -- and that policy's own remedy is to pin the name as a
SCANNED TOKEN, so a new caller surfaces as its own ledger site.

Here that remedy is unaffordable. The two names have **127 production call
sites** between them; pinning them in `token_patterns()` would mint roughly
that many new ledger rows and raise `MAXIMUM_TODO_COUNT` by the same amount,
for zero proof value. **Owner ruling R3-O-3's deliberate deviation: pin the
COUNTS instead.** `PINNED_CALLER_COUNTS` in `tools/room_resolve_census.py`
carries the measured figures, and `--check` fails on drift in EITHER
direction (a new caller is what the pin exists to catch; a removed caller
means the literal is stale and would stop catching the next one), with a
message naming the two-edit fix -- the script literal and this section, always
updated together.

| Name | Pinned production call sites |
|---|---|
| `CAN_SEE(` | 86 |
| `get_char_room_vis(` | 41 |

Measured at this commit by the method recorded verbatim beside
`PINNED_CALLER_COUNTS`: every `SOURCE_SUFFIXES` file under `src/` except
`src/tests/`, comment/string-masked with the tool's own masker, attributed
with the tool's own `attribute_lines`, counting every regex occurrence (per
occurrence, not per line) on a line whose attribution is NOT `file-scope`.
The file-scope exclusion is what drops the declarations and definition heads
(`src/utils.h:669`/`:670` and `src/combat/visibility.cpp:107`/`:527` for
`CAN_SEE`; `src/handler.h:509` and `src/combat/visibility.cpp:617` for
`get_char_room_vis`) while KEEPING real calls that live inside a macro body --
`src/script/spec_pro.cpp:2345` is one such `CAN_SEE(` call, and counting only
`function`-attributed lines would have silently lost it. Neither name occurs
anywhere in `src/tests/` today, so the production scoping is discipline
rather than a concession.

## Stayed-TODO taxonomy: the Wave R3 additions

The canonical taxonomy lives in `docs/superpowers/room-resolve-playbook.md`
("The stayed-TODO taxonomy"), which named four categories out of Wave R2. Wave
R3 adds THREE (`APPLY_SPELL-window`, `owner-punted`, `intervening-relocation`)
and widens one; they are recorded here because R3's rows cite them and this is
the file those rows live in. (The "adds two" this sentence carried until Wave
R3 Task 4 predated Task 1c's `intervening-relocation` entry, which was appended
to the list below without updating the count -- corrected by re-counting the
list itself.)

- **`APPLY_SPELL-window`** (owner ruling R3-O-2) -- rows reachable with an
  unplaced caster through `affect_modify`'s APPLY_SPELL arm (producer P1, the
  login/rent-load window; see the dispatch-entry section above for why that
  arm is deliberately unguarded). **10 combat rows / 14 sites at R3**, every
  one of them in `src/combat/mage.cpp` -- re-derived from the landed rows at
  Wave R3 Task 4. The "11 combat rows / 16 sites" this section carried until
  then came from census A's own summary SENTENCE; Wave R3 Task 2d found it
  disagrees with that census's own sub-table, which marks 10 rows / 14 sites
  "C-2 reachable = YES". The eleventh row is `src/combat/spell_pa.cpp ·
  do_sense_magic · room_by_id_total(` (2 sites, both spell_pa.cpp:132), whose
  C-2 column reads "n/a": it does stay `TODO`, but NOT for the login window.
  Its caster half would land (`do_cast` is its sole caller, so it inherits that
  entry's guard); its `character` half does not, because `character =
  player->character` for every `CON_PLYNG` descriptor in
  `get_descriptor_list_head()`'s list, and "a `CON_PLYNG` descriptor has a
  placed character" is a different unproven invariant that no R3 ruling covers
  and the dispatch-entry registry says nothing about. The row is not
  splittable -- both sites share one physical line, one function and one token,
  so they are one ledger key. **The stayed-TODO TOTAL is unaffected by the
  correction** (11 rows / 16 sites still stay `TODO` either way); only the
  labelling is. The 10 rows in this class are NOT covered by
  `dispatch-invariant` and are not silently left: they stay `TODO` as a named
  class, to be ruled together with the program's pending `RR-O-1` ruling, since
  both are rulings about the same login window.
- **`owner-punted`** (owner ruling R3-O-4) -- `src/combat/olog_hai.cpp ·
  get_random_target` (1 row / 2 sites) has zero production callers; the
  repository's own dead-code heuristic would delete it, but the owner ruled it
  is NOT deleted, NOT proven, and NOT to be removed: intent is unknown and the
  disposition is a future design decision. The row stays `TODO` with that
  reason, and no proof is to be written for it.
- **`intervening-relocation`** (Wave R3 Task 1c) -- a row whose every door IS a
  registered dispatch entry, but where a statement between one entry's guard and
  the site can move the actor, so the guard's `location_of(...) != NOWHERE`
  conclusion does not survive to the site. `src/combat/visibility.cpp ·
  target_from_word · EXIT(` (1 row / 2 sites, both visibility.cpp:1164) is the
  first: its `command_interpreter` door is fully dominated after Task 1c, but its
  `do_cast` door (src/combat/spell_pa.cpp:624, reachable with `TAR_DIR_NAME` set
  by the `freeze` spell) sits below `complete_delay(ch)` at
  src/combat/spell_pa.cpp:516, which re-enters `command_interpreter`
  (src/app/comm.cpp:2837) and from there dispatches arbitrary spec procs through
  `special()`; `appear(ch)` at src/combat/spell_pa.cpp:721 is a second such
  statement (`affect_from_char` -> `affect_remove` -> `affect_total` ->
  `affect_modify`'s APPLY_SPELL arm, which runs an arbitrary ASPELL with
  `caster == victim == ch`). This is NOT the T3d "guard does not dominate" shape,
  which was about textual order and is now closed; it is about what runs in
  between. It WAS the open question T2d's report flagged against the `do_cast`
  entry. Coordinator ruling **R3-C-7** answered it structurally, and Wave R3 Task
  1d implemented the answer: `do_cast` now carries a SECOND tripwire immediately
  before its dispatch (src/combat/spell_pa.cpp:928) and `command_interpreter` one
  immediately before its ACMD dispatch (src/app/interpre.cpp:1175), so every row
  whose site is reached THROUGH a dispatch now inherits an ADJACENT guard. This
  row is not one of those, and stays `TODO`: its `do_cast` door is the
  `target_from_word` call at src/combat/spell_pa.cpp:624, which sits BETWEEN the
  two tripwires -- below `complete_delay(ch)` at :516 and above the pre-dispatch
  guard at :928. Closing it would take a third tripwire immediately above :624,
  a production change no ruling has authorized.
- **`scale-flagged`** (widened by owner ruling R3-O-3) -- already carrying
  `CAN_GO` and `obj_to_room` from Wave R2, this category now also covers
  `src/combat/visibility.cpp`'s `CAN_SEE` (the `:578`/`:580` half; the `:121`
  entry-guard half drains normally, so the row splits) and
  `get_char_room_vis`. See "Caller-count pins (R3-O-3)" above for the
  compensating control these two carry while they wait.

## The two-room-macro rule

`IS_SUNLIT_EXIT`/`IS_SHADOWY_EXIT` each take **two** room-id arguments (the
current room and the adjacent room across a door). A row classifying either
macro's call site must state a proof covering **both** room-id arguments
separately -- proving only one leaves the other's validity unaddressed
(design doc section 6/O-13). (The macros' own unchecked `dir_option[door]`
dereference is a distinct, explicitly out-of-scope defect class -- design doc
section 6 -- these rows prove the room-id arguments only, and say so.)

## The line-split-call note

Every token pattern anchors on the callee name immediately followed by `(`,
matched per physical line: a call whose *arguments* wrap to a following line
(`` room_of(\\n    ch) ``) still hits, but a genuinely split spelling (the
callee name and its `(` on two different physical lines, e.g.
`` room_of\\n    (ch) ``) cannot, by this line-based design (carried from Task
2's docstring note in this file). A one-time real-tree grep for that second
shape (`grep -rnE "room_of\\s*$" src --include="*.cpp"`) found **zero
matches** at this commit -- the shape does not exist in production today.

**The same limit applies to `DISPATCH_SPELLING_TOKENS`**, and review-1's E2
probe demonstrated it: `cmd_info[5].command_pointer)` on one physical line
with its `(ch, ...)` on the next defeats every dispatch spelling, exactly as
it defeats every resolver token. So does a slot copy taken in an expression
position none of the pinned spellings names (`helper(cmd_info[5].command_pointer)`
reads the address with a `)` after it, which the bare-identifier patterns
exclude because that is also how every presence test is written). These are
the residual limits of a line-based gate, recorded rather than papered over --
and the reason the closure paragraph above says citation part (iii), not the
token check, is what actually closes a `dispatch-invariant` row.

## An `LS1-ALLOW` annotation is NOT a proof

`tools/location_read_census.py`'s `LS1-ALLOW` annotation licenses
*representation access* -- whether a raw `->in_room`/`world[`/etc. spelling is
allowed to exist outside the Stage-1 Placement API at all. This ledger asks a
**different** question: *input validity* -- whether the id handed to a
resolver-reaching spelling can be proven in-range before it is dereferenced. A
site can be simultaneously `LS1-ALLOW`'d (representation access is fine) and
an unclassified `TODO` here (its input validity is still unproven) -- the two
gates ask two different questions and keep two different ledgers (design doc
section 1, the `world[` bullet -- review O-1/F-1, both spec reviews' top
finding).

## Known reconciliation blind spots

The gate's cross-checks are count-based, not identity-based, which leaves a
few structural blind spots recorded here rather than silently discovered
later (review-1 F-5/F-6/F-7/W-12):

- **The same-function site-swap limit.** Deleting one already-`GUARDED` site
  from a function and adding a new, unguarded site to the SAME function in
  the SAME edit needs no ledger edit at all if the function's per-key site
  count for that token does not change -- sharper than ordinary two-sided
  drift, since nothing here even changes a row's `Count`. This is a review
  concern, not a gate one: the gate can only ever see aggregate counts per
  `(file, function, token)` key, never individual call-site identity.
- **Cross-class count-shuffling between rows sharing a key is review
  territory.** Two rows for the same `(file, function, token)` key but
  different classes (e.g. one `PROVEN` site and one `TODO` site inside the
  same function, same token) can trade sites between themselves while their
  COMBINED total stays fixed, and the gate cannot tell which physical site
  moved where. A mixed-class key's rows should disambiguate which sites each
  row actually covers in their own proof text (R2 rule) -- the reviews check
  this by reading the source, not by rerunning the gate.
- **`#decl` conflates three different things**: a genuine forward
  declaration, a definition HEADER line the scanner's attribution missed
  (not yet inside the body), and a truly unattributable site. A reader must
  not assume every `#decl` row is a pure declaration -- see `PINNED_DECL_KEYS`
  above for why the `.cpp`-file case is pinned shut rather than left open to
  silently reclassify.
- **The caller-contract token-pinning policy.** A `caller-contract` proof
  for an id-TAKING function (one whose room-id argument comes from a caller,
  not a loop/table it owns itself) is only as strong as the claim that every
  caller is accounted for. Such a proof must either have the function's own
  name pinned as a token this gate scans for directly (so a NEW caller
  anywhere would itself need a ledger row), or enumerate every existing
  caller explicitly in the proof text -- the `world_room_vnum`/
  `dispatch_room_vnum` hook-dispatch pair (`STATIC_TOKEN_PATTERNS` above) is
  the precedent: pinning the dispatch alias itself as a token is what lets a
  future SECOND registered target surface as its own new site instead of
  silently inheriting the first target's proof.
- **The gate cannot see guard ORDER.** The registry's downward direction is a
  SUBSTRING check over the function's masked body: it asserts the guard
  literal is still *present*, never that it still runs *before* the dispatch.
  Both whole-branch reviews found this (review-2 M-2, review-1 M-4's tail);
  review-2 demonstrated it by moving `do_use`'s entire guard block below both
  of its dispatches with `--check` still exiting 0. R3-C-7's ADJACENCY rule is
  therefore witnessed by the per-entry TESTS, not by the gate -- with one
  exception introduced at the Task 5-fix round: `do_use`'s two pre-dispatch
  literals each quote the guard AND the dispatch statement it protects,
  contiguously, so for those two the substring check *is* an ordering check
  (moving either guard below its dispatch fails `--check`). Extending that
  shape to the other entries is R4 work.
- **Recorded scan-scope limits.** The scan is `<root>/src` only (via
  `_resolve_scan_targets`'s default) -- nothing outside `src/` is ever
  scanned or can carry a ledger row. `collect_define_bodies` keeps
  LAST-WINS on a duplicate macro name across scanned files (a later
  `#define NAME(...)` silently overwrites an earlier one in the body map
  `derive_macro_family` closes over) -- two files defining the same macro
  name differently is a latent blind spot this gate does not detect.

"""


def _generated_key_class(key_file, key_function):
    """The three mechanical classes --generate-ledger can derive without
    judgment (RESOLVER-IMPL/PROVEN/GUARDED are hand-applied afterward)."""
    if key_file == "src/tests" or key_file.startswith("src/tests/"):
        return "TEST-FIXTURE"
    if key_function.startswith("#"):
        return "DECL"
    return "TODO"


def build_generated_rows(sites):
    """Aggregate scanned sites into ledger rows, mechanically classified.
    Deterministic (file, function, token) order (the brief's Step 1
    requirement)."""
    counts = {}
    for site in sites:
        kind, name = site["attribution"], site["function"]
        if kind == "macro":
            function_key = f"#{name}"
        elif kind == "file-scope":
            function_key = "#decl"
        else:
            function_key = name
        key = (site["file"], function_key, site["token"])
        counts[key] = counts.get(key, 0) + 1

    rows = []
    for key in sorted(counts):
        key_file, key_function, key_token = key
        rows.append({
            "key_file": key_file, "key_function": key_function, "key_token": key_token,
            "count": counts[key], "cls": _generated_key_class(key_file, key_function),
            "kind": LEDGER_EMPTY_MARKER, "proof": LEDGER_EMPTY_MARKER,
        })
    return rows


def build_token_counts(sites):
    """Per-token total counts across every scanned site (all tokens, not
    just the ones that happened to hit -- a 0-site token still gets a row,
    matching the O-10 pinned-floor discipline)."""
    counts = {}
    for site in sites:
        counts[site["token"]] = counts.get(site["token"], 0) + 1
    return counts


def render_ledger(rows, token_counts):
    """The inverse of parse_ledger: rows/token_counts -> ledger markdown
    text. Must produce output parse_ledger accepts -- proven by the
    generate-ledger-round-trip self-test direction."""
    lines = [LEDGER_PROSE.rstrip("\n"), "", LEDGER_CLASSIFICATION_MARKER,
             LEDGER_CLASSIFICATION_HEADER, "| --- | --- | --- | --- | --- |"]
    for row in rows:
        key_text = KEY_SEPARATOR.join((row["key_file"], row["key_function"], row["key_token"]))
        proof_escaped = row["proof"].replace("|", "\\|")
        lines.append(
            f"| `{key_text}` | {row['count']} | {row['cls']} | {row['kind']} | {proof_escaped} |"
        )
    lines += ["", LEDGER_TOKEN_COUNTS_MARKER, LEDGER_TOKEN_COUNTS_HEADER, "| --- | --- |"]
    for token_name, _ in token_patterns():
        lines.append(f"| `{token_name}` | {token_counts.get(token_name, 0)} |")
    # The dispatch-entry registry is EMPTY in generated output, by design
    # (R3 Task 1a). Its rows are human judgment -- which functions are
    # dispatch entry points, which actor argument each one guards -- and this
    # generator mints no judgment (spec section 4/F-12, the same rule that
    # forbids machine-minted PROVEN rows). The marker and header are still
    # emitted so a regenerated ledger PARSES, and the empty table then fails
    # `--check` closed on both the row-count floor and every unregistered
    # dispatch site, which is the correct outcome: `--generate-ledger
    # --force-regenerate` discards hand-refined classification work, and the
    # registry is exactly such work.
    lines += ["", LEDGER_DISPATCH_ENTRIES_MARKER, LEDGER_DISPATCH_ENTRIES_HEADER,
              "| --- | --- | --- | --- |"]
    lines.append("")
    return "\n".join(lines)


def _generate_ledger(args):
    """--generate-ledger: fresh scan -> mechanically classified ledger file.
    Refuses to overwrite an existing ledger without --force-regenerate.
    Returns a process exit code."""
    repository_root = args.root.resolve()
    ledger_path = (
        args.ledger.resolve() if args.ledger is not None
        else repository_root / "docs" / "superpowers" / "room-resolve-ledger.md"
    )
    scanned_files = _resolve_scan_targets(args.paths, repository_root)
    minimum_files = (
        args.minimum_files_override if args.minimum_files_override is not None
        else MINIMUM_SCANNED_FILE_COUNT
    )
    if len(scanned_files) < minimum_files:
        print(
            f"error: only {len(scanned_files)} file(s) scanned under {repository_root} -- "
            f"expected at least {minimum_files}. This almost always means a broken --root or "
            "a typo'd path argument, not a genuine shrink of production src/.",
            file=sys.stderr,
        )
        return 1
    print(f"[scanned] {len(scanned_files)} file(s) under {repository_root} -- no directory "
          "is excluded (R-B8).")

    if ledger_path.exists() and not args.force_regenerate:
        print(
            f"error: ledger already exists at {ledger_path} -- pass --force-regenerate to "
            "overwrite it deliberately (this refusal is the direct guard against an accidental "
            "silent overwrite of hand-refined classification work).",
            file=sys.stderr,
        )
        return 1

    source_texts = {
        str(path): path.read_text(encoding="utf-8", errors="replace") for path in scanned_files
    }
    derived_family = derive_macro_family(source_texts)
    unknown_macros = sorted(derived_family - set(MACRO_FAMILY))
    if unknown_macros:
        print(
            f"error: macro family closed-world violation: {unknown_macros} not in MACRO_FAMILY -- "
            "refusing to generate a ledger against an open macro world (review and pin the new "
            "macro name first)",
            file=sys.stderr,
        )
        return 1

    sites = []
    for path in scanned_files:
        sites.extend(scan_file(path, repository_root))

    rows = build_generated_rows(sites)
    token_counts = build_token_counts(sites)

    ledger_path.parent.mkdir(parents=True, exist_ok=True)
    ledger_path.write_text(render_ledger(rows, token_counts), encoding="utf-8")

    todo_count = sum(row["count"] for row in rows if row["cls"] == "TODO")
    test_fixture_count = sum(row["count"] for row in rows if row["cls"] == "TEST-FIXTURE")
    decl_count = sum(row["count"] for row in rows if row["cls"] == "DECL")
    print(
        f"[generated] {ledger_path} -- {len(rows)} row(s): {todo_count} TODO site(s), "
        f"{decl_count} DECL site(s), {test_fixture_count} TEST-FIXTURE site(s)"
    )
    return 0


# Advisory-only pattern regexes (spec section 4/F-12). None of these feed
# the gate; they only drive --advise's stdout suggestions.
ADVISE_ENTRY_GUARD_RE = re.compile(r"\blocation_of\s*\([^()]*\)\s*(?:==|!=)\s*NOWHERE\b")
ADVISE_OCCUPANT_LOOP_RE = re.compile(
    r"\boccupants\s*\(|\boccupant_range\s*\(|\bsunlight\b", re.IGNORECASE)
ADVISE_LOOP_BOUND_RE = re.compile(r"\btop_of_world\b")


def build_advisories(scanned_files, repository_root):
    """For every production, non-macro/non-decl (i.e. TODO-candidate) site,
    suggest entry-guard?/occupant-loop?/loop-bound? per the brief's Step 2
    patterns. Returns a list of human-readable advisory strings, sorted for
    determinism. Advisory only -- no gate effect, reads nothing but source
    text, and never writes anything (self-test direction 3)."""
    advisories = []
    for path in scanned_files:
        rel = path.relative_to(repository_root).as_posix()
        if rel == "src/tests" or rel.startswith("src/tests/"):
            continue  # TEST-FIXTURE candidates never need advice
        text = path.read_text(encoding="utf-8", errors="replace")
        masked = mask_comments_and_string_literals(text).splitlines()
        attributions = attribute_lines(masked)

        function_lines = {}
        for i, (kind, name) in enumerate(attributions):
            if kind == "function":
                function_lines.setdefault(name, []).append(i)

        site_lines_by_key = {}
        for i, line in enumerate(masked):
            kind, name = attributions[i]
            if kind != "function":
                continue  # advise only makes TODO-candidate (function) sites
            for token_name, pattern in token_patterns():
                if pattern.search(line):
                    site_lines_by_key.setdefault((name, token_name), []).append(i)

        for (function_name, token_name), site_lines in sorted(site_lines_by_key.items()):
            func_lines = function_lines.get(function_name, [])
            entry_guard_line = next(
                (i for i in func_lines if ADVISE_ENTRY_GUARD_RE.search(masked[i])), None)
            occupant_line = next(
                (i for i in func_lines if ADVISE_OCCUPANT_LOOP_RE.search(masked[i])), None)
            loop_bound_candidates = list(site_lines)
            if entry_guard_line is not None:
                loop_bound_candidates.append(entry_guard_line)
            if occupant_line is not None:
                loop_bound_candidates.append(occupant_line)
            loop_bound_hit = any(
                ADVISE_LOOP_BOUND_RE.search(masked[i]) for i in loop_bound_candidates)

            flags = []
            if entry_guard_line is not None:
                flags.append(f"entry-guard? (line {entry_guard_line + 1})")
            if occupant_line is not None:
                flags.append(f"occupant-loop? (line {occupant_line + 1})")
            if loop_bound_hit:
                flags.append("loop-bound?")
            if flags:
                key_text = KEY_SEPARATOR.join((rel, function_name, token_name))
                advisories.append(f"{key_text}: {', '.join(flags)}")
    return sorted(advisories)


def _advise(args):
    """--advise: advisory-only stdout suggestions; no gate effect, no ledger
    reads or writes. Returns a process exit code (always 0 -- advisory)."""
    repository_root = args.root.resolve()
    scanned_files = _resolve_scan_targets(args.paths, repository_root)
    advisories = build_advisories(scanned_files, repository_root)
    if not advisories:
        print("advise: no advisory patterns matched any TODO-candidate site")
    for line in advisories:
        print(line)
    return 0


def _parse_caller_count_pins_override(raw_value, parser):
    """Self-test-only: `TOKEN=COUNT[,TOKEN=COUNT...]` -> the pin dict that
    REPLACES PINNED_CALLER_COUNTS. The empty string yields an empty dict
    (pin nothing), which is what every hermetic fixture root needs: a
    synthetic tree contains none of the real production callers, so applying
    the real 86/41 literals there would fail every unrelated direction."""
    pins = {}
    for chunk in raw_value.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        token_name, separator, count_text = chunk.partition("=")
        if not separator or not count_text.strip().lstrip("-").isdigit():
            parser.error(
                f"--caller-count-pins-override: expected TOKEN=COUNT pairs, got {chunk!r}"
            )
        pins[token_name.strip()] = int(count_text)
    return pins


def _parse_dispatch_exempt_sites_override(raw_value, parser):
    """Self-test-only: `file::function::token=count[;...]` -> the exemption
    map that REPLACES DISPATCH_TOKEN_EXEMPT_SITES. The empty string yields an
    empty map, which is what every hermetic fixture root needs: a synthetic
    tree contains none of the 26 real exempted sites, so applying the real
    map there would report all of them STALE in every unrelated direction.

    The parsed reason is a fixed placeholder -- only the COUNT participates
    in any check; the category and reason are documentation for humans."""
    exemptions = {}
    for chunk in raw_value.split(";"):
        chunk = chunk.strip()
        if not chunk:
            continue
        key_text, separator, count_text = chunk.partition("=")
        parts = key_text.split("::")
        if not separator or len(parts) != 3 or not count_text.strip().lstrip("-").isdigit():
            parser.error(
                "--dispatch-exempt-sites-override: expected "
                f"file::function::token=COUNT records, got {chunk!r}"
            )
        exemptions[tuple(part.strip() for part in parts)] = (
            int(count_text), "presence-test", "self-test override")
    return exemptions


def build_argument_parser():
    parser = argparse.ArgumentParser(description="Room-resolve retirement census.")
    parser.add_argument("paths", nargs="*", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--self-test", action="store_true",
                        help="prove the gate still fails in every direction it must")
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).parents[1])
    parser.add_argument("--ledger", type=pathlib.Path, default=None,
                        help="defaults to <root>/docs/superpowers/room-resolve-ledger.md")
    parser.add_argument(
        "--derive-macros", action="store_true",
        help="Print the macro family derived from all scanned files' "
             "#define bodies (headers AND .cpp files), one name per line, "
             "sorted (debug/maintenance).")
    parser.add_argument("--todo-ceiling-override", type=int, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1")
    parser.add_argument("--minimum-files-override", type=int, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1")
    parser.add_argument("--minimum-ledger-rows-override", type=int, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1")
    parser.add_argument("--minimum-dispatch-entries-override", type=int, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1")
    parser.add_argument("--caller-count-pins-override", type=str, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1; a comma-separated "
                             "TOKEN=COUNT list REPLACING PINNED_CALLER_COUNTS (the empty "
                             "string pins nothing, which is what every hermetic fixture root "
                             "wants -- a synthetic tree has none of the real callers)")
    parser.add_argument("--dispatch-exempt-sites-override", type=str, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1; a semicolon-separated "
                             "file::function::token=COUNT list REPLACING "
                             "DISPATCH_TOKEN_EXEMPT_SITES (the empty string exempts nothing, "
                             "which is what every hermetic fixture root wants -- a synthetic "
                             "tree has none of the real exempted sites)")
    parser.add_argument("--generate-ledger", action="store_true",
                        help="write docs/superpowers/room-resolve-ledger.md (or --ledger) from "
                             "a fresh scan: every production key TODO, every src/tests/ key "
                             "TEST-FIXTURE, every #decl/#NAME key DECL (Task 3 Step 1)")
    parser.add_argument("--force-regenerate", action="store_true",
                        help="allow --generate-ledger to overwrite an existing ledger file")
    parser.add_argument("--advise", action="store_true",
                        help="advisory-only stdout suggestions (entry-guard?/occupant-loop?/"
                             "loop-bound?) for TODO-candidate sites; no gate effect, no writes "
                             "(Task 3 Step 2)")
    return parser


def main(argv=None):
    parser = build_argument_parser()
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    if (args.todo_ceiling_override is not None or args.minimum_files_override is not None
            or args.minimum_ledger_rows_override is not None
            or args.minimum_dispatch_entries_override is not None
            or args.caller_count_pins_override is not None
            or args.dispatch_exempt_sites_override is not None) \
            and os.environ.get("RR_SELF_TEST") != "1":
        parser.error(
            "--todo-ceiling-override/--minimum-files-override/--minimum-ledger-rows-override/"
            "--minimum-dispatch-entries-override/--caller-count-pins-override/"
            "--dispatch-exempt-sites-override "
            "are self-test-only hooks -- set RR_SELF_TEST=1 to use them (see run_self_test())"
        )

    if args.generate_ledger and args.advise:
        parser.error("--generate-ledger and --advise are mutually exclusive")

    if args.generate_ledger:
        return _generate_ledger(args)

    if args.advise:
        return _advise(args)

    if args.derive_macros and args.paths:
        parser.error(
            "--derive-macros scans the whole tree; positional paths are not supported"
        )

    repository_root = args.root.resolve()

    if args.derive_macros:
        scanned_for_derive = source_files(repository_root / "src")
        source_texts = {
            str(path): path.read_text(encoding="utf-8", errors="replace")
            for path in scanned_for_derive
        }
        for name in sorted(derive_macro_family(source_texts)):
            print(name)
        return 0

    ledger_path = (
        args.ledger.resolve() if args.ledger is not None
        else repository_root / "docs" / "superpowers" / "room-resolve-ledger.md"
    )

    scanned_files = _resolve_scan_targets(args.paths, repository_root)

    minimum_files = (
        args.minimum_files_override if args.minimum_files_override is not None
        else MINIMUM_SCANNED_FILE_COUNT
    )
    if len(scanned_files) < minimum_files:
        print(
            f"error: only {len(scanned_files)} file(s) scanned under {repository_root} -- "
            f"expected at least {minimum_files}. This almost always means a broken --root or "
            "a typo'd path argument, not a genuine shrink of production src/.",
            file=sys.stderr,
        )
        return 1
    print(f"[scanned] {len(scanned_files)} file(s) under {repository_root} -- no directory "
          "is excluded (R-B8).")

    if not ledger_path.exists():
        raise SystemExit(
            f"error: ledger not found at {ledger_path} -- Task 3 generates "
            "docs/superpowers/room-resolve-ledger.md; nothing to reconcile against until then."
        )

    source_texts = {
        str(path): path.read_text(encoding="utf-8", errors="replace") for path in scanned_files
    }
    derived_family = derive_macro_family(source_texts)
    unknown_macros = sorted(derived_family - set(MACRO_FAMILY))

    sites = []
    dispatch_occurrences = []
    measured_caller_counts = {}
    attribution_errors = []
    for path in scanned_files:
        file_sites, file_dispatch, file_callers, file_attribution_errors = scan_file_full(
            path, repository_root)
        sites.extend(file_sites)
        dispatch_occurrences.extend(file_dispatch)
        attribution_errors.extend(file_attribution_errors)
        for token_name, count in file_callers.items():
            measured_caller_counts[token_name] = measured_caller_counts.get(token_name, 0) + count

    minimum_ledger_rows = (
        args.minimum_ledger_rows_override if args.minimum_ledger_rows_override is not None
        else MINIMUM_LEDGER_ROW_COUNT
    )
    ledger_text = ledger_path.read_text(encoding="utf-8")
    rows, token_counts = parse_ledger(ledger_text, minimum_rows=minimum_ledger_rows)

    minimum_dispatch_entries = (
        args.minimum_dispatch_entries_override
        if args.minimum_dispatch_entries_override is not None
        else MINIMUM_DISPATCH_ENTRY_ROWS
    )
    dispatch_entries = parse_dispatch_entries(
        ledger_text, minimum_rows=minimum_dispatch_entries)
    exempt_sites = (
        _parse_dispatch_exempt_sites_override(args.dispatch_exempt_sites_override, parser)
        if args.dispatch_exempt_sites_override is not None
        else DISPATCH_TOKEN_EXEMPT_SITES
    )
    dispatch_errors, dispatch_warnings = check_dispatch_entries(
        dispatch_entries, dispatch_occurrences, repository_root, exempt_sites)

    pinned_caller_counts = (
        _parse_caller_count_pins_override(args.caller_count_pins_override, parser)
        if args.caller_count_pins_override is not None
        else PINNED_CALLER_COUNTS
    )
    caller_count_errors = check_caller_counts(measured_caller_counts, pinned_caller_counts)

    if MAXIMUM_TODO_COUNT is None and args.todo_ceiling_override is None:
        print("notice: MAXIMUM_TODO_COUNT is unset -- ceiling check skipped (Task 3 seeds it)",
              file=sys.stderr)
    todo_ceiling = (
        args.todo_ceiling_override if args.todo_ceiling_override is not None
        else MAXIMUM_TODO_COUNT
    )

    errors = reconcile(sites, rows, token_counts, todo_ceiling=todo_ceiling)
    errors.extend(dispatch_errors)
    errors.extend(caller_count_errors)
    errors.extend(attribution_errors)
    errors.extend(check_pinned_caller_macro_aliases(source_texts, pinned_caller_counts))
    for warning in dispatch_warnings:
        print(f"WARNING: {warning}", file=sys.stderr)
    if unknown_macros:
        errors.insert(
            0,
            f"macro family closed-world violation: {unknown_macros} not in MACRO_FAMILY -- a "
            "new resolver-reaching macro must be reviewed and added to the pinned literal"
        )

    for error in errors:
        print(error)

    if args.check and errors:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
