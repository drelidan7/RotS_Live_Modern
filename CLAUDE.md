# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Team conventions (build/test/style/commit/security) live in AGENTS.md. Read it:

@AGENTS.md

## Non-obvious gotchas

- **32-bit build is mandatory, so building on this machine (Apple Silicon macOS) requires Docker.** The Makefile compiles with `-m32`, which cannot build/run natively on arm64 macOS. Use the 32-bit Linux container — see `docs/BUILD.md` and `scripts/rots-docker.sh`. Warnings (`-w` suppresses most) and "deprecated function" notices on build are expected — the game still compiles.
- **The `-p` flag means "expect a proxy", not a port.** It makes the game read a 4-byte client-IP header per connection (`comm.cpp:1186`). Run WITHOUT `-p` for plain telnet; default port is 1024. `make run` uses `-p` and needs the Rust proxy in front.
- **World files and player data are not in this repo.** `lib/world/` comes from a separate repo (https://github.com/Noobinabox/RotS-WorldFiles); `lib/players/`, `lib/plrobjs/`, `lib/exploits/`, and `log/` are git-ignored. Run `cd src && make setup` once to create the runtime directory structure. Never commit anything under these paths.
- **First character created becomes a level-100 Implementor** on a fresh `make setup` install — expected for local dev.
- **Running C/C++ tests** (Google Test): `cd src/tests && make tests`, then run the produced binary at `bin/tests`. Coverage is minimal; primary verification is a build + boot smoke test (see `/build-and-smoke`).
- **Proxy IP header:** `proxy/` (Rust) prepends a 4-byte client IP to the game connection before forwarding. Relevant when touching connection/auth/logging code. `--cloudflare` reads the IP from the `CF-Connecting-IP` header instead.
