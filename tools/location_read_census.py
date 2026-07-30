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
``ls2-wholebranch-review-opus.md``). Its disposition, and every other room-id
store's, is now recorded closed-world in the ledger's "Location-state
registry" (Tables A/B), cross-checked against ``TOKEN_PATTERNS`` below by
``--check`` -- see ``parse_registry``/``check_registry_consistency``.

Wave **LS-3b T5** (THE SPLIT) privatized ``char_data::in_room`` /
``char_data::next_in_room`` / ``room_data::people`` by RENAME, to
``char_data::ls_location_id_`` / ``char_data::ls_next_in_room_`` /
``room_data::ls_first_occupant_`` (ruling R-3b-A, the private-handle keying
option). The rename is what makes corrected criterion (a)'s "no location
field" clause provable: the OLD spellings are now simply gone from
``char_data``/``room_data``, so any surviving ``->in_room``/``.people``/etc.
site is necessarily either ``obj_data``/``shop_data`` (objects were never in
this wave's charter) or a genuine regression. **T8** re-tokened this script
for the post-split tree: the five original tokens STAY exactly as they were
(deleting one would re-open a blind spot for the object-side population that
still legitimately uses the ``in_room``/``people`` spellings, and every
surviving char/room-side annotation under them now serves as a
reintroduction TRIPWIRE instead of a live-representation marker), and three
new BARE-WORD tokens were added for the private spellings themselves --
``ls_location_id_``/``ls_next_in_room_``/``ls_first_occupant_`` -- for a
total of **eight**. T8 also retired a second reason prefix (``manual
occupant-list splice``, zero production lines; nine prefixes now) and raised
``MINIMUM_SCANNED_FILE_COUNT`` 250 -> 300 against a 315-file scan. See
``ls3b-t8-report.md`` for the full token/prefix audit.

This census is the checked-in regression gate (LS-1 T3, widened by LS-2
T5, again by LS-3a T4, and again by LS-3b T8): it flags any raw token
outside the census-named allow-list file set or an inline
``// LS1-ALLOW: <reason>`` annotation ("LS1" names LocationSystem
Stage 1, which spans both LS-1 and LS-2 -- see the ledger doc). Modeled on
``tools/string_view_census.py`` (rglob discovery, comment/string masking,
``--check`` mode, non-zero exit on violation) -- see
``.superpowers/sdd/ls1-census.md`` Step 8 and ``.superpowers/sdd/ls2-census.md``
/ ``ls2-census-b.md`` PART 3 for the full design, and
``docs/superpowers/location-read-allowlist.md`` for the allow-listed file
set this script reads via ``--exceptions``.

TWO LIMITS ARE KNOWN AND DELIBERATE (LS-3a follow-up PR, review-1 F3/F4):

*Multi-line spellings stay latent.* The matcher is strictly line-based, so a
token split across a physical line break -- ``ch->`` with ``in_room`` alone on
the next line, or an identifier split by a backslash-continuation
(``next_in_`` / ``room``) -- is invisible to it. Ruling AM-5 WITHDREW a proposed
multi-line matcher: re-deriving across lines what the per-line annotation
contract already states would put that contract at risk, for a shape nothing in
this tree produces (``clang-format`` breaks BEFORE a ``->``, which leaves the
token whole on the following line -- and that line IS matched; the
``people-split-write-flagged`` self-test case pins exactly that). What the
follow-up did close is every *line-feasible* spelling -- see TOKEN_PATTERNS.

*Annotations are line-scoped, not token-scoped.* One ``// LS1-ALLOW: <reason>``
silences every tracked token on its line, so a line carrying two tokens of
different character (``room_by_id_total(obj->in_room)->people`` -- an object
location AND an occupant-chain head read) is covered by whichever single reason
its author wrote. Enforcing one-token-per-annotated-line was MEASURED and
rejected: 21 annotated lines tree-wide carry two or more distinct tokens, and
splitting them would churn live expressions to restate what the reason already
says. The follow-up instead hand-audited all 21 and reworded the five whose
reason was silent on a materially different second token (the F13 precedent set
at ``src/combat/mystic.cpp:671``). The limit itself is pinned by the
``multi-token-line-scoped-annotation`` self-test case, so it stays a documented
property rather than an accident.
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
# is precisely the regression this wave must make impossible. LS-3b T8
# re-measured the scanned count against the current tree (315 -- the wave's
# own T2/T3 tasks added files such as render_cursor.h and the perf-benchmark
# headers/tests) and raised the floor again, 250 -> 300: still 15 files of
# headroom below the real count (proportionally similar to the 250-against-
# 307 headroom T4 itself left), tight enough that losing a handful of files
# from the sweep is caught rather than tolerated.
MINIMUM_SCANNED_FILE_COUNT = 300

# LS-3b T9b (review-1 finding m-13, second half): the tuple stays CLOSED-WORLD
# by design -- an open-world "scan every file" sweep would pull in goldens,
# JSON fixtures and world data -- but it was missing six C++ spellings a
# future file could legitimately use, each of which would have been a silent
# blind spot: `.hxx`/`.h++` (alternate header spellings), `.tcc` (libstdc++'s
# own template-implementation convention), `.inc` (textual include), and
# `.ixx`/`.cppm` (C++20 module interface units -- not used in this codebase
# today, which is exactly the kind of new-file-type migration that reopens a
# gate hole). A suffix nothing uses costs nothing to list.
SOURCE_SUFFIXES = (".cpp", ".h", ".hpp", ".cc", ".cxx", ".c", ".inl", ".ipp",
                   ".hxx", ".h++", ".tcc", ".inc", ".ixx", ".cppm")
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
#
# LINE-FEASIBLE SPELLINGS (LS-3a follow-up, review-1 F3). The three accessor
# patterns tolerate whitespace around the accessor (``ch-> in_room``,
# ``room . people`` -- what a hand edit or an unusual formatting run leaves
# behind), and ``::`` adds qualified / member-pointer access
# (``&char_data::in_room``, ``&room_data::people``), which reaches the
# representation with no object at all. ``next_in_room`` needed no widening:
# ``\bnext_in_room\b``'s leading word boundary already matches after ``::``
# (``:`` is a non-word character) -- the ``qualified-next_in_room`` self-test
# case pins that rather than leaving it to inspection.
#
# The ``\s*`` widen cannot over-match, and this is why: ``\.`` followed by
# optional whitespace and the LITERAL field name is a member access in every
# C++ spelling there is. A decimal literal (``1.5``) fails it on the digit, an
# ellipsis (``...)``) fails it on the punctuation, and the bare word ``in_room``
# elsewhere on the line has no accessor before it -- all three are pinned green
# in the self-test. The anchoring discipline is unchanged, so ``was_in_room`` /
# ``next_in_room`` still cannot trip the ``.in_room`` pattern (the character
# after the accessor is ``w`` / ``n``) and ``peoples`` / ``mypeople`` still
# cannot trip the ``people`` one.
TOKEN_PATTERNS = (
    ("->in_room", re.compile(r"->\s*in_room\b")),
    (".in_room", re.compile(r"\.\s*in_room\b")),
    ("::in_room", re.compile(r"::\s*in_room\b")),
    ("world[", re.compile(r"\bworld\s*\[")),
    ("next_in_room", re.compile(r"\bnext_in_room\b")),
    ("people", re.compile(r"(?:->|\.|::)\s*people\b")),
    # LS-3b T5 privatized char_data::in_room / char_data::next_in_room /
    # room_data::people by RENAME (not deletion) to char_data::ls_location_id_ /
    # char_data::ls_next_in_room_ / room_data::ls_first_occupant_ -- see
    # ls3b-t5-report.md Sec 1.1. Left untracked, these three new spellings would
    # be a silent blind spot exactly like the pre-.people gap R-B6 closed: every
    # production line the T5 rename touched still carries its ORIGINAL
    # annotation comment (the rename changed only the field spelling, not the
    # prose beside it), but with no token matching the new spelling those
    # annotations sit on lines this gate no longer even LOOKS at -- measured at
    # T8 kickoff: zero findings anywhere in the tree for any of the three new
    # names before this token widen landed, including the two field
    # DECLARATIONS themselves (character.h / room.h), neither of which had ever
    # needed to carry a `representation-decl` annotation because nothing
    # previously required one. Unlike `people` (an English word needing an
    # accessor anchor to avoid over-matching), each of these three identifiers
    # is unique enough tree-wide to use a bare `\b...\b` word-boundary pattern
    # with NO accessor requirement -- the same design LS-1 chose for
    # `next_in_room` and for the same reason: a bare-word pattern also catches
    # the field's own DECLARATION (no `->`/`.`/`::` precedes a declaration),
    # which an accessor-anchored pattern structurally cannot. `::` access needs
    # no special widening either, unlike the original `in_room`/`people`
    # patterns before their LS-3a follow-up widen: the character immediately
    # before the identifier after `::` is `:`, a non-word character, so `\b`
    # already matches there with no extra alternation.
    ("ls_location_id_", re.compile(r"\bls_location_id_\b")),
    ("ls_next_in_room_", re.compile(r"\bls_next_in_room_\b")),
    ("ls_first_occupant_", re.compile(r"\bls_first_occupant_\b")),
    # LS-3b deferred-MINORs follow-up (spec review O-3/F-2). The fourth ls_*
    # private store -- the login-window VNUM channel. Same bare-word design
    # and same rationale as the three rename tokens above: the spelling is
    # unique tree-wide, and a bare pattern also catches the declaration.
    # Promoted instead of `was_in_room` (the original draft's candidate):
    # this one costs exactly ONE annotation (the declaration; the only two
    # real access lines live in the whole-file-exempt representation owner,
    # placement.cpp), fits an existing prefix, and converts the registry's
    # ACCESSOR-GATED prose claim into a checked invariant.
    ("ls_load_room_vnum_", re.compile(r"\bls_load_room_vnum_\b")),
    # THE PASTE OPERATOR ITSELF (LS-3b T9b; review-1 finding m-13). The
    # whole-branch review demonstrated a working evasion of every pattern
    # above, and it compiled clean into `ageland`:
    #
    #     #define LS_EVADE_A(a, b) a##b
    #     ch->LS_EVADE_A(ls_location, _id_) = 0;
    #
    # Neither line ever contains a tracked identifier as a contiguous run of
    # characters -- the preprocessor assembles it at expansion time -- so no
    # amount of widening the identifier patterns can reach it. Line-based
    # normalization does not help either: the `##` sits on the DEFINITION and
    # the operands sit on the USE, in different files as often as not.
    #
    # So the operator is tracked directly. This is the cheapest closure that
    # actually works, and it is free in this tree: `##` appears on exactly TWO
    # lines tree-wide, both inside string literals ("Usage: top ##"), which
    # the masker blanks before this pattern ever sees them -- so the gate
    # stays green today and the FIRST future use of token pasting anywhere
    # under src/ has to justify itself with a `token-paste` annotation. A
    # single `#` (stringize, and every `#include`/`#define` line in the tree)
    # is deliberately NOT matched.
    #
    # WHAT THIS STILL DOES NOT CATCH, said plainly rather than left implied:
    # a member pointer handed out of an allow-listed file
    # (`&char_data::ls_location_id_` as an `int char_data::*`), a
    # `reinterpret_cast` over the struct, or a hand-computed offset. Those
    # are not name-based evasions, and neither this gate nor the compile-time
    # static_assert companion in src/entity/placement.cpp can see them. The
    # gate is a tripwire against accidental reintroduction plus the one
    # deliberate trick that was actually demonstrated -- not a sandbox, and
    # docs/superpowers/location-read-allowlist.md now says so.
    ("## (preprocessor token paste)", re.compile(r"##")),
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
# stayed ELEVEN); it widened what `representation-impl` covers to a third
# class its wording already fits, the occupant-chain SHAPE assertions in
# `src/tests/spec_pro_tests.cpp`/`load_room_placement_tests.cpp` that pin
# raw `next_in_room` links because no Stage-1 API expresses a tail walk.
# LS-3b T2 RETIRED `in_room used as mutable room cursor` (the fail-closed
# burndown rule, .superpowers/sdd/ls3b-global-constraints.md): its last
# production line converted onto rots::entity::ScopedRenderLocation in the
# same commit that removed it here, bringing the count to TEN. LS-3b T8
# re-audited every remaining prefix's live line count tree-wide (per-prefix
# counts in ls3b-t8-report.md) and RETIRED a second one, `manual
# occupant-list splice`: it has carried zero PRODUCTION lines since at least
# the T0 census (ls3b-census-d.md Sec 6.2, "0 production / 6 test"), and its
# six surviving test-tier sites (src/tests/test_placement.h) sit inside a
# file this gate already exempts WHOLE -- `findings_for_file` returns before
# ever reaching the annotation check there, so those six comments were
# already inert documentation, not gate-enforced text, both before and after
# this retirement. Removing the prefix is fail-closed by construction (an
# off-list reason still fails `--check` as `invalid-reason`), so retiring it
# changes nothing observable in test_placement.h and closes the count to
# NINE. See docs/superpowers/location-read-allowlist.md for the full
# per-prefix disposition table.
ALLOWED_REASON_PREFIXES = (
    "save-next",
    "peek-ahead",
    "manual first-match advance",
    "write",
    "obj-location",
    "resolver-impl",
    "representation-decl",
    "representation-impl",
    "not-a-location",
    # LS-3b T9b (review-1 finding m-13): the annotation path for the `##`
    # token above. Minted because the token has no other escape hatch and a
    # legitimate token-pasting macro is a perfectly ordinary thing for a
    # future wave to write -- it just has to be visible when it happens. The
    # prefix count goes nine -> ten; T8 had taken it eleven -> nine.
    "token-paste",
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


# The registry's two tables are marker-anchored for the same reason the
# allow-list table is (Opus M10: a look-alike table elsewhere in the doc must
# be inert). Floors are pinned per table so the check can never pass by
# finding an empty or truncated registry.
REGISTRY_TABLE_A_MARKER = "<!-- LOCATION-STATE-REGISTRY-TABLE-A -->"
REGISTRY_TABLE_A_HEADER = "| Store | Declared at | Kind | Repr | Coverage |"
REGISTRY_TABLE_B_MARKER = "<!-- LOCATION-STATE-REGISTRY-TABLE-B -->"
REGISTRY_TABLE_B_HEADER = "| Carrier | Declared at | Repr | Class |"
#
# The A floor is pinned AT the real ledger's current row count, 9 -- zero
# headroom, deliberately. Review-1 (round 1) demonstrated concretely that an
# 8 floor (one row of headroom) lets a real single-row deletion through
# undetected: dropping `shop_data::in_room` (whose coverage tokens are
# byte-duplicates of `obj_data::in_room`'s, so Direction 1/2 see nothing
# missing) or `was_in_room` (UNTRACKED, carries no coverage tokens at all)
# both parse clean at floor 8 with ZERO consistency errors -- the row-count
# floor is the ONLY backstop for exactly that class of masked/uncovered row
# loss, so it must trip on any single-row deletion from the real table
# (9 -> 8). The self-test's own fixture (`SELF_TEST_REGISTRY_OK`) carries the
# one row of headroom instead: it has 10 rows, one more than the real
# ledger's 9, purely so sabotage (b)'s one-row drop lands AT 9 (still >= the
# floor) and reaches `check_registry_consistency`'s Direction 2 rather than
# tripping the parser's floor first. Production tightness and self-test
# reach are two different rows now, not one row serving both jobs.
MINIMUM_REGISTRY_ROWS_A = 9
MINIMUM_REGISTRY_ROWS_B = 6

# Tokens that legitimately map to no Table-A row. Structural tokens track
# syntax, not stores; the retired-spelling guards track member names no
# current store spells (kept to catch reintroduction). Validity condition
# recorded in the ledger beside the registry.
REGISTRY_EXEMPT_TOKENS = frozenset({
    "world[",
    "## (preprocessor token paste)",
    "next_in_room",
    "people",
})


def _strip_fences(text):
    """Drop fenced code blocks -- documentation, never data (M10 precedent)."""
    live_lines = []
    in_fence = False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            live_lines.append(line)
    return live_lines


def _parse_marked_table(live_lines, marker, header, minimum_rows, label):
    """Parse the rows of the single marker-anchored table. Fails closed."""
    marker_count = sum(1 for line in live_lines if line.strip() == marker)
    if marker_count != 1:
        raise SystemExit(
            f"error: registry: {label} marker {marker!r} must appear exactly once "
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
                    f"error: registry: the line after {marker!r} must be "
                    f"{header!r}, got {stripped!r}."
                )
            state = "in-table"
            continue
        if stripped.startswith("|") and set(stripped) <= set("|- :"):
            continue
        if not stripped.startswith("|"):
            break
        cells = [cell.strip() for cell in stripped.strip("|").split("|")]
        first_cell_match = re.match(r"^`([^`]+)`", cells[0])
        if first_cell_match is None:
            continue
        spelling = first_cell_match.group(1)
        member = spelling.rsplit("::", 1)[-1].rsplit(".", 1)[-1]
        coverage_tokens = re.findall(r"`([^`]+)`", cells[-1]) if cells[-1].startswith("TOKEN") else []
        rows.append({"store": spelling, "member": member, "coverage_tokens": coverage_tokens})
    if len(rows) < minimum_rows:
        raise SystemExit(
            f"error: registry: {label} has {len(rows)} rows, below the floor of "
            f"{minimum_rows} -- a truncated registry must not pass."
        )
    return rows


def parse_registry(text):
    """Parse the location-state registry (Tables A and B) out of ledger text."""
    live_lines = _strip_fences(text)
    rows_a = _parse_marked_table(live_lines, REGISTRY_TABLE_A_MARKER,
                                 REGISTRY_TABLE_A_HEADER, MINIMUM_REGISTRY_ROWS_A, "Table A")
    rows_b = _parse_marked_table(live_lines, REGISTRY_TABLE_B_MARKER,
                                 REGISTRY_TABLE_B_HEADER, MINIMUM_REGISTRY_ROWS_B, "Table B")
    return rows_a, rows_b


def check_registry_consistency(rows_a, rows_b):
    """The bidirectional closed-world cross-check (spec 1.3). Returns errors."""
    del rows_b  # Table B is floor-checked by the parser; it carries no tokens.
    errors = []
    patterns_by_name = dict(TOKEN_PATTERNS)

    # Direction 1: every TOKEN row names real tokens whose pattern matches a
    # synthesized probe for the member. Accessor-anchored patterns cannot
    # match a bare spelling (the anchor is their point -- spec review O-5), so
    # probe with each access shape and accept any hit.
    for row in rows_a:
        for token_name in row["coverage_tokens"]:
            pattern = patterns_by_name.get(token_name)
            if pattern is None:
                errors.append(
                    f"registry row `{row['store']}` names token `{token_name}`, "
                    f"which is not in TOKEN_PATTERNS"
                )
                continue
            member = row["member"]
            probes = (member, f"x->{member}", f"x.{member}", f"char_data::{member}")
            if not any(pattern.search(probe) for probe in probes):
                errors.append(
                    f"registry row `{row['store']}`: token `{token_name}`'s pattern "
                    f"matches no synthesized probe for member `{member}`"
                )

    # Direction 2: every non-exempt token maps to at least one Table-A row.
    covered = {name for row in rows_a for name in row["coverage_tokens"]}
    for token_name, _ in TOKEN_PATTERNS:
        if token_name in REGISTRY_EXEMPT_TOKENS:
            continue
        if token_name not in covered:
            errors.append(
                f"token `{token_name}` has no location-state registry row and no "
                f"exemption -- register the store or exempt the token, in the ledger"
            )
    return errors


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

# A synthetic, known-good registry mirroring the real one's token coverage.
# Used only by --self-test; the real registry lives in the ledger doc and is
# asserted by --check. If a future wave adds a token, this fixture needs the
# matching row too -- the self-test failing here is the reminder.
#
# This fixture is deliberately 10 Table-A rows, ONE more than the real
# ledger's 9: the extra `synthetic_self_test_fixture::ls_headroom_only_`
# row (review-1 round 1) exists purely so sabotage (b) below -- which drops
# exactly one row -- lands the probe AT 9 rows (still >= the production
# floor, `MINIMUM_REGISTRY_ROWS_A`) instead of one row BELOW it. Without this
# extra row, dropping a row from a 9-row fixture would trip the parser's own
# fail-closed floor check before `check_registry_consistency`'s Direction 2
# ever ran, masking the very check the probe exists to exercise -- exactly
# the bug review-1 caught when the floor itself had been lowered to 8
# instead. It carries UNTRACKED-BY-DESIGN coverage (no coverage tokens), so
# it participates in neither Direction 1 (nothing to mismatch) nor
# Direction 2 (it names no token) -- its only job is row-count headroom.
SELF_TEST_REGISTRY_OK = """
<!-- LOCATION-STATE-REGISTRY-TABLE-A -->
| Store | Declared at | Kind | Repr | Coverage |
| --- | --- | --- | --- | --- |
| `char_data::ls_location_id_` | `core/character.h:862` | live | rnum | TOKEN `ls_location_id_` |
| `char_data::ls_next_in_room_` | `core/character.h:898` | live | handle | TOKEN `ls_next_in_room_` |
| `room_data::ls_first_occupant_` | `core/room.h:126` | live | handle | TOKEN `ls_first_occupant_` |
| `char_data::specials.ls_load_room_vnum_` | `core/character.h:353` | live | vnum | TOKEN `ls_load_room_vnum_` |
| `char_data::specials.was_in_room` | `core/character.h:340` | live | rnum | UNTRACKED-BY-DESIGN (R-C5) |
| `char_special2_data::load_room` | `core/types.h` | PERSISTED | vnum | UNTRACKED-BY-DESIGN (pervasive) |
| `affected_type::modifier` | `core/types.h:719` | PERSISTED | rnum | UNTRACKED-BY-DESIGN (generic name) |
| `obj_data::in_room` | `core/object.h:165` | deferred | rnum | TOKEN `->in_room` `.in_room` `::in_room` |
| `shop_data::in_room` | `app/shop.cpp:63` | not-a-location | vnum | TOKEN `->in_room` `.in_room` `::in_room` |
| `synthetic_self_test_fixture::ls_headroom_only_` | `self-test fixture only` | synthetic | n/a | UNTRACKED-BY-DESIGN (self-test row-count headroom only, review-1 round 1) |

<!-- LOCATION-STATE-REGISTRY-TABLE-B -->
| Carrier | Declared at | Repr | Class |
| --- | --- | --- | --- |
| `room_direction_data::to_room` | `core/types.h:395` | rnum | world topology |
| `shop_data::stock_room` | `app/shop.cpp:64` | vnum | object-parameter |
| `obj_data::obj_flags.value[0]` | `core/object.h` | vnum | object-parameter |
| `target_data::ptr.room` | `core/types.h:248` | handle | transient targeting |
| `r_mortal_start_room[]` etc. | `core/consts.cpp` | rnum | boot configuration |
| `r_bugged_start_room` | `core/consts.cpp:2554` | vnum | boot configuration |
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
    # --- the LINE-FEASIBLE SPELLINGS (LS-3a follow-up, review-1 F3). Each
    # reaches the representation exactly as the canonical spelling does. SIX
    # of the seven were invisible to this gate before the widen -- measured by
    # running each body against the pre-widen patterns, where all six matched
    # nothing. The seventh, `qualified-next_in_room`, already matched (the
    # `\b` boundary), and is here to PIN that rather than to close a gap.
    ("arrow-spaced-in_room", "int a = ch-> in_room;\n", 1),
    ("dot-spaced-in_room", "int a = character . in_room;\n", 1),
    ("qualified-in_room", "auto member = &char_data::in_room;\n", 1),
    ("qualified-next_in_room", "auto member = &char_data::next_in_room;\n", 1),
    ("qualified-people", "auto member = &room_data::people;\n", 1),
    ("arrow-spaced-people", "char_data* head = room-> people;\n", 1),
    ("dot-spaced-people", "char_data* head = room . people;\n", 1),
    # Over-match controls for that widen, both deliberately CODE-level: the
    # risky text sits in live code, not in a comment, so comment masking
    # cannot be what makes them green -- only the pattern's own discipline
    # can. Each pairs a real non-accessor dot (a decimal literal; an ellipsis)
    # with the bare word `in_room` LATER ON THE SAME LINE, which is exactly
    # what a widen from `\s*` to any looser inter-token class would swallow.
    # (A first draft used `// in_room mentioned in prose` and an
    # `in_room_total` identifier; sabotaging the pattern to `\.[^;]*in_room\b`
    # left both green -- the `;` and the `\b` did the work, not the widen, so
    # they proved nothing. These bodies go RED under that same sabotage
    # extended past the semicolon, which is the property a control needs.)
    ("float-not-an-accessor", "double ratio = 1.5; int in_room = 0;\n", 0),
    ("ellipsis-not-an-accessor",
     "void trace(const char* fmt, ...); int in_room = 0;\n", 0),
    # --- the LINE-SCOPED ANNOTATION limit (review-1 F4), pinned as the
    # documented behavior it is: ONE reason silences EVERY token on its line.
    # This body is `src/app/comm.cpp:2714` verbatim -- two tokens of different
    # character (an object location and an occupant-chain head read) under a
    # single `obj-location` reason. Measured at 21 such lines tree-wide when
    # this was written; enforcement was considered and rejected (see the module
    # docstring for the measurement and the reasoning). A future wave that
    # enforces token-scoping must invert this case DELIBERATELY.
    ("multi-token-line-scoped-annotation",
     "to = room_by_id_total(obj->in_room)->people; // LS1-ALLOW: obj-location\n", 0),
    # --- LS-3b T8: the three NEW private-handle tokens (the T5 rename's
    # spellings). Each pair proves the token fires unannotated and stays green
    # once annotated -- the same shape every earlier token's pair uses.
    ("ls-location-id-unannotated", "int a = ch->ls_location_id_;\n", 1),
    ("ls-location-id-annotated",
     "int a = ch->ls_location_id_; // LS1-ALLOW: write\n", 0),
    ("ls-next-in-room-unannotated", "int a = ch->ls_next_in_room_ != nullptr;\n", 1),
    ("ls-next-in-room-annotated",
     "int a = ch->ls_next_in_room_ != nullptr; // LS1-ALLOW: save-next (probe)\n", 0),
    ("ls-first-occupant-unannotated", "char_data* h = room->ls_first_occupant_;\n", 1),
    ("ls-first-occupant-annotated",
     "char_data* h = room->ls_first_occupant_; // LS1-ALLOW: representation-impl (probe)\n", 0),
    # The three new tokens are BARE-WORD (`\bls_..._\b`, no accessor anchor
    # required) precisely so a field's own DECLARATION -- which has no `->`/
    # `.`/`::` before it -- is caught too, the same design `next_in_room` used
    # and the direct reason character.h:861/room.h:126 (the two real
    # declarations) needed a `representation-decl` annotation added this task
    # rather than staying invisible. This body has NO accessor at all.
    ("ls-location-id-bare-declaration", "int ls_location_id_;\n", 1),
    ("ls-location-id-bare-declaration-annotated",
     "int ls_location_id_; // LS1-ALLOW: representation-decl (probe)\n", 0),
    # `::` qualified access needs no special-casing for these three tokens
    # (unlike the original `in_room`/`people` patterns' LS-3a follow-up widen):
    # the character before the identifier after `::` is `:`, already a
    # non-word boundary, so the bare `\b...\b` pattern matches it for free.
    ("ls-location-id-qualified", "auto m = &char_data::ls_location_id_;\n", 1),
    # Over-match guards, both directions: a longer identifier that merely
    # CONTAINS one of the three new tokens as a substring -- prefixed or
    # suffixed -- must not trip the gate. `\b` on both ends of the pattern is
    # what prevents it; these bodies go RED if either boundary is dropped.
    ("ls-location-id-prefixed-field-not-matched",
     "int x_ls_location_id_ = 0;\n", 0),
    ("ls-location-id-suffixed-field-not-matched",
     "int ls_location_id_2 = 0;\n", 0),
    # --- LS-3b T8: the fail-closed prefix retirement. `manual occupant-list
    # splice` was removed from ALLOWED_REASON_PREFIXES this task (zero
    # production lines; its six surviving sites sit inside the whole-file-
    # exempt src/tests/test_placement.h and were never gate-enforced either
    # way). A reason that used to be valid must now fail as `invalid-reason`
    # -- proving the retirement actually changed gate behavior, not just the
    # tuple's literal contents. (Reverting the retirement, i.e. adding the
    # prefix back, turns this case green again -- the sabotage direction a
    # retirement always needs.)
    ("retired-prefix-no-longer-authorized",
     'char_data* h = room->people; // LS1-ALLOW: manual occupant-list splice (retired)\n', 1),
    # --- LS-3b T9b: the token-paste evasion (review-1 finding m-13, probe P6).
    # The reviewer's own probe, verbatim in shape. Note that NEITHER line
    # contains a tracked identifier as a contiguous run of characters: that is
    # the entire finding, and it is why the operator rather than the
    # reassembled name is what this gate now tracks.
    ("token-paste-evasion",
     "#define LS_EVADE_A(a, b) a##b\nint z = ch->LS_EVADE_A(ls_location, _id_);\n", 1),
    # The annotation path for the new token: a legitimate paste is silenceable,
    # like every other tracked spelling.
    ("token-paste-annotated",
     "#define JOIN(a, b) a##b // LS1-ALLOW: token-paste (a real one)\n", 0),
    # ...and only under the NEW prefix -- proving the mint changed gate
    # behavior rather than just the tuple's contents.
    ("token-paste-bogus-reason",
     "#define JOIN(a, b) a##b // LS1-ALLOW: not-an-authorized-reason\n", 1),
    # OVER-MATCH CONTROLS. A single `#` -- stringize, and every #include/
    # #define/#ifdef line in the tree -- must not fire, or the gate would flag
    # thousands of lines. And the two `##` occurrences that DO exist in the
    # tree today live inside string literals; masking runs first, so they stay
    # green (src/app/act_wiz.cpp:4207's "Usage: top ##" is this body verbatim).
    ("stringize-single-hash-not-matched", "#define STR(x) #x\nconst char* s = STR(a);\n", 0),
    ("include-directive-not-matched", "#include \"handler.h\"\nint clean = 0;\n", 0),
    ("token-paste-inside-string-literal",
     'send_to_char("Usage: top ## [[oldest] race].", ch);\n', 0),
    # --- LS-3b T9b: SOURCE_SUFFIXES coverage for the six spellings added with
    # the paste token. `header-suffix-scanned` above proves `.h` is scanned;
    # these prove the closed-world tuple was widened for real, across a
    # header-shaped, a template-implementation and a module suffix.
    ("hxx-suffix-scanned", "int a = ch->in_room;\n", 1, "probe.hxx"),
    ("tcc-suffix-scanned", "int a = ch->in_room;\n", 1, "probe.tcc"),
    ("cppm-suffix-scanned", "int a = ch->in_room;\n", 1, "probe.cppm"),
    # LS-3b deferred-MINORs follow-up (spec review O-3/F-2): the fourth ls_*
    # private store gets the same bare-word token as its three siblings. The
    # ACCESSOR-GATED claim in the location-state registry is otherwise
    # enforced by nothing -- a raw write to the channel anywhere outside the
    # representation owner must be visible to this gate.
    ("ls-load-room-vnum-unannotated", "ch->specials.ls_load_room_vnum_ = 5;\n", 1),
    ("ls-load-room-vnum-annotated",
     "ch->specials.ls_load_room_vnum_ = 5; // LS1-ALLOW: write (probe)\n", 0),
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
    # a 307-file scan; LS-3b T8 raised it again to 300 against a 315-file
    # scan; raising it later is fine and needs this literal raised with it,
    # which is the deliberate second edit.
    if MINIMUM_SCANNED_FILE_COUNT < 300:
        failures.append(
            f"MINIMUM_SCANNED_FILE_COUNT is {MINIMUM_SCANNED_FILE_COUNT}, below the 300 that "
            "LS-3b T8 set against a 315-file scan -- a lowered floor lets a broken scan "
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

        # ------------------------------------------------------------------
        # Location-state registry cross-check (deferred-MINORs follow-up,
        # spec 1.3). All synthetic: the real ledger is never read or mutated
        # here -- --check owns the real-doc assertion.
        # ------------------------------------------------------------------
        rows_a, rows_b = parse_registry(SELF_TEST_REGISTRY_OK)
        errors = check_registry_consistency(rows_a, rows_b)
        if errors:
            failures.append(f"registry: the known-good synthetic registry failed: {errors}")

        # Sabotage (a): a TOKEN row naming a token that does not exist.
        bad_a, bad_b = parse_registry(
            SELF_TEST_REGISTRY_OK.replace("TOKEN `ls_location_id_`",
                                          "TOKEN `no_such_token_`"))
        if not check_registry_consistency(bad_a, bad_b):
            failures.append("registry: a row naming a nonexistent token was not caught")

        # Sabotage (b): a non-exempt token with no registry row. Drop the
        # ls_load_room_vnum_ row entirely.
        dropped = "\n".join(line for line in SELF_TEST_REGISTRY_OK.splitlines()
                            if "ls_load_room_vnum_" not in line)
        drop_a, drop_b = parse_registry(dropped)
        if not check_registry_consistency(drop_a, drop_b):
            failures.append("registry: a rowless non-exempt token was not caught")

        # Sabotage (c): registry missing / unparseable -- parse_registry must
        # raise, not return empty (fail closed).
        for broken in ("no tables at all",
                       SELF_TEST_REGISTRY_OK.replace(
                           "<!-- LOCATION-STATE-REGISTRY-TABLE-A -->", "")):
            try:
                parse_registry(broken)
                failures.append("registry: a missing/unmarked table parsed as success")
            except SystemExit:
                pass

        # Sabotage (c'): below the row floor -- Table A truncated to one row.
        header_end = SELF_TEST_REGISTRY_OK.index("| `char_data::ls_next_in_room_`")
        try:
            parse_registry(SELF_TEST_REGISTRY_OK[:header_end])
            failures.append("registry: a below-floor Table A parsed as success")
        except SystemExit:
            pass

        # Accessor-anchored coverage must satisfy check 1 via probe synthesis
        # (spec review O-5/F-5): the known-good registry's obj_data::in_room
        # row is the standing witness -- it is covered by the three accessor
        # patterns, none of which matches the bare member spelling.

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

    # The location-state registry consistency assertion (spec 1.3) runs in
    # --check against the REAL ledger only. A caller supplying --exceptions
    # (the hermetic self-test's synthetic ledgers, which carry no registry)
    # is probing the token gate, not the registry -- the registry's own
    # failure directions are standing synthetic cases in run_self_test().
    # Every existing --exceptions self-test case proves this gating already:
    # a registry-less synthetic ledger still gates token findings normally.
    if arguments.check and arguments.exceptions is None:
        registry_rows_a, registry_rows_b = parse_registry(
            exception_path.read_text(encoding="utf-8"))
        registry_errors = check_registry_consistency(registry_rows_a, registry_rows_b)
        if registry_errors:
            for error in registry_errors:
                print(f"registry inconsistency: {error}", file=sys.stderr)
            return 1

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
