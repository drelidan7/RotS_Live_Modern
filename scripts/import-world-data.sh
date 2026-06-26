#!/usr/bin/env bash
#
# import-world-data.sh — Populate this repo's lib/ with world + player data
# extracted from an old RotS server backup.
#
# Why this exists:
#   World files (rooms/mobs/objects/zones/shops/scripts) and player data are
#   intentionally NOT committed to this repo (see .gitignore). They live in a
#   separate backup. This script copies them into ./lib so the MUD can boot
#   with the old content.
#
# What it imports:
#   - World  <- <backup>/lib/world-for-6001.tbz  (a clean server snapshot whose
#               world/scr/*.scr filenames match world/scr/index; the loose
#               <backup>/lib/world dir has macOS-munged "*.scr.txt" files that
#               would make index_boot() fail, so we prefer the tarball).
#   - Players / plrobjs / exploits <- the loose <backup>/lib/{players,plrobjs,
#               exploits}/{A-E,F-J,K-O,P-T,U-Z} buckets. These are the only
#               buckets build_player_index() / Crash_get_filename() ever read.
#               The ZZZ "graveyard" bucket (deleted chars) is skipped unless
#               --with-graveyard is given; the game never indexes it at boot.
#
# Usage:
#   scripts/import-world-data.sh [options]
#
# Options:
#   --backup DIR       Path to the old backup root (default: ~/Documents/rots)
#   --world-only       Import world data only (skip players/plrobjs/exploits)
#   --players-only     Import players/plrobjs/exploits only (skip world)
#   --with-graveyard   Also import the players/ZZZ graveyard (large; PII; never
#                      loaded by the game). Off by default.
#   -h, --help         Show this help.
#
# Safe to re-run: existing files are overwritten in place; nothing is deleted.

set -euo pipefail

# --- Locate the repo (this script lives in <repo>/scripts) -------------------
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LIB="${REPO}/lib"

# --- Defaults / arg parsing --------------------------------------------------
BACKUP="${HOME}/Documents/rots"
DO_WORLD=1
DO_PLAYERS=1
WITH_GRAVEYARD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --backup)        BACKUP="$2"; shift 2 ;;
        --backup=*)      BACKUP="${1#*=}"; shift ;;
        --world-only)    DO_PLAYERS=0; shift ;;
        --players-only)  DO_WORLD=0; shift ;;
        --with-graveyard) WITH_GRAVEYARD=1; shift ;;
        -h|--help)
            sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *)
            echo "Unknown option: $1" >&2
            echo "Run with --help for usage." >&2
            exit 1 ;;
    esac
done

BACKUP_LIB="${BACKUP}/lib"

if [[ ! -d "${BACKUP_LIB}" ]]; then
    echo "ERROR: backup lib not found at '${BACKUP_LIB}'." >&2
    echo "       Pass --backup /path/to/rots (the dir that contains 'lib/')." >&2
    exit 1
fi

echo "Repo:   ${REPO}"
echo "Backup: ${BACKUP}"
echo

# --- Helpers -----------------------------------------------------------------

# Copy one player-data bucket dir, stripping CVS / dotfiles. Skips silently if
# the source bucket is absent.
copy_bucket() {
    local kind="$1" bucket="$2"
    local src="${BACKUP_LIB}/${kind}/${bucket}"
    local dst="${LIB}/${kind}/${bucket}"
    [[ -d "${src}" ]] || { echo "  (skip ${kind}/${bucket}: not in backup)"; return; }
    mkdir -p "${dst}"
    # -a preserve, exclude CVS metadata and macOS junk. Trailing slash on src
    # copies contents into dst.
    rsync -a \
        --exclude='CVS' \
        --exclude='.DS_Store' \
        --exclude='._*' \
        "${src}/" "${dst}/"
    echo "  ${kind}/${bucket}: $(find "${dst}" -maxdepth 1 -type f | wc -l | tr -d ' ') files"
}

# --- World import ------------------------------------------------------------
if [[ "${DO_WORLD}" -eq 1 ]]; then
    echo "== Importing world =="
    TARBALL="${BACKUP_LIB}/world-for-6001.tbz"
    if [[ ! -f "${TARBALL}" ]]; then
        # Allow any world*.tbz as a fallback.
        TARBALL="$(ls "${BACKUP_LIB}"/world*.tbz 2>/dev/null | head -1 || true)"
    fi
    if [[ -z "${TARBALL}" || ! -f "${TARBALL}" ]]; then
        echo "ERROR: no world tarball (world*.tbz) found under ${BACKUP_LIB}." >&2
        exit 1
    fi
    echo "  source: ${TARBALL}"
    mkdir -p "${LIB}"
    # The tarball expands to a top-level 'world/' dir, so extract into lib/.
    tar -xj \
        --exclude='CVS' \
        --exclude='.DS_Store' \
        --exclude='._*' \
        -f "${TARBALL}" -C "${LIB}"

    # Sanity: every type the boot loader requires must have an index file.
    missing=0
    for d in wld mob obj zon shp mdl scr; do
        if [[ ! -f "${LIB}/world/${d}/index" ]]; then
            echo "  WARN: lib/world/${d}/index missing" >&2
            missing=1
        fi
    done
    echo "  rooms=$(wc -l < "${LIB}/world/wld/index" 2>/dev/null || echo 0) \
mobs=$(wc -l < "${LIB}/world/mob/index" 2>/dev/null || echo 0) \
objs=$(wc -l < "${LIB}/world/obj/index" 2>/dev/null || echo 0) \
zones=$(wc -l < "${LIB}/world/zon/index" 2>/dev/null || echo 0)"
    [[ "${missing}" -eq 0 ]] && echo "  world OK" || echo "  world imported WITH WARNINGS"
    echo
fi

# --- Player data import ------------------------------------------------------
if [[ "${DO_PLAYERS}" -eq 1 ]]; then
    echo "== Importing players / plrobjs / exploits =="
    BUCKETS=(A-E F-J K-O P-T U-Z)
    if [[ "${WITH_GRAVEYARD}" -eq 1 ]]; then
        BUCKETS+=(ZZZ)
        echo "  (including ZZZ graveyard — large, contains deleted accounts)"
    fi
    for kind in players plrobjs exploits; do
        for b in "${BUCKETS[@]}"; do
            copy_bucket "${kind}" "${b}"
        done
        # Ensure the empty ZZZ delete-target dir exists even if not imported.
        mkdir -p "${LIB}/${kind}/ZZZ"
    done
    echo
fi

echo "Done. World/player data is git-ignored by design; nothing here will be"
echo "committed. Build + boot with the 32-bit Docker flow (see docs/BUILD.md)."
