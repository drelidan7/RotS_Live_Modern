#!/usr/bin/env python3
"""PreToolUse guard for the 32-bit-only legacy binary goldens.

src/tests/goldens/legacy_{rent,board,mail,pkill,crime,exploits}_fixture.bin encode the
historical 32-bit compiler struct layout. Regenerating them on any 64-bit build (macOS
native, linux-x64, the rots64 container) silently bakes in the wrong layout and defeats
the fixtures' purpose (AGENTS.md, Testing Guidelines). This hook makes that rule
structural instead of documentation:

  * Edit/Write/NotebookEdit touching a path matching legacy_*_fixture.bin is blocked
    outright -- those files are only ever produced by the test binary inside the
    32-bit `rots` container.
  * Bash commands that NAME a legacy fixture path are blocked unless the command is a
    recognized read-only inspection (grep/cat/cmp/diff/git diff/...) or carries the
    HOST_GOLDENS_OK=1 override -- closing the cp/mv/dd/redirect side door around the
    Edit/Write block.
  * Bash commands containing UPDATE_GOLDENS are blocked unless the regeneration runs
    inside the i386 container (`docker compose run ... rots ...` -- NOT rots64 -- or
    scripts/rots-docker.sh) with UPDATE_GOLDENS appearing INSIDE that invocation (after
    the container spelling, so merely echoing the spelling earlier does not unlock), or
    the command carries an explicit HOST_GOLDENS_OK=1 token. The override keeps the
    legitimate host path open (regenerating *non-legacy* characterization goldens on
    64-bit hosts is sanctioned) while making it deliberate.

Threat model: this is a guard against ACCIDENTAL footguns by an agent or human, not a
security boundary. It string-matches the command text; it does not parse shell, so a
deliberately obfuscated command (variables, eval, an override token smuggled inside an
echo) can bypass it. That residual is accepted -- a determined actor already has
unrestricted Bash. Fail-open on malformed hook input is likewise deliberate: wedging
every tool call on a hook bug is worse than losing the guard for one call.

Exit 0 allows the tool call; exit 2 blocks it and feeds stderr back to Claude.
"""

import json
import re
import sys

# Any spelling of the frozen 32-bit golden fixtures, with or without a directory prefix.
LEGACY_FIXTURE = re.compile(r"legacy_[a-z]+_fixture\.bin")

# The i386 `rots` container invocations (word boundary keeps `rots` from matching
# `rots64`, which is 64-bit and does NOT qualify). The compose form carries its real
# command inside the trailing quoted string, so UPDATE_GOLDENS must appear after it;
# the rots-docker.sh wrapper form takes UPDATE_GOLDENS as a leading env assignment,
# so position is not checked for it.
I386_COMPOSE = re.compile(r"docker\s+compose\s+run\b(?!.*\brots64\b).*\brots\b")
I386_WRAPPER = re.compile(r"rots-docker\.sh")

# Explicit acknowledgment token, as a standalone word (not a substring of a longer name).
OVERRIDE = re.compile(r"(?:^|[\s;&|(])HOST_GOLDENS_OK=1(?:$|[\s;&|)])")

# First command word of a shell segment we accept as read-only inspection of a fixture.
READ_ONLY_LEADERS = (
    "grep", "rg", "cat", "cmp", "diff", "file", "ls", "stat", "wc", "head", "tail",
    "hexdump", "xxd", "od", "shasum", "md5", "md5sum", "sha256sum", "du", "find", "git",
)


def command_is_read_only_inspection(command: str) -> bool:
    """Every ;/&&/|| -separated segment must start with a read-only leader.

    Coarse by design: pipes within a segment are fine because the fixture path can only
    be an argument to the leading command; anything with a redirect is not read-only.
    """
    if re.search(r"[<>]", command):
        return False
    for segment in re.split(r"[;&|]+", command):
        segment = segment.strip()
        if not segment:
            continue
        leader = segment.split()[0]
        if leader not in READ_ONLY_LEADERS:
            return False
    return True


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0  # malformed input: never wedge the session on a hook bug

    tool_name = payload.get("tool_name", "")
    tool_input = payload.get("tool_input") or {}

    if tool_name in ("Edit", "Write", "NotebookEdit"):
        file_path = str(tool_input.get("file_path") or tool_input.get("notebook_path") or "")
        if LEGACY_FIXTURE.search(file_path):
            print(
                "BLOCKED: {} is a 32-bit-only legacy golden (AGENTS.md). It must never be "
                "edited or regenerated outside the i386 `rots` container; it is only ever "
                "produced by the test binary in that container via UPDATE_GOLDENS=1.".format(file_path),
                file=sys.stderr,
            )
            return 2
        return 0

    if tool_name == "Bash":
        command = str(tool_input.get("command") or "")
        override = bool(OVERRIDE.search(command))

        # Gate 1: any command that names a legacy fixture path and is not a recognized
        # read-only inspection (closes the cp/mv/dd/redirect side door).
        if LEGACY_FIXTURE.search(command) and not override:
            if not command_is_read_only_inspection(command):
                print(
                    "BLOCKED: this command names a 32-bit-only legacy_*_fixture.bin golden "
                    "and is not a recognized read-only inspection (grep/cat/cmp/diff/git/...). "
                    "Those fixtures must never be written outside the i386 `rots` container "
                    "(AGENTS.md). If this really is a safe operation, re-run with "
                    "HOST_GOLDENS_OK=1 prepended to acknowledge it.",
                    file=sys.stderr,
                )
                return 2

        # Gate 2: UPDATE_GOLDENS regeneration must run inside the i386 container --
        # and the UPDATE_GOLDENS spelling must appear AFTER the container invocation,
        # i.e. inside its quoted command, not merely alongside it.
        if "UPDATE_GOLDENS" in command and not override:
            compose = I386_COMPOSE.search(command)
            inside_container = (
                bool(I386_WRAPPER.search(command))
                or (bool(compose) and command.find("UPDATE_GOLDENS") > compose.start())
            )
            if not inside_container:
                print(
                    "BLOCKED: UPDATE_GOLDENS on a 64-bit host can silently rewrite the "
                    "32-bit-only legacy_*_fixture.bin goldens with the wrong struct layout "
                    "(AGENTS.md). Either run the regeneration inside the i386 container "
                    "(docker compose run --rm --pull never rots ...), or -- if you are "
                    "deliberately regenerating only non-legacy characterization goldens on "
                    "this host -- re-run with HOST_GOLDENS_OK=1 prepended to acknowledge that.",
                    file=sys.stderr,
                )
                return 2
        return 0

    return 0


if __name__ == "__main__":
    sys.exit(main())
