#!/usr/bin/env python3

"""Combat characterization smoke harness (combat-pilot Task 1).

Boots `ageland` on a scratch port against a FRESH scratch `lib/` tree (no
existing players/accounts, so the first character created gets the
documented level-100 Implementor promotion -- see AGENTS.md "Runtime Data
and Persistence"), scripts account creation + character creation over a
direct (non-proxied, no `-x`) socket connection, then as that Implementor
loads a fixed low-level mob and kills it, capturing the server's combat
output between explicit BEGIN/END markers.

`--mode capture` writes the transcript to `<transcript-dir>/transcript.golden`
and a normalized "combat lines only" companion,
`transcript.normalized.golden`. `--mode verify` re-runs the same scenario and
diffs both against their committed goldens for a human to inspect, but
per the Step 7 determinism probe's finding, this harness LANDED ON FALLBACK
LADDER RUNG (B) -- "capture-only", named loudly, not silently accepted (see
the Task 1 report's determinism-ladder section for the full trial log):
repeated same-seed trials showed BOTH the raw and the normalized transcript
occasionally drifting, even after a weapon-equip mitigation shrank the real-
time fight window ~5x (~50s unarmed -> ~10s armed). Root cause: this legacy
engine drives combat off a real-time pulse loop through ONE shared global
`rots_rng` engine with no way to freeze or single-step it, so wall-clock
jitter can shift how many pulse-driven, RNG-consuming background events
(door mechanics were the one instance actually observed; not exhaustively
enumerated) interleave with the scripted fight and perturb the replayed
draw sequence downstream. That's a property of the current engine, not a
bug in this harness -- so `verify` does NOT fail on transcript drift by
itself; it always attempts both comparisons, prints a diff when they
differ, and exits 0 as long as the scenario itself completed (mob loaded,
fought, died). Per the brief, "the wave's bar rests on discriminators +
goldens" (the existing gtest-level `CharacterizationCombatTest.*` goldens
and similar, which run outside real time and don't share this problem),
not on this tool's diff.

Model: tools/account_smoke.py (expect-style socket client, marker-paced
waits, NEVER sleep-paced past a fixed poll loop) -- this tool reuses that
file's socket/telnet-sanitizing/process-polling patterns but is otherwise
self-contained (no import), since account_smoke.py isn't structured as a
library and this tool's scratch-lib provisioning needs differ (a FRESH
player table, not an isolated-by-random-name character in the real repo
lib/, since account_smoke.py's approach of reusing the real lib/ would defeat
the Implementor-promotion precondition this tool depends on).
"""

import argparse
import difflib
import os
import random
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

IAC = 255
LOOPBACK_HOST = "127.0.0.1"
MIN_SMOKE_PORT = 20000
MAX_GAME_PORT = 32767
VERIFICATION_CODE_PROMPT = "Verification code (or type RESEND/CANCEL):"
CHILD_ENV_ALLOWLIST = ("PATH", "HOME", "USER", "LOGNAME", "TMPDIR")

# Fixed (not randomized) identities: capture/verify must draw the exact same
# sequence of rots_rng values up to the "kill" command on every invocation,
# so nothing that could perturb that sequence (account email, character
# name, race/class choice) is allowed to vary run-to-run the way
# account_smoke.py's uuid-based names deliberately do for its own,
# isolation-not-determinism, purpose.
FIXED_ACCOUNT_EMAIL = "combat-smoke@example.com"
FIXED_ACCOUNT_PASSWORD = "CombatSmoke1"
FIXED_CHARACTER_NAME = "Smokey"

# vnum 11915 ("a sickly rat", lib/world/mob/119.mob) -- a genuinely
# low-level (level 5), non-special-procedure mob so the fight resolves in a
# bounded, short number of rounds regardless of the fixed Implementor's
# (level 100, per the fresh-lib promotion) huge OB advantage. Deliberately
# NOT tied to the immortal start room's zone -- real_mobile()/read_mobile()
# resolve any vnum present in ANY world/mob/*.mob file regardless of zone
# reset placement, and `load mob` spawns it directly into the caster's
# current room, so proximity to the spawn room is irrelevant.
DEFAULT_MOB_VNUM = 11915
DEFAULT_SEED = 424242

# "Large Campsite" (lib/world/wld/60.wld) -- an ordinary outdoor wilderness
# room (room_flags=8, no PEACEROOM bit), reachable via `goto` from the
# Implementor's PEACEROOM-flagged starting area. See the module-level note
# at its `goto` call site for why a peace room can't host the fight at all.
NON_PEACE_ROOM_VNUM = 6002

# 10021, "a serrated scimitar" (lib/world/obj/100.obj, ITEM_WEAPON). See the
# `wield` call site's comment for why this is load-bearing for determinism,
# not just faster combat.
WEAPON_OBJ_VNUM = 10021

MOB_LOAD_MARKER = "You create a sickly rat."
DEATH_MARKER_TEXT = "is dead!  R.I.P."
TRANSCRIPT_BEGIN_MARKER = "===COMBAT_SMOKE_BEGIN==="
TRANSCRIPT_END_MARKER = "===COMBAT_SMOKE_END==="

# A prompt ("S:Powered>", "HP:Healthy S:Powered, a sickly rat:Bruised>", ...)
# is sometimes flushed on the SAME line as the very next message with no
# intervening newline -- observed for the fight-opening "You attack a sickly
# rat!" line specifically (its message arrives in the same TCP read/game-loop
# tick as the freshly-recomputed prompt, before the usual blank-line-then-
# prompt-then-message pattern the rest of the fight settles into). A bare
# `^You ...` anchor silently drops such a line instead of matching it -- this
# is exactly the bug the Step 1 review caught (transcript.normalized.golden
# was missing the attack-initiation line entirely). Every prompt observed
# ends in a literal '>' with no '>' earlier in the line, so stripping an
# optional ".*>" prefix before the anchor handles this for any "You .../A
# sickly rat ..." pattern below, not just the one line where it was caught.
PROMPT_PREFIX = r"(?:.*>)?"

# Lines worth keeping under the Step 7 fallback ladder's rung (a) --
# "normalized comparison (combat message sequence -- hit/miss/damage/death
# lines only)". Matched against each already-sanitized line of the raw
# transcript. Deliberately anchored phrases (not bare substrings), mirroring
# scripts/boot-golden.sh's own normalize() comment about why anchored
# matching matters (an earlier boot-golden draft leaked thousands of
# unrelated debug lines via a bare substring match).
NORMALIZED_LINE_PATTERNS = [
    re.compile(PROMPT_PREFIX + pattern)
    for pattern in (
        r"You create a sickly rat\.$",
        r"You attack a sickly rat!$",
        # dam_weapons[]'s message tiers (fight.cpp) -- miss/scratch/barely/
        # lightly/plain/hard/very hard/extremely hard/deeply wound/severely
        # wound/MUTILATE -- combined with whichever verb pair
        # attack_hit_text[] (consts.cpp) selects for the attacker's current
        # weapon (hit/pound/pierce/slash/stab/whip/claw/bite/sting/cleave/
        # flail/smite/crush). Rather than spelling out all ~14 verb pairs x
        # ~8 adverb tiers, match the message SHAPE instead (attacker-
        # directed "You ... a sickly rat[.!]" / victim-directed "A sickly
        # rat ... you[.!]") -- still anchored to this scenario's fixed
        # mob/wording, just verb-agnostic so a future --mob-vnum or
        # different weapon choice doesn't silently go unmatched the way an
        # earlier hardcoded-verb draft did here (caught during the Step 7
        # determinism probe: it missed every "You slash a sickly rat."
        # line).
        r"You \S.* a sickly rat[.!]$",
        r"A sickly rat \S.* you[.!]$",
        r"You (deflect|dodge) a sickly rat's attack\.$",
        r"A sickly rat (deflects|dodges) your attack\.$",
        r"That really did HURT!$",
        r"A sickly rat is (stunned|incapacitated).*$",
        r"A sickly rat is dead!  R\.I\.P\.$",
        r"You receive \d+ experience points?\.$",
    )
]


class NonRetryableSmokeError(RuntimeError):
    pass


class TelnetStreamSanitizer:
    """Strips telnet IAC negotiation/subnegotiation sequences and normalizes
    CRLF -> LF so recv_until() can match plain-text markers. Verbatim
    behavior copy of tools/account_smoke.py's class of the same name."""

    def __init__(self) -> None:
        self._pending_iac = False
        self._pending_option = False
        self._in_subnegotiation = False
        self._subnegotiation_pending_iac = False
        self._pending_cr = False
        self._sanitized = bytearray()

    def feed(self, chunk: bytes) -> str:
        for byte in chunk:
            if self._pending_cr:
                if byte != 0:
                    self._sanitized.append(13)
                self._pending_cr = False
                if byte == 0:
                    continue

            if self._in_subnegotiation:
                if self._subnegotiation_pending_iac:
                    if byte == 240:
                        self._in_subnegotiation = False
                    self._subnegotiation_pending_iac = False
                    continue
                if byte == IAC:
                    self._subnegotiation_pending_iac = True
                continue

            if self._pending_option:
                self._pending_option = False
                continue

            if self._pending_iac:
                self._pending_iac = False
                if byte == IAC:
                    self._sanitized.append(IAC)
                    continue
                if byte in (251, 252, 253, 254):
                    self._pending_option = True
                    continue
                if byte == 250:
                    self._in_subnegotiation = True
                    self._subnegotiation_pending_iac = False
                    continue
                continue

            if byte == IAC:
                self._pending_iac = True
                continue

            if byte == 13:
                self._pending_cr = True
                continue

            self._sanitized.append(byte)

        return self.text

    @property
    def text(self) -> str:
        return self._sanitized.decode("latin1", errors="ignore")


def find_first_marker_end(text: str, markers: list[str]) -> int | None:
    matched_end = None
    matched_start = None
    for marker in markers:
        index = text.find(marker)
        if index < 0:
            continue
        marker_end = index + len(marker)
        if matched_start is None or index < matched_start or (index == matched_start and marker_end < matched_end):
            matched_start = index
            matched_end = marker_end
    return matched_end


class BufferedPromptReader:
    def __init__(self, sock: socket.socket) -> None:
        self._sock = sock
        self._sanitizer = TelnetStreamSanitizer()
        self._buffer = ""

    def recv_until(self, markers: list[str], timeout_seconds: float) -> str:
        deadline = time.time() + timeout_seconds
        raw_data = bytearray()
        self._sock.settimeout(0.5)

        while time.time() < deadline:
            marker_end = find_first_marker_end(self._buffer, markers)
            if marker_end is not None:
                text = self._buffer[:marker_end]
                self._buffer = self._buffer[marker_end:]
                return text

            try:
                chunk = self._sock.recv(4096)
            except socket.timeout:
                continue

            if not chunk:
                break

            raw_data.extend(chunk)
            previous_length = len(self._sanitizer.text)
            sanitized_text = self._sanitizer.feed(chunk)
            self._buffer += sanitized_text[previous_length:]
            marker_end = find_first_marker_end(self._buffer, markers)
            if marker_end is not None:
                text = self._buffer[:marker_end]
                self._buffer = self._buffer[marker_end:]
                return text

        marker_end = find_first_marker_end(self._buffer, markers)
        if marker_end is not None:
            text = self._buffer[:marker_end]
            self._buffer = self._buffer[marker_end:]
            return text

        text = self._buffer
        raw_tail = bytes(raw_data[-800:]).decode("latin1", errors="ignore")
        raise RuntimeError(
            "Timed out waiting for markers "
            + ", ".join(markers)
            + ". Last sanitized output was:\n"
            + text[-800:]
            + "\nRaw tail was:\n"
            + raw_tail
        )

    def drain(self, quiet_seconds: float = 0.4, max_total_seconds: float = 3.0) -> str:
        """Discards whatever is currently buffered plus anything that keeps
        arriving until `quiet_seconds` pass with no new bytes. Used right
        after entering the game, before the capture window opens, so an
        asynchronous room-entry banner (or, in principle, a stray tick
        message) can't leak into the combat transcript's start."""
        discarded = [self._buffer]
        self._buffer = ""
        deadline = time.time() + max_total_seconds
        last_activity = time.time()
        self._sock.settimeout(0.1)

        while time.time() < deadline:
            try:
                chunk = self._sock.recv(4096)
            except socket.timeout:
                if time.time() - last_activity >= quiet_seconds:
                    break
                continue

            if not chunk:
                break

            last_activity = time.time()
            discarded.append(self._sanitizer.feed(chunk))

        return "".join(discarded)


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendall(line.encode("utf-8") + b"\n")


def require_markers(text: str, markers: list[str], context: str) -> str:
    missing_markers = [marker for marker in markers if marker not in text]
    if missing_markers:
        raise RuntimeError(
            f"{context} was missing expected markers: {', '.join(missing_markers)}. Full output was:\n{text[-800:]}"
        )
    return text


def wait_for_account_menu(reader: BufferedPromptReader, timeout_seconds: float) -> str:
    text = reader.recv_until(["Choice:"], timeout_seconds)
    return require_markers(text, ["0) Log out", "Choice:"], "Account menu")


def wait_for_character_menu(reader: BufferedPromptReader, timeout_seconds: float) -> str:
    text = reader.recv_until(["Make your choice:"], timeout_seconds)
    return require_markers(text, ["Make your choice:"], "Character menu")


def read_file_tail(path: Path, max_bytes: int = 4000) -> str:
    try:
        with path.open("rb") as file:
            file.seek(0, os.SEEK_END)
            size = file.tell()
            file.seek(max(0, size - max_bytes))
            return file.read().decode("utf-8", errors="replace")
    except OSError as error:
        return f"<unable to read {path}: {error}>"


def wait_for_process_port(
    process: subprocess.Popen,
    host: str,
    port: int,
    timeout_seconds: float,
    log_path: Path,
) -> None:
    deadline = time.time() + timeout_seconds
    last_error: OSError | None = None
    while time.time() < deadline:
        exit_code = process.poll()
        if exit_code is not None:
            raise NonRetryableSmokeError(
                f"game server exited with status {exit_code} before accepting {host}:{port}.\n"
                f"Last game log output:\n{read_file_tail(log_path)}"
            )
        try:
            with socket.create_connection((host, port), timeout=0.5):
                return
        except OSError as error:
            last_error = error
            time.sleep(0.1)

    exit_code = process.poll()
    if exit_code is not None:
        raise NonRetryableSmokeError(
            f"game server exited with status {exit_code} before accepting {host}:{port}.\n"
            f"Last game log output:\n{read_file_tail(log_path)}"
        )
    detail = f" Last connection error: {last_error}." if last_error is not None else ""
    raise NonRetryableSmokeError(
        f"Timed out waiting for game server to accept connections on {host}:{port}.{detail}\n"
        f"Last game log output:\n{read_file_tail(log_path)}"
    )


def wait_for_verification_code(capture_path: Path, timeout_seconds: float) -> str:
    deadline = time.time() + timeout_seconds
    pattern = re.compile(r"Verification code:\s*(\d{6})")
    while time.time() < deadline:
        if capture_path.exists():
            contents = capture_path.read_text(encoding="utf-8", errors="ignore")
            match = pattern.search(contents)
            if match:
                return match.group(1)
        time.sleep(0.1)
    raise RuntimeError(f"Timed out waiting for a verification code in {capture_path}.")


def allocate_free_tcp_port(host: str = LOOPBACK_HOST, minimum: int = MIN_SMOKE_PORT, maximum: int = MAX_GAME_PORT) -> int:
    candidate_ports = list(range(minimum, maximum + 1))
    random.shuffle(candidate_ports)
    for port in candidate_ports:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            try:
                sock.bind((host, port))
            except OSError:
                continue
            return int(sock.getsockname()[1])
    raise RuntimeError(f"Failed to find a free TCP port in {minimum}-{maximum}.")


def smoke_child_env(extra_values: dict) -> dict:
    environment = {key: value for key in CHILD_ENV_ALLOWLIST if (value := os.environ.get(key)) is not None}
    environment.update(extra_values)
    return environment


def terminate_process(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


BUCKET_DIRECTORIES = ("A-E", "F-J", "K-O", "P-T", "U-Z", "ZZZ")
# Mirrors src/Makefile's `setup` target's mkdir list for the buckets that
# MUST be genuinely empty (build_player_index() -- db_players.cpp -- scans
# players/<bucket>/ and account_characters/<bucket>/ directly; an empty tree
# there is what leaves top_of_p_table at -1, the documented precondition for
# the first character created to become the level-100 Implementor).
FRESH_BUCKETED_DIRECTORIES = ("players", "accounts", "account_characters", "plrobjs", "exploits")


def provision_scratch_lib(scratch_root: Path, repo_root: Path) -> Path:
    """Builds a FRESH data directory (no existing players/accounts) so the
    first character created is promoted to Implementor, while sharing the
    (large, read-only-for-this-purpose) world/text reference data with the
    real checkout via symlink rather than copying it. Returns the lib/ path
    to pass as `-d`."""
    lib_dir = scratch_root / "lib"
    for bucket_root in FRESH_BUCKETED_DIRECTORIES:
        for bucket in BUCKET_DIRECTORIES:
            (lib_dir / bucket_root / bucket).mkdir(parents=True, exist_ok=True)

    (lib_dir / "misc").mkdir(parents=True, exist_ok=True)
    (lib_dir / "boards").mkdir(parents=True, exist_ok=True)
    (lib_dir / "last_cmds").touch()
    (lib_dir / "misc" / "pklist").touch()

    # misc/messages (MESS_FILE) and misc/socials (SOCMESS_FILE) are read via
    # load_messages()/boot_social_messages() (fight.cpp / act_soci.cpp), and
    # BOTH exit(0) on a missing file -- unlike every other lib/misc/* file
    # boot_db() touches (badsites/xnames/wizlist/etc, all read through
    # tolerant file_to_string_alloc()/fopen-then-return call sites). messages
    # additionally supplies the literal damage-message text this harness's
    # combat transcript captures, so sharing the real checkout's copy (static
    # reference data, never rewritten at runtime by anything this scenario
    # exercises) is required for boot to succeed AND for the transcript's
    # wording to match the real server's.
    for required_misc_file in ("messages", "socials"):
        real_misc_file = (repo_root / "lib" / "misc" / required_misc_file).resolve()
        if not real_misc_file.is_file():
            raise NonRetryableSmokeError(f"{real_misc_file} is missing -- boot_db() requires it (see comment above).")
        (lib_dir / "misc" / required_misc_file).symlink_to(real_misc_file)

    log_dir = scratch_root / "log"
    log_dir.mkdir(parents=True, exist_ok=True)
    (log_dir / "lastdeath").touch()

    real_world = (repo_root / "lib" / "world").resolve()
    if not real_world.is_dir():
        raise NonRetryableSmokeError(
            f"{real_world} is missing -- combat_smoke.py needs lib/world/ checked out "
            "(see AGENTS.md 'Runtime Data and Persistence': it comes from the separate "
            "RotS-WorldFiles depot, not this checkout)."
        )
    (lib_dir / "world").symlink_to(real_world)

    real_text = (repo_root / "lib" / "text").resolve()
    if real_text.is_dir():
        (lib_dir / "text").symlink_to(real_text)

    return lib_dir


def run_combat_scenario(binary: Path, seed: int, port: int, mob_vnum: int, startup_timeout: float, keep_artifacts: bool) -> str:
    """Boots a fresh scratch server and scripts the account -> character ->
    combat sequence, returning the raw sanitized transcript captured between
    (and including) the load-mob confirmation and the death message, wrapped
    in TRANSCRIPT_BEGIN_MARKER/TRANSCRIPT_END_MARKER.

    Login/character-creation sequence mirrors tools/account_smoke.py's proven
    menu flow, but with FIXED identities (see FIXED_ACCOUNT_EMAIL et al.'s
    module comment) instead of per-run random ones, and without a proxy in
    front (no -x; the game's login/account menu flow is unaffected by
    whether a proxy prepends the 4-byte client-IP header)."""
    repo_root = Path(__file__).resolve().parent.parent
    scratch_root = Path(tempfile.mkdtemp(prefix="rots-combat-smoke-"))
    game_log_path = scratch_root / "game.log"
    capture_path = scratch_root / "verification-email.txt"
    capture_script_path = scratch_root / "capture-sendmail.sh"
    capture_script_path.write_text("#!/bin/sh\ncat > '" + str(capture_path) + "'\nexit 0\n", encoding="utf-8")
    capture_script_path.chmod(0o700)

    lib_dir = provision_scratch_lib(scratch_root, repo_root)

    game_env = smoke_child_env(
        {
            "ROTS_SENDMAIL_COMMAND": str(capture_script_path),
            "ROTS_RNG_SEED": str(seed),
        }
    )

    game_process = None
    try:
        with game_log_path.open("wb") as game_log:
            game_process = subprocess.Popen(
                [str(binary), "-d", str(lib_dir), "-p", str(port)],
                cwd=scratch_root,
                env=game_env,
                stdout=game_log,
                stderr=subprocess.STDOUT,
            )
        wait_for_process_port(game_process, LOOPBACK_HOST, port, startup_timeout, game_log_path)

        # First connection: create the account, verify the email, and run
        # the character-creation wizard through to the character menu (then
        # disconnect without entering the world -- matches
        # tools/account_smoke.py's proven two-connection shape).
        with socket.create_connection((LOOPBACK_HOST, port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, FIXED_ACCOUNT_EMAIL)

            reader.recv_until(["Create one? (Y/N):"], 8.0)
            send_line(sock, "y")

            reader.recv_until(["Please enter a password:"], 8.0)
            send_line(sock, FIXED_ACCOUNT_PASSWORD)

            reader.recv_until(["Please retype your password:"], 8.0)
            send_line(sock, FIXED_ACCOUNT_PASSWORD)

            reader.recv_until([VERIFICATION_CODE_PROMPT], 8.0)
            verification_code = wait_for_verification_code(capture_path, 15.0)
            send_line(sock, verification_code)
            wait_for_account_menu(reader, 8.0)

            send_line(sock, "4")
            reader.recv_until(["New character name:"], 8.0)
            send_line(sock, FIXED_CHARACTER_NAME)

            reader.recv_until(["suitable name for roleplay in Middle-earth"], 8.0)
            send_line(sock, "y")

            reader.recv_until(["What is your sex (M/F)?"], 8.0)
            send_line(sock, "m")

            reader.recv_until(["Race:"], 8.0)
            send_line(sock, "h")

            reader.recv_until(["Class:"], 8.0)
            send_line(sock, "a")

            reader.recv_until(["Do you wish to enable the default colour set (Y/N)?"], 8.0)
            send_line(sock, "n")

            reader.recv_until(["Do you see an 'a' with a pair of dots above it:"], 8.0)
            send_line(sock, "n")

            reader.recv_until(["Make your choice:"], 8.0)
            send_line(sock, "0")

        # Second connection: log in and actually enter the game as the
        # (now Implementor-promoted) character.
        with socket.create_connection((LOOPBACK_HOST, port), timeout=5) as sock:
            reader = BufferedPromptReader(sock)
            reader.recv_until(["Account email:"], 8.0)
            send_line(sock, FIXED_ACCOUNT_EMAIL)

            reader.recv_until(["Account password:"], 8.0)
            send_line(sock, FIXED_ACCOUNT_PASSWORD)

            wait_for_account_menu(reader, 8.0)
            send_line(sock, "2")

            reader.recv_until(["Character number:"], 8.0)
            send_line(sock, "1")

            wait_for_character_menu(reader, 8.0)
            send_line(sock, "1")
            reader.recv_until(["Here we go...", "This is your new MUD character."], 8.0)

            # Every per-race mortal start room (and the OOC Creation-Hall
            # complex the Implementor's own room_flags path runs through) is
            # PEACEROOM-flagged -- do_hit refuses to even attempt combat
            # there ("A peaceful feeling overwhelms you..."). `goto` (a
            # LEVEL_IMMORT command, satisfied by the fresh-lib Implementor
            # promotion) teleports out to a plain wilderness room instead.
            # Room 6002 ("Large Campsite", lib/world/wld/60.wld) is a normal
            # outdoor crossroads with room_flags=8 (no PEACEROOM bit).
            send_line(sock, f"goto {NON_PEACE_ROOM_VNUM}")

            # Drain the asynchronous room-entry banner (goto's own room
            # description, any prompt) before opening the capture window, so
            # it can't leak into the transcript.
            reader.drain(quiet_seconds=0.5, max_total_seconds=3.0)

            # Equip a weapon (vnum WEAPON_OBJ_VNUM, "a serrated scimitar")
            # before fighting bare-handed. This is a determinism fix, not
            # flavor: the Step 7 probe found that an UNARMED Implementor's
            # fist damage is so low against DEFAULT_MOB_VNUM that the fight
            # ran ~40 rounds / ~50 real seconds, long enough for the game's
            # OTHER real-time pulse-driven systems (observed cause: a nearby
            # room's door auto-close event) to interleave and consume
            # additional draws from the shared global rots_rng engine mid-
            # fight -- byte-identical seeds then produced diverging combat
            # rolls past that point (confirmed both in the raw transcript
            # AND the normalized combat-lines-only view). Armed, the same
            # fight resolves in ~11 rounds / ~10 real seconds; three
            # consecutive same-seed captures at that length were
            # byte-identical (see the Task 1 report's determinism-ladder
            # section) -- i.e. shrinking the real-time exposure window, not
            # silencing a specific room's noise source, is what fixed it.
            send_line(sock, f"load obj {WEAPON_OBJ_VNUM}")
            reader.drain(quiet_seconds=0.5, max_total_seconds=3.0)
            send_line(sock, "wield scimitar")
            reader.drain(quiet_seconds=0.5, max_total_seconds=3.0)

            # MOB_LOAD_MARKER/"kill rat" below are hardcoded to DEFAULT_MOB_VNUM's
            # short description/keyword ("a sickly rat"); --mob-vnum is exposed for
            # ad hoc use but only the default vnum's markers are guaranteed to match.
            transcript_parts = []
            send_line(sock, f"load mob {mob_vnum}")
            transcript_parts.append(reader.recv_until([MOB_LOAD_MARKER], 8.0))

            send_line(sock, "kill rat")
            transcript_parts.append(reader.recv_until([DEATH_MARKER_TEXT], 90.0))

            send_line(sock, "quit")

        raw_transcript = "".join(transcript_parts)
        return f"{TRANSCRIPT_BEGIN_MARKER}\n{raw_transcript}\n{TRANSCRIPT_END_MARKER}\n"
    finally:
        terminate_process(game_process)
        if not keep_artifacts:
            shutil.rmtree(scratch_root, ignore_errors=True)
        else:
            print(f"Artifacts kept at {scratch_root}", file=sys.stderr)


def normalize_transcript(raw_transcript: str) -> str:
    """Step 7 fallback ladder rung (a): keep only the combat message
    sequence (hit/miss/damage/death lines), for use if the raw byte-compare
    proves flaky across otherwise-identical runs."""
    kept_lines = []
    for line in raw_transcript.splitlines():
        stripped = line.strip()
        if stripped in (TRANSCRIPT_BEGIN_MARKER, TRANSCRIPT_END_MARKER):
            kept_lines.append(stripped)
            continue
        if any(pattern.match(stripped) for pattern in NORMALIZED_LINE_PATTERNS):
            kept_lines.append(stripped)
    return "\n".join(kept_lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("capture", "verify"), required=True)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--port", type=int, default=0, help="Game TCP port; 0 picks a free port.")
    parser.add_argument("--binary", default=None, help="Path to the ageland binary (default: <repo-root>/bin/ageland).")
    parser.add_argument("--mob-vnum", type=int, default=DEFAULT_MOB_VNUM)
    parser.add_argument(
        "--transcript-dir",
        default=None,
        help="Directory holding transcript.golden / transcript.normalized.golden (default: docs/superpowers/goldens/combat-smoke).",
    )
    parser.add_argument("--startup-timeout", type=float, default=30.0)
    parser.add_argument("--keep-artifacts", action="store_true")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    # Resolved to absolute: run_combat_scenario() launches the binary with
    # cwd=<scratch_root> (the game's own -d/data-directory chdir happens
    # AFTER that, inside the process), so a relative --binary would resolve
    # against the wrong directory once Popen's cwd takes effect.
    binary = (Path(args.binary) if args.binary else repo_root / "bin" / "ageland").resolve()
    if not binary.exists():
        print(f"ERROR: missing game binary at {binary}. Build the server before running the smoke test.", file=sys.stderr)
        return 2

    transcript_dir = Path(args.transcript_dir) if args.transcript_dir else repo_root / "docs" / "superpowers" / "goldens" / "combat-smoke"
    port = args.port or allocate_free_tcp_port()

    raw_golden_path = transcript_dir / "transcript.golden"
    normalized_golden_path = transcript_dir / "transcript.normalized.golden"

    try:
        raw_transcript = run_combat_scenario(binary, args.seed, port, args.mob_vnum, args.startup_timeout, args.keep_artifacts)
    except Exception as error:
        print(f"Combat smoke scenario failed: {error}", file=sys.stderr)
        return 2

    normalized_transcript = normalize_transcript(raw_transcript)

    if args.mode == "capture":
        transcript_dir.mkdir(parents=True, exist_ok=True)
        raw_golden_path.write_text(raw_transcript, encoding="utf-8")
        normalized_golden_path.write_text(normalized_transcript, encoding="utf-8")
        print(f"captured {raw_golden_path} ({len(raw_transcript.splitlines())} lines)")
        print(f"captured {normalized_golden_path} ({len(normalized_transcript.splitlines())} lines)")
        return 0

    # verify -- Step 7 determinism probe's LANDING (fallback ladder rung
    # (b), "capture-only", named loudly per the Task 1 brief -- see the Task
    # 1 report's determinism-ladder section for the full trial log): a
    # weapon-equip mitigation (see the `wield` call site's comment) shrank
    # the fight from ~40 rounds/~50s to ~10 rounds/~10s, which raised the
    # raw-byte-compare match rate but did NOT make it reliable -- repeated
    # same-seed trials showed BOTH the raw AND the normalized (combat-lines-
    # only) transcript drifting on some runs and not others, with no
    # deeper room-specific or verb-specific cause found (the earlier
    # rung-(a) door-auto-close finding turned out to be one instance of a
    # more general problem, not the whole story): this legacy engine drives
    # combat off a real-time pulse loop through ONE shared global rots_rng
    # engine with no way to single-step or freeze it, so wall-clock jitter
    # (scheduling, socket round-trips) can shift how many pulse-driven,
    # RNG-consuming background events (observed: at least door mechanics;
    # not fully enumerated) interleave with the scripted fight, which then
    # perturbs the replayed draw sequence downstream. That is a property of
    # the current engine, not a bug in this harness or evidence of a code
    # regression -- so verify does NOT fail the run on transcript drift by
    # itself. It always attempts both comparisons and prints a diff when
    # they differ, so a human (or a future task, once true determinism
    # becomes available -- e.g. a frozen/single-stepped pulse clock) can
    # judge; per the brief, "the wave's bar rests on discriminators +
    # goldens" (the existing gtest-level CharacterizationCombatTest.*
    # goldens and similar, which run outside real time and don't have this
    # problem), not on this tool's diff.
    if not normalized_golden_path.exists():
        print(f"ERROR: no golden at {normalized_golden_path}; run --mode capture first.", file=sys.stderr)
        return 2

    raw_matches = raw_golden_path.exists() and raw_golden_path.read_text(encoding="utf-8") == raw_transcript
    print(f"raw transcript: {'matches' if raw_matches else 'DRIFTED FROM'} golden (informational; see Step 7 fallback ladder rung (b) -- not gating)")

    golden_normalized = normalized_golden_path.read_text(encoding="utf-8")
    normalized_matches = golden_normalized == normalized_transcript
    print(f"normalized transcript: {'matches' if normalized_matches else 'DRIFTED FROM'} golden (informational; not gating -- see rung (b))")

    if not normalized_matches:
        print(f"--- {normalized_golden_path}", file=sys.stderr)
        print("+++ (live run)", file=sys.stderr)
        for line in difflib.unified_diff(
            golden_normalized.splitlines(),
            normalized_transcript.splitlines(),
            lineterm="",
        ):
            print(line, file=sys.stderr)

    print("combat scenario completed (mob loaded, fought, and killed) -- verify exits 0 regardless of transcript drift; see comment above.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
