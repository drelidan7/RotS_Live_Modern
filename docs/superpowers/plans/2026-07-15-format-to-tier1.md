# format_to Tier 1 Conversion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert all 113 production `accumulator += std::format(...)` call sites to `std::format_to(std::back_inserter(accumulator), ...)`, eliminating one temporary `std::string` allocation+copy per call, and add `reserve()` to the largest loop-driven string builders.

**Architecture:** Pure mechanical refactor with byte-identical output, verified by the existing characterization goldens (`act_info_format_tests`, `act_wiz_format_tests`, `CharacterizationJson`, boot goldens) and the full ctest suite. No new behavior, no new tests. Work is grouped per file cluster so each task builds and tests independently.

**Tech Stack:** C++20 `std::format_to` / `std::back_inserter` (`<iterator>`), CMake presets `macos-arm64` and `linux-x64` (rots64 container), GoogleTest via ctest.

## Global Constraints

- **NEVER use the Edit/Write tools on the target `.cpp` files.** A PostToolUse hook runs clang-format on edited C++ files, and every target file has 100+ lines of pre-existing formatter drift (e.g. `act_info.cpp` would change 1418 lines). `handler.cpp` additionally has mixed CRLF/LF line endings. All source edits MUST be made with the byte-safe Python script below (or equivalent Python byte-level edits) run via Bash. The plan file itself and non-C++ files may use Edit/Write normally.
- Do NOT run `clang-format`, `make format`, or any formatter on the touched files.
- Output must be byte-identical: the characterization suites and goldens are the acceptance test. If a golden diff appears, the change is wrong — fix the code, never regenerate goldens.
- `src/tests/comm_output_tests.cpp` has 1 `+= std::format` site — it is a test file and explicitly OUT OF SCOPE.
- All GNU builds use `-Wall -Wextra -Werror`; any new warning is a failure.
- Branch: all work happens on `perf/format-to-tier1`, never directly on `master`. Merge to master is the owner's decision — do not merge.
- Verification cadence (AGENTS.local.md): every task runs macos-arm64 build + ctest; final task adds boot goldens on both macos-arm64 (native) and rots64 (container). The i386/qemu battery is deferred to branch finalization per local cadence. No new test files are created, so no AddressSanitizer gate is required.
- Commit messages: imperative subject ≤72 chars, and end the body with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Site inventory (verified by grep at plan time)

| File | `+= std::format` sites |
|---|---|
| src/act_info.cpp | 68 |
| src/act_wiz.cpp | 29 |
| src/interpre.cpp | 4 |
| src/handler.cpp | 4 (CRLF/LF mixed file) |
| src/graph.cpp | 2 |
| src/shapezon.cpp | 1 |
| src/shaperom.cpp | 1 |
| src/shapeobj.cpp | 1 |
| src/protocol.cpp | 1 |
| src/mudlle.cpp | 1 |
| src/color.cpp | 1 |
| **Total** | **113** |

All 113 sites are standalone statements of the form `identifier += std::format(...);` (verified: no compound expressions like `x += std::format(...) + y`). All accumulators are plain local identifiers (`out`, `stats_line`, `line`, `Result`, etc.). Every file already has `#include <format>` and none has `#include <iterator>`.

---

### Task 1: Branch, conversion script, and green baseline

**Files:**
- Create: `/private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py`
- No repo source changes in this task.

**Interfaces:**
- Produces: git branch `perf/format-to-tier1`; the converter script at the path above, invoked as `python3 <script> <file.cpp>`, which (a) rewrites every `X += std::format(` head to `std::format_to(std::back_inserter(X), `, (b) inserts `#include <iterator>` directly after `#include <format>` if missing, (c) prints `<file>: N sites converted` and lists any resulting lines >100 columns. Later tasks consume this script verbatim.

- [ ] **Step 1: Create the branch**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
git checkout -b perf/format-to-tier1
```

Expected: `Switched to a new branch 'perf/format-to-tier1'`

- [ ] **Step 2: Write the converter script**

Write this exact content to `/private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py`:

```python
#!/usr/bin/env python3
"""Byte-safe converter: X += std::format(...)  ->  std::format_to(std::back_inserter(X), ...).

Reads/writes latin-1 so every byte (including CRLF) round-trips unchanged.
Also inserts '#include <iterator>' after '#include <format>' if missing.
Usage: convert_format_to.py <file.cpp>
"""
import re
import sys

path = sys.argv[1]
with open(path, 'rb') as f:
    text = f.read().decode('latin-1')

pattern = re.compile(r'(?m)^([ \t]*)([A-Za-z_]\w*)\s*\+=\s*std::format\(')
text, count = pattern.subn(r'\1std::format_to(std::back_inserter(\2), ', text)

if '#include <iterator>' not in text and count:
    text, inc = re.subn(r'(?m)^(#include <format>\r?\n)',
                        '\\g<1>#include <iterator>\n', text, count=1)
    if inc != 1:
        sys.exit(f'{path}: could not insert #include <iterator>')

with open(path, 'wb') as f:
    f.write(text.encode('latin-1'))

print(f'{path}: {count} sites converted')
for lineno, line in enumerate(text.splitlines(), 1):
    if len(line.rstrip("\r")) > 100 and 'std::format_to(' in line:
        print(f'  LONG {lineno}: {len(line.rstrip(chr(13)))} cols')
```

- [ ] **Step 3: Confirm green baseline (build + full ctest)**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern/src
cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
```

Expected: build succeeds; ctest reports `100% tests passed` (~1071 tests, ~71 skipped on macOS). If the baseline is red, STOP and report — do not proceed on a broken baseline.

No commit in this task (no repo changes).

---

### Task 2: Convert src/act_info.cpp (68 sites)

**Files:**
- Modify: `src/act_info.cpp` (68 sites; functions `do_info`, `do_who`, `do_fame`, `do_time`, `print_exploits`, `do_exits`, `do_score`, `do_commands`, `do_fame_leader_string`, `do_help`, `do_users`, `do_levels`, `do_affections`, `do_rank`)

**Interfaces:**
- Consumes: converter script from Task 1 at `/private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py`
- Produces: `src/act_info.cpp` with 0 remaining `+= std::format` sites and `#include <iterator>` present.

- [ ] **Step 1: Run the converter**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
python3 /private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py src/act_info.cpp
```

Expected: `src/act_info.cpp: 68 sites converted`. If any `LONG` lines are reported, rewrap them with a Python byte-edit (move arguments to the next line matching the file's existing continuation indent) — never with the Edit tool.

- [ ] **Step 2: Verify the conversion counts**

```bash
grep -cE '\+=\s*std::format' src/act_info.cpp; grep -c 'std::format_to(std::back_inserter(' src/act_info.cpp; grep -c '#include <iterator>' src/act_info.cpp
```

Expected: `0`, `68`, `1`.

- [ ] **Step 3: Sanity-check the diff is heads-only**

```bash
git diff --stat src/act_info.cpp
```

Expected: roughly `1 file changed, ~69 insertions(+), ~68 deletions(-)` (68 heads + 1 include). If hundreds of lines changed, the formatter hook fired — `git checkout src/act_info.cpp` and redo via Python only.

- [ ] **Step 4: Build and run the full test suite**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
```

Expected: build clean (no warnings — `-Werror`), `100% tests passed`. The `act_info_format_tests.cpp` characterization suite pins this file's output byte-for-byte.

- [ ] **Step 5: Commit**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
git add src/act_info.cpp
git commit -m "perf: format directly into accumulators in act_info

Convert 68 'out += std::format(...)' sites to
std::format_to(std::back_inserter(out), ...), removing one temporary
string allocation and copy per call. Output is byte-identical
(act_info_format_tests characterization suite).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Convert src/act_wiz.cpp (29 sites)

**Files:**
- Modify: `src/act_wiz.cpp` (29 sites; functions `do_show`, `do_stat_character`, `do_stat_room`, `do_stat_object`)

**Interfaces:**
- Consumes: converter script from Task 1 at `/private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py`
- Produces: `src/act_wiz.cpp` with 0 remaining `+= std::format` sites and `#include <iterator>` present.

- [ ] **Step 1: Run the converter**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
python3 /private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py src/act_wiz.cpp
```

Expected: `src/act_wiz.cpp: 29 sites converted`. Rewrap any reported `LONG` lines via Python byte-edit only.

- [ ] **Step 2: Verify the conversion counts**

```bash
grep -cE '\+=\s*std::format' src/act_wiz.cpp; grep -c 'std::format_to(std::back_inserter(' src/act_wiz.cpp; grep -c '#include <iterator>' src/act_wiz.cpp
```

Expected: `0`, `29`, `1`.

- [ ] **Step 3: Sanity-check the diff is heads-only**

```bash
git diff --stat src/act_wiz.cpp
```

Expected: ~30 insertions / ~29 deletions. Hundreds of changed lines means formatter noise — revert and redo via Python.

- [ ] **Step 4: Build and run the full test suite**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
```

Expected: clean build, `100% tests passed` (`act_wiz_format_tests.cpp` pins `do_stat_*`/`do_show` output byte-for-byte).

- [ ] **Step 5: Commit**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
git add src/act_wiz.cpp
git commit -m "perf: format directly into accumulators in act_wiz

Convert 29 'out += std::format(...)' sites to
std::format_to(std::back_inserter(out), ...). Output is byte-identical
(act_wiz_format_tests characterization suite).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Convert the remaining 9 files (16 sites)

**Files:**
- Modify: `src/interpre.cpp` (4), `src/handler.cpp` (4 — mixed CRLF/LF, byte-safety is critical), `src/graph.cpp` (2), `src/shapezon.cpp` (1), `src/shaperom.cpp` (1), `src/shapeobj.cpp` (1), `src/protocol.cpp` (1), `src/mudlle.cpp` (1), `src/color.cpp` (1)

**Interfaces:**
- Consumes: converter script from Task 1 at `/private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py`
- Produces: zero `+= std::format` sites in production code repo-wide.

- [ ] **Step 1: Run the converter on all nine files**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
for f in interpre handler graph shapezon shaperom shapeobj protocol mudlle color; do
  python3 /private/tmp/claude-501/-Users-drelidan-Projects-GitHub-RotS-Live-Modern/82d69c58-eb19-4052-b983-5ad19937946c/scratchpad/convert_format_to.py src/$f.cpp
done
```

Expected output lines: `interpre.cpp: 4`, `handler.cpp: 4`, `graph.cpp: 2`, and `1` each for the other six. Rewrap any `LONG` lines via Python byte-edit only.

- [ ] **Step 2: Verify counts and handler.cpp byte-safety**

```bash
grep -rcE '\+=\s*std::format' src --include='*.cpp' | grep -v ':0$'
file src/handler.cpp
git diff --stat src/handler.cpp
```

Expected: only `src/tests/comm_output_tests.cpp:1` remains (out of scope). `file` still reports `with CRLF, LF line terminators` (line endings preserved). handler.cpp diff shows ~5 insertions / ~4 deletions.

- [ ] **Step 3: Build and run the full test suite**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
```

Expected: clean build, `100% tests passed` (protocol/shape/mudlle output is pinned by `protocol_tests.cpp` and `shape_format_tests.cpp`).

- [ ] **Step 4: Commit**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
git add src/interpre.cpp src/handler.cpp src/graph.cpp src/shapezon.cpp src/shaperom.cpp src/shapeobj.cpp src/protocol.cpp src/mudlle.cpp src/color.cpp
git commit -m "perf: format directly into accumulators across nine files

Convert the remaining 16 'x += std::format(...)' sites (interpre,
handler, graph, shapezon, shaperom, shapeobj, protocol, mudlle, color)
to std::format_to(std::back_inserter(x), ...). handler.cpp edited
byte-safely to preserve its mixed CRLF/LF line endings.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: Add reserve() to the largest loop-driven builders

**Files:**
- Modify: `src/act_info.cpp` (functions `do_who`, `do_info`, `do_commands`, `print_exploits`, `do_fame`)
- Modify: `src/act_wiz.cpp` (function `do_show`)

**Interfaces:**
- Consumes: the converted files from Tasks 2–3 (accumulators now filled via `std::format_to`).
- Produces: a `reserve()` call immediately after the accumulator's declaration in each listed function.

- [ ] **Step 1: Locate each accumulator declaration**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
for fn in do_who do_info do_commands print_exploits do_fame; do
  echo "== $fn =="; awk "/^ACMD\\($fn\\)|^[a-z].*[ *]$fn\\(/,/^}/" src/act_info.cpp | grep -n 'std::string' | head -4
done
awk '/^ACMD\(do_show\)/,/^}/' src/act_wiz.cpp | grep -n 'std::string' | head -6
```

Note the exact declaration line of each function's loop accumulator (the variable that received the bulk of the `format_to` calls — `out` in most, plus `godrooms`/`death_traps`/`owners` style locals in `do_show`).

- [ ] **Step 2: Insert reserve() after each declaration via Python byte-edit**

For each accumulator found in Step 1, apply this pattern (example for `do_who`'s `out`; repeat with the right line number and variable for each function). 2048 bytes covers a typical full listing without reallocation and costs nothing when output is short:

```python
#!/usr/bin/env python3
import sys
path, lineno, var = sys.argv[1], int(sys.argv[2]), sys.argv[3]
with open(path, 'rb') as f:
    lines = f.read().decode('latin-1').splitlines(keepends=True)
decl = lines[lineno - 1]
indent = decl[:len(decl) - len(decl.lstrip())]
eol = '\r\n' if decl.endswith('\r\n') else '\n'
lines.insert(lineno, f'{indent}{var}.reserve(2048);{eol}')
with open(path, 'wb') as f:
    f.write(''.join(lines).encode('latin-1'))
print(f'{path}:{lineno} + {var}.reserve(2048)')
```

Run as: `python3 insert_reserve.py src/act_info.cpp <decl-line> out` (once per function; save the script to the scratchpad directory first).

- [ ] **Step 3: Verify insertions**

```bash
grep -n '\.reserve(2048)' src/act_info.cpp src/act_wiz.cpp
```

Expected: 5 hits in act_info.cpp (one per listed function), 1+ in act_wiz.cpp (`do_show`), each directly after its accumulator's declaration (spot-check with `git diff`).

- [ ] **Step 4: Build and run the full test suite**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
```

Expected: clean build, `100% tests passed` (reserve never changes content, only capacity).

- [ ] **Step 5: Commit**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
git add src/act_info.cpp src/act_wiz.cpp
git commit -m "perf: reserve accumulators in the big listing builders

Pre-reserve 2048 bytes in do_who/do_info/do_commands/print_exploits/
do_fame (act_info) and do_show (act_wiz) so loop appends via
std::format_to don't reallocate mid-listing.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: Two-platform verification (macos-arm64 + rots64) with boot goldens

**Files:**
- No source changes. Verification only.

**Interfaces:**
- Consumes: the completed branch `perf/format-to-tier1` (Tasks 1–5 committed).
- Produces: a pass/fail verification report for the owner's merge decision.

- [ ] **Step 1: Native macOS full battery**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern/src
cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64
cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
```

Expected: `100% tests passed`; boot-golden `verify` exits 0 (no drift).

- [ ] **Step 2: rots64 Linux x64 battery**

```bash
cd /Users/drelidan/Projects/GitHub/RotS_Live_Modern
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```

Expected: `100% tests passed`; boot-golden `verify` exits 0.

- [ ] **Step 3: Final repo-wide count check**

```bash
grep -rcE '\+=\s*std::format' src --include='*.cpp' --include='*.h' | grep -v ':0$'
grep -rc 'std::format_to(std::back_inserter(' src --include='*.cpp' | grep -v ':0$' | awk -F: '{s+=$2} END {print "format_to sites:", s}'
```

Expected: only `src/tests/comm_output_tests.cpp:1` remains; `format_to sites: 113`.

- [ ] **Step 4: Report to owner**

Summarize: sites converted per file, test totals on both platforms, boot-golden results. Surface the merge decision and the deferred i386 finalization battery — both are the owner's call. Do NOT merge or push.

---

## Self-review notes

- Spec coverage: 113 production sites (Tasks 2–4) + reserve on big builders (Task 5) + two-platform verification (Task 6) — matches the Tier 1 scope. The single test-file site is explicitly excluded.
- No placeholders: every step has exact commands, exact expected counts, and complete script content.
- Type consistency: the converter script is defined once (Task 1) and consumed by path in Tasks 2–4; the reserve inserter is self-contained in Task 5.
- Risk register: formatter-hook noise (mitigated: Python-only edits + diff-stat checks), CRLF corruption in handler.cpp (mitigated: latin-1 byte round-trip + `file` check), long lines under the ~100-col convention (mitigated: converter reports `LONG` lines for manual wrap), behavior drift (mitigated: characterization suites + boot goldens on two platforms).
