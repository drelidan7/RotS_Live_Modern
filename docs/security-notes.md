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

### Where this is actually live (three sites, not one)

An earlier scout pass estimated "1 live strncmp" against a legacy password; a closer audit for
this task found **three** live comparison sites in `interpre.cpp`, all gated by
`else if (*d->account_name)` (interpre.cpp:4025/4042) so that account-linked characters are routed
to the account-password branches instead — these three only ever fire for characters that are
*not* linked to an account:

- `interpre.cpp:3453` — `CON_ACCTLEGPWD`: linking an existing legacy character to an account
  (account menu option 3) verifies the character's legacy password before migrating it.
- `interpre.cpp:4068` — `CON_PWDNQO`: "change password" for a non-account character.
- `interpre.cpp:4118` — `CON_DELCNF1`: "delete character" confirmation for a non-account character.

A fourth site, `interpre.cpp:2948` (`CON_PWDNRM`), is dead code — nothing in the current state
machine ever transitions a descriptor into `CON_PWDNRM`.

The still-active **text** player-save format (`db.cpp`, see `docs/data-formats/player-save.md`)
reads/writes this same plaintext `pwd` field for every character that saves through it; this is
expected given the finding above, not an additional bug.

### The account-native sentinel

Characters created under an active account session never get a real legacy password at all.
`interpre.cpp:2293` defines a marker:

```cpp
constexpr char kAccountOnlyPasswordMarker[] = "*ACCOUNT*";
```

`set_account_only_character_password()` (interpre.cpp:2345-2348) stamps a new account-native
character's `pwd` field with this marker instead of a real password, called once from the
new-character-creation path (interpre.cpp:2905). It is **write-only**: nothing anywhere reads or
compares against `kAccountOnlyPasswordMarker` — it is a defensive placeholder documenting intent
("this field is meaningless for this character"), not an enforced guard. Account-native characters
never reach the three live legacy-`pwd` comparison sites above in the first place, because
`*d->account_name` is non-empty for them and routes into the account-password branches instead;
the sentinel is redundant-but-harmless belt-and-braces, not the actual enforcement mechanism.

### Why this is parked rather than fixed here

Changing the legacy character password format (e.g. routing it through `rots_crypt`) would rewrite
a live, still-read on-disk field (`char_file_u.pwd` / the text save format's password line) for a
population of characters that predate the account system and may never migrate. That is a
save-format migration in the same family as the binary-format conversions tracked elsewhere in
this repo (`docs/data-formats/object-rent-files.md`, the `crime_json`/`exploits_json` converters)
and needs the same treatment: an explicit plan, a converter, and — per the standing project
constraint — the legacy path kept intact until live server data is confirmed migrated. That is
Phase 5-shaped work, not a Phase 3 MSVC-bring-up fix. Recorded here so the finding has a durable,
discoverable disposition instead of living only in a phase-progress ledger entry.
