import pathlib
import subprocess
import sys
import tempfile
import unittest


TOOL_PATH = pathlib.Path(__file__).with_name("string_view_census.py")
ALLOWED_REASONS = (
    "nullable-state",
    "retains-storage",
    "binary-data",
    "printf-varargs",
    "c-boundary",
    "abi-layout",
    "sentinel-table",
)


class StringViewCensusTests(unittest.TestCase):
    def test_report_finds_candidates_but_not_mutable_buffer(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.h").write_text(
                """void eligible(const char* text);
void eligible_string(const std::string& text);
void mutable_buffer(char* text);
constexpr const char* greeting = \"hello\";
""",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("void eligible(const char* text);", result.stdout)
            self.assertIn("void eligible_string(const std::string& text);", result.stdout)
            self.assertIn('constexpr const char* greeting = "hello";', result.stdout)
            self.assertNotIn("mutable_buffer", result.stdout)

    def test_check_fails_for_unclassified_finding(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.h").write_text(
                "void eligible(const char* text);\n",
                encoding="utf-8",
            )

            result = self.run_census(repository_root, "--check")

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unclassified", result.stderr)

    def test_report_collapses_multiline_declarations_and_ignores_comments_and_tests(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            test_directory = source_directory / "tests"
            test_directory.mkdir(parents=True)
            (source_directory / "sample.cpp").write_text(
                """// void ignored(const char* text);
void multiline(
    const char* text);

constexpr const char* url = "https://example.test";

const char* result()
{
    return \"ok\";
}
""",
                encoding="utf-8",
            )
            (test_directory / "ignored.cpp").write_text(
                "void test_only(const char* text);\n",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("src/sample.cpp:2: void multiline( const char* text);", result.stdout)
            self.assertIn(
                'constexpr const char* url = "https://example.test";', result.stdout
            )
            self.assertNotIn("const char* result() {", result.stdout)
            self.assertNotIn("return", result.stdout)
            self.assertNotIn("ignored", result.stdout)

    def test_check_accepts_every_permitted_exception_reason(self):
        for allowed_reason in ALLOWED_REASONS:
            with self.subTest(reason=allowed_reason):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    repository_root = pathlib.Path(temporary_directory)
                    source_directory = repository_root / "src"
                    source_directory.mkdir()
                    (source_directory / "sample.h").write_text(
                        "void eligible(const char* text);\n",
                        encoding="utf-8",
                    )
                    exception_path = repository_root / "exceptions.md"
                    exception_path.write_text(
                        "| Normalized declaration | Reason |\n"
                        "| --- | --- |\n"
                        f"| `void eligible(const char* text);` | `{allowed_reason}` |\n",
                        encoding="utf-8",
                    )

                    result = self.run_census(
                        repository_root,
                        "--check",
                        "--exceptions",
                        str(exception_path),
                    )

                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_report_ignores_literal_delimiters_when_finding_declaration_bounds(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.h").write_text(
                'constexpr const char* delimiters = "semi; open{ close}";\n',
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                'constexpr const char* delimiters = "semi; open{ close}";',
                result.stdout,
            )

    def test_report_treats_preprocessor_directives_as_declaration_boundaries(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.h").write_text(
                """#define DECLARATION_SWITCH 1
#if DECLARATION_SWITCH
void wrapped(
    const char* text);
#endif
""",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "src/sample.h:3: void wrapped( const char* text);",
                result.stdout,
            )
            self.assertNotIn("#define", result.stdout)
            self.assertNotIn("#if", result.stdout)

    def test_report_excludes_range_for_and_mutable_local_bindings(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.cpp").write_text(
                """void eligible(const char* text);

void examine(const std::vector<std::string>& items, const char* source)
{
    for (const std::string& item : items)
    {
    }
    const char* mutable_pointer = source;
    const std::string& local_alias = items.front();
    constexpr const char* scalar_constant = "ok";
    const char* const lookup[] = { "ok" };
}
""",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("void eligible(const char* text);", result.stdout)
            self.assertIn(
                "void examine(const std::vector<std::string>& items, const char* source) {",
                result.stdout,
            )
            self.assertIn(
                'constexpr const char* scalar_constant = "ok";', result.stdout
            )
            self.assertIn('const char* const lookup[] = { "ok" };', result.stdout)
            self.assertNotIn("for (", result.stdout)
            self.assertNotIn("mutable_pointer", result.stdout)
            self.assertNotIn("local_alias", result.stdout)

    def run_census(self, repository_root, *arguments):
        return subprocess.run(
            [
                sys.executable,
                str(TOOL_PATH),
                "--root",
                str(repository_root),
                *arguments,
            ],
            capture_output=True,
            check=False,
            text=True,
        )


if __name__ == "__main__":
    unittest.main()
