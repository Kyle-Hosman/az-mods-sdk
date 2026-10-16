#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# Run community mods under the test harness with the host's own compiler, in up to three
# passes:
#
#   plain   no instrumentation; on Linux iocount.so is preloaded, so no-io and no-alloc
#           are measured too (it interposes glibc, so not on macOS)
#   asan    AddressSanitizer and UndefinedBehaviorSanitizer
#   tsan    ThreadSanitizer (the harness runs a mod's thread_start workers as real threads)
#
#     sanitize.sh [options] [MOD ...]
#
#   MOD                 ID:RATE:SOURCE[,SOURCE...]  the mod's id (MOD_ID), the most repaints
#                       a second it may request from tick, and its sources (.c, and .cpp or
#                       .cc for C++)
#   -p, --passes LIST   which passes, e.g. "asan" or "plain,tsan" (default: all three)
#   -s, --scenarios F   a C file of functional scenarios, compiled into the harness (see
#                       harness_scenarios in harness.c); the mods then also run them
#   -c, --controls      also run the planted controls (planted.c)
#   -I DIR              an extra include directory for the mod sources
#
# Needs a C compiler with the sanitizers: clang on Linux or macOS (CC overrides it), and
# for C++ sources clang++ (CXX overrides it). No Docker and no deck. Every pass must be able to fail, so:
#   - every binary must carry its pass's instrumentation (a build that silently dropped the
#     flags would pass everything);
#   - with --controls, planted.c with no fault must pass every check, and each planted
#     fault must be caught: in plain, faults 1..9 must each fail exactly their own check;
#     in asan, a heap overflow, a stack overflow and a signed integer overflow must each
#     fail no-crash with the sanitizer's report; in tsan, a data race must.
# The exit status is non-zero if a mod fails a check or a control is NOT caught.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
INC=$(cd "$HERE/../.." && pwd)/include
[ -f "$INC/azmod-mod.h" ] || { echo "azmod-mod.h not found in $INC" >&2; exit 2; }
CC=${CC:-clang}
CXX=${CXX:-clang++}
PASSES="plain asan tsan"
SCEN=""
CONTROLS=0
EXTRA_INC=""
usage() { sed -n '12,22p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2; }
while [ $# -gt 0 ]; do
    case $1 in
        -p|--passes) [ $# -ge 2 ] || usage; PASSES=$(printf '%s' "$2" | tr ',' ' '); shift 2 ;;
        -s|--scenarios) [ $# -ge 2 ] || usage; SCEN=$(cd "$(dirname "$2")" && pwd)/$(basename "$2"); shift 2 ;;
        -c|--controls) CONTROLS=1; shift ;;
        -I) [ $# -ge 2 ] || usage; EXTRA_INC="$EXTRA_INC -I$2"; shift 2 ;;
        -h|--help) usage ;;
        --) shift; break ;;
        -*) echo "unknown option: $1" >&2; usage ;;
        *) break ;;
    esac
done
[ $# -gt 0 ] || [ "$CONTROLS" = 1 ] || { echo "nothing to run: give a MOD or --controls" >&2; usage; }
for m in "$@"; do
    case $m in *:*:*) ;; *) echo "bad MOD (want ID:RATE:SOURCE[,SOURCE...]): $m" >&2; exit 2 ;; esac
done
[ -z "$SCEN" ] || [ -f "$SCEN" ] || { echo "no such scenarios file: $SCEN" >&2; exit 2; }

# is_cxx SOURCES...: any C++ source among them
is_cxx() {
    for s in "$@"; do case $s in *.cpp|*.cc) return 0 ;; esac; done
    return 1
}
# A C++ mod is linked with $CXX, and so is the harness when any mod is C++: the sanitizers'
# C++ runtime parts (operator new and delete, UBSan's type checks) come with the program that
# is linked by the C++ driver.
ANY_CXX=0
for m in "$@"; do
    srcs=$(printf '%s' "${m#*:*:}" | tr ',' ' ')
    # shellcheck disable=SC2086
    if is_cxx $srcs; then ANY_CXX=1; fi
done

B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT

WARN="-Wall -Wextra -Werror"
MODFLAGS="-fPIC -fvisibility=hidden -fno-omit-frame-pointer -I$INC$EXTRA_INC"
if [ "$(uname -s)" = Darwin ]; then
    # flat namespace: the mod's clock calls resolve to the harness's simulated clock
    SHARED="-shared -flat_namespace -undefined dynamic_lookup"
    LIBS="-lm -lpthread"
    PRELOAD_VAR=DYLD_INSERT_LIBRARIES
else
    SHARED="-shared"
    LIBS="-ldl -lm -lpthread"
    PRELOAD_VAR=LD_PRELOAD
fi
HFLAGS=""
FUNC=""
if [ -n "$SCEN" ]; then
    cp "$SCEN" "$B/scenarios.c"
    HFLAGS="-DAZ_HARNESS_SCENARIOS=\"$B/scenarios.c\""
    FUNC=--functional
fi

FAILED=0
note() { printf '%s\n' "$*"; }
bad() { note "MISMATCH: $*"; FAILED=1; }

# expect_pass LABEL OUTFILE: every CHECK line must be PASS or NOT-MEASURED, and no-crash ran
expect_pass() {
    if grep -q '^CHECK .* FAIL' "$2"; then
        bad "$1: $(grep '^CHECK .* FAIL' "$2" | tr '\n' ';')"
        grep -E 'Sanitizer|runtime error|^    #[0-9]|^    FAIL' "$2" | head -20 || true
    fi
    grep -q '^CHECK no-crash PASS' "$2" || bad "$1: no-crash did not report PASS"
    unmeasured=$(grep '^CHECK .* NOT-MEASURED' "$2" | cut -d' ' -f2 | tr '\n' ' ' | sed 's/ $//' || true)
    note "$1: $(grep -c '^CHECK .* PASS' "$2" || true) checks passed${unmeasured:+, not measured: $unmeasured}"
}

# instrumented FILE SYMBOL: FILE references the sanitizer runtime (SYMBOL is a prefix)
instrumented() {
    nm "$1" 2>/dev/null | grep -q "$2" || bad "$(basename "$1") is not instrumented ($2 not found)"
}

# caught LABEL OUTFILE PATTERN: a planted fault must fail no-crash with PATTERN in the report
caught() {
    if grep -q '^CHECK no-crash FAIL' "$2" && grep -q "$3" "$2"; then
        note "control $1: caught ($(grep -m1 "$3" "$2" | sed 's/^[= 0-9]*//' | cut -c1-90))"
    else
        bad "control $1: NOT caught"
        tail -5 "$2"
    fi
}

for pass in $PASSES; do
    case $pass in
        plain) SAN="-O2"; SYM=""; ENV="" ;;
        asan)  SAN="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined"
               SYM="__asan_"
               ENV="ASAN_OPTIONS=exitcode=86:detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1" ;;
        tsan)  SAN="-g -O1 -fsanitize=thread"
               SYM="__tsan_"
               ENV="TSAN_OPTIONS=halt_on_error=1:exitcode=66" ;;
        *) echo "unknown pass: $pass" >&2; exit 2 ;;
    esac
    note "== $pass"
    D=$B/$pass
    mkdir -p "$D"
    if [ "$ANY_CXX" = 0 ]; then
        # shellcheck disable=SC2086
        $CC $SAN $WARN -fno-omit-frame-pointer -I"$INC" $HFLAGS -rdynamic -o "$D/harness" \
            "$HERE/harness.c" $LIBS
    else
        # shellcheck disable=SC2086
        $CC $SAN $WARN -fno-omit-frame-pointer -I"$INC" $HFLAGS -c -o "$D/harness.o" "$HERE/harness.c"
        # shellcheck disable=SC2086
        $CXX $SAN -rdynamic -o "$D/harness" "$D/harness.o" $LIBS
    fi
    [ -z "$SYM" ] || instrumented "$D/harness" "$SYM"

    # iocount.so counts file, process, network and heap calls from the mod's callbacks.
    # The sanitizers bring their own allocator, so it is preloaded in the plain pass only.
    PRE=""
    if [ "$pass" = plain ]; then
        # shellcheck disable=SC2086
        if $CC -O2 $WARN -fPIC -shared -o "$D/iocount.so" "$HERE/iocount.c" $LIBS 2>"$D/iocount.err"; then
            PRE="$PRELOAD_VAR=$D/iocount.so"
        elif [ "$(uname -s)" = Darwin ]; then
            note "no-io and no-alloc are not measured on macOS (iocount.c interposes glibc)"
        else
            bad "iocount.so did not build: $(head -1 "$D/iocount.err")"
        fi
    fi

    # run ID SO RATE OUT [--functional]
    run() {
        # shellcheck disable=SC2086
        env $ENV $PRE "$D/harness" "$2" "$1" "$3" ${5:-} > "$4" 2>&1 || true
    }

    for m in "$@"; do
        id=${m%%:*}; rest=${m#*:}; rate=${rest%%:*}; srcs=$(printf '%s' "${rest#*:}" | tr ',' ' ')
        # shellcheck disable=SC2086
        if ! is_cxx $srcs; then
            # shellcheck disable=SC2086
            $CC $SAN $WARN $MODFLAGS $SHARED -o "$D/$id.so" $srcs $LIBS
        else
            objs="" n=0
            for s in $srcs; do
                n=$((n + 1))
                case $s in *.cpp|*.cc) cc=$CXX ;; *) cc=$CC ;; esac
                # shellcheck disable=SC2086
                $cc $SAN $WARN $MODFLAGS -fexceptions -c -o "$D/$id.$n.o" "$s"
                objs="$objs $D/$id.$n.o"
            done
            # shellcheck disable=SC2086
            $CXX $SAN $MODFLAGS $SHARED -o "$D/$id.so" $objs $LIBS
        fi
        [ -z "$SYM" ] || instrumented "$D/$id.so" "$SYM"
        run "$id" "$D/$id.so" "$rate" "$D/$id.out" $FUNC
        expect_pass "$id ($pass)" "$D/$id.out"
    done

    [ "$CONTROLS" = 1 ] || continue
    planted() {
        # shellcheck disable=SC2086
        $CC $SAN $WARN $MODFLAGS $SHARED -DFAULT="$1" -o "$D/planted-$1.so" "$HERE/planted.c" $LIBS
        [ -z "$SYM" ] || instrumented "$D/planted-$1.so" "$SYM"
        run planted "$D/planted-$1.so" 1 "$D/planted-$1.out"
    }
    planted 0
    expect_pass "control none ($pass)" "$D/planted-0.out"
    case $pass in
        plain)
            for f in 1:no-crash 2:draw-area 3:draw-context 4:text-size 5:no-io 6:no-alloc \
                     7:redraw-rate 8:log-quiet 9:api; do
                n=${f%%:*}; want=${f#*:}
                case $want in no-io|no-alloc)
                    if [ -z "$PRE" ]; then note "control $want: SKIP (iocount.so not loaded)"; continue; fi ;;
                esac
                planted "$n"
                fails=$(grep '^CHECK .* FAIL' "$D/planted-$n.out" | cut -d' ' -f2 | tr '\n' ' ' | sed 's/ $//' || true)
                if [ "$fails" = "$want" ]; then
                    note "control $want: caught ($(grep "^CHECK $want FAIL" "$D/planted-$n.out" | cut -d' ' -f4- | cut -c1-80))"
                else
                    bad "control $want: expected only '$want' to fail, got '${fails:-nothing}'"
                fi
            done ;;
        asan)
            planted 11; caught "heap overflow" "$D/planted-11.out" 'heap-buffer-overflow'
            planted 10; caught "stack overflow" "$D/planted-10.out" 'stack-buffer-overflow'
            planted 12; caught "signed overflow" "$D/planted-12.out" 'runtime error: signed integer overflow' ;;
        tsan)
            planted 13; caught "data race" "$D/planted-13.out" 'ThreadSanitizer: data race' ;;
    esac
done

if [ "$FAILED" = 0 ]; then note "ALL SANITIZER GATES PASS"; else note "SOME SANITIZER GATES FAILED"; fi
exit "$FAILED"
