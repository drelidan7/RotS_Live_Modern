# Phase 4 seed list: 64-bit hazard audit cosmetic/deferred hits

Produced by Phase 2b Task 8 (bounded 64-bit hazard audit). These are hits from
the three specified greps, or findings surfaced while triaging them, that are
**not reachable at current runtime** (values fit their narrower type today;
the hazard only manifests at a future/hypothetical value, or the code path is
dead/commented) and were therefore left as-is rather than "fixed" under the
audit's bounded scope. Revisit in Phase 4/5 if the underlying assumption
changes (e.g. as 2038 approaches for the Y2038-class items).

## Grep (a): `%d` fed a `long`/`time()` value

Only 3 hits from `grep -rn '%d' src/*.cpp | grep -iE 'long|time\('`, none
actionable:

- `src/account_management_presentation.cpp:17` — `strftime(..., "%Y-%m-%d
  %H:%M:%S", ...)`. Matched only because `%m-%d` contains the substring
  `%d`; this is a `strftime` format spec, not a `printf`-style int
  conversion. Not a hazard.
- `src/act_wiz.cpp:823` — `sprintf(buf, "Exists for %ld ticks, Difficulty
  %d.\n\r", MOB_AGE_TICKS(k, time(0)), GET_DIFFICULTY(k));`. Already
  correctly typed: `%ld` for `MOB_AGE_TICKS`'s time_t-arithmetic result,
  `%d` for `GET_DIFFICULTY` (`(ch)->specials.prompt_number`, a genuine
  `int`). No bug.
- `src/mudlle.cpp:692` — `//  printf("ch=%ld host=%ld flag=%d\n",...)`.
  Commented-out code, not compiled.

## Grep (b): pointer↔int casts

Zero hits from `grep -rnE '\(int\)\s*\(?[a-z_]*ptr|\(long\)\s*[a-z_]*ptr'
src/*.cpp` and the `(int)(intptr_t`/`(int)(long)` variants. No pointer-value
truncating casts found in `src/*.cpp`.

## Grep (c): `time(0)`/`time(NULL)` narrowed into `int`

`grep -rn 'time(0)\|time(NULL)' src/*.h src/*.cpp | grep -v
'time_t|long|rots_rng'` returned ~60 hits (see below for the one false
positive). Triaging by destination type, the following fields/locals store an
epoch-seconds `time_t` value into a plain 32-bit `int` (a real narrowing that
did **not** exist on the 32-bit build, where `time_t` was already 4 bytes —
this is a genuine LP64-introduced hazard):

- `src/structs.h:1220` — `int retiredon; /* time of retirement */`, written
  at `src/utility.cpp:77` (`ch->specials2.retiredon = time(0);`) and read at
  `src/objsave.cpp:1894` (`(ch->specials2.retiredon + RENT_TIME) -
  time(0)`).
- `src/pkill.h:16` — `int kill_time;` (already flagged in-code: `src/pkill.h:8`
  "XXX: kill_time should be time_t"), written at `src/pkill.cpp:937`
  (`pkill_tab[start + i].kill_time = t;` where `t = time(0)`), read at
  `src/pkill.cpp:470`/`1063`/`1102`.
- `src/limits.cpp:605,617` — `int ... mytime; mytime = time(0);`, used only
  for `(mytime / 3600) % 24` / `(mytime / 60) % 60` time-of-day boundary
  checks.
- `src/db.cpp:819,833` — `int nr, tt; tt = time(0);`, compared against
  `player_table[nr].log_time` (a real `time_t`, `src/db.h:234`) for the
  auto-delete-stale-player sweep.
- `src/db.cpp:4172,4184` — `int time_kill; time_kill = time(0);`, stored into
  `crime_record[num_of_crimes].crime_time` (an `int` field in the on-disk
  legacy crime-record layout — frozen format, cannot widen without breaking
  the binary/JSON codec).
- `src/protocol.cpp:1432,2370` — `static time_t s_Uptime = 0; s_Uptime =
  time(0); ... sprintf(Buffer, "%d", (int)s_Uptime);` (`GetMSSP_Uptime()`).
  Explicit narrowing cast (not UB, just lossy) of an absolute boot epoch
  time into an `int` for the MSSP uptime field.
- `src/ban.cpp:32,50` — `load_banned()` reads the on-disk ban date via
  `fscanf(fl, " %s %s %d %s ", ..., &date, ...)` into a local `int date`,
  then assigns into `ban_node->date` (a `long`, `src/db.h:303`). The `%d`
  parse itself is the narrowing point.

**Why deferred:** every one of these is a "Y2038-class" hazard — the stored
`int` can represent any Unix timestamp up to `INT_MAX` (2038-01-19
03:14:07Z). At today's date the value is ~1.78 billion, well inside
`int32`'s ~2.147 billion ceiling, so the narrowing is currently a no-op (no
bits are lost) and no reachable bug exists at current runtime. Widening
`retiredon`/`kill_time`/`crime_time`/etc. to `time_t`/`int64_t` would ripple
into on-disk legacy binary layouts this project has explicitly frozen
(`static_assert(sizeof(...) == N, ...)` guards elsewhere in the codebase) —
out of scope for a bounded runtime-hazard audit. Revisit for real ahead of
2038, or as part of a deliberate on-disk format version bump.

The rest of grep (c)'s ~50 hits (`ch->player.time.logon = time(0)`,
`message.mail_time = time(0)`, `st->last_logon = time(0)`, `boot_time =
time(0)`, etc.) write into fields already declared `time_t` or `long` — safe,
just not textually containing the literal substrings the grep's exclusion
filter looked for on that exact line (the type lives in a header a few lines
away). One grep hit was an outright false positive: `src/structs.h:1414`
(`defender_data()` constructor's `last_block_time(0)` member-initializer) —
matched only because the text `...time(0)` appears at the end of the
identifier `last_block_time`, not because of an actual `time()` call.

## Related finding surfaced during triage (fixed, not deferred)

`src/protocol.cpp`'s `GetMSSP_Uptime()` reports `s_Uptime` (an *absolute*
boot-time epoch value) directly instead of `time(0) - s_Uptime` (elapsed
uptime) — likely a pre-existing logic bug unrelated to 64-bit width. Left
alone: out of scope for this audit (no width/UB hazard, just a possibly-odd
MSSP field), flagged here for whoever next touches the MSSP protocol code.

## Standalone monolithic `ageland_tests` cross-suite state pollution (deep, not bounded-fixable here)

Phase 2b Task 8 also root-caused (but did not fix) a second, much broader
class of test-isolation bug distinct from the (bounded, fixed)
`DamageMethodTest.ShieldAbsorptionConsumesManaAndReducesDamage` RNG-queue bug
described in the Task 8 report:

- **Repro:** `cd build/linux-x64 && ./ageland_tests --gtest_repeat=2` (no
  shuffle needed) segfaults deterministically, always inside
  `InterpreAccountMenu.SelectingSameLinklessActiveCharacterReconnectsExistingBody`
  on its *second* (pass-2) invocation.
- **Also observed** under `--gtest_shuffle` with a single pass (no repeat):
  roughly half of a sample of 15 random seeds crashed (SIGSEGV or SIGABRT),
  landing in `InterpreAccountMenu.SelectingSameLinklessActiveCharacterReconnectsExistingBody`,
  `InterpreAccountMenu.SelectingSameActivePlayingCharacterUsurpsExistingDescriptor`,
  or (once) `CharacterizationCombatTest.DamageTranscriptSeed42`.
- **Ruled out:** `./ageland_tests --gtest_filter="InterpreAccountMenu.*"
  --gtest_repeat=20` (this suite alone, repeated 20x) never crashes — the
  trigger requires *other* suites' state from an earlier pass/position in
  the shuffle order, not just this suite's own repetition.
- **Root-cause candidate (documented, not fully bisected):**
  `ensure_test_world_room()` is independently reimplemented with different
  semantics in at least three files —
  `src/tests/interpre_account_menu_tests.cpp:372`,
  `src/tests/db_loader_tests.cpp:413`, `src/tests/spell_pa_tests.cpp:29` —
  all sharing the one process-wide static `room_data::BASE_WORLD`. The
  `interpre_account_menu_tests.cpp` version only actually allocates a world
  if `BASE_WORLD` is still null; once any *other* suite has already
  allocated one, it silently reuses `world[0]` and re-`strdup()`s
  `.name`/`.description` without freeing the previous pointer, and nothing
  RAII-resets `world[0].people` the way `ScopedDescriptorListReset`/
  `ScopedPlayerTableReset` reset `descriptor_list`/`player_table`. This is
  the same class of hazard already called out by name in
  `src/tests/damage_test_context.h`'s `ensure_test_world()` comment
  ("turns into order-dependent segfaults once earlier tests' allocator
  churn dirties the heap first").
- **Confirmed NOT relevant to CI/real verification:** the project's ctest
  registration (`gtest_discover_tests`) runs every individual test case as
  its own process invocation (`--gtest_filter=<one test>` per ctest entry),
  so there is no cross-test global-state accumulation in the path CI and
  this project's standard battery actually exercise. Only the ad-hoc
  monolithic `./ageland_tests` binary (run directly, with `--gtest_repeat`
  or `--gtest_shuffle`) is affected.
- **Disposition:** left undone. Pinning the exact contributing suite(s) and
  designing a consistent scoped-reset convention for the shared
  world/character_list-family globals across 44+ test files is a
  cross-cutting test-infrastructure change, not a small bounded fix —
  outside this task's scope per its own guidance ("if it is deeper, document
  precisely ... and leave"). A prior report (Phase 2b Task 1) had also
  logged a monolithic-run segfault, in `WeaponMasterProcTest`; that exact
  crash did not reproduce in >5 default-order monolithic runs during this
  audit, plausibly already resolved as a side effect of Task 7a's broader
  raw-storage-construction audit (commit `eac83f2`) — not independently
  re-verified against the pre-Task-7a commit.
