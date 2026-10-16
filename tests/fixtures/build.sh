#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Rebuild the fixture libraries in tests/fixtures/bin/ inside the SDK container.
# The tests use the committed binaries, so they run anywhere without Docker; run this
# only when you change a fixture source. Usage: tests/fixtures/build.sh [image]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
sdk=$(cd "$here/../.." && pwd)
image=${1:-az-mods-sdk:1}
docker run --rm --platform=linux/arm64 -v "$here":/fx -v "$sdk/include":/sdk/include:ro \
    -w /fx/src "$image" sh -euc '
F="-O2 -fPIC -I/sdk/include -fexceptions -fasynchronous-unwind-tables"
SO="-shared -static-libgcc -Wl,--no-undefined"
B=/fx/bin
gcc $F -fvisibility=hidden $SO -o $B/good.so          good.c
gcc $F -fvisibility=hidden $SO -o $B/uses-libm.so     uses-libm.c -lm
gcc $F -fvisibility=hidden $SO -o $B/export-read.so   export-read.c
gcc $F -fvisibility=hidden $SO -o $B/export-sqlite.so export-sqlite.c
gcc $F                     $SO -o $B/extra-export.so  extra-export.c
gcc $F -fvisibility=hidden $SO -o $B/no-entry.so      no-entry.c
gcc $F -fvisibility=hidden $SO -o $B/ctor.so          ctor.c
mkdir -p /tmp/stub
gcc -fPIC -shared -Wl,-soname,libX11.so.6     -o /tmp/stub/libX11.so   stub-x11.c
gcc -fPIC -shared -Wl,-soname,libazother.so.1 -o /tmp/stub/libazother.so stub-other.c
gcc $F -fvisibility=hidden $SO -o $B/needs-x11.so   needs-x11.c   -L/tmp/stub -lX11
gcc $F -fvisibility=hidden $SO -o $B/needs-other.so needs-other.c -L/tmp/stub -lazother
gcc -O2 -no-pie -o $B/not-shared.exe exe.c
'
ls -l "$here/bin"
