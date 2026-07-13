# Repository-Wide `std::string_view` First-Pass Design

**Date:** 2026-07-13

## Objective

Perform the first pass of a multi-pass string cleanup across the repository. Change eligible
read-only text parameters in place to `std::string_view`, convert compatible named text constants
and lookup tables to view-based representations, and make the complete communication pipeline
length-aware. Preserve legacy text behavior while moving null-termination requirements to genuine
C boundaries.

This pass is repository-wide. It includes communication, gameplay helpers, parsers, account and
JSON code, persistence, filesystem paths, and protocol code wherever the callee borrows text only
for the duration of the call.

## Type Policy

Eligible read-only parameters change in place from `const char*` or `const std::string&` to
`std::string_view`. Compatibility overloads are not retained merely to preserve the old API shape.
Callers may pass literals, `std::string` objects, character arrays, bounded slices, and formatting
results without `.c_str()` or `.data()` adapters.

Named read-only scalar text constants become `constexpr std::string_view` where possible. Lookup
tables become `std::array<std::string_view, N>` or an equivalent view collection when every
consumer can migrate safely. A C-string representation remains only when its semantics require
one.

The following are not mechanical conversion candidates:

- Mutable `char*` and `char**` input or output buffers.
- Functions that retain, transfer, or assume ownership of supplied storage.
- Return values for which a view would introduce a lifetime hazard.
- Nullable pointers whose null value carries a distinct state that has not been redesigned.
- C library, operating-system, printf-style varargs, cryptographic, or protocol boundaries that
  require null termination.
- Character arrays whose storage is part of a persisted, wire, or ABI layout.
- Null-sentinel tables until the sentinel has been deliberately represented another way.

Every remaining `const char*` or `const std::string&` text input must have a documented reason based
on one of these contracts. A broad "legacy code" designation is not sufficient.

## Text Semantics

Migrated textual APIs preserve legacy C-string behavior: the first embedded `\0` terminates the
text. A shared helper accepts a `std::string_view` and returns the prefix before its first null
character. Explicitly binary or length-delimited functions retain all bytes and must be named or
documented so that their different contract is visible.

Normalization occurs at the first semantic consumer. A function that only forwards a view may
forward it unchanged, avoiding repeated linear scans through layered call chains. A function that
compares, parses, formats, writes, or otherwise interprets the text normalizes it before doing so.

No migrated function may assume that `view.data()` is null-terminated. No migrated function may
store the view or its data pointer beyond the call unless ownership is separately established.

## Nullable Inputs

`std::string_view` cannot safely be constructed from a null pointer. Existing nullable behavior is
made explicit at callers:

- Null meaning "do nothing" becomes an explicit branch.
- Null meaning "empty text" becomes an explicit empty view.
- Null carrying a distinct semantic state remains a pointer-based exception until that state has
  an explicit representation.

Potentially null pointers must never be passed directly to a view constructor. Existing helpers
that translate null into visible legacy text, such as `nz`, retain that behavior until their callers
are deliberately redesigned.

## C and Ownership Boundaries

Ordinary internal call chains remain view-based. When a downstream C API requires a terminator, an
already-owned null-terminated string may expose `c_str()` directly. A genuinely bounded view is
materialized into one temporary owner at the final boundary. These conversions are expected and
remain visible in the residual adapter census.

APIs that retain text must copy it into owning storage before the call returns. Paging, queueing,
protocol state, and descriptor state require specific lifetime audits; they must not retain a view
or pointer into a temporary formatting result.

Printf-style varargs functions such as `vsend_to_char` and `vmudlog` remain explicit C-format
boundaries during this pass. Replacing their formatting model is a separate design problem.

## Layered Migration

### Layer 1: Foundation and Census

Add and characterize the shared first-null helper. Produce a reproducible inventory of candidate
parameters, named constants, lookup tables, nullable contracts, ownership-sensitive APIs, and C
boundaries. The inventory is a work queue rather than a blind replacement list.

### Layer 2: Core Text Utilities and Constants

Convert foundational comparisons, searches, case-insensitive helpers, formatting helpers, scalar
constants, and compatible lookup tables. Preserve sentinel behavior and exact lookup results.
These utilities establish the view-based vocabulary required by later layers.

### Layer 3: Complete Communication Pipeline

Convert every eligible message-bearing API, including broadcasts, room/sector/outdoor sends,
descriptor output, queue insertion, `act` and its internal expansion path, and related helpers.
Replace `strlen`, `strcpy`, and pass-through `.c_str()` operations with length-aware operations.

Queue and paging functions that retain storage must copy into owned storage or remain documented
exceptions. The migrated pipeline must safely accept literals, strings, non-null-terminated slices,
and temporary `std::format` results for the duration of each call.

### Layer 4: Domain Parsers and Gameplay Helpers

Convert read-only names, keywords, command fragments, descriptions, and lookup parameters.
Functions that tokenize, split in place, or otherwise modify command buffers remain mutable-buffer
APIs pending a later parser redesign.

### Layer 5: Account, JSON, Persistence, Filesystem, and Protocol Code

Convert borrowed `const std::string&` and pointer inputs where no ownership is retained. Preserve
binary payload contracts and persistence bytes. Materialize terminated storage only at APIs such as
filesystem or operating-system calls that require it. Protocol functions receive the same lifetime
audit as communication queues before conversion.

### Layer 6: Residual Sweep and Enforcement

Re-run the full census, remove obsolete compatibility overloads and caller adapters, and document
every remaining pointer/reference exception. Add a lightweight source check that identifies new
eligible read-only string parameters without rejecting justified C, nullable, mutable, binary, or
ownership boundaries.

## Performance Requirements

The expected performance direction is neutral to positive:

- Pass-through calls avoid temporary strings and repeated `strlen` operations.
- Length-aware comparisons, writes, and copies use the known extent.
- View-based constants and lookup tables remain allocation-free.
- First-null normalization is not repeated in pure pass-through layers.
- Null-terminated materialization occurs only at actual C boundaries.

Representative prompt, broadcast, lookup, and parser paths are measured before and after their
layers. A type modernization is not accepted as justification for a measurable regression. Hot
incremental composition may use reserved owning buffers, `std::format_to`, or `std::to_chars` when
measurement supports the added complexity; those choices must be documented at their decision
points.

## Testing Strategy

Each layer begins with focused characterization tests covering the contracts it changes:

- String literals and ordinary `std::string` inputs.
- Non-null-terminated bounded slices.
- Empty views.
- Truncation at the first embedded null.
- Exact output-buffer boundaries and retained-output behavior.
- Rewritten nullable behavior.
- Lookup-table indexing and sentinel behavior.
- Persistence, protocol, and communication byte equivalence.

Each layer must build and pass its focused tests before commit. Existing characterization and boot
goldens remain byte-for-byte authorities unless a separately approved behavior change explicitly
requires regeneration.

Final validation includes:

- The complete native build and test suite.
- Required container builds and tests where available.
- Communication and boot-golden verification.
- Representative before/after performance measurements.
- A residual signature, constant, lookup-table, and adapter census.
- `git diff --check` and a clean working tree.
- Confirmation that no 32-bit legacy binary fixture was regenerated outside the i386 container.

## Completion Criteria

The pass is complete when:

1. Every eligible repository read-only text parameter accepts `std::string_view` in place.
2. Every compatible named text constant and lookup table uses a view-based representation.
3. The complete eligible communication pipeline is length-aware.
4. Textual APIs consistently terminate at the first embedded null.
5. No migrated function retains borrowed view storage.
6. Obsolete `.c_str()` and `.data()` pass-through adapters are removed.
7. Every remaining legacy text parameter or C-string table has a specific documented exception.
8. Focused, full-suite, golden, platform, census, and performance gates pass.

## Out of Scope

- Redesigning mutable in-place command parsing around immutable views.
- Replacing printf-style varargs formatting APIs.
- Changing binary formats, persisted structure layouts, wire formats, or legacy fixtures.
- Converting owning return values to views without a separate lifetime design.
- Treating embedded nulls as ordinary textual data.
- Unrelated gameplay or output behavior changes.
