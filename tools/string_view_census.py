#!/usr/bin/env python3
"""Inventory C++ declarations that still expose candidate string types."""

import argparse
import bisect
import pathlib
import re
import sys


CANDIDATE_PATTERNS = (
    re.compile(r"\bconst\s+char\s*\*"),
    re.compile(r"\bconst\s+std::string\s*&"),
)
ALLOWED_REASONS = {
    "nullable-state",
    "retains-storage",
    "binary-data",
    "printf-varargs",
    "c-boundary",
    "abi-layout",
    "sentinel-table",
}
SOURCE_SUFFIXES = {".h", ".cpp"}
RAW_STRING_PATTERN = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(')
QUOTED_LITERAL_PATTERN = re.compile(r'(?:u8|u|U|L)?(["\'])')


def parse_arguments():
    """Parse command-line census configuration."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).parents[1])
    parser.add_argument("--exceptions", type=pathlib.Path)
    return parser.parse_args()


def raw_string_end(source_text, token_start):
    """Return the end of a C++ raw string beginning at token_start, if present."""
    raw_string_match = RAW_STRING_PATTERN.match(source_text, token_start)
    if raw_string_match is None:
        return None
    delimiter = raw_string_match.group(1)
    terminator = f'){delimiter}"'
    contents_start = raw_string_match.end()
    terminator_start = source_text.find(terminator, contents_start)
    if terminator_start < 0:
        return len(source_text)
    return terminator_start + len(terminator)


def quoted_literal_end(source_text, token_start):
    """Return the end of a normal string or character literal, if present."""
    prefix_match = QUOTED_LITERAL_PATTERN.match(source_text, token_start)
    if prefix_match is None:
        return None
    quote_character = prefix_match.group(1)
    character_offset = prefix_match.end()
    while character_offset < len(source_text):
        character = source_text[character_offset]
        if character == "\\":
            character_offset += 2
            continue
        character_offset += 1
        if character == quote_character:
            return character_offset
    return len(source_text)


def literal_end(source_text, token_start):
    """Return the end of any C++ literal beginning at token_start, if present."""
    return raw_string_end(source_text, token_start) or quoted_literal_end(source_text, token_start)


def mask_comments_and_directives(source_text):
    """Mask comments and directives while retaining offsets and literal contents."""
    masked_characters = list(source_text)
    character_offset = 0
    line_contains_only_whitespace = True
    while character_offset < len(source_text):
        character = source_text[character_offset]
        if character == "\n":
            line_contains_only_whitespace = True
            character_offset += 1
            continue
        if line_contains_only_whitespace and character in " \t\r":
            character_offset += 1
            continue
        if line_contains_only_whitespace and character == "#":
            masked_characters[character_offset] = ";"
            directive_offset = character_offset + 1
            continuing_directive = True
            while directive_offset < len(source_text) and continuing_directive:
                line_end = source_text.find("\n", directive_offset)
                if line_end < 0:
                    line_end = len(source_text)
                line_contents = source_text[directive_offset:line_end].rstrip()
                continuing_directive = line_contents.endswith("\\")
                for masked_offset in range(directive_offset, line_end):
                    masked_characters[masked_offset] = " "
                directive_offset = line_end + 1
            character_offset = directive_offset
            line_contains_only_whitespace = True
            continue
        line_contains_only_whitespace = False

        detected_literal_end = literal_end(source_text, character_offset)
        if detected_literal_end is not None:
            character_offset = detected_literal_end
            continue
        if source_text.startswith("//", character_offset):
            comment_end = source_text.find("\n", character_offset)
            if comment_end < 0:
                comment_end = len(source_text)
            for masked_offset in range(character_offset, comment_end):
                masked_characters[masked_offset] = " "
            character_offset = comment_end
            continue
        if source_text.startswith("/*", character_offset):
            comment_end = source_text.find("*/", character_offset + 2)
            comment_end = len(source_text) if comment_end < 0 else comment_end + 2
            for masked_offset in range(character_offset, comment_end):
                if masked_characters[masked_offset] != "\n":
                    masked_characters[masked_offset] = " "
            character_offset = comment_end
            continue
        character_offset += 1

    return "".join(masked_characters)


def normalize_declaration(declaration):
    """Collapse declaration whitespace into its stable comparison form."""
    return " ".join(declaration.split())


def lexical_delimiters(source_text):
    """Return declaration delimiter offsets while ignoring literal contents."""
    delimiter_offsets = []
    character_offset = 0
    while character_offset < len(source_text):
        detected_literal_end = literal_end(source_text, character_offset)
        if detected_literal_end is not None:
            character_offset = detected_literal_end
            continue
        if source_text[character_offset] in ";{}":
            delimiter_offsets.append(character_offset)
        character_offset += 1
    return delimiter_offsets


def declaration_bounds(source_text, candidate_offset, delimiter_offsets):
    """Locate the surrounding declaration for a candidate type match."""
    insertion_index = bisect.bisect_left(delimiter_offsets, candidate_offset)
    declaration_start = delimiter_offsets[insertion_index - 1] + 1 if insertion_index else 0
    following_delimiters = delimiter_offsets[insertion_index:]
    semicolon_offset = next(
        (offset for offset in following_delimiters if source_text[offset] == ";"), -1
    )
    opening_brace_offset = next(
        (offset for offset in following_delimiters if source_text[offset] == "{"), -1
    )
    is_function_definition = (
        opening_brace_offset >= 0
        and (semicolon_offset < 0 or opening_brace_offset < semicolon_offset)
        and "(" in source_text[declaration_start:opening_brace_offset]
    )
    if is_function_definition:
        declaration_end = opening_brace_offset + 1
    elif semicolon_offset >= 0:
        declaration_end = semicolon_offset + 1
    else:
        declaration_end = len(source_text)

    return declaration_start, declaration_end


def parenthesis_spans(declaration):
    """Return matched parenthesis spans outside declaration literals."""
    spans = []
    opening_offsets = []
    character_offset = 0
    while character_offset < len(declaration):
        detected_literal_end = literal_end(declaration, character_offset)
        if detected_literal_end is not None:
            character_offset = detected_literal_end
            continue
        if declaration[character_offset] == "(":
            opening_offsets.append(character_offset)
        elif declaration[character_offset] == ")" and opening_offsets:
            spans.append((opening_offsets.pop(), character_offset))
        character_offset += 1
    return spans


def function_body_ranges(source_text, delimiter_offsets):
    """Return source ranges occupied by function and lambda bodies."""
    scope_stack = []
    ranges = []
    previous_delimiter = -1
    for delimiter_offset in delimiter_offsets:
        delimiter = source_text[delimiter_offset]
        if delimiter == "{":
            scope_prefix = source_text[previous_delimiter + 1 : delimiter_offset]
            inside_function = any(scope_kind == "function" for _, scope_kind in scope_stack)
            is_type_or_namespace = re.search(
                r"\b(?:class|enum|namespace|struct|union)\b", scope_prefix
            )
            if not inside_function and is_type_or_namespace is None and ")" in scope_prefix:
                scope_kind = "function"
            else:
                scope_kind = "other"
            scope_stack.append((delimiter_offset, scope_kind))
        elif delimiter == "}" and scope_stack:
            opening_offset, scope_kind = scope_stack.pop()
            if scope_kind == "function":
                ranges.append((opening_offset + 1, delimiter_offset))
        previous_delimiter = delimiter_offset
    return sorted(ranges)


def offset_is_in_ranges(source_offset, source_ranges):
    """Return whether an offset falls within any sorted source range."""
    return any(range_start <= source_offset < range_end for range_start, range_end in source_ranges)


def is_function_pointer_parameter(declaration, candidate_offset):
    """Return whether a candidate is in a function-pointer declarator parameter list."""
    pointer_declarator_pattern = re.compile(
        r"\(\s*(?:[A-Za-z_]\w*::)?\*\s*(?:const\s+)?[A-Za-z_]*\w*\s*\)\s*$"
    )
    for opening_offset, closing_offset in parenthesis_spans(declaration):
        if opening_offset < candidate_offset < closing_offset:
            declarator_prefix = declaration[:opening_offset]
            if pointer_declarator_pattern.search(declarator_prefix):
                return True
    return False


def is_block_scope_function_parameter(declaration, candidate_offset):
    """Return whether a candidate is in a block-scope function declaration."""
    excluded_leading_keywords = {
        "co_return",
        "delete",
        "if",
        "new",
        "return",
        "sizeof",
        "switch",
        "throw",
        "while",
    }
    for opening_offset, closing_offset in parenthesis_spans(declaration):
        if not opening_offset < candidate_offset < closing_offset:
            continue
        declarator_prefix = declaration[:opening_offset].strip()
        first_word_match = re.match(r"([A-Za-z_]\w*)", declarator_prefix)
        if first_word_match is None or first_word_match.group(1) in excluded_leading_keywords:
            continue
        if any(character in declarator_prefix for character in "=.;?()"):
            continue
        function_declaration_pattern = re.compile(
            r"^[~A-Za-z_][A-Za-z0-9_:<>,\s*&]*\s+[~A-Za-z_][A-Za-z0-9_]*$"
        )
        if function_declaration_pattern.match(declarator_prefix):
            return True
    return False


def is_function_parameter(declaration, candidate_offset, inside_function_body):
    """Return whether a candidate occurrence is part of a function parameter list."""
    cast_prefix = declaration[:candidate_offset]
    if re.search(
        r"\b(?:const_cast|dynamic_cast|reinterpret_cast|static_cast)\s*<[^>]*$",
        cast_prefix,
    ):
        return False

    if is_function_pointer_parameter(declaration, candidate_offset):
        return True
    if inside_function_body:
        return is_block_scope_function_parameter(declaration, candidate_offset)

    excluded_prefixes = {
        "alignof",
        "catch",
        "decltype",
        "for",
        "if",
        "return",
        "sizeof",
        "static_assert",
        "switch",
        "while",
    }
    for opening_offset, closing_offset in parenthesis_spans(declaration):
        if not opening_offset < candidate_offset < closing_offset:
            continue
        declarator_prefix = declaration[:opening_offset].rstrip()
        prefix_match = re.search(r"([A-Za-z_]\w*)$", declarator_prefix)
        if prefix_match is not None and prefix_match.group(1) in excluded_prefixes:
            continue
        if "=" in declarator_prefix and "operator=" not in declarator_prefix:
            continue
        if declarator_prefix:
            return True
    return False


def is_scalar_constant(declaration, candidate_offset):
    """Return whether a candidate occurrence declares a scalar pointer constant."""
    declaration_prefix = declaration[:candidate_offset]
    if re.search(r"\bconstexpr\b", declaration_prefix):
        return True
    declaration_tail = declaration[candidate_offset:]
    return re.match(r"const\s+char\s*\*\s*const\s+[A-Za-z_]\w*", declaration_tail) is not None


def is_lookup_table(declaration, candidate_offset):
    """Return whether a candidate occurrence declares a C-string lookup table."""
    declaration_tail = declaration[candidate_offset:]
    table_pattern = re.compile(
        r"const\s+char\s*\*\s*(?:const\s+)?[A-Za-z_]\w*\s*\[[^\]]*\]"
    )
    return table_pattern.match(declaration_tail) is not None


def is_target_declaration(declaration, candidate_offset, inside_function_body):
    """Return whether a candidate belongs to a requested census category."""
    return (
        is_function_parameter(declaration, candidate_offset, inside_function_body)
        or is_scalar_constant(declaration, candidate_offset)
        or is_lookup_table(declaration, candidate_offset)
    )


def source_files(search_paths, repository_root):
    """Yield eligible C++ source files in deterministic path order."""
    discovered_paths = set()
    for search_path in search_paths:
        resolved_path = search_path if search_path.is_absolute() else repository_root / search_path
        candidates = [resolved_path] if resolved_path.is_file() else resolved_path.rglob("*")
        for candidate_path in candidates:
            if not candidate_path.is_file() or candidate_path.suffix not in SOURCE_SUFFIXES:
                continue
            try:
                relative_path = candidate_path.relative_to(repository_root)
            except ValueError:
                relative_path = candidate_path
            if len(relative_path.parts) >= 2 and relative_path.parts[:2] == ("src", "tests"):
                continue
            discovered_paths.add(candidate_path)
    yield from sorted(discovered_paths)


def findings_for_file(source_path, repository_root):
    """Return normalized candidate declarations and their source locations."""
    source_text = mask_comments_and_directives(
        source_path.read_text(encoding="utf-8", errors="replace")
    )
    delimiter_offsets = lexical_delimiters(source_text)
    function_ranges = function_body_ranges(source_text, delimiter_offsets)
    declaration_ranges = set()
    for candidate_pattern in CANDIDATE_PATTERNS:
        for candidate_match in candidate_pattern.finditer(source_text):
            declaration_start, declaration_end = declaration_bounds(
                source_text, candidate_match.start(), delimiter_offsets
            )
            declaration_text = source_text[declaration_start:declaration_end]
            relative_candidate_offset = candidate_match.start() - declaration_start
            inside_function_body = offset_is_in_ranges(candidate_match.start(), function_ranges)
            if is_target_declaration(
                declaration_text, relative_candidate_offset, inside_function_body
            ):
                declaration_ranges.add((declaration_start, declaration_end))

    findings = []
    for declaration_start, declaration_end in sorted(declaration_ranges):
        declaration_text = source_text[declaration_start:declaration_end]
        first_declaration_character = (
            declaration_start + len(declaration_text) - len(declaration_text.lstrip())
        )
        declaration = normalize_declaration(declaration_text)
        source_line = source_text.count("\n", 0, first_declaration_character) + 1
        try:
            display_path = source_path.relative_to(repository_root)
        except ValueError:
            display_path = source_path
        findings.append((display_path, source_line, declaration))
    return findings


def load_exceptions(exception_path):
    """Load permitted reasons keyed by normalized markdown-table declarations."""
    if not exception_path.exists():
        return {}

    exceptions = {}
    for line in exception_path.read_text(encoding="utf-8").splitlines():
        columns = [column.strip().strip("`") for column in line.strip().strip("|").split("|")]
        if len(columns) < 2 or columns[1] not in ALLOWED_REASONS:
            continue
        exceptions[normalize_declaration(columns[0])] = columns[1]
    return exceptions


def main():
    """Print the census and enforce exception classification in check mode."""
    arguments = parse_arguments()
    repository_root = arguments.root.resolve()
    search_paths = arguments.paths or [repository_root / "src"]
    exception_path = arguments.exceptions or repository_root / "docs/superpowers/string-view-exceptions.md"
    exceptions = load_exceptions(exception_path)

    findings = []
    for source_path in source_files(search_paths, repository_root):
        findings.extend(findings_for_file(source_path, repository_root))

    unclassified_findings = []
    for display_path, source_line, declaration in findings:
        print(f"{display_path}:{source_line}: {declaration}")
        if declaration not in exceptions:
            unclassified_findings.append((display_path, source_line, declaration))

    if arguments.check and unclassified_findings:
        for display_path, source_line, declaration in unclassified_findings:
            print(
                f"unclassified: {display_path}:{source_line}: {declaration}",
                file=sys.stderr,
            )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
