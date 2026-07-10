# 32-bit Linux build/run environment for RotS.
#
# The game's Makefile forces a 32-bit build (-m32), which cannot run natively on
# Apple Silicon / modern macOS. This image provides an i386 Linux toolchain so the
# code compiles and runs UNCHANGED. On arm64 hosts Docker runs it via QEMU emulation.
#
# Build/run with docker compose (see docker-compose.yml) or scripts/rots-docker.sh.
FROM --platform=linux/386 i386/debian:trixie

# g++ 14 (supports the Makefile's -std=c++20, incl. std::format) + make for the
# primary Makefile build, and cmake for the alternate CMake build (cmake -S src
# -B build). libgtest-dev is needed to build and link the GoogleTest suite
# (tests link -lgtest -lpthread); python3 is for tools/account_smoke.py when
# run in-container. pkg-config is a CMake convenience. telnet/procps are dev
# conveniences. libcrypt-dev is intentionally NOT installed: same as the
# rots64 sibling image, Phase 2b Task 4's vendored SHA-512-crypt
# (src/rots_crypt.cpp) means nothing in this tree links or includes libc
# crypt() either (confirmed via `grep -rn 'lcrypt\|crypt.h' src/CMakeLists.txt
# src/Makefile src/tests/Makefile src/*.cpp src/*.h`).
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make cmake telnet procps ca-certificates \
        libgtest-dev python3 pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rots
CMD ["bash"]
