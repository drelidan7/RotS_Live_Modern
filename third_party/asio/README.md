# Vendored Asio (standalone, non-Boost)

- **Version:** 1.38.1 (upstream tag `asio-1-38-1`)
- **Upstream repo:** https://github.com/chriskohlhoff/asio
- **Source archive:** https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-38-1.tar.gz
- **Archive SHA-256:** `2827b229972be80cdb14e5497962fa393d1adf036b5869e2b9c99f644daadacc`
- **Vendored on:** 2026-07-07
- **License:** Boost Software License 1.0 (`LICENSE` in this directory, copied
  unmodified from the upstream `LICENSE_1_0.txt`).

## Why vendored instead of FetchContent

The Phase 3 platform-layer plan (`docs/superpowers/plans/2026-07-07-phase-3-windows-platform-layer.md`,
Global Constraints) calls for Asio pinned via CMake `FetchContent`, but the i386
build container has no network access at build time (`src/Makefile` / the root
`make` flow compiles fully offline). Vendoring a pinned release under
`third_party/asio/` gives every build path — the 32-bit container Makefile,
`src/tests/Makefile`, and all four CMake presets (`linux-x64`,
`linux-x86-legacy`, `macos-arm64`, `windows-msvc`) — the exact same headers
with no network dependency and no version skew between platforms.

## What's here

Only the headers needed by the standalone (non-Boost) variant:

```
third_party/asio/
├── LICENSE                  # Boost Software License 1.0
├── README.md                 # this file
└── include/
    ├── asio.hpp               # umbrella header
    └── asio/                  # implementation headers
```

Build tooling files from the upstream `include/` directory (`Makefile.am`,
`.gitignore`) were dropped — they're irrelevant to a vendored copy. Nothing
under `include/` was modified from upstream.

## How it's wired in

All targets that compile project sources define `ASIO_STANDALONE` (so Asio
never looks for Boost) and add `third_party/asio/include` to their header
search path:

- `src/Makefile` — `-Ithird_party/asio/include -DASIO_STANDALONE` folded into
  `REQ_CPPFLAGS`.
- `src/tests/Makefile` — the same two additions in `CXXFLAGS`.
- `src/CMakeLists.txt` — `target_include_directories(... SYSTEM
  third_party/asio/include)` (SYSTEM so Asio's own warnings don't get
  attributed to the game's `-Wall`/`-Wextra` in the test build) and
  `target_compile_definitions(... ASIO_STANDALONE)`, applied to both the
  `ageland` and `ageland_tests` targets.

## Known toolchain quirk: include `<climits>` before `<asio.hpp>`

On the i386 container's GCC 10 / libstdc++, `asio/detail/thread_info_base.hpp`
uses `UCHAR_MAX` without including `<climits>` itself, relying on a transitive
include that AppleClang's libc++ and newer libstdc++ provide but GCC 10 does
not. The vendored copy is kept byte-for-byte upstream (no local patch), so any
translation unit that includes `<asio.hpp>` must `#include <climits>` first to
build on that toolchain — `src/tests/asio_smoke_tests.cpp` does, and Task 4's
`comm.cpp` connection layer will need the same.

## Re-vendoring / upgrading

1. Download a newer standalone release tarball from
   `https://github.com/chriskohlhoff/asio/archive/refs/tags/<tag>.tar.gz`.
2. Replace `third_party/asio/include/` with the tarball's `include/` (drop
   `Makefile.am` / `.gitignore` again) and `third_party/asio/LICENSE` with
   the new `LICENSE_1_0.txt` (Asio's license has not changed versions).
3. Update the version/SHA-256/date fields above.
4. Re-run the full battery (32-bit container, `rots64`, macOS native) before
   committing.
