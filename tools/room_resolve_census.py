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
family derivation, and per-line function attribution). Task 2 builds the
ledger reconciliation and ``--check``/``--self-test`` gate on top of the
exact interfaces below; no gate exists yet in this file.
"""

import argparse
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
FUNCTION_DEFINER_RE = re.compile(r"^\s*(?:ACMD|SPECIAL)\s*\(\s*(\w+)\s*\)")
# GoogleTest's own definer family (fix round 1, CRITICAL): TEST/TEST_F/TEST_P
# each take TWO comma-separated arguments (suite, name) and key as
# "Suite.Name" -- gtest's own convention, and the shape the ledger's test-
# tier rows must match. Measured before this fix: 244 of 1177 tree-wide
# token hits (~21%, 15 test files) silently mis-keyed to the literal macro
# name "TEST"/"TEST_F"/"TEST_P" via the generic heuristic below, which is
# blind to the second comma-separated argument.
GTEST_DEFINER_RE = re.compile(r"^\s*(?:TEST|TEST_F|TEST_P)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)")
# The recognized macro-definer names -- anything matching an all-caps
# macro-invocation shape that is NOT one of these is an unrecognized
# definer, not a function name (see ALL_CAPS_MACRO_INVOCATION_RE below).
RECOGNIZED_DEFINER_NAMES = frozenset({"ACMD", "SPECIAL", "TEST", "TEST_F", "TEST_P"})
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
            ns = re.match(r"^\s*namespace\s+([\w:]+)?\s*\{", line)
            if ns:
                raw_name = ns.group(1)
                segments = raw_name.split("::") if raw_name else ["<anon>"]
                for segment in segments:
                    namespace_stack.append((segment, depth))
                depth += line.count("{") - line.count("}")
                continue
            dm = FUNCTION_DEFINER_RE.match(line)
            gm = None if dm else GTEST_DEFINER_RE.match(line)
            if dm:
                pending_header = dm.group(1)
                header_accum = ""
            elif gm:
                pending_header = f"{gm.group(1)}.{gm.group(2)}"
                header_accum = ""
            elif stripped and not stripped.startswith("#"):
                header_accum = (header_accum + " " + stripped).strip()
                # A ';' at base depth ends any declaration/statement: no header.
                if ";" in stripped:
                    header_accum = ""
                    pending_header = None
                # An '=' before '{' means initializer, not a function body.
                open_here = "{" in stripped
                if open_here and "=" not in header_accum.split("{")[0]:
                    head = header_accum.split("{")[0].strip()
                    first_word = re.match(r"\s*(\w+)", head)
                    all_caps = ALL_CAPS_MACRO_INVOCATION_RE.match(head)
                    if (pending_header is None and head.endswith("(") is False
                            and not (all_caps and all_caps.group(1) not in RECOGNIZED_DEFINER_NAMES)):
                        name_part = head[: head.rfind("(")] if "(" in head else ""
                        nm = re.search(r"([A-Za-z_][\w:~]*(?:::operator\s*\S+|)|operator\s*\S+)\s*$",
                                       name_part)
                        if (nm and "(" in head
                                and (first_word is None or first_word.group(1)
                                     not in CONTROL_KEYWORDS)
                                and not TYPE_DEFINITION_HEAD_RE.match(head)):
                            pending_header = nm.group(1)
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

    for failure in failures:
        print(f"dev-selftest FAILED: {failure}", file=sys.stderr)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Room-resolve retirement census scanner core.")
    parser.add_argument(
        "--derive-macros", action="store_true",
        help="Print the macro family derived from all scanned headers' "
             "#define bodies, one name per line, sorted (debug/maintenance).")
    args = parser.parse_args(argv)

    if args.derive_macros:
        repository_root = pathlib.Path(__file__).resolve().parent.parent
        header_texts = {
            str(path): path.read_text(encoding="utf-8", errors="replace")
            for path in source_files(repository_root / "src")
        }
        family = derive_macro_family(header_texts)
        for name in sorted(family):
            print(name)
        return 0

    return dev_selftest()


if __name__ == "__main__":
    raise SystemExit(main())
