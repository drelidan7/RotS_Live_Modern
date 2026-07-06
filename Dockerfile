# 32-bit Linux build/run environment for RotS.
#
# The game's Makefile forces a 32-bit build (-m32), which cannot run natively on
# Apple Silicon / modern macOS. This image provides an i386 Linux toolchain so the
# code compiles and runs UNCHANGED. On arm64 hosts Docker runs it via QEMU emulation.
#
# Build/run with docker compose (see docker-compose.yml) or scripts/rots-docker.sh.
FROM --platform=linux/386 i386/debian:bullseye

# g++ 10 (supports the Makefile's -std=c++1z) + make for the primary Makefile build,
# and cmake for the alternate CMake build (cmake -S src -B build). libgtest-dev and
# libcrypt-dev are needed to build and link the GoogleTest suite (tests link
# -lgtest -lcrypt); python3 is for tools/account_smoke.py when run in-container.
# telnet/procps are dev conveniences.
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make cmake telnet procps ca-certificates \
        libgtest-dev libcrypt-dev python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /rots
CMD ["bash"]
