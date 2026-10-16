#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check that the SDK matches the core it documents.

    tools/check-header.py PATH/TO/core/azmod-mod.h [PATH/TO/core/MOD-MENU-KEYS.md]

include/azmod-mod.h must be byte-identical to the core's header: a mod compiled against it
sees exactly the struct layouts the core was built with. With a second argument, the key
table in docs/MOD-MENU-KEYS.md (between the keys:begin and keys:end markers) must also equal
the core's generated table. Exit status: 0 identical, 1 different (the first differing line
is printed), 2 a file cannot be read or the markers are missing.
"""
import os
import re
import sys

SDK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_HEADER = os.path.join(SDK, "include", "azmod-mod.h")
SDK_KEYS = os.path.join(SDK, "docs", "MOD-MENU-KEYS.md")
KEYS_BLOCK = re.compile(rb"<!-- keys:begin.*?<!-- keys:end -->", re.S)


def keys_block(path):
    """The generated key table in a Markdown file, or None if the markers are missing."""
    with open(path, "rb") as f:
        m = KEYS_BLOCK.search(f.read())
    return m.group(0) if m else None


def compare(sdk_path, core_path):
    """Return None when the files are identical, else a one-line description."""
    with open(sdk_path, "rb") as f:
        sdk = f.read()
    with open(core_path, "rb") as f:
        core = f.read()
    return compare_bytes(sdk, core)


def compare_bytes(sdk, core):
    if sdk == core:
        return None
    a, b = sdk.split(b"\n"), core.split(b"\n")
    for i, (x, y) in enumerate(zip(a, b), 1):
        if x != y:
            return f"line {i} differs:\n  sdk:  {x.decode(errors='replace')}\n  core: {y.decode(errors='replace')}"
    return f"one file is longer: sdk {len(a)} lines, core {len(b)} lines"


def main(argv):
    if len(argv) not in (2, 3):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    status = 0
    try:
        diff = compare(SDK_HEADER, argv[1])
        if diff is None:
            print("include/azmod-mod.h is identical to the core's")
        else:
            print(f"include/azmod-mod.h differs from {argv[1]}: {diff}")
            status = 1
        if len(argv) == 3:
            mine, theirs = keys_block(SDK_KEYS), keys_block(argv[2])
            if mine is None or theirs is None:
                print("keys:begin / keys:end markers missing", file=sys.stderr)
                return 2
            diff = compare_bytes(mine, theirs)
            if diff is None:
                print("docs/MOD-MENU-KEYS.md matches the core's key table")
            else:
                print(f"docs/MOD-MENU-KEYS.md differs from {argv[2]}: {diff}")
                status = 1
    except OSError as e:
        print(f"cannot read: {e}", file=sys.stderr)
        return 2
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
