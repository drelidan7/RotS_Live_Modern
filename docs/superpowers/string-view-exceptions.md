# `std::string_view` Migration Exceptions

This ledger records declarations that intentionally retain C-string or `const std::string&`
contracts during the repository-wide migration. Entries are keyed by the census tool's normalized
declaration. A permitted reason identifies the contract constraint; inconvenience alone is not an
exception.

Permitted reasons are `nullable-state`, `retains-storage`, `binary-data`, `printf-varargs`,
`c-boundary`, `abi-layout`, and `sentinel-table`.

| Normalized declaration | Reason | Evidence |
| --- | --- | --- |
| `int rots_asprintf(char** out, const char* fmt, ...);` | `printf-varargs` | The format parameter is consumed by `va_start` and passed to `vsnprintf`. |
| `int rots_asprintf(char** out, const char* fmt, ...) {` | `printf-varargs` | The implementation uses the format parameter as the final named argument for C varargs. |
| `void vmudlog(char type, const char* format, ...);` | `printf-varargs` | The format parameter is the final named argument for C varargs. |
| `void vmudlog(char type, const char* format, ...) {` | `printf-varargs` | The implementation initializes a `va_list` from the format parameter. |
| `void vsend_to_char(struct char_data* ch, const char* format, ...);` | `printf-varargs` | The format parameter is the final named argument for C varargs. |
| `void vsend_to_char(char_data* character, const char* format, ...) {` | `printf-varargs` | The implementation initializes a `va_list` from the format parameter. |
| `extern const char* const dirs[];` | `sentinel-table` | The shared direction table terminates with the legacy `"\n"` sentinel. |
| `extern const char* const dirs[] = { "north", "east", "south", "west", "up", "down", "\n" };` | `sentinel-table` | The initializer explicitly contains the terminal legacy sentinel. |
| `const char* const dirs[] = { "north", "east", "south", "west", "up", "down", "\n" };` | `sentinel-table` | The local direction-name table explicitly contains the terminal legacy sentinel. |
