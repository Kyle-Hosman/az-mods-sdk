#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile every C code block in the guide against include/azmod-mod.h.

    tools/check-doc-snippets.py [--image az-mods-sdk:1] [--control]

Each ```c block in README.md and docs/*.md is compiled in the SDK container after a short
prelude: the standard headers, the SDK header, `MOD_ID`, `C` and `core` (the core's table,
as the guide's examples keep it) and declarations of the template's callbacks and page
state (the walkthrough in 03-first-mod.md refers to them before it shows them). A block
passes if it compiles as file-scope code, as the body of a function, or after the other
file-scope blocks of the same page (a block that uses a table shown further down).
Errors that point at a real mistake (an unknown name, a wrong argument type, a missing
service) fail the check; warnings about unused names do not. Exit status: 0 all
compiled, 1 some did not (each is printed), 2 no Docker.

--control adds one block that passes a string where a service wants a row handle; the run
then passes only if that block alone fails, which shows the check can fail.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

SDK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BLOCK = re.compile(r"^```c\n(.*?)^```", re.S | re.M)
PRELUDE = """#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "azmod-mod.h"
#define MOD_ID "example-mod"
static const az_core_v1 *C, *core;
/* the template's callbacks and page state (template/hello-az.c) */
static int init(const az_core_v1 *core);
static void tick(void);
static void on_draw(az_draw *d, az_rect area, void *user);
static int on_input(const az_event *ev, void *user);
static void on_enter(void *user);
static void toggle_titles(void);
static const az_screen_v1 SCREEN;
static int show_titles;
static int highlighted;
static az_rect titles_row;
/* the guide's exercise (03-first-mod.md, section 10) adds these beside them */
static int show_bpm;
static az_rect bpm_row;
static void draw_fitted(az_draw *d, int x, int y, const char *s, int px, uint32_t color, int max_w);
static int draw_setting_row(az_draw *d, az_rect area, int *y, const char *label, int on,
                            int highlight, az_rect *hit);
"""
FLAGS = ["-std=gnu11", "-c", "-fPIC", "-Wall", "-Wextra", "-Wno-unused-function",
         "-Wno-unused-variable", "-Wno-unused-parameter", "-Wno-unused-but-set-variable",
         "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
         "-Werror=int-conversion", "-Werror=return-type", "-I/sdk/include"]


def blocks():
    files = [os.path.join(SDK, "README.md")] + sorted(glob.glob(os.path.join(SDK, "docs", "*.md")))
    for path in files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for m in BLOCK.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            yield os.path.relpath(path, SDK), line, m.group(1)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--image", default="az-mods-sdk:1")
    ap.add_argument("--control", action="store_true", help="prove a broken block is caught")
    args = ap.parse_args(argv[1:])
    if not shutil.which("docker"):
        print("docker not found", file=sys.stderr)
        return 2
    work = tempfile.mkdtemp(prefix="az-snippets-", dir=os.path.expanduser("~"))
    try:
        items = list(blocks())
        if args.control:
            items.append(("(control)", 1, 'static int bad(void) { return C->row_value("not a handle"); }\n'))
        ok_file = run_pass(args.image, work, items, None)
        context = {}
        for i, (path, line, code) in enumerate(items):
            if ok_file[i]:
                context.setdefault(path, []).append((i, line, code))
        results = run_pass(args.image, work, items, context, report=True)
        print(f"{len(items)} code blocks checked")
        if args.control:
            caught = results[-1] is False and all(results[:-1])
            print("control: the broken block was caught" if caught else "control: NOT caught")
            return 0 if caught else 1
        return 0 if all(results) else 1
    finally:
        shutil.rmtree(work, ignore_errors=True)


def run_pass(image, work, items, context, report=False):
    """Compile every block; with `context`, also try each after its page's file-scope blocks.
    Returns a list of per-block results, or (when reporting) True if every block passed."""
    # A fresh folder per pass: Docker Desktop can serve a stale view of a file rewritten in
    # a folder it already mounted.
    work = tempfile.mkdtemp(prefix="pass-", dir=work)
    if True:
        script = ["set -u", "fail=0"]
        for i, (path, line, code) in enumerate(items):
            with open(os.path.join(work, f"s{i}-file.c"), "w") as f:
                f.write(PRELUDE + f'#line {line + 1} "{path}"\n' + code)
            with open(os.path.join(work, f"s{i}-body.c"), "w") as f:
                f.write(PRELUDE + "static void snippet(void) {\n" + f'#line {line + 1} "{path}"\n' + code + "\n}\n")
            ctx = [c for c in (context or {}).get(path, []) if c[0] != i]
            with open(os.path.join(work, f"s{i}-ctx.c"), "w") as f:
                f.write(PRELUDE + "".join(f'#line {l + 1} "{path}"\n' + c for _, l, c in ctx)
                        + f'#line {line + 1} "{path}"\n' + code)
            cc = "gcc " + " ".join(FLAGS)
            tries = [("file scope", "file"), ("function body", "body")] + ([("after the other blocks on its page", "ctx")] if context else [])
            cond = " elif ".join(f"{cc} -o /tmp/s{i}.o s{i}-{k}.c 2>s{i}-{k}.err; then echo 'OK   {path}:{line} ({label})';"
                                 for label, k in tries)
            script.append(f"if {cond} else echo 'FAIL {path}:{line}'; "
                          f"sed 's/^/     /' s{i}-file.err | grep -E 'error' | head -5; fail=1; fi")
        script.append("exit $fail")
        with open(os.path.join(work, "run.sh"), "w") as f:
            f.write("\n".join(script) + "\n")
        r = subprocess.run(["docker", "run", "--rm", "--platform=linux/arm64",
                            "-v", f"{work}:/w", "-v", f"{os.path.join(SDK, 'include')}:/sdk/include:ro",
                            "-w", "/w", image, "sh", "run.sh"], capture_output=True, text=True)
        if report:
            out = r.stdout or ""
            print(out, end="")
            return [f"OK   {path}:{line} (" in out for path, line, _ in items]
        return [f"OK   {path}:{line} (file scope)" in r.stdout for path, line, _ in items]


if __name__ == "__main__":
    sys.exit(main(sys.argv))
