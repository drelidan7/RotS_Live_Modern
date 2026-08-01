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
# An ordinary definition header's name: the last identifier-ish path
# (possibly class-qualified, possibly operator[]) before the argument
# list's opening parenthesis. Rejects control flow and declarations.
HEADER_NAME_RE = re.compile(
    r"([A-Za-z_]\w*(?:::[A-Za-z_~]\w*|::operator\s*(?:\[\]|\(\)|[-+*/<>=!]+))?"
    r"|operator\s*(?:\[\]|\(\)|[-+*/<>=!]+))\s*\($")
CONTROL_KEYWORDS = frozenset({"if", "while", "for", "switch", "return", "sizeof", "catch"})


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
            ns = re.match(r"^\s*namespace\s+(\w+)?\s*\{", line)
            if ns:
                namespace_stack.append((ns.group(1) or "<anon>", depth))
                depth += line.count("{") - line.count("}")
                continue
            dm = FUNCTION_DEFINER_RE.match(line)
            if dm:
                pending_header = dm.group(1)
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
                    if pending_header is None and head.endswith("(") is False:
                        name_part = head[: head.rfind("(")] if "(" in head else ""
                        nm = re.search(r"([A-Za-z_][\w:~]*(?:::operator\s*\S+|)|operator\s*\S+)\s*$",
                                       name_part)
                        if (nm and "(" in head
                                and (first_word is None or first_word.group(1)
                                     not in CONTROL_KEYWORDS)
                                and not head.startswith(("struct ", "class ", "enum ",
                                                         "union ", "typedef "))):
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
