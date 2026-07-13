# Security notes

Findings surfaced during modernization work that are pre-existing, scoped, and deliberately
**not** auto-fixed as part of the task that found them. Each entry records the finding, its
current disposition, and why it was deferred rather than silently left undocumented.

## Legacy (non-account-native) character passwords are stored and compared in plaintext

**Source files:** `src/interpre.h`, `src/interpre.cpp`, `src/db.cpp`

**Status:** parked finding, disposition recorded here (Phase 3 Task 5) — not a regression from
any modernization task, and not fixed by this entry.

### What CRYPT actually does

`interpre.h` defines the historical `CRYPT` macro used everywhere a character password is
hashed/verified:

```cpp
// #define CRYPT(a,b) ((char *) crypt((a),(b)))
#define CRYPT(a, b) ((char*)(a))
```

The real `crypt(3)` call is commented out; `CRYPT(a, b)` is a no-op passthrough that returns its
first argument unchanged. Every `strncmp(CRYPT(arg, stored_pwd), stored_pwd, MAX_PWD_LENGTH)` call
site therefore reduces to a raw plaintext `strncmp(arg, stored_pwd, MAX_PWD_LENGTH)` — the
just-typed password compared byte-for-byte against whatever is sitting in the character's on-disk
`pwd` field, unhashed.

This is a **separate system** from account credentials: account passwords go through
`rots_crypt.cpp`'s SHA-512-crypt (`$6$`) implementation (see `docs/BUILD.md` and
`.superpowers/sdd/p2b-task-4-report.md`) and are not affected by this finding.

### Where this is actually live (five sites, not one)

An earlier scout pass estimated "1 live strncmp" against a legacy password; a full audit
(`grep -n "strncmp(CRYPT" src/interpre.cpp` yields six sites) found **five** live comparison
sites in `interpre.cpp`, all reachable only for characters that are *not* linked to an account
(account-linked characters are routed to the account-password branches by `*d->account_name`
checks at the relevant menu decision points, e.g. interpre.cpp:2904/4025/4042).

Three verify a **stored credential** (typed password vs. the password persisted in the
character's save file):

- `interpre.cpp:3453` — `CON_ACCTLEGPWD`: linking an existing legacy character to an account
  (account menu option 3) verifies the character's legacy password before migrating it.
- `interpre.cpp:4068` — `CON_PWDNQO`: "change password" for a non-account character (old
  password check).
- `interpre.cpp:4118` — `CON_DELCNF1`: "delete character" confirmation for a non-account
  character.

Two verify a **same-session retype confirmation** (typed password vs. the password typed
moments earlier into `d->pwd`, not vs. anything on disk) — still live plaintext comparison
touch-points for a future migration, plus their paired `strncpy(d->pwd, CRYPT(...))` writes
are where the plaintext first enters the field that later gets persisted:

- `interpre.cpp:3648` — `CON_PWDCNF`: new-character password confirmation. Reachable for a
  character created *without* an active account session: the new-character branch at
  interpre.cpp:2913 sets `STATE(d) = CON_PWDGET` (the `else` of the `*d->account_name` check
  at 2904), `CON_PWDGET` stores the plaintext into `d->pwd` (strncpy at 3638) and
  transitions to `CON_PWDCNF` (3642).
- `interpre.cpp:4099` — `CON_PWDNCNF`: password-change confirmation. Reachable from
  `CON_PWDNQO` above: a correct old password sets `STATE(d) = CON_PWDNEW` (4076), and
  `CON_PWDNEW` stores the new plaintext into `d->pwd` (strncpy at 4090) and transitions to
  `CON_PWDNCNF` (4093).

The sixth grep hit, `interpre.cpp:2948` (`CON_PWDNRM`), is dead code — nothing in the current
state machine ever transitions a descriptor into `CON_PWDNRM`.

The still-active **text** player-save format (`db.cpp`, see `docs/data-formats/player-save.md`)
reads/writes this same plaintext `pwd` field for every character that saves through it; this is
expected given the finding above, not an additional bug.

### The account-native sentinel

Characters created under an active account session never get a real legacy password at all.
`interpre.cpp:2293` defines a marker:

```cpp
const char* kAccountOnlyPasswordMarker = "*ACCOUNT*";
```

`set_account_only_character_password()` (interpre.cpp:2345-2348) stamps a new account-native
character's `pwd` field with this marker instead of a real password, called once from the
new-character-creation path (interpre.cpp:2905). It is **write-only**: nothing anywhere reads or
compares against `kAccountOnlyPasswordMarker` — it is a defensive placeholder documenting intent
("this field is meaningless for this character"), not an enforced guard. Account-native characters
never reach the five live legacy-`pwd` comparison sites above in the first place, because
`*d->account_name` is non-empty for them and routes into the account-password branches instead;
the sentinel is redundant-but-harmless belt-and-braces, not the actual enforcement mechanism.

### Why this is parked rather than fixed here

Changing the legacy character password format (e.g. routing it through `rots_crypt`) would rewrite
a live, still-read on-disk field (`char_file_u.pwd` / the text save format's password line) for a
population of characters that predate the account system and may never migrate. That is a
save-format migration in the same family as the binary-format conversions tracked elsewhere in
this repo (`docs/data-formats/object-rent-files.md`, the `crime_json`/`exploits_json` converters)
and needs the same treatment: an explicit plan, a converter, and — per the standing project
constraint — the legacy path kept intact until live server data is confirmed migrated. Phase 5
(hardening: `-Wall -Wextra -Werror`/`/W4 /WX` compile-warning cleanup) came and went without
touching this — it remains parked pending a dedicated future save-format-migration effort, not a
quick fix folded into another task. Recorded here so the finding has a durable, discoverable
disposition instead of living only in a phase-progress ledger entry.

## `script.cpp`'s `SCRIPT_DO_SAY`-family sites pass a builder-authored string directly as a `sprintf` format

**Source files:** `src/script.cpp`, `src/safe_template.h`, `src/safe_template.cpp`

**Status:** resolved (Backlog Cleanup wave, Task 1 — `modernization/backlog-cleanup` branch;
see the `fix: harden script DO_SAY-family templates via safe_template (backlog T1)` commit,
preceded by the `test: pin script DO_SAY-family template expansion (backlog T1)` pin commit).

`SCRIPT_DO_SAY` and four sibling cases (`SCRIPT_DO_YELL`, `SCRIPT_SEND_TO_CHAR`,
`SCRIPT_SEND_TO_ROOM`, `SCRIPT_SEND_TO_ROOM_X`) called `sprintf(output, curr->text, txt1)` where
`curr->text` is a mudlle-script string authored by a builder (`.mdl`/`.scr` world data), used
directly as the `printf`-style format template rather than as a literal. This was the same class of
risk as the `death_cry2`/`shop.cpp` `message_*` fields documented in `docs/BUILD.md`'s "Formatting"
section — a malformed template (wrong conversion count/type, a stray `%n`) is undefined behavior at
the `sprintf` call — but unlike those fields, these five `script.cpp` sites were never routed
through `safe_template::expand_checked()`. They were only a documented, pragma-wrapped **justified
skip** from the Phase 4 `std::format` conversion sweep (`std::format`'s format-string argument is
compile-time-checked and has no runtime-template equivalent for this `printf`-style pattern), not a
`safe_template` hardening pass — the skip addressed the modernization goal (convert to
`std::format`) without addressing the security goal (validate a builder-supplied template before
using it as one). Well-formed world data expanded fine; a malformed or malicious `.mdl`/`.scr`
template at one of these five sites was not defended against.

**Fix:** all five sites now route through a new `safe_template::expand_checked_one(tmpl, arg,
fallback, context)` convenience entry point (delegates to the existing `expand_checked()`
scan/validate/substitute logic for the "exactly one bare `%s` consuming one argument" shape). A
malformed template falls back to `curr->text` displayed literally (never re-interpreted as a format
string) and logs once via `mudlog`, matching `expand_checked()`'s existing fallback mechanic. The
new entry point also closes a second, previously-latent gap the `death_cry2`/`shop.cpp` call sites
never had: `txt1` (`get_text_param()`'s return) can legitimately be a null `char*` (an unset
`SCRIPT_PARAM_STRn`, or a `SCRIPT_PARAM_CHn_NAME` whose target character is gone) — the old
`sprintf(output, curr->text, txt1)` read that null pointer as the `%s` argument (undefined
behavior); `expand_checked_one()` coalesces a null `arg` to `""` before it ever reaches
`std::string_view`, so the template stays well-formed and substitutes an empty string instead of
hitting UB. The five `-Wdeprecated-declarations` justified-skip pragmas are removed; well-formed
world data expands byte-identical to the old `sprintf` output (characterization-pinned), and
`src/tests/safe_template_tests.cpp` carries both the pass-first well-formed pins and the fail-first
malformed/null-arg pins for this family.
