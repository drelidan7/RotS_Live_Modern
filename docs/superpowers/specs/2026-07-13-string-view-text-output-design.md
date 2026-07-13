# String-View Text Output Design

## Purpose

Modernize the core player-output and logging entry points to accept `std::string_view` without
requiring callers that already own a `std::string` to manufacture a null-terminated pointer with
`.c_str()`. Preserve the existing text protocol, buffer limits, and compatibility behavior while
making the implementation length-aware.

## Scope

This change covers:

- `write_to_output`, the descriptor output-buffer writer used by `SEND_TO_Q`.
- Both `send_to_char` overloads.
- `log` and `mudlog`.
- Focused tests for view boundaries and compatibility behavior.
- Direct callers of these APIs whose `.c_str()` or `.data()` conversion becomes unnecessary.

Other communication functions, formatted varargs functions such as `vsend_to_char` and `vmudlog`,
and unrelated C-string modernization are outside this change.

## Design

### Length-aware descriptor output

`write_to_output` will take a `std::string_view` and use the view's explicit length instead of
calling `strlen`. It will copy bytes with `memcpy`, update `bufptr` and `bufspace` from that length,
and write the trailing null byte explicitly so the descriptor's output buffer remains compatible
with existing C-string consumers.

The existing small-buffer, large-buffer, and overflow-state transitions remain unchanged. The
implementation will continue to treat `bufspace` as the number of payload bytes available before
the required terminator.

### Player output APIs

Both `send_to_char` overloads will expose `std::string_view` as their primary text parameter. They
will retain `const char*` compatibility overloads so existing nullable-pointer call sites remain
valid. The pointer overloads will preserve the current behavior: a null message does nothing.

All overloads remain synchronous and copy the message into descriptor-owned storage before
returning. They will not retain the view or a pointer into its source, so views of temporary
`std::format` results are safe for the duration of a call.

### Logging APIs

`log` and `mudlog` will accept `std::string_view`. Their stderr writes will use explicit lengths
rather than passing a possibly non-null-terminated view to `%s`. `mudlog` will continue to format
the in-game broadcast as `[ message ]\n\r` and send it only to qualifying listeners.

Unlike `send_to_char`, the existing `log` and `mudlog` functions do not support null message
pointers. No null-pointer compatibility overload is required for them.

### Embedded null bytes

All four APIs will preserve the legacy C-string behavior by treating the first embedded null byte
as the end of the message. A small internal normalization step will trim an incoming view at its
first null before size checks, buffering, stderr output, or in-game formatting.

This prevents null bytes and any suffix after them from entering the text protocol or log files.
It also ensures that a view and a legacy C string containing the same bytes produce identical
observable output.

## Caller Migration

After the APIs are length-aware, direct calls that pass `std::string::c_str()`,
`std::string::data()`, or `std::format(...).c_str()` solely to satisfy these four signatures may
pass the string object or temporary directly. This cleanup is limited to these APIs and will not
expand into unrelated C-string conversions.

## Testing

Focused GoogleTest coverage will establish the behavior before each production change:

- A non-null-terminated substring view writes only the selected bytes.
- Exact small-buffer writes and small-to-large buffer promotion preserve buffer counters and
  termination.
- Empty views produce no player output.
- The `send_to_char` pointer compatibility overload treats null as a no-op.
- Both character-pointer and character-ID `send_to_char` paths accept views.
- Embedded null bytes truncate output for descriptor, player, and logging paths.
- `mudlog` wraps a sliced view correctly for qualifying listeners.
- `log` writes only the selected view to stderr with its existing timestamp prefix.

Focused tests will run during each red-green cycle. The complete configured GoogleTest suite will
run after the refactor, followed by the repository formatting check and `git diff --check`.

## Compatibility and Risks

- No view escapes the synchronous call that receives it.
- Existing descriptor buffer sizes and overflow policy do not change.
- Existing C-string callers continue to compile through implicit view conversion or the nullable
  `send_to_char` pointer overloads.
- The function signatures change their C++ linkage names. The repository builds these functions
  into the server and test executables rather than exposing a stable binary plugin API, so callers
  are rebuilt together.
- Length arithmetic will retain the existing descriptor counter types; checked comparisons will
  occur before any narrowing required by those counters.

## Implementation Order

1. Add failing descriptor-output and `send_to_char` tests.
2. Make `write_to_output` and both `send_to_char` paths length-aware, retaining null compatibility.
3. Add failing `mudlog` and `log` view tests.
4. Convert `mudlog` and `log` to length-aware views.
5. Remove obsolete `.c_str()` and `.data()` conversions at direct call sites.
6. Format and run the complete verification suite.
