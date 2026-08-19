---
name: build-and-smoke
description: Build the RotS game server and run a boot smoke test to verify changes. Use after C/C++ changes in src/ to confirm the server still compiles and starts.
---

The de-facto test process for the C/C++ game server is "build it and boot it" (per AGENTS.md). Run these steps and report results.

1. **Format** the changed code first (CI expects formatted diffs):
   ```
   cd src && make format
   ```

2. **Build** the server:
   ```
   cd src && make all
   ```
   Deprecated-function warnings are expected and not failures. Report only link/compile errors.

3. **Smoke test** — boot the server and confirm it starts, then stop it:
   ```
   cd src && make run     # starts ./bin/ageland -p in the background
   ```
   Confirm the process is running and the log shows a clean boot (check `log/`). Then stop the background process. Report whether it booted cleanly and accepts connections.

4. If C/C++ unit tests are relevant to the change, also run:
   ```
   cd src/tests && make tests && ../../bin/tests
   ```

Note: this requires the world files (separate repo) and `make setup` to have been run. If `lib/world/` or runtime dirs are missing, report that the smoke test can't run rather than failing silently.
