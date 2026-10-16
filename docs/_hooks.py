# SPDX-License-Identifier: GPL-3.0-only
"""MkDocs hook: the guide is written to read on GitHub, next to the code, so it links to
files outside docs/ (../include/azmod-mod.h, ../template/, ../LICENSE). On the site those
links point at the same files on GitHub, at the branch the site is built from."""
import re

REPO = "https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/"
OUTSIDE = re.compile(r"\]\(\.\./([^)]*)\)")


def on_page_markdown(markdown, **kwargs):
    return OUTSIDE.sub(lambda m: "](" + REPO + m.group(1) + ")", markdown)
