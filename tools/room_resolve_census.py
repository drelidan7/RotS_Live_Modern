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
``docs/superpowers/room-resolve-ledger.md`` against the real tree. That ledger
is currently a deliberately PARTIAL, honest baseline, not yet fully green:
nine keys (75 sites, three files -- see the ledger's own "Known gap: BLOCKED"
section and ``.superpowers/sdd/2026-07-31-rr1-census/task-3-report.md``) are
real production call sites two Task 1 scanner attribution gaps mis-key to
file-scope (an unrecognized third function-definer macro, ``ASPELL``; a
function-pointer-parameter header that defeats the generic name-extraction
heuristic). Per the task-3 brief's explicit instruction, those nine keys are
left OUT of the ledger rather than misclassified, so ``--check`` against the
real tree currently fails with exactly those nine ``unclassified site``
errors -- that is the CORRECT, deliberately isolated state at this commit,
not a silent gap and not yet fixed.

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


# The five non-macro resolver-reaching spellings (spec section 1). world[ is
# the operator[] invocation itself -- ~30 live production sites survive the
# LS waves under LS1-ALLOW annotations, which license REPRESENTATION access,
# not input validity; they classify here like every other site (review
# O-1/F-1, the dual spec review's convergent blocker). world_room_vnum/
# dispatch_room_vnum are the one hook-dispatch alias pair at HEAD (F-2),
# pinned while the set is closed.
STATIC_TOKEN_PATTERNS = (
    ("room_of(", re.compile(r"\broom_of\s*\(")),
    ("room_by_id_total(", re.compile(r"\broom_by_id_total\s*\(")),
    ("world[", re.compile(r"(?:\b|::)world\s*\[")),
    ("world_room_vnum(", re.compile(r"\bworld_room_vnum\s*\(")),
    ("dispatch_room_vnum(", re.compile(r"\bdispatch_room_vnum\s*\(")),
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


def derive_macro_family(header_texts):
    """Transitive closure: macros whose bodies reach a resolver spelling.

    Seeds: any body matching a STATIC token pattern (this includes world[,
    which is how ASSIGNROOM joins -- review F-3/O-1). Closure: any body
    invoking an already-in-family macro name.
    """
    bodies = {}
    for text in header_texts.values():
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
# An ordinary definition header's name: the last identifier-ish path
# (possibly class-qualified, possibly operator[]) before the argument
# list's opening parenthesis. Rejects control flow and declarations.
HEADER_NAME_RE = re.compile(
    r"([A-Za-z_]\w*(?:::[A-Za-z_~]\w*|::operator\s*(?:\[\]|\(\)|[-+*/<>=!]+))?"
    r"|operator\s*(?:\[\]|\(\)|[-+*/<>=!]+))\s*\($")
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
            is_bare_scope_close = bool(re.fullmatch(r"\}+;?", stripped))
            if is_bare_scope_close:
                header_accum = ""
                pending_header = None
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


def scan_file(source_path, repository_root):
    """Every resolver-token site in one file, keyed by its enclosing
    function/macro. Returns a list of Site dicts: {"file": rel_posix,
    "function": name_or_macro, "attribution": kind, "token": token_name}."""
    text = source_path.read_text(encoding="utf-8", errors="replace")
    masked = mask_comments_and_string_literals(text).splitlines()
    attributions = attribute_lines(masked)
    sites = []
    rel = source_path.relative_to(repository_root).as_posix()
    for i, line in enumerate(masked):
        for token_name, pattern in token_patterns():
            for _ in pattern.finditer(line):
                # Two hits of one token on one line are two sites -- the
                # per-key COUNT is the anti-inheritance mechanism and must
                # not collapse them.
                kind, name = attributions[i]
                sites.append({"file": rel, "function": name,
                              "attribution": kind, "token": token_name})
    return sites


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
PROOF_KINDS = ("entry-guard", "caller-contract", "occupant-chain",
               "loop-bound", "dominating-resolve")

# Kinds whose Proof text MUST carry a file:line citation (review F-4/O-6).
CITATION_REQUIRED_KINDS = frozenset({"entry-guard", "dominating-resolve", "caller-contract"})
CITATION_RE = re.compile(r"\b[\w./-]+\.(?:cpp|h|hpp|cc|inl)\s*:\s*\d+")
MINIMUM_PROOF_TEXT_LENGTH = 20

# Task 3 seeds this from a real measured TODO-row count; while it is None the
# ceiling check in reconcile() is a no-op and main() prints a loud stderr
# notice instead of silently skipping it. Both `--todo-ceiling-override`
# (self-test only) and the ceiling-pin direction in run_self_test() key off
# this same literal -- the brief's F-10 mirror of MINIMUM_SCANNED_FILE_COUNT's
# own pin below.
MAXIMUM_TODO_COUNT = None

# Same floor tools/location_read_census.py's own MINIMUM_SCANNED_FILE_COUNT
# uses, at the same value: measured at 315 files under src/ at this commit
# (python3 -c a rglob-and-count over SOURCE_SUFFIXES), 15 files of headroom.
# The self-test's own symbolic pin (run_self_test) fails loudly if this is
# ever lowered below 300 rather than silently tracking a weakened floor.
MINIMUM_SCANNED_FILE_COUNT = 300

# Task 3 raises this to (the real ledger's measured row count - 10) once that
# ledger exists; 4 is a placeholder floor, sized so the self-test's own
# four-row synthetic fixture sits AT it rather than resting inside slack of
# its own choosing. The self-test's floor direction spends this constant
# SYMBOLICALLY (MINIMUM_LEDGER_ROW_COUNT - 1 / + 0), the same discipline
# MINIMUM_SCANNED_FILE_COUNT's own boundary probe uses, so a future edit that
# raises the literal keeps exercising the real edge rather than a stale one.
MINIMUM_LEDGER_ROW_COUNT = 4

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
    fail-closed SystemExit (malformed key, non-integer count, unknown class/
    kind, proof-shape violations, TEST-FIXTURE path illegality)."""
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

    if cls not in CLASSES:
        raise SystemExit(f"error: ledger: unknown class {cls!r} for key {key_text!r}")

    if kind != LEDGER_EMPTY_MARKER and kind not in PROOF_KINDS:
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


def parse_ledger(text):
    """Parse the ledger's two marker-anchored tables.

    Returns (rows, token_counts): `rows` is a list of dicts with keys
    key_file/key_function/key_token/count/cls/kind/proof; `token_counts` maps
    token name -> declared site count. Fails closed (SystemExit) on any
    marker/header/row-shape/vocabulary/proof-shape violation -- see the
    per-row parsers above for the exact conditions."""
    live_lines = _strip_fences(text)
    classification_cells = _parse_marked_table(
        live_lines, LEDGER_CLASSIFICATION_MARKER, LEDGER_CLASSIFICATION_HEADER,
        MINIMUM_LEDGER_ROW_COUNT, "classification table")
    token_count_cells = _parse_marked_table(
        live_lines, LEDGER_TOKEN_COUNTS_MARKER, LEDGER_TOKEN_COUNTS_HEADER,
        0, "token-counts table")
    rows = [_parse_classification_row(cells) for cells in classification_cells]
    token_counts = dict(_parse_token_count_row(cells) for cells in token_count_cells)
    return rows, token_counts


_UNSET = object()  # sentinel: "no todo_ceiling override was passed" (see reconcile())


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
"""

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
"""


def _run_gate(root, ledger=None, scan_path=None, *,
              todo_ceiling_override=None, minimum_files_override=None):
    """Invoke the REAL gate (subprocess, full main()) against a synthetic
    tree. `ledger=None` omits --ledger entirely, exercising main()'s own
    default-path computation (self-test direction 17). `scan_path=None`
    scans <root>/src (main()'s own default when no positional path is
    given)."""
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
    # direction 5). MAXIMUM_TODO_COUNT is None until Task 3 seeds the real
    # ceiling from a measured TODO count; while it is None this direction is
    # a deliberate no-op -- Task 3 replaces the `pass` below with the real
    # `if MAXIMUM_TODO_COUNT > <literal>: failures.append(...)` comparison
    # the moment the constant stops being None.
    if MAXIMUM_TODO_COUNT is not None:
        # Task 3: replace this stub with the real ceiling-pin comparison.
        pass

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
        def expect_parse_ledger_systemexit(name, ledger_text):
            try:
                parse_ledger(ledger_text)
                failures.append(f"{name}: parse_ledger() did NOT raise SystemExit")
            except SystemExit:
                pass

        try:
            parse_ledger(SELF_TEST_LEDGER_OK)
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
        sneaky_header = app_dir / "sneaky.h"
        sneaky_header.write_text(
            "#define SNEAKY(ch) (room_of(ch)->number)\n", encoding="utf-8")
        try:
            exit_code, output = _run_gate(root, ledger)
            if exit_code == 0 or "SNEAKY" not in output:
                failures.append(
                    f"macro-family-closed-world: expected a failure naming SNEAKY, got "
                    f"exit {exit_code}\n{output}"
                )
        finally:
            sneaky_header.unlink()

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
            fenced_rows, _ = parse_ledger(SELF_TEST_LEDGER_FENCED_DECOY)
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
            _synthetic_classification_ledger(MINIMUM_LEDGER_ROW_COUNT - 1))
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
             "--root", str(root), "--ledger", str(ledger), "src/app", "src/pad"],
            capture_output=True, text=True,
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
                parse_ledger(generated_ledger.read_text(encoding="utf-8"))
            except SystemExit as exc:
                failures.append(
                    f"generate-ledger-round-trip: parse_ledger() rejected the generated file: {exc}"
                )
            check_completed = subprocess.run(
                [sys.executable, str(pathlib.Path(__file__).resolve()), "--check",
                 "--root", str(generate_root), "--ledger", str(generated_ledger),
                 "--minimum-files-override", "4"],
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
onward** (design doc section 4/O-10).

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
ness instead follows from placement's M-1 precondition
(`src/entity/placement.cpp:369-395`) plus append-only room allocation (a room
id, once valid, stays valid for the process lifetime). The invariant's second
half -- that the occupant-chain-derived id still equals the character's own
stored location field -- does **not** hold inside a `ScopedRenderLocation`
window (the render cursor temporarily diverges from the stored field by
design); an `occupant-chain` proof taken from inside such a window must say so
explicitly, or it is incomplete.

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

    header_texts = {
        str(path): path.read_text(encoding="utf-8", errors="replace") for path in scanned_files
    }
    derived_family = derive_macro_family(header_texts)
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
        help="Print the macro family derived from all scanned headers' "
             "#define bodies, one name per line, sorted (debug/maintenance).")
    parser.add_argument("--todo-ceiling-override", type=int, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1")
    parser.add_argument("--minimum-files-override", type=int, default=None,
                        help="self-test only -- requires RR_SELF_TEST=1")
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

    if (args.todo_ceiling_override is not None or args.minimum_files_override is not None) \
            and os.environ.get("RR_SELF_TEST") != "1":
        parser.error(
            "--todo-ceiling-override/--minimum-files-override are self-test-only hooks -- "
            "set RR_SELF_TEST=1 to use them (see run_self_test())"
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
        header_texts = {
            str(path): path.read_text(encoding="utf-8", errors="replace")
            for path in scanned_for_derive
        }
        for name in sorted(derive_macro_family(header_texts)):
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

    header_texts = {
        str(path): path.read_text(encoding="utf-8", errors="replace") for path in scanned_files
    }
    derived_family = derive_macro_family(header_texts)
    unknown_macros = sorted(derived_family - set(MACRO_FAMILY))

    sites = []
    for path in scanned_files:
        sites.extend(scan_file(path, repository_root))

    rows, token_counts = parse_ledger(ledger_path.read_text(encoding="utf-8"))

    if MAXIMUM_TODO_COUNT is None and args.todo_ceiling_override is None:
        print("notice: MAXIMUM_TODO_COUNT is unset -- ceiling check skipped (Task 3 seeds it)",
              file=sys.stderr)
    todo_ceiling = (
        args.todo_ceiling_override if args.todo_ceiling_override is not None
        else MAXIMUM_TODO_COUNT
    )

    errors = reconcile(sites, rows, token_counts, todo_ceiling=todo_ceiling)
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
