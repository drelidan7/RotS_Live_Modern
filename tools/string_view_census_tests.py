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
                        "| Owner file | Normalized declaration | Reason | Contract |\n"
                        "| --- | --- | --- | --- |\n"
                        "| `src/sample.h` | `void eligible(const char* text);` | "
                        f"`{allowed_reason}` | The fixture deliberately exercises this reason. |\n",
                        encoding="utf-8",
                    )

                    result = self.run_census(
                        repository_root,
                        "--check",
                        "--exceptions",
                        str(exception_path),
                    )

                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_check_rejects_exception_for_a_different_owner_file(self):
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
                "| Owner file | Normalized declaration | Reason | Contract |\n"
                "| --- | --- | --- | --- |\n"
                "| `src/other.h` | `void eligible(const char* text);` | `c-boundary` | "
                "The other declaration feeds a C API. |\n",
                encoding="utf-8",
            )

            result = self.run_census(
                repository_root,
                "--check",
                "--exceptions",
                str(exception_path),
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unclassified", result.stderr)

    def test_check_rejects_exception_without_a_contract(self):
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
                "| Owner file | Normalized declaration | Reason | Contract |\n"
                "| --- | --- | --- | --- |\n"
                "| `src/sample.h` | `void eligible(const char* text);` | `c-boundary` | |\n",
                encoding="utf-8",
            )

            result = self.run_census(
                repository_root,
                "--check",
                "--exceptions",
                str(exception_path),
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("unclassified", result.stderr)

    def test_check_matches_declarations_across_pointer_and_reference_spacing(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.h").write_text(
                "bool decode(const std::string &bytes, const char *label);\n",
                encoding="utf-8",
            )
            exception_path = repository_root / "exceptions.md"
            exception_path.write_text(
                "| Owner file | Normalized declaration | Reason | Contract |\n"
                "| --- | --- | --- | --- |\n"
                "| `src/sample.h` | `bool decode(const std::string& bytes, const char* label);` | "
                "`binary-data` | The decoder consumes the complete explicit byte range. |\n",
                encoding="utf-8",
            )

            result = self.run_census(
                repository_root,
                "--check",
                "--exceptions",
                str(exception_path),
            )

            self.assertEqual(result.returncode, 0, result.stderr)

    def test_check_matches_html_escaped_pipe_in_table_declaration(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.h").write_text(
                'constexpr const char* spinner[] = { "|" };\n',
                encoding="utf-8",
            )
            exception_path = repository_root / "exceptions.md"
            exception_path.write_text(
                "| Owner file | Normalized declaration | Reason | Contract |\n"
                "| --- | --- | --- | --- |\n"
                "| `src/sample.h` | `constexpr const char* spinner[] = { \"&#124;\" };` | "
                "`c-boundary` | The spinner retains null-terminated literal storage. |\n",
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

    def test_report_excludes_casts_and_calls_but_keeps_function_declarators(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.cpp").write_text(
                """struct Handler
{
    Handler(const char* name);
    Handler& operator=(const std::string& name);
    void operator()(const char* text) const;
};

using Callback = void (*)(const char* text);

void inspect(char* buffer)
{
    extern void local_declaration(const char* text);
    void (*local_callback)(const char* text) = nullptr;
    consume(static_cast<const char*>(buffer));
    consume(reinterpret_cast<const char*>(buffer));
    consume((const char*)buffer);
}
""",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("Handler(const char* name);", result.stdout)
            self.assertIn(
                "Handler& operator=(const std::string& name);", result.stdout
            )
            self.assertIn("void operator()(const char* text) const;", result.stdout)
            self.assertIn(
                "using Callback = void (*)(const char* text);", result.stdout
            )
            self.assertIn(
                "void (*local_callback)(const char* text) = nullptr;", result.stdout
            )
            self.assertIn(
                "extern void local_declaration(const char* text);", result.stdout
            )
            self.assertNotIn("static_cast", result.stdout)
            self.assertNotIn("reinterpret_cast", result.stdout)
            self.assertNotIn("consume((const char*)buffer)", result.stdout)

    def test_report_recognizes_template_function_body_scope(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.cpp").write_text(
                """template <class Reader>
bool parse_value(Reader* reader, const char* label)
{
    consume([](const std::string& key) { return key; });
    consume((const char*)reader);
    return label != nullptr;
}
""",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "template <class Reader> bool parse_value(Reader* reader, const char* label) {",
                result.stdout,
            )
            self.assertNotIn("const std::string& key", result.stdout)
            self.assertNotIn("consume((const char*)reader)", result.stdout)

    def test_report_recognizes_elaborated_parameter_function_body_scope(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            repository_root = pathlib.Path(temporary_directory)
            source_directory = repository_root / "src"
            source_directory.mkdir()
            (source_directory / "sample.cpp").write_text(
                """void inspect_character(struct char_data* character, const char* label)
{
    consume([](const std::string& key) { return key; });
    consume((const char*)character);
    consume(label);
}
""",
                encoding="utf-8",
            )

            result = self.run_census(repository_root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "void inspect_character(struct char_data* character, const char* label) {",
                result.stdout,
            )
            self.assertNotIn("const std::string& key", result.stdout)
            self.assertNotIn("consume((const char*)character)", result.stdout)

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
