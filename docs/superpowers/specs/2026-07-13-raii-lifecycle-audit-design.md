# RAII Lifecycle-Audit wave — char_data/obj_data ownership (design)

**Date:** 2026-07-13 · **Predecessors:** Phase 5 (`bd8c216`), Backlog Cleanup (`b87aa62`) both merged, master CI green · **Branch:** `modernization/raii-audit` off master (`b87aa62` or later) ·
**Parent-spec origin:** the deferred "RAII / owner-explicit smart pointers at ownership boundaries" wave named in `2026-07-06-cpp-modernization-design.md` Phase 4 ("audit char_data/obj_data lifecycles first; raw non-owning pointers remain fine for the world graph").

## Why this is the highest-risk wave

`char_data`/`obj_data` are the hottest data structures with a deeply entangled, hand-rolled ownership model: `free_char`/`free_obj` free STRING members conditionally (a PC owns its name/title/descriptions; an NPC SHARES pointers into `mob_proto` and must NOT free them — encoded as an `IS_NPC` conditional, `db.cpp:free_char`); `clear_char` placement-news over raw `CREATE()` storage; and the world graph is full of DELIBERATELY non-owning cross-links (people lists, contents chains, `affected` chains, room back-pointers). Naive smart-pointer conversion would double-free prototype strings or sever non-owning links. The parent spec's guidance is binding: **audit first; the world graph stays raw.**

## User decisions binding this wave (2026-07-13)

1. **Audit-then-convert, ONE wave** — a read-only ownership audit gates every subsequent conversion.
2. **Moderate depth: instance ownership** — make the `char_data`/`obj_data` heap-block allocation owning (RAII factory / deleter), NOT just local-scratch fixes; world-graph cross-links stay raw.
3. **Prototype-shared strings: ESCALATE after the audit quantifies them** — the audit counts the shared-string sites and reads every copy/free path; the coordinator brings the owner a concrete keep-raw-conditional-vs-model-sharing recommendation BEFORE any instance-ownership code touches those fields. The owner rules; default posture (if kept raw) is: block-ownership captured around raw+conditional string members left untouched.

## Tasks

1. **T1 — Ownership audit (READ-ONLY; the gate).** Produce `docs/superpowers/ownership-map.md`: every `char_data`/`obj_data` pointer field + every allocation/free path classified into **owning** (this object frees it), **non-owning** (world-graph cross-link — stays raw forever), **conditional** (IS_NPC/prototype-shared, exact free-path logic quoted), **local-scratch** (self-contained, RAII-safe now). Include: the prototype-shared string site count and every copy/free path they flow through; a per-boundary verdict (greenlight-convert / hold / escalate); the fixed-buffer families (output[500] and the act_info group the Backlog review flagged as one family). ZERO code changes. This doc is committed and gates all later tasks.
2. **T2 — Escalation checkpoint (coordination, not code).** Coordinator presents the audit's prototype-string numbers + a keep-raw-vs-model recommendation; owner rules on the string model. Recorded in the plan/ledger. No conversion of instance-owned string fields until this lands.
3. **T3+ — Conversions, greenlit boundaries only, safest-first:**
   - **Local-scratch + fixed-buffer families** (safest): output[500]/act_info fixed-buffer family; self-contained scratch allocations; `char*` fields `free_char`/`free_obj` UNCONDITIONALLY own (poofIn/poofOut, PC-only titles) → `std::string`/small owning type. Byte-pinned where output-visible.
   - **Instance ownership** (the moderate-depth deliverable): the `char_data`/`obj_data` heap block becomes owning — a factory returning `unique_ptr<char_data, deleter>` (deleter = current `free_char` teardown) or `free_char`-as-destructor — with world-graph cross-links kept raw and the prototype-shared string members handled per T2's ruling. Characterization-first; the combat/boot/JSON goldens are the outer net; ASan/UBSan CI is a first-class asset for this class.
   - Each conversion is its own task with the dual local gate; task count is whatever the audit's greenlit-boundary set yields (not fixed pre-audit — the audit sizes the wave).
4. **T(exit) — Exit.** Finalize the audit doc (holds/escalations become the next backlog); i386 battery + 7-job CI green; exit note; final whole-branch review; merge decision to the owner.

## Standing constraints (carry unchanged)

Goldens STOP-on-diff (boot/combat/JSON), zero sanctioned changes; RNG discipline; `-Werror`/`/WX` green on all four compilers throughout (RAII changes are the likeliest to trip MSVC/libc++-vs-libstdc++/ASan divergence — the sanitizer CI jobs are the wave's safety net; every conversion task runs the macOS ASan gate and the CI ASan jobs must stay green); suppression discipline (net-neutral-or-negative); per-task dual local gate (macOS + rots64); i386 + CI at exit; standing fixture rules; characterization pins PASS before any conversion.

## Risks

- **Prototype-shared string double-free** — the wave's signature hazard; T1 quantifies, T2 rules, default keeps the proven conditional logic untouched. ASan double-free detection is the backstop.
- **World-graph link severance** — non-owning cross-links must NEVER become owning; the audit classifies them explicitly and they are OUT of scope for conversion.
- **clear_char placement-new interaction** — instance-ownership changes must preserve the placement-new-over-raw-CREATE()-storage contract (and its documented libc++-vs-libstdc++ divergence); the factory wraps this, doesn't replace it.
- **Sizing unknown pre-audit** — accepted; T1 sizes the wave, and the exit note carries any boundary held for a future effort (no silent scope creep).

## Exit criteria

`ownership-map.md` committed and complete; T2 ruling recorded; every GREENLIT boundary converted (world graph + held/escalated boundaries explicitly untouched, documented); instance ownership landed for char_data/obj_data heap blocks; goldens byte-identical; ASan/UBSan CI green throughout; suite green everywhere; i386 battery + 7-job CI green; exit note (converted vs held vs escalated ledger); final review; merge decision to the owner.
