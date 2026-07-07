# Building & running RotS locally (32-bit Linux via Docker)

RotS compiles **32-bit** (`-m32` in `src/Makefile`). That cannot build or run natively
on Apple Silicon / modern macOS (Apple removed 32-bit support in 2019). We build and run
it **unchanged** inside a 32-bit Linux (i386) container. On arm64 Macs, Docker runs the
i386 image via QEMU emulation — slower than native, but fine for development.

This keeps the original binary save-file format intact (no code changes), so a Docker
build matches how the live game behaves.

## Prerequisites

1. **Docker Desktop for Mac** (Apple Silicon build) — https://www.docker.com/products/docker-desktop/
   Verify with `docker --version`. Make sure Docker Desktop is running before the commands below.
2. **World files** at `lib/world/` — required to **boot** (not to compile). These are *not*
   in this repo; `lib/world/` is git-ignored and the upstream `Noobinabox/RotS-WorldFiles`
   repo is no longer publicly available. You need to obtain the world data and place it so
   that files exist at `lib/world/wld*`, `lib/world/mob*`, `lib/world/obj*`,
   `lib/world/zon*`, `lib/world/shp*`, etc. (The game `chdir`s into `lib/` at startup —
   `src/comm.cpp` / `config.cpp:58 DFLT_DIR="lib"` — and reads the `world/...` prefixes from
   `src/db.h`.)

   **If you have an old server backup**, use the importer to populate `lib/` from it:

   ```bash
   scripts/import-world-data.sh --backup /path/to/old/rots   # world + active players
   scripts/import-world-data.sh --backup /path/to/old/rots --world-only
   ```

   It extracts the world from the backup's clean `lib/world-for-6001.tbz` snapshot
   (whose `world/scr/*.scr` filenames match `world/scr/index` — the loose `lib/world`
   dir in a backup may contain macOS-munged `*.scr.txt` files that would make
   `index_boot()` fail), and copies the `A-E … U-Z` player buckets (the only ones the
   game indexes) into `lib/players`, `lib/plrobjs`, and `lib/exploits`. CVS metadata
   and `.DS_Store` are stripped; the `players/ZZZ` graveyard is skipped unless you pass
   `--with-graveyard`. Run `scripts/import-world-data.sh --help` for all options. All of
   this data stays git-ignored.

## Commands

A helper script wraps the common flows (`scripts/rots-docker.sh`):

```bash
scripts/rots-docker.sh build      # build the i386 toolchain image (first time / Dockerfile changes)
scripts/rots-docker.sh compile    # make setup + make all  -> bin/ageland  (no world files needed)
scripts/rots-docker.sh boot       # compile then start the server on port 1024 (needs world files)
scripts/rots-docker.sh shell      # interactive shell inside the container
```

Or directly with compose:

```bash
docker compose build
docker compose run --rm --service-ports rots bash
#   inside the container:
cd /rots/src && make setup && make all      # build
cd /rots && ./bin/ageland                   # run on port 1024 (see -p note below)
```

## Connecting

The server listens on **port 1024** (default `DFLT_PORT`). Published to the host by
compose, so from a second macOS terminal:

```bash
telnet localhost 1024
```

The first character you create is auto-promoted to a level-100 Implementor.

### The `-p` flag (important)

`make run` launches `./bin/ageland -p`. The `-p` flag means **"expect a proxy"** — the
game then reads a 4-byte client-IP header from each new connection before anything else
(`src/comm.cpp:1186`). That header is supplied by the Rust `proxy/` (WebSocket bridge).

For plain `telnet` development, run **without** `-p` (as the helper's `boot` does), so the
game falls back to `getpeername` for the client address. Only use `-p` if you also run the
proxy in front of the game.

## Notes

- Build artifacts (`bin/ageland`, `*.o`) land on the host via the bind mount, but the
  binary is a Linux i386 ELF — it only runs inside the container.
- "deprecated function" warnings during `make all` are expected and harmless.
- The Rust proxy (`proxy/`) is not needed for telnet play; it builds natively on macOS
  with `cargo build -p proxy` if you later want browser-client access.

## Build matrix (Phase 1)

The authoritative build is CMake. Presets live in `src/CMakePresets.json`
(CMake ≥ 3.23 to use presets; the i386 container's CMake 3.18 uses the root
`Makefile` flow instead).

| Preset / path | Platform | Status |
|---|---|---|
| container + root `Makefile` (`make configure/build/test`) | Linux i386 (`-m32`) | **green — the shipping ABI; CI-required** |
| `linux-x86-legacy` | Linux i386 via multilib | builds the game; tests stay in the container path |
| `linux-x64` | Linux x86-64 | builds + 503/503 tests green in CI already; NOT yet the blessed runtime (Phase 2 must verify boot/behavior + retire binary saves before it ships) |
| `macos-arm64` | macOS arm64 | red until Phase 2 (64-bit port) |
| `windows-msvc` | Windows x64 MSVC | red until Phase 3 (platform layer) |

Per-platform (from `src/`): `cmake --preset <name>`, `cmake --build --preset <name>`,
`ctest --preset <name>`. `cmake --list-presets` shows what runs on this host.

CI (`.github/workflows/ci.yml`): the `legacy-32bit` job is required; the three
64-bit jobs run allowed-to-fail until their enabling phase lands — their logs are the
porting work-list. `linux-x64` is the exception to watch: it's already green (503/503
tests passing on 64-bit Linux/GCC, including the characterization goldens), so Phase 2's
Linux work is mostly runtime verification (boot/behavior + retiring binary saves) rather
than a from-scratch port — the remaining porting effort is concentrated on macOS and
Windows.
