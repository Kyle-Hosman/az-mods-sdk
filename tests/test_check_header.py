# SPDX-License-Identifier: GPL-3.0-only
"""Tests for tools/check-header.py: an identical copy passes, and a one-byte change, a
truncated copy and a missing file are each caught (the controls)."""
import importlib.util
import io
import contextlib
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("check_header", os.path.join(SDK, "tools", "check-header.py"))
ch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ch)


class CheckHeader(unittest.TestCase):
    def setUp(self):
        with open(ch.SDK_HEADER, "rb") as f:
            self.data = f.read()
        self.tmp = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.tmp.cleanup()

    def core(self, data):
        p = os.path.join(self.tmp.name, "azmod-mod.h")
        with open(p, "wb") as f:
            f.write(data)
        return p

    def run_main(self, path):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return ch.main(["check-header.py", path])

    def test_identical_copy_passes(self):
        self.assertEqual(self.run_main(self.core(self.data)), 0)

    def test_one_changed_byte_is_caught(self):
        i = self.data.index(b"AZ_MOD_ABI 1")
        changed = self.data[:i + 11] + b"2" + self.data[i + 12:]
        self.assertEqual(self.run_main(self.core(changed)), 1)
        self.assertIn("line", ch.compare(ch.SDK_HEADER, self.core(changed)))

    def test_truncated_copy_is_caught(self):
        self.assertEqual(self.run_main(self.core(self.data[:-200])), 1)

    def keys(self, data):
        p = os.path.join(self.tmp.name, "MOD-MENU-KEYS.md")
        with open(p, "wb") as f:
            f.write(data)
        return p

    def test_key_table_identical_passes(self):
        block = ch.keys_block(ch.SDK_KEYS)
        core = self.keys(b"# core wording differs\n\n" + block + b"\n")
        self.assertEqual(self.run_main_keys(core), 0)

    def test_key_table_change_is_caught(self):
        block = ch.keys_block(ch.SDK_KEYS)
        core = self.keys(block.replace(b"`stems`", b"`stemz`", 1))
        self.assertEqual(self.run_main_keys(core), 1)

    def test_key_table_without_markers(self):
        self.assertEqual(self.run_main_keys(self.keys(b"no table here")), 2)

    def run_main_keys(self, keys_path):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return ch.main(["check-header.py", self.core(self.data), keys_path])

    def test_missing_core_header(self):
        self.assertEqual(self.run_main(os.path.join(self.tmp.name, "nope.h")), 2)


if __name__ == "__main__":
    unittest.main()
