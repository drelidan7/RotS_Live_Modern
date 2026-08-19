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
  * Bash commands containing UPDATE_GOLDENS are blocked unless they either run inside
    the i386 container (`docker compose run ... rots ...` -- NOT rots64 -- or
    scripts/rots-docker.sh) or carry an explicit HOST_GOLDENS_OK=1 token. The override
    keeps the legitimate host path open (regenerating *non-legacy* characterization
    goldens on 64-bit hosts is sanctioned) while making it deliberate.

Exit 0 allows the tool call; exit 2 blocks it and feeds stderr back to Claude.
"""

import json
import re
import sys


def main() -> int:
    try:
        payload = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        return 0  # malformed input: never wedge the session on a hook bug

    tool_name = payload.get("tool_name", "")
    tool_input = payload.get("tool_input") or {}

    if tool_name in ("Edit", "Write", "NotebookEdit"):
        file_path = str(tool_input.get("file_path") or tool_input.get("notebook_path") or "")
        if re.search(r"legacy_[a-z]+_fixture\.bin", file_path):
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
        if "UPDATE_GOLDENS" not in command:
            return 0
        if "HOST_GOLDENS_OK=1" in command:
            return 0
        # The i386 container is the ONLY sanctioned environment for legacy-fixture
        # regeneration. `rots64` is 64-bit and does not qualify -- hence the word
        # boundary that keeps `rots` from matching `rots64`.
        in_i386_container = bool(
            re.search(r"docker\s+compose\s+run\b(?!.*\brots64\b).*\brots\b", command)
            or "rots-docker.sh" in command
        )
        if in_i386_container:
            return 0
        print(
            "BLOCKED: UPDATE_GOLDENS on a 64-bit host can silently rewrite the 32-bit-only "
            "legacy_*_fixture.bin goldens with the wrong struct layout (AGENTS.md). Either run "
            "the regeneration inside the i386 container (docker compose run --rm --pull never "
            "rots ...), or -- if you are deliberately regenerating only non-legacy "
            "characterization goldens on this host -- re-run the command with HOST_GOLDENS_OK=1 "
            "prepended to acknowledge that.",
            file=sys.stderr,
        )
        return 2

    return 0


if __name__ == "__main__":
    sys.exit(main())
