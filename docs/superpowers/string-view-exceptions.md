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
| `extern const char* const refer_dirs[];` | `sentinel-table` | Direction-reference consumers stop at the terminal `"\n"` entry. |
| `extern const char* const refer_dirs[] = { "the north", "the east", "the south", "the west", "above", "below", "\n" };` | `sentinel-table` | The initializer explicitly contains the terminal legacy sentinel. |
| `extern const char* const pc_races[];` | `sentinel-table` | Race lookup consumers retain the terminal `"\n"` entry used by legacy search helpers. |
| `extern const char* const pc_race_types[];` | `sentinel-table` | Race-type lookup consumers retain the terminal `"\n"` entry used by legacy search helpers. |
| `extern const char* const pc_race_keywords[];` | `sentinel-table` | Race-keyword lookup consumers retain the terminal `"\n"` entry used by legacy search helpers. |
| `extern const char* const pc_star_types[];` | `sentinel-table` | Race-display lookup consumers retain the terminal `"\n"` entry used by legacy search helpers. |
| `extern const char* const pc_named_star_types[];` | `sentinel-table` | Named race-display lookup consumers retain the terminal `"\n"` entry used by legacy search helpers. |
| `extern const char* color_color[];` | `sentinel-table` | The ANSI color-name table retains its terminal `"\n"` entry for legacy consumers. |
| `int search_block(char* arg, const char* const* list, char exact);` | `sentinel-table` | The legacy search API discovers table length by scanning for a `"\n"` entry. |
| `int search_block(char* arg, const char* const* list, char exact) {` | `sentinel-table` | The implementation stops only when the supplied table reaches its `"\n"` entry. |
| `int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode);` | `sentinel-table` | The legacy search API discovers table length by scanning for a `"\n"` entry. |
| `int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode) {` | `sentinel-table` | The implementation stops only when the supplied table reaches its `"\n"` entry. |
| `const char* const fill[] = { "in", "from", "with", "the", "on", "at", "to", "\n" };` | `sentinel-table` | The interpreter filler-word table terminates with the legacy `"\n"` sentinel. |
