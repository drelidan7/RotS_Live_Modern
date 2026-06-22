# Random number generation

**Source files:** the RNG wrappers `utility.cpp:928-988` (`number()`, `number(double)`, `number_d`,
`number(int,int)`, `dice`); seeding `comm.cpp:161` (`std::srand`) and `comm.cpp:251` (`srandom`); the
two roll sites this doc evaluates — stat assignment `profs.cpp:435-479` (`roll_stat`/`roll_stats`/
`get_stat_array`) and const-HP `limits.cpp:884-888` (`do_start`).
**Status:** ✅ how RNG works today, its real (and merely theoretical) problems, the `<random>`
alternatives, and the trade-offs for switching stat/HP rolling.

> **TL;DR.** RotS draws everything from C `std::rand()` via a thin `number()`/`dice()` wrapper. On the
> actual build target (32-bit Linux/glibc) the *quality* is fine and modulo bias is negligible, so
> switching to `<random>` is mostly about **portability, fixing two real wrapper bugs, and unlocking
> richer distribution *shapes***. It will **not** by itself visibly change stat or HP outcomes —
> those are set by the *roll method*, which is a separate (design) lever. See §6–§7.

---

## 1. How RNG works today

Everything routes through five functions in `utility.cpp`, all backed by C `std::rand()`:

| Function | Returns | Body | Range |
|---|---|---|---|
| `number()` | `double` | `rand() / RAND_MAX` | **[0.0, 1.0]** (inclusive — see §4) |
| `number(double max)` | `double` | `number() * max` | **[0, max]** (inclusive) |
| `number_d(from, to)` | `double` | `number(to) + from` | **[from, from+to]** ⚠️ (bug, §4) |
| `number(int from, int to)` | `int` | `(rand() % (to−from+1)) + from` | [from, to] |
| `dice(n, size)` | `int` | Σ of `n` × `((rand() % size) + 1)` | [n, n·size] |

`number(int,int)` is the workhorse (called from essentially every system — combat, spells, skills,
mobs). `dice()` builds dice-sum distributions. `number()`/`number(double)` give the fractional rolls
used for probability checks (`if (number() < p)`).

**Seeding** happens once at boot: `std::srand(std::time(0))` (`comm.cpp:161`) **and**
`srandom(std::time(0))` (`comm.cpp:251`). There is also exactly **one** stray BSD call —
`random() % 3` in `act_soci.cpp:332` (a social randomizer) — so the codebase nominally touches *two*
C RNG streams (`rand()` and `random()`).

---

## 2. What `std::rand()` actually is here

`std::rand()`'s algorithm, period, and `RAND_MAX` are **implementation-defined** — the C++ standard
guarantees almost nothing about quality.

- **On the deployment target (the mandated 32-bit Linux/glibc Docker build):** glibc implements
  `rand()` as its `random()` **TYPE_3 additive-feedback** generator (31×`int32` of state), with
  `RAND_MAX = 2³¹−1 = 2147483647` and a period of ~`16·(2³¹−1) ≈ 2³⁵`. Its low-order bits are **not**
  weak (unlike a naive LCG), so `% n` is acceptable and the fractional `rand()/RAND_MAX` has ~31 bits
  of resolution. In short: *fine for a MUD*. Because glibc's `rand()` **is** `random()`, the two
  seeders in §1 reseed the **same** stream (the second is redundant), and `act_soci`'s `random()`
  shares that stream.
- **On other platforms (MSVC, some embedded libc):** `rand()` is often a 16-bit LCG with
  `RAND_MAX = 32767` and **weak low bits** — there `% n` is biased *and* the bits are poor, and any
  `number(int,int)` with a range > 32767 silently breaks. glibc's `rand()` ≠ `random()` is not
  guaranteed elsewhere, so the redundant seeding and shared stream are glibc-specific accidents.

So the portability story is the real weakness: the code is only "good" because of where it happens to
run.

---

## 3. The two roll sites you're considering

### Stat assignment (`profs.cpp:435-479`, then `stat_assigner` §-stats-and-character-power §3)
1. **`roll_stat`** rolls **4d6 and drops the lowest** → an integer in **3–18**, the classic
   bell-shaped curve (mean ≈ **12.24**, mode 13).
2. **`roll_stats`** makes six such rolls.
3. **`get_stat_array`** does **rejection sampling on the *sum***: it rerolls *all six* until the total
   lands in `[sum_min, sum_max]` (**[80, 85]** at creation via `do_start`; `[80, 93]` on the
   `profs.cpp:418` path), then **sorts ascending**. The natural sum of six 4d6-drop-lowest stats has
   mean ≈ **73** (σ ≈ 7), so requiring ≥ 80 keeps only the upper tail — it rerolls ~**8×** on average
   until it gets a roughly **+1σ** array. The sorted result is then handed to `stat_assigner`, which
   maps the biggest rolls onto your most-invested class's primary stats (stats-and-character-power §3).

**Distribution character:** already a bell curve with a deliberately **high floor** (the sum gate).
The RNG *quality* barely matters here; the *shape* is governed entirely by "4d6-drop-low + sum window."

### Const-HP roll (`limits.cpp:884-888`)
```
constabilities.hit = 10;            // base
if (!number(0, 1)) constabilities.hit++;   // ≈50% → 11
```
A single Bernoulli: base const-HP is **10 or 11**, ~50/50. (`number(0,1)` is `rand() % 2`; with
`RAND_MAX` odd on glibc this split is *exactly* even.) Const-HP then grows with level via small
increments elsewhere (`limits.cpp:88-92`, `:983`); §profs `recalc_abilities` turns `constabilities.hit`
into the real HP pool.

**Distribution character:** essentially none — a coin flip. RNG quality is irrelevant to it.

---

## 4. Real vs. theoretical problems with the current setup

Ranked by actual impact on the glibc build:

1. **`number()` is inclusive of 1.0** (real, latent). `rand()/RAND_MAX` can return exactly `1.0`
   (when `rand() == RAND_MAX`), so `number(max)` can return `max`. Probability checks `number() < p`
   are unaffected, but any `(int)(number() * n)` used as an index can occasionally yield `n`
   (out of range) — once per ~2³¹ draws, but real. A proper `[0,1)` generator removes the class of
   bug entirely.
2. **`number_d(from, to)` returns `[from, from+to]`, not `[from, to]`** (real bug). The body is
   `number(to) + from`; it should be `number(to − from) + from`. Any caller expecting `[from, to]`
   gets a wrong (wider, shifted) range. Low-traffic function, but it is incorrect.
3. **`number(int,int)`'s overflow guard is broken** (edge). When `to − from + 1` overflows to 0 it
   sets `to = from` but still computes `rand() % upper_end` with `upper_end == 0` → division by zero /
   UB. Only reachable with an absurd (≈2³¹-wide) range, so practically dormant.
4. **Modulo bias** in `number(int,int)` and `dice()` (theoretical on glibc). `rand() % n` is uniform
   only when `n` divides `RAND_MAX+1`. With `RAND_MAX+1 = 2³¹`, the bias for the ranges actually used
   (d6, `%2`, `%3`, gold `200–300`, etc.) is on the order of `n / 2³¹ ≈ 10⁻⁹` — **negligible**. It
   becomes real only on small-`RAND_MAX` platforms or for ranges approaching `RAND_MAX`.
5. **Seeding / streams** (cosmetic). Seeding with `time(0)` means two restarts within the same second
   repeat the sequence; the dual `srand`/`srandom` is redundant on glibc and *two separate streams*
   off-glibc; `act_soci`'s lone `random()` should just use `number()`.
6. **Quality is not portable** (the umbrella concern). None of the above is guaranteed across
   compilers; the code is correct-enough by luck of running on glibc.

---

## 5. The `<random>` toolbox (C++11, available under `-m32`)

`<random>` cleanly separates the **engine** (the bit source) from the **distribution** (the shape) —
which is exactly the distinction that matters for your question.

**Engines:**

| Engine | Quality | Speed | State | Notes |
|---|---|---|---|---|
| `std::mt19937` | very good (period 2¹⁹⁹³⁷−1, 623-dim equidistributed) | fast | ~2.5 KB | **the standard default**; ideal here |
| `std::mt19937_64` | very good | fast | ~2.5 KB | 64-bit output variant |
| `std::minstd_rand` | poor (LCG) | fastest | 8 B | **no better than `rand()`** — skip |
| `std::ranlux24/48` | excellent | **slow** | small | overkill; too slow for the combat hot path |
| *(non-std) PCG / xoshiro256\*\*\** | excellent | fastest | 8–32 B | best quality-per-byte, tiny state, but an external file/dependency |

**Distributions** (the part that fixes correctness and enables shapes):

- `std::uniform_int_distribution<int>(a,b)` — **unbiased** uniform integers (internally rejection-
  samples), eliminating modulo bias. Drop-in for `number(int,int)`.
- `std::uniform_real_distribution<double>(0,1)` — proper **`[0,1)`** reals, fixing §4.1.
- For **shape** (relevant to stats/HP if you want a different *feel*): `std::binomial_distribution`
  (bell curve, tunable), `std::normal_distribution` (Gaussian; round+clamp for a stat), and
  `std::discrete_distribution` (designer-authored per-outcome weights). A `dice()` sum is already a
  bell — these just give finer control than re-summing d6s.

**The key distinction:** swapping the *engine/distribution* fixes **uniformity and quality**. It does
**not** change the *shape* of a roll — a `uniform_int_distribution(1,6)` is the same flat d6 as
`rand()%6+1`, just unbiased. Changing the *shape* (e.g., making stats cluster more, or giving HP a
spread) is a **design decision** made by choosing a different distribution/method, independent of which
library produces the bits.

---

## 6. Trade-offs of switching

| Dimension | Verdict |
|---|---|
| **Quality / portability** | ✅ Big win in principle: `mt19937` is guaranteed-good on every platform; distributions remove modulo bias and the `[0,1]` off-by-one. On the *current* glibc build the practical gain is modest (bias already ~0), so the honest case is **portability + bug fixes + future-proofing**, not "your dice are broken today." |
| **Performance** | **No meaningful impact.** Per *draw*, `mt19937` is in the same nanosecond class as glibc `rand()` — plausibly *faster*, since glibc's `rand()`/`random()` take an internal thread-safety **lock** per call while a single-threaded global engine takes none; `mt19937`'s only lumpy cost is the 624-word "twist", amortized over 624 draws. `uniform_int_distribution` adds a rejection loop averaging ~1 iteration; `uniform_real` adds a divide (same as today's `rand()/RAND_MAX`). State grows from ~128 B to ~2.5 KB — trivial and L1-resident. **The one real pitfall:** the engine must be a single persistent (`static`/global) object seeded **once** — constructing/seeding a fresh `mt19937` *per call* would be both slow (~2.5 KB re-init) and wrong (resets the stream); distributions, by contrast, are fine to build per call (no allocation). RNG isn't on a pulse-driven (4 Hz) MUD's critical path — total draws/sec are in the low thousands, dwarfed by I/O and world iteration; even the ~200-draw stat reroll at creation is immaterial. **Avoid `ranlux`** (genuinely slow); on the `-m32` build prefer 32-bit `mt19937` over `mt19937_64`. |
| **State / 32-bit** | `mt19937` is ~2.5 KB of global state — trivial, and compiles/runs identically under `-m32`. PCG/xoshiro are 8–32 B if cache footprint ever matters. No toolchain blocker (codebase is C++17). |
| **Determinism / testing** | A single global engine mirrors today's global `rand()`. An engine *object* is easier to seed deterministically for reproducible tests than `srand`. Caveat: distribution **output** is *not* portable across stdlib versions (only the engine bitstream is standardized) — irrelevant here, since RotS never replays an RNG sequence across builds. |
| **Thread-safety** | Same as today: a global engine is not thread-safe, but the MUD is single-threaded. No regression. |
| **Save / replay impact** | **None.** No persisted state depends on the RNG sequence; characters store *results*, not seeds. |
| **Migration effort / risk** | **Low and localized.** Keep the `number()`/`dice()` signatures; reimplement their bodies over one global `mt19937` + the two uniform distributions in `utility.cpp`. Drop the redundant `srandom` and migrate `act_soci`'s `random()` to `number()`. The hundreds of existing `number(a,b)`/`dice(...)` call sites compile and behave unchanged (just unbiased). The change also *fixes* `number_d` and the `[0,1]` inclusivity in passing — audit the handful of `(int)(number()*n)` and `number_d` callers first. |

---

## 7. Recommendation

1. **Do the engine swap — it's cheap, safe, and fixes real bugs.** Replace the `rand()` bodies with a
   single global `std::mt19937` seeded once; route `number(int,int)` through
   `uniform_int_distribution`, `number()`/`number(double)`/`number_d` through
   `uniform_real_distribution` (which also corrects the `[0,1]` inclusivity and the `number_d` range
   bug). Retire `srandom`/the lone `random()`. This is a one-file change behind the existing API.
2. **Treat stat/HP *distribution shape* as a separate, design-level question.** The RNG swap will
   **not** make stats or HP "roll better" in any visible way — a d6 is a d6, and the 10/11 HP coin
   flip is a coin flip. If the goal is a different *feel*:
   - **Stats:** tune the *method* — change the dice (`5d6`-drop-2, `3d6`), widen/narrow the sum window
     `[80,85]`, or replace `4d6-drop-low` with a `binomial`/`normal`/`discrete` distribution for a
     designer-controlled curve. (Note the current sum-rejection already imposes a strong high floor;
     decide whether that stays.)
   - **HP:** if you want base-HP variety, roll a *range* or a small `binomial`/`dice` instead of the
     single Bernoulli — again a shape choice, not an RNG-library choice.
3. **Sequence:** land the library swap first (correctness/portability, zero balance change), *then*
   iterate on shape with whoever owns game balance — so any change in stat/HP feel is intentional and
   attributable, not a side effect of changing bit sources.

---

## 8. Open questions / flags

- **`number()` returns `[0,1]` inclusive** (`utility.cpp:932`) — can return exactly `1.0`; audit
  `(int)(number()*n)` indexers. A `uniform_real_distribution<double>(0,1)` gives the correct `[0,1)`.
- **`number_d(from,to)` range bug** (`utility.cpp:948`): yields `[from, from+to]`, not `[from, to]`.
- **`number(int,int)` overflow guard is dead/UB** for ~2³¹-wide ranges (`utility.cpp:963`).
- **Redundant/dual seeding** (`comm.cpp:161` + `:251`) and a lone BSD `random()` (`act_soci.cpp:332`)
  — consolidate onto one stream when migrating to `<random>`.
- **`time(0)` seeding** repeats the sequence on same-second restarts — minor, but a deterministic or
  entropy-seeded engine (`std::random_device`) is cleaner.
