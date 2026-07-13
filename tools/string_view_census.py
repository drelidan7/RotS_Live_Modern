#!/usr/bin/env python3
"""Inventory C++ declarations that still expose candidate string types."""

import argparse
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


def parse_arguments():
    """Parse command-line census configuration."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=pathlib.Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).parents[1])
    parser.add_argument("--exceptions", type=pathlib.Path)
    return parser.parse_args()


def strip_comments(source_text):
    """Remove comments while retaining newlines and source offsets."""
    token_pattern = re.compile(
        r'"(?:\\.|[^"\\])*"'
        r"|'(?:\\.|[^'\\])*'"
        r"|//[^\n]*"
        r"|/\*.*?\*/",
        re.DOTALL,
    )

    def replace_comment(token_match):
        token = token_match.group(0)
        if not token.startswith(("//", "/*")):
            return token
        return "".join("\n" if character == "\n" else " " for character in token)

    return token_pattern.sub(replace_comment, source_text)


def normalize_declaration(declaration):
    """Collapse declaration whitespace into its stable comparison form."""
    return " ".join(declaration.split())


def declaration_bounds(source_text, candidate_offset):
    """Locate the surrounding declaration for a candidate type match."""
    previous_boundaries = (
        source_text.rfind(";", 0, candidate_offset),
        source_text.rfind("{", 0, candidate_offset),
        source_text.rfind("}", 0, candidate_offset),
        source_text.rfind("\n\n", 0, candidate_offset),
    )
    declaration_start = max(previous_boundaries) + 1

    semicolon_offset = source_text.find(";", candidate_offset)
    opening_brace_offset = source_text.find("{", candidate_offset)
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
    source_text = strip_comments(source_path.read_text(encoding="utf-8", errors="replace"))
    declaration_ranges = set()
    for candidate_pattern in CANDIDATE_PATTERNS:
        for candidate_match in candidate_pattern.finditer(source_text):
            declaration_ranges.add(declaration_bounds(source_text, candidate_match.start()))

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
