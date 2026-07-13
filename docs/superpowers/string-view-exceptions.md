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
| `void page_string_borrowed(struct descriptor_data* descriptor, char* text);` | `retains-storage` | This explicitly named legacy pager path retains caller-owned mutable storage until paging completes; normal `page_string` copies a bounded view instead. |
| `void page_string_borrowed(struct descriptor_data* descriptor, char* text) {` | `retains-storage` | The implementation exposes the borrowed lifetime by name and is used only with long-lived world or global help text. |
| `void vmudlog(char type, const char* format, ...);` | `printf-varargs` | The format parameter is the final named argument for C varargs. |
| `void vmudlog(char type, const char* format, ...) {` | `printf-varargs` | The implementation initializes a `va_list` from the format parameter. |
| `void vsend_to_char(struct char_data* ch, const char* format, ...);` | `printf-varargs` | The format parameter is the final named argument for C varargs. |
| `void vsend_to_char(char_data* character, const char* format, ...) {` | `printf-varargs` | The implementation initializes a `va_list` from the format parameter. |
| `const char* ProtocolOutput(descriptor_t* apDescriptor, const char* apData, int* apLength);` | `retains-storage` | This is a textual formatter: `apLength` bounds available storage but the first null terminates input, and the result may reference shared scratch storage. |
| `const char* ProtocolOutput(descriptor_t* apDescriptor, const char* apData, int* apLength) {` | `retains-storage` | The implementation stops at the first null and returns protocol-managed scratch storage, so changing only the input parameter would obscure the returned lifetime. |
| `void MSDPSetTable(descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue);` | `binary-data` | The marker-bearing table payload is retained in the protocol's legacy null-terminated C field; embedded-null support requires a separate state-layout change. |
| `void MSDPSetTable(descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue) {` | `binary-data` | The implementation copies marker-bearing bytes into the protocol's retained legacy C field. |
| `void MSDPSendTable(descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue);` | `binary-data` | The marker-bearing table payload is copied into retained legacy protocol state before any conditional send. |
| `void MSDPSendTable(descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue) {` | `binary-data` | The implementation stores the marker-bearing payload in a null-terminated protocol C field. |
| `void MSDPSetArray(descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue);` | `binary-data` | The marker-bearing array payload is retained in the protocol's legacy null-terminated C field; embedded-null support requires a separate state-layout change. |
| `void MSDPSetArray(descriptor_t* apDescriptor, variable_t aMSDP, const char* apValue) {` | `binary-data` | The implementation copies marker-bearing bytes into the protocol's retained legacy C field. |
| `const char* MXPCreateTag(descriptor_t* apDescriptor, const char* apTag);` | `retains-storage` | The API returns either the input pointer or a pointer to a shared scratch buffer, so changing only its parameter would obscure the returned lifetime. |
| `const char* MXPCreateTag(descriptor_t* apDescriptor, const char* apTag) {` | `retains-storage` | The implementation conditionally returns borrowed input storage and otherwise returns shared scratch-buffer storage. |
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
| `extern const char* const color_sequence[];` | `c-boundary` | Terminal escape sequences still flow through macros and `snprintf`-style consumers that require null-terminated storage. |
| `const char* const color_sequence[] = { "\x1B[0m", "\x1B[31m", "\x1B[32m", "\x1B[33m", "\x1B[34m", "\x1B[35m", "\x1B[36m", "\x1B[37m", "\x1B[01m\x1B[31m", "\x1B[01m\x1B[32m", "\x1B[01m\x1B[33m", "\x1B[01m\x1B[34m", "\x1B[01m\x1B[35m", "\x1B[01m\x1B[36m", "\x1B[01m\x1B[37m", "" };` | `c-boundary` | The fixed table owns null-terminated literals until communication and formatting consumers become length-aware. |
| `int isname_nullable(const char* query, const char* name_list, char full = 1);` | `nullable-state` | Legacy object, exit, and character name pointers can be null; this wrapper rejects null before calling the bounded core. |
| `int isname_nullable(const char* query, const char* name_list, char full) {` | `nullable-state` | The implementation branches on both pointers before constructing either view. |
| `int str_cmp_nullable(const char* first, const char* second);` | `nullable-state` | Legacy persisted/runtime text fields can be null; this wrapper gives null deterministic ordering before calling the bounded core. |
| `int str_cmp_nullable(const char* first, const char* second) {` | `nullable-state` | The implementation branches on both pointers before constructing either view. |
| `int strn_cmp_nullable(const char* first, const char* second, int count);` | `nullable-state` | Legacy bounded comparison callers can expose nullable object names; this wrapper checks them before calling the bounded core. |
| `int strn_cmp_nullable(const char* first, const char* second, int count) {` | `nullable-state` | The implementation branches on both pointers before constructing either view. |
| `int search_block(char* arg, const char* const* list, char exact);` | `sentinel-table` | The legacy search API discovers table length by scanning for a `"\n"` entry. |
| `int search_block(char* arg, const char* const* list, char exact) {` | `sentinel-table` | The implementation stops only when the supplied table reaches its `"\n"` entry. |
| `int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode);` | `sentinel-table` | The legacy search API discovers table length by scanning for a `"\n"` entry. |
| `int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode) {` | `sentinel-table` | The implementation stops only when the supplied table reaches its `"\n"` entry. |
| `extern const char* const fill[] = { "in", "from", "with", "the", "on", "at", "to", "\n" };` | `sentinel-table` | The interpreter filler-word table terminates with the legacy `"\n"` sentinel. |
| `extern const char* const fill[];` | `sentinel-table` | Interpreter word filtering scans until the table's terminal `"\n"` entry. |
