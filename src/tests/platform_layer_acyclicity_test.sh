#!/usr/bin/env bash
# Fails if the rots_platform foundation library imports any game-layer symbol —
# i.e. if a "clean leaf" TU gained an upward dependency on the game. Uses the
# archive's UNDEFINED (imported) symbol list; a pure leaf imports only libc /
# libstdc++ / build-provided symbols, never game entities like char_to_room.
set -euo pipefail
archive="${1:?usage: platform_layer_acyclicity_test.sh <path/to/librots_platform.a>}"

# nm -u lists undefined symbols; -C demangles C++ names so the denylist can be
# plain source identifiers. Fall back to piping through c++filt on toolchains
# whose nm lacks -C (some older/BSD nm); the branch is chosen by probing the
# capability at runtime, not by inspecting the OS.
if nm -uC "$archive" >/dev/null 2>&1; then
    undefined="$(nm -uC "$archive")"
else
    undefined="$(nm -u "$archive" | c++filt)"
fi

# Game-layer symbols the foundation must NEVER reference. Representative, not
# exhaustive: any real upward edge pulls in at least one of these hubs.
denylist='char_to_room|obj_to_char|char_from_room|descriptor_list|interpret_command|(^|[[:space:]])_*world([[:space:]]|$)|boot_db|game_loop'

if echo "$undefined" | grep -Eq "$denylist"; then
    echo "FAIL: rots_platform imports a game-layer symbol (upward edge):" >&2
    echo "$undefined" | grep -E "$denylist" >&2
    exit 1
fi
echo "PASS: rots_platform imports no game-layer symbols"
