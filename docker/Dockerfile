# SPDX-License-Identifier: GPL-3.0-only
# The AZ-MODS build container: Ubuntu 18.04 on arm64 with GCC 7.5 (C and C++) and
# binutils 2.30, the same compiler, linker and C library the official AZ-MODS releases are
# built with.
#
# Why this exact toolchain:
#   - The deck runs glibc 2.29. A library built against Ubuntu 18.04's glibc 2.27 can
#     never require a newer GLIBC_ symbol version than the deck has.
#   - The deck's libstdc++.so.6 is 6.0.25 (GCC 8: up to GLIBCXX_3.4.25). C++ built with
#     GCC 7's headers needs at most GLIBCXX_3.4.24, so a C++ mod links the deck's own
#     libstdc++ instead of carrying a second copy of the C++ runtime into EP147.
#   - Pinning the base image by digest and every toolchain package by version means
#     the same source builds to the same bytes on any machine, so anyone can check that
#     a published .azmod was built from the source it links to.
#
# The official image also carries X11, FreeType and ALSA headers for the core mods.
# Community mods may not link those libraries (the core owns the display), so they are
# left out here. They do not change the compiler's output for code that does not use
# them.
#
# Build it (azmod-pack does this for you the first time):
#   tools/azmod-pack image
# which labels it with a hash of this folder; `azmod-pack build` refuses an image whose label
# differs (one built from another SDK version, or by hand) and prints the command again.
#
# On an Apple Silicon Mac or an arm64 Linux machine this runs natively. On x86 Docker
# runs it through QEMU emulation: slower, same result.
FROM ubuntu:18.04@sha256:152dc042452c496007f07ca9127571cb9c29697f42acbfad72324b2bb2e43c98

RUN apt-get update -qq && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
        gcc-7=7.5.0-3ubuntu1~18.04 \
        cpp-7=7.5.0-3ubuntu1~18.04 \
        libgcc-7-dev=7.5.0-3ubuntu1~18.04 \
        gcc=4:7.4.0-1ubuntu2.3 \
        g++-7=7.5.0-3ubuntu1~18.04 \
        libstdc++-7-dev=7.5.0-3ubuntu1~18.04 \
        g++=4:7.4.0-1ubuntu2.3 \
        binutils=2.30-21ubuntu1~18.04.9 \
        libc6-dev=2.27-3ubuntu1.6 \
        make=4.1-9.1ubuntu1 && \
    rm -rf /var/lib/apt/lists/*

# Builds mount the mod at /mod and the SDK headers at /sdk/include. Keeping these paths
# fixed keeps any path the compiler records identical on every machine.
WORKDIR /mod
