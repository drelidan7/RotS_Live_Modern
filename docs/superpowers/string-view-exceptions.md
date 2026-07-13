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
| `const char* rots_crypt(const char* key, const char* setting);` | `nullable-state` | The compatibility overload preserves crypt-style null failure semantics before bounded text reaches the view overload. |
| `const char* rots_crypt(const char* key, const char* setting) {` | `nullable-state` | Null key and setting pointers remain distinct invalid states and return null before any view is constructed. |
| `char* str_dup(const char* source) {` | `nullable-state` | The legacy allocator returns null for a null source and allocated storage for an empty source. |
| `bool fixed_width_field_has_no_nul(const char* field, size_t width) {` | `binary-data` | The explicit width governs inspection of the complete fixed-size legacy field, including internal and trailing zero bytes. |
| `std::string sanitize_fixed_width_field(const char* field, size_t width) {` | `binary-data` | Recovery sanitization consumes a declared binary field width and deliberately caps the preserved prefix at width minus one. |
| `bool legacy_plrobj_bytes_round_trip_losslessly(const std::string& legacy_bytes) {` | `binary-data` | Strict recovery qualification must decode the complete ABI-sensitive object payload, including internal zero bytes. |
| `void mudlog_debug_mob(const char* buf, char_data* ch) {` | `c-boundary` | The helper forwards null-terminated log text into legacy logging APIs. |
| `void mudlog_aliased_mob(const char* buf, char_data* ch, const char* mob_alias) {` | `c-boundary` | Both inputs are consumed by legacy `strstr` and logging boundaries that require terminated pointers. |
| `void sprintbit(long vektor, const char* const names[], char* result, int var) {` | `sentinel-table` | The lookup table is terminated by a legacy newline sentinel and the output remains a caller-owned mutable C buffer. |
| `void sprinttype(int type, const char* const names[], char* result) {` | `sentinel-table` | The lookup table length is discovered through its terminal newline entry and output is copied into mutable caller storage. |
| `void* create_function(int elem_size, int elem_num, int line, const char* file) {` | `c-boundary` | The macro-provided `__FILE__` string is used only by the allocation-failure `printf` diagnostic. |
| `int find_player_in_table(const char* name, int idnum) {` | `nullable-state` | Name lookup intentionally accepts null through `str_cmp_nullable`, while negative IDs select name-based lookup. |
| `int has_alias(char_data* host, const char* keyword) {` | `c-boundary` | The keyword is consumed directly by legacy `strstr` against the mobile alias C field. |
| `void page_string_borrowed(struct descriptor_data* descriptor, char* text);` | `retains-storage` | This explicitly named legacy pager path retains caller-owned mutable storage until paging completes; normal `page_string` copies a bounded view instead. |
| `void page_string_borrowed(struct descriptor_data* descriptor, char* text) {` | `retains-storage` | The implementation exposes the borrowed lifetime by name and is used only with long-lived world or global help text. |
| `void vmudlog(char type, const char* format, ...);` | `printf-varargs` | The format parameter is the final named argument for C varargs. |
| `void vmudlog(char type, const char* format, ...) {` | `printf-varargs` | The implementation initializes a `va_list` from the format parameter. |
| `void vsend_to_char(struct char_data* ch, const char* format, ...);` | `printf-varargs` | The format parameter is the final named argument for C varargs. |
| `void vsend_to_char(char_data* character, const char* format, ...) {` | `printf-varargs` | The implementation initializes a `va_list` from the format parameter. |
| `const char* ProtocolOutput(descriptor_t* apDescriptor, const char* apData, int* apLength);` | `retains-storage` | This textual formatter requires null-terminated readable input; `apLength` limits processing but not parser lookahead, and the result may reference shared scratch storage. |
| `const char* ProtocolOutput(descriptor_t* apDescriptor, const char* apData, int* apLength) {` | `retains-storage` | The implementation may inspect null-terminated input beyond the processing limit and returns protocol-managed scratch storage, so changing only the input parameter would obscure both contracts. |
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
| `std::string format_account_character_name_for_display(const char* character_name) {` | `nullable-state` | Active-session rendering accepts missing legacy character names and preserves the empty-display fallback before calling the bounded account presentation API. |
| `bool account_links_character(const account::AccountData& account_data, const char* character_name) {` | `nullable-state` | Descriptor and character records can expose a missing name; the helper rejects null before normalizing the bounded account name. |
| `AccountCharacterSessionMatch descriptor_or_character_matches_account( const descriptor_data* descriptor, const account::AccountData& account_data, const char* character_name) {` | `nullable-state` | Session matching preserves the distinct missing-character-name state while forwarding non-null names to bounded helpers. |
| `void mudlog_account_event(struct descriptor_data* d, std::string_view action, const char* email_override = nullptr) {` | `nullable-state` | A null email override explicitly requests fallback to descriptor email and then the legacy unknown-account label. |
| `bool legacy_board_file_from_binary(const std::string& bytes, BoardSaveData* data, std::string* error_message = nullptr);` | `binary-data` | The legacy board payload includes fixed-width integers, pointer-layout bytes, padding, and null-delimited record text; every declared byte participates in decoding. |
| `bool legacy_board_file_from_binary(const std::string& bytes, BoardSaveData* data, std::string* error_message) {` | `binary-data` | The decoder traverses the complete legacy board byte range and must not stop at internal zero bytes. |
| `bool read_i32(const std::string& bytes, size_t* offset, int* value, std::string* error_message, std::string_view label) {` | `binary-data` | The board scalar reader uses an explicit offset into the full legacy payload. |
| `bool skip_bytes(const std::string& bytes, size_t* offset, size_t length, std::string* error_message, std::string_view label) {` | `binary-data` | The board decoder skips fixed-width binary fields that can contain zero bytes. |
| `bool read_text(const std::string& bytes, size_t* offset, size_t length_including_nul, std::string* out, std::string* error_message, std::string_view label) {` | `binary-data` | The legacy record supplies an explicit field length inside a larger binary payload. |
| `bool read_legacy_record(const std::string& bytes, size_t* offset, BoardMessageData* message, std::string* error_message) {` | `binary-data` | Each board record contains fixed-layout binary fields followed by explicitly sized text. |
| `bool exploit_records_from_binary(const std::string& bytes, std::vector<exploit_record>* records, std::string* error_message = nullptr);` | `binary-data` | The exploit payload is a repeated fixed-layout record sequence whose internal arrays and padding contain zero bytes. |
| `bool exploit_records_from_binary(const std::string& bytes, std::vector<exploit_record>* records, std::string* error_message) {` | `binary-data` | The decoder validates and reads the complete fixed-record byte count. |
| `bool read_i32_at(const std::string& bytes, size_t record_offset, size_t field_offset, int* value, std::string* error_message, std::string_view label) {` | `binary-data` | The exploit decoder reads an integer from an explicit offset in a full-byte record payload. |
| `bool read_i16_at(const std::string& bytes, size_t record_offset, size_t field_offset, sh_int* value, std::string* error_message, std::string_view label) {` | `binary-data` | The exploit decoder reads a short from an explicit offset in a full-byte record payload. |
| `bool read_fixed_bytes_at(const std::string& bytes, size_t record_offset, size_t field_offset, char* dest, size_t length, std::string* error_message, std::string_view label) {` | `binary-data` | Fixed-width exploit character arrays are copied byte-for-byte, including internal and trailing zeros. |
| `void write_fixed_bytes_at(std::string* bytes, size_t record_offset, size_t field_offset, const char* source, size_t length) {` | `binary-data` | The explicit length governs a fixed-width legacy record copy, so zeros in the source range remain significant. |
| `bool legacy_mail_file_from_binary(const std::string& bytes, MailStoreData* data, std::string* error_message = nullptr);` | `binary-data` | The legacy mail store is a chain of fixed-size binary blocks containing zero-padded fields. |
| `bool legacy_mail_file_from_binary(const std::string& bytes, MailStoreData* data, std::string* error_message) {` | `binary-data` | Block links can target data after many internal zero-padded fields. |
| `bool read_i32(const std::string& bytes, size_t* offset, long* value, std::string* error_message, std::string_view label) {` | `binary-data` | The mail decoder reads fixed-width little-endian integers from the full block payload. |
| `bool read_fixed_cstring(const std::string& bytes, size_t* offset, size_t field_size, std::string* out, std::string* error_message, std::string_view label) {` | `binary-data` | A fixed-width mail field is embedded in a larger binary block and consumes its entire declared width. |
| `bool decode_message_at(const std::string& bytes, size_t header_offset, MailMessageData* message, std::string* error_message) {` | `binary-data` | Mail chain traversal must retain block bytes beyond zero-padded header fields. |
| `bool legacy_pkill_file_from_binary(const std::string& bytes, std::vector<PKILL>* records, std::string* error_message = nullptr);` | `binary-data` | The player-kill store is a repeated native-layout legacy record sequence. |
| `bool legacy_pkill_file_from_binary(const std::string& bytes, std::vector<PKILL>* records, std::string* error_message) {` | `binary-data` | Internal integer bytes can be zero while later records remain significant. |
| `bool read_i32_at(const std::string& bytes, size_t record_offset, size_t field_offset, int* value, std::string* error_message, std::string_view label) {` | `binary-data` | Fixed-offset persistence readers operate on complete record payloads, not textual prefixes. |
| `bool read_u8_at(const std::string& bytes, size_t record_offset, size_t field_offset, unsigned char* value, std::string* error_message, std::string_view label) {` | `binary-data` | Player-kill byte fields are addressed within a complete binary record sequence. |
| `bool legacy_crime_file_from_binary(const std::string &bytes, std::vector<crime_record_type> *records, std::string *error_message = nullptr);` | `binary-data` | The crime store is a repeated native-layout legacy record sequence. |
| `bool legacy_crime_file_from_binary(const std::string& bytes, std::vector<crime_record_type>* records, std::string* error_message) {` | `binary-data` | Internal zero bytes do not delimit the crime record payload. |
| `bool read_i16_at(const std::string& bytes, size_t record_offset, size_t field_offset, sh_int* value, std::string* error_message, std::string_view label) {` | `binary-data` | Fixed-offset persistence readers operate on complete record payloads, not textual prefixes. |
| `bool object_save_data_from_binary(const std::string& bytes, ObjectSaveData* data, std::string* error_message = nullptr);` | `binary-data` | Object saves contain ABI-sensitive fixed records, sentinels, and variable binary sections with internal zeros. |
| `bool object_save_data_from_binary(const std::string& bytes, ObjectSaveData* data, std::string* error_message) {` | `binary-data` | The strict object decoder requires the complete serialized byte range. |
| `bool legacy_object_save_data_from_binary( const std::string& bytes, ObjectSaveData* data, bool* accepted_missing_follower_section = nullptr, std::string* error_message = nullptr);` | `binary-data` | The compatibility decoder consumes the same full-byte object payload while allowing one historical missing section. |
| `bool legacy_object_save_data_from_binary( const std::string& bytes, ObjectSaveData* data, bool* accepted_missing_follower_section, std::string* error_message) {` | `binary-data` | Compatibility does not change the payload's binary, length-delimited semantics. |
| `bool recover_object_save_data_from_binary( const std::string& bytes, ObjectSaveData* data, int* dropped_partial_record_count = nullptr, std::string* error_message = nullptr);` | `binary-data` | Recovery scans the complete byte range for intact record and sentinel boundaries. |
| `bool recover_object_save_data_from_binary( const std::string& bytes, ObjectSaveData* data, int* dropped_partial_record_count, std::string* error_message) {` | `binary-data` | Recovery must see zero-containing bytes after earlier partial sections. |
| `bool object_save_data_from_binary_impl( const std::string& bytes, ObjectSaveData* data, bool allow_missing_follower_section, bool* accepted_missing_follower_section, std::string* error_message) {` | `binary-data` | The shared strict and compatibility implementation parses complete binary storage. |
| `bool recover_object_save_data_from_binary_impl( const std::string& bytes, ObjectSaveData* data, int* dropped_partial_record_count, std::string* error_message) {` | `binary-data` | The recovery implementation scans full-byte record storage. |
| `template <typename T> bool read_pod(const std::string& bytes, size_t* offset, T* value, std::string* error_message, std::string_view label) {` | `binary-data` | POD values and fixed arrays are copied from explicit offsets without sentinel semantics. |
| `uint32_t read_u32le(const std::string& bytes, size_t offset) {` | `binary-data` | The helper decodes four binary bytes at a declared offset. |
| `int32_t read_s32le(const std::string& bytes, size_t offset) {` | `binary-data` | The helper decodes four binary bytes at a declared offset. |
| `int16_t read_s16le(const std::string& bytes, size_t offset) {` | `binary-data` | The helper decodes two binary bytes at a declared offset. |
| `bool check_bounds(const std::string& bytes, size_t offset, size_t length, std::string* error_message, std::string_view label) {` | `binary-data` | Bounds validation is based on the complete serialized payload length. |
| `bool read_rent_info(const std::string& bytes, size_t* offset, DecodedRentInfo* rent, std::string* error_message) {` | `binary-data` | The rent header is decoded from explicit legacy binary offsets. |
| `bool read_obj_file_elem(const std::string& bytes, size_t* offset, DecodedObjFileElem* elem, std::string* error_message, std::string_view label) {` | `binary-data` | Object records contain ABI padding and zero-valued fields before later significant bytes. |
| `bool read_follower_file_elem(const std::string& bytes, size_t* offset, DecodedFollowerFileElem* follower, std::string* error_message) {` | `binary-data` | Follower records are fixed-size binary payloads. |
| `bool read_object_record_or_sentinel(const std::string& bytes, size_t* offset, ObjectRecord* record, bool* is_sentinel, std::string* error_message, std::string_view label) {` | `binary-data` | Record and sentinel decoding requires fixed-width binary access. |
| `bool try_read_complete_object_list(const std::string& bytes, size_t* offset, std::vector<ObjectRecord>* records, std::string_view label) {` | `binary-data` | Recovery scans a complete binary object list through its fixed sentinel. |

## Mutable gameplay buffers outside the read-only census

These declarations are intentionally absent from the read-only candidate census. They mutate
caller-provided storage and require a separate immutable-parser design rather than a cast or view
adapter.

- `int get_number(char** name);` advances and rewrites the caller's numbered-name cursor in place.
- `int find_all_dots(char* arg);` rewrites command arguments while classifying `all` forms.
- `char* one_argument(char* argument, char* first_arg);` tokenizes a command buffer and writes the
  extracted token into caller-owned storage.
- ACMD command handlers retain mutable `char* argument` parameters because many handlers split or
  rewrite the interpreter-owned command buffer in place.
