# SPDX-License-Identifier: GPL-3.0-only
"""Tests for tools/azmod_pack.py. Run from the SDK root:

    python3 -m unittest discover -s tests -v

The fixture libraries in tests/fixtures/bin/ are real aarch64 builds from the SDK
container (tests/fixtures/build.sh). Every refusal test has a passing control (good.so)
in the same suite, so a checker that refuses everything cannot pass.
"""
import contextlib
import hashlib
import io
import json
import struct
import os
import shutil
import sys
import tarfile
import tempfile
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SDK = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(SDK, "tools"))
import azmod_pack as ap  # noqa: E402

BIN = os.path.join(HERE, "fixtures", "bin")
TEMPLATE = os.path.join(SDK, "template")


def fixture(name):
    with open(os.path.join(BIN, name), "rb") as f:
        return f.read()


def elf_report(data):
    r = ap.Report()
    ap.check_elf(data, r)
    return r


def manifest():
    with open(os.path.join(TEMPLATE, "azmod.json")) as f:
        return json.load(f)


def run(argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        rc = ap.main(argv)
    return rc, out.getvalue()


class ElfChecks(unittest.TestCase):
    def assertRefused(self, data, needle):
        r = elf_report(data)
        self.assertFalse(r.ok, "expected a refusal")
        self.assertTrue(any(needle in e for e in r.errors), f"{needle!r} not in {r.errors}")

    def test_good_passes(self):
        r = elf_report(fixture("good.so"))
        self.assertTrue(r.ok, r.errors)
        self.assertEqual(r.warnings, [])

    def test_libm_is_allowed(self):
        r = elf_report(fixture("uses-libm.so"))
        self.assertTrue(r.ok, r.errors)

    def test_exports_read(self):
        self.assertRefused(fixture("export-read.so"), "exports read: only the core")

    def test_exports_sqlite(self):
        self.assertRefused(fixture("export-sqlite.so"), "exports sqlite3_open")

    def test_extra_export(self):
        self.assertRefused(fixture("extra-export.so"), "exports helper_scale")

    def test_no_entry(self):
        self.assertRefused(fixture("no-entry.so"), "does not export az_mod_v1_entry")

    def test_needs_x11(self):
        self.assertRefused(fixture("needs-x11.so"), "depends on libX11.so.6")

    def test_needs_unknown_lib(self):
        self.assertRefused(fixture("needs-other.so"), "depends on libazother.so.1")

    def test_executable_is_not_a_library(self):
        self.assertRefused(fixture("not-shared.exe"), "not a shared library")

    def test_constructor_warns_but_loads(self):
        r = elf_report(fixture("ctor.so"))
        self.assertTrue(r.ok, r.errors)
        self.assertTrue(any("constructor" in w for w in r.warnings), r.warnings)

    def test_section_table_required(self):
        d = bytearray(fixture("good.so"))
        d[40:48] = (0).to_bytes(8, "little")      # e_shoff = 0, as sstrip leaves it
        self.assertRefused(bytes(d), "section table is missing")
        d = bytearray(fixture("good.so"))
        d[60:62] = (0xFFFF).to_bytes(2, "little") # e_shnum past the end of the file
        self.assertRefused(bytes(d), "section table is missing")

    def test_wrong_machine(self):
        d = bytearray(fixture("good.so"))
        d[18:20] = (62).to_bytes(2, "little")      # EM_X86_64
        self.assertRefused(bytes(d), "not aarch64")

    def test_32bit(self):
        d = bytearray(fixture("good.so"))
        d[4] = 1                                   # ELFCLASS32
        self.assertRefused(bytes(d), "not a 64-bit ELF")

    def test_garbage_and_truncation(self):
        self.assertRefused(b"hello world" * 10, "not an ELF file")
        self.assertRefused(b"", "not an ELF file")
        good = fixture("good.so")
        # good.so's loaded segments end at byte 4120 (readelf -l): a cut before that
        # faults when loaded. A cut after it loses the section table at the end of the
        # file, which the deck's core reads the symbols through: refused as well.
        for cut in (64, 200, 1000, 1936, 4119, 4120, len(good) - 1):
            r = elf_report(good[:cut])
            self.assertFalse(r.ok, f"truncated at {cut} should be refused")

    def test_glibc_too_new(self):
        d = fixture("uses-libm.so")
        self.assertIn(b"GLIBC_2.17", d)
        bad = d.replace(b"GLIBC_2.17", b"GLIBC_2.99")
        self.assertRefused(bad, "needs GLIBC_2.99")

    def test_glibc_at_ceiling_passes(self):
        d = fixture("uses-libm.so").replace(b"GLIBC_2.17", b"GLIBC_2.29")
        self.assertTrue(elf_report(d).ok)

    def test_glibc_one_past_ceiling(self):
        d = fixture("uses-libm.so").replace(b"GLIBC_2.17", b"GLIBC_2.30")
        self.assertRefused(d, "needs GLIBC_2.30")

    def test_forbidden_names_in_a_real_library(self):
        # export-read.so really exports `read`. Rename it in the string table to the
        # other four-letter names, which keeps every offset valid.
        d = fixture("export-read.so")
        for name in ("open", "poll"):
            mutated = d.replace(b"\0read\0", b"\0" + name.encode() + b"\0", 1)
            self.assertRefused(mutated, f"exports {name}: only the core")

    def test_every_forbidden_name(self):
        good = fixture("good.so")
        real = ap.Elf.exports
        names = sorted(ap.FORBIDDEN_EXPORTS) + ["sqlite3_exec", "sqlite3_key"]
        try:
            for name in names:
                ap.Elf.exports = lambda self, n=name: [(ap.ENTRY_SYMBOL, 2), (n, 2)]
                self.assertRefused(good, f"exports {name}: only the core")
        finally:
            ap.Elf.exports = real

    def test_matches_readelf_counts(self):
        # Measured with binutils readelf in the SDK container (tests/fixtures/build.sh):
        # dynamic symbol counts including the null symbol, and defined global exports.
        expect = {"good.so": (17, 10), "extra-export.so": (18, 11),
                  "export-read.so": (18, 11), "uses-libm.so": (18, 10)}
        for name, (nsym, nexp) in expect.items():
            e = ap.Elf(fixture(name))
            self.assertEqual(len(e.dynsyms()) + 1, nsym, name)
            self.assertEqual(len(e.exports()), nexp, name)


class CxxSources(unittest.TestCase):
    """build checks the image for g++ only when the mod has C++ sources."""

    def test_detects_cpp_and_cc(self):
        for name in ("mod.cpp", "src/part.cc"):
            with tempfile.TemporaryDirectory() as td:
                os.makedirs(os.path.dirname(os.path.join(td, name)), exist_ok=True)
                open(os.path.join(td, name), "w").close()
                self.assertTrue(ap._has_cxx_sources(td), name)

    def test_c_only_and_build_output_are_not_cxx(self):
        self.assertFalse(ap._has_cxx_sources(TEMPLATE))      # control: the C template
        with tempfile.TemporaryDirectory() as td:
            for d in ("out", "dist", ".git"):
                os.makedirs(os.path.join(td, d))
                open(os.path.join(td, d, "x.cpp"), "w").close()
            open(os.path.join(td, "mod.c"), "w").close()
            self.assertFalse(ap._has_cxx_sources(td))

    def test_the_cxx_test_mod(self):
        self.assertTrue(ap._has_cxx_sources(os.path.join(HERE, "cxx", "hello-cxx")))


class ImageLabel(unittest.TestCase):
    """The image's label is a hash of docker/: equal for equal folders, different otherwise."""

    def hash_of(self, files):
        with tempfile.TemporaryDirectory() as td:
            os.makedirs(os.path.join(td, "docker"))
            for name, data in files.items():
                with open(os.path.join(td, "docker", name), "wb") as f:
                    f.write(data)
            old = ap.SDK_ROOT
            ap.SDK_ROOT = td
            try:
                return ap.docker_dir_hash()
            finally:
                ap.SDK_ROOT = old

    def test_same_folder_same_hash(self):
        a = self.hash_of({"Dockerfile": b"FROM x\n"})
        self.assertEqual(a, self.hash_of({"Dockerfile": b"FROM x\n"}))

    def test_changed_or_added_file_changes_it(self):
        a = self.hash_of({"Dockerfile": b"FROM x\n"})
        self.assertNotEqual(a, self.hash_of({"Dockerfile": b"FROM y\n"}))
        self.assertNotEqual(a, self.hash_of({"Dockerfile": b"FROM x\n", "extra": b""}))

    def test_build_command_carries_the_label(self):
        cmd = ap.image_build_cmd("az-mods-sdk:1")
        self.assertIn(f"{ap.IMAGE_LABEL}={ap.docker_dir_hash()}", cmd)


class ManifestChecks(unittest.TestCase):
    def check(self, m):
        r = ap.Report()
        ap.check_manifest(m, r)
        return r

    def assertRefused(self, m, needle):
        r = self.check(m)
        self.assertFalse(r.ok)
        self.assertTrue(any(needle in e for e in r.errors), f"{needle!r} not in {r.errors}")

    def test_template_manifest_is_valid(self):
        r = self.check(manifest())
        self.assertTrue(r.ok, r.errors)
        self.assertEqual(r.warnings, [])

    def test_hook_mod_with_tags_is_valid(self):
        m = manifest()
        m["firmware"] = ["ep147-0123456789abcdef", "ep147-fedcba9876543210"]
        m["hooks"] = True
        self.assertTrue(self.check(m).ok)

    def test_missing_source(self):
        m = manifest()
        del m["source"]
        self.assertRefused(m, "missing \"source\"")

    def test_http_source(self):
        m = manifest()
        m["source"] = "http://example.com/x"
        self.assertRefused(m, "\"source\" must be a public https:// URL")

    def test_bad_ids(self):
        for bad in ("ab", "Hello", "has space", "a" * 33, "under_score", 7):
            m = manifest()
            m["id"] = bad
            self.assertRefused(m, "\"id\"")

    def test_any_with_hooks(self):
        m = manifest()
        m["hooks"] = True
        self.assertRefused(m, "may not claim hooks")

    def test_any_mixed_with_tags(self):
        m = manifest()
        m["firmware"] = ["any", "ep147-0123456789abcdef"]
        self.assertRefused(m, "not both")

    def test_bad_tags(self):
        for bad in (["ep147-XYZ"], ["1.32"], [], "any", ["ep147-0123456789abcde"]):
            m = manifest()
            m["firmware"] = bad
            self.assertFalse(self.check(m).ok, bad)

    def test_hooks_must_be_bool(self):
        m = manifest()
        m["hooks"] = "no"
        self.assertRefused(m, "\"hooks\" must be true or false")

    def test_version_and_api(self):
        # the deck's version compare reads MAJOR.MINOR.PATCH, numbers only, each up to 9999
        for bad in ("1.0", "1.0.0-beta.1", "1.0.0+build", "v1.0.0", "1.0.10000", "01.0.0"):
            m = manifest()
            m["version"] = bad
            self.assertRefused(m, "is not MAJOR.MINOR.PATCH")
        for good in ("0.0.0", "1.2.3", "9999.9999.9999"):   # control: the same check passes these
            m = manifest()
            m["version"] = good
            self.assertTrue(self.check(m).ok, good)
        m = manifest()
        m["min_core"] = "1.2.3-rc1"
        self.assertRefused(m, "\"min_core\" must be MAJOR.MINOR.PATCH")
        m = manifest()
        m["api"] = 2
        self.assertRefused(m, "\"api\" must be 1")

    def test_category(self):
        m = manifest()
        m["category"] = "visual"
        self.assertTrue(self.check(m).ok)
        m["category"] = "Visual"
        self.assertRefused(m, "\"category\"")

    def test_manifest_file_rules(self):
        r = ap.Report()
        ap.check_manifest_bytes(b"{" + b" " * 17000 + b"}", r)
        self.assertTrue(any("reads at most" in e for e in r.errors), r.errors)
        r = ap.Report()
        ap.check_manifest_bytes(b'{"name": "Caf\\u00e9"}', r)
        self.assertTrue(r.ok)
        self.assertTrue(any("escapes" in w for w in r.warnings), r.warnings)
        r = ap.Report()
        ap.check_manifest_bytes('{"name": "Café"}'.encode(), r)
        self.assertEqual((r.errors, r.warnings), ([], []))

    def test_lengths_match_the_core(self):
        m = manifest()
        m["name"] = "x" * 47
        m["summary"] = "y" * 119
        self.assertTrue(self.check(m).ok)
        m["name"] = "x" * 48
        self.assertRefused(m, "\"name\" is 48 characters")
        m = manifest()
        m["summary"] = "y" * 120
        self.assertRefused(m, "\"summary\" is 120 characters")

    def test_unknown_key_warns(self):
        m = manifest()
        m["flavor"] = "red"
        r = self.check(m)
        self.assertTrue(r.ok)
        self.assertTrue(r.warnings)


class IconChecks(unittest.TestCase):
    def test_template_icon(self):
        r = ap.Report()
        with open(os.path.join(TEMPLATE, "icon.png"), "rb") as f:
            ap.check_icon(f.read(), r)
        self.assertTrue(r.ok, r.errors)

    def test_wrong_size_and_not_png(self):
        with open(os.path.join(TEMPLATE, "icon.png"), "rb") as f:
            d = bytearray(f.read())
        d[16:20] = (128).to_bytes(4, "big")
        r = ap.Report()
        ap.check_icon(bytes(d), r)
        self.assertTrue(any("must be 256x256" in e for e in r.errors))
        r = ap.Report()
        ap.check_icon(b"GIF89a....", r)
        self.assertFalse(r.ok)
        r = ap.Report()
        ap.check_icon(None, r)
        self.assertFalse(r.ok)


class ModDir:
    """A temporary copy of the template with a fixture library in out/."""

    def __init__(self, so="good.so", mutate=None):
        self.td = tempfile.mkdtemp()
        self.dir = os.path.join(self.td, "mod")
        os.makedirs(os.path.join(self.dir, "out"))
        for f in ("azmod.json", "icon.png"):
            shutil.copy(os.path.join(TEMPLATE, f), self.dir)
        shutil.copy(os.path.join(SDK, "LICENSE"), self.dir)
        shutil.copy(os.path.join(BIN, so), os.path.join(self.dir, "out", "hello-az.so"))
        if mutate:
            m = manifest()
            mutate(m)
            with open(os.path.join(self.dir, "azmod.json"), "w") as f:
                json.dump(m, f)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        shutil.rmtree(self.td)


class Packaging(unittest.TestCase):
    def test_validate_dir(self):
        with ModDir() as md:
            r, _ = ap.validate_dir(md.dir)
            self.assertTrue(r.ok, r.errors)
        with ModDir(so="export-read.so") as md:
            r, _ = ap.validate_dir(md.dir)
            self.assertFalse(r.ok)

    def test_new_points_the_makefile_at_this_sdk(self):
        with tempfile.TemporaryDirectory() as td:
            dest = os.path.join(td, "mods", "beat-counter")
            rc, text = run(["new", dest, "--id", "beat-counter"])
            self.assertEqual(rc, 0, text)
            with open(os.path.join(dest, "Makefile")) as f:
                mk = f.read()
            vals = {k.strip(): v.split("#")[0].strip() for k, v in
                    (l.split("?=", 1) for l in mk.splitlines()
                     if l.startswith(("AZ_SDK ", "AZ_SDK_INCLUDE ")))}
            sdk = os.path.normpath(os.path.join(dest, vals["AZ_SDK"]))
            inc = os.path.normpath(os.path.join(dest, vals["AZ_SDK_INCLUDE"]))
            self.assertEqual(os.path.realpath(sdk), os.path.realpath(SDK))
            self.assertTrue(os.path.isfile(os.path.join(inc, "azmod-mod.h")))
            self.assertNotIn("AZ_SDK          ?= ..\n", mk)                     # not the template's

    def test_publishing_refuses_placeholders(self):
        def placeholder(m):
            m["summary"] = ap.PLACEHOLDER_SUMMARY
        with ModDir(mutate=placeholder) as md:
            out = os.path.join(md.td, "dist")
            rc, text = run(["package", md.dir, "--out", out])
            self.assertEqual(rc, 0, text)                                        # CONTROL: local test package
            for extra in (["--github", "someone/az-mods"],
                          ["--package-url", "https://example.com/hello-az-1.0.0.azmod"]):
                rc, text = run(["package", md.dir, "--out", out] + extra)
                self.assertNotEqual(rc, 0, text)
                self.assertIn("summary in azmod.json is still the placeholder", text)
        with ModDir() as md:                                                     # CONTROL: filled in
            rc, text = run(["package", md.dir, "--out", os.path.join(md.td, "dist"),
                            "--github", "someone/az-mods"])
            self.assertEqual(rc, 0, text)

    def test_package_is_deterministic_and_valid(self):
        shas = []
        with ModDir() as md:
            for _ in range(2):
                out = os.path.join(md.td, "dist")
                rc, text = run(["package", md.dir, "--out", out,
                                "--github", "someone/az-mods"])
                self.assertEqual(rc, 0, text)
                pkg = os.path.join(out, "hello-az-1.0.0.azmod")
                with open(pkg, "rb") as f:
                    shas.append(hashlib.sha256(f.read()).hexdigest())
                os.utime(os.path.join(md.dir, "out", "hello-az.so"), (1, 1))
            self.assertEqual(shas[0], shas[1])
            r, m = ap.validate_package(pkg)
            self.assertTrue(r.ok, r.errors)
            self.assertEqual(m["id"], "hello-az")
            with open(os.path.join(out, "hello-az-1.0.0.entry.json")) as f:
                entry = json.load(f)
            self.assertEqual(entry["sha256"], shas[0])
            self.assertEqual(entry["size"], os.path.getsize(pkg))
            self.assertEqual(entry["package"], "https://github.com/someone/az-mods/releases/"
                             "download/hello-az-1.0.0/hello-az-1.0.0.azmod")
            self.assertEqual(entry["icon"], "https://github.com/someone/az-mods/releases/"
                             "download/hello-az-1.0.0/hello-az-1.0.0-icon.png")
            r = ap.Report()
            ap.check_repo_entry(entry, r, "entry")
            self.assertTrue(r.ok, r.errors)
            if ap.PACKAGE_FORMAT == "zip":
                with zipfile.ZipFile(pkg) as z:
                    self.assertEqual(sorted(z.namelist()),
                                     ["LICENSE", "azmod.json", "hello-az.so", "icon.png"])

    def test_assets_ship_and_are_checked(self):
        with ModDir() as md:
            os.makedirs(os.path.join(md.dir, "assets", "kits"))
            with open(os.path.join(md.dir, "assets", "kits", "kick.wav"), "wb") as f:
                f.write(b"RIFF" + b"\0" * 60)
            with open(os.path.join(md.dir, "assets", ".DS_Store"), "wb") as f:
                f.write(b"junk")
            for junk in ("desktop.ini", "Thumbs.db"):       # Windows makes these by itself
                with open(os.path.join(md.dir, "assets", "kits", junk), "wb") as f:
                    f.write(b"junk")
            r, _ = ap.validate_dir(md.dir)
            self.assertTrue(r.ok, r.errors)
            self.assertFalse(any("assets/" in w for w in r.warnings), r.warnings)   # small: no warning
            out = os.path.join(md.td, "dist")
            rc, text = run(["package", md.dir, "--out", out])
            self.assertEqual(rc, 0, text)
            pkg = os.path.join(out, "hello-az-1.0.0.azmod")
            r, _ = ap.validate_package(pkg)
            self.assertTrue(r.ok, r.errors)
            if ap.PACKAGE_FORMAT == "zip":
                with zipfile.ZipFile(pkg) as z:
                    names = z.namelist()
                self.assertIn("assets/kits/kick.wav", names)
                self.assertNotIn("assets/.DS_Store", names)               # hidden files left out
                self.assertNotIn("assets/kits/desktop.ini", names)
                self.assertNotIn("assets/kits/Thumbs.db", names)
            # CONTROL: big assets warn (allowed), a bad name and a third folder level are refused
            with open(os.path.join(md.dir, "assets", "big.bin"), "wb") as f:
                f.write(b"\0" * (ap.ASSETS_WARN + 1))
            r, _ = ap.validate_dir(md.dir)
            self.assertTrue(r.ok, r.errors)
            self.assertTrue(any("assets/ is" in w for w in r.warnings), r.warnings)
            os.remove(os.path.join(md.dir, "assets", "big.bin"))
            os.makedirs(os.path.join(md.dir, "assets", "a", "b", "c"))
            with open(os.path.join(md.dir, "assets", "a", "b", "c", "x.wav"), "wb") as f:
                f.write(b"x")
            with open(os.path.join(md.dir, "assets", "has space.wav"), "wb") as f:
                f.write(b"x")
            r, _ = ap.validate_dir(md.dir)
            self.assertFalse(r.ok)
            self.assertEqual(len(r.errors), 2, r.errors)
            self.assertTrue(any("has space.wav: rename it" in e for e in r.errors), r.errors)
            self.assertTrue(any("x.wav: assets/ can hold folders two levels" in e for e in r.errors))
            # FAT32 on the stick: names that differ only in case are one file there
            r = ap.Report()
            ap.check_assets([("assets/Kick.wav", 1), ("assets/kick.wav", 1)], r)
            self.assertTrue(any("differ only in case" in e for e in r.errors), r.errors)
            r = ap.Report()
            ap.check_assets([("assets/kick.wav", 1), ("assets/kick2.wav", 1)], r)   # CONTROL
            self.assertTrue(r.ok, r.errors)

    def test_template_pins_this_sdk_version(self):
        with open(os.path.join(TEMPLATE, ".github", "workflows", "release.yml")) as f:
            wf = f.read()
        self.assertIn(f'SDK_REF: "v{ap.SDK_VERSION}"', wf)   # CI builds with the SDK the docs describe

    def test_id_mismatch_warns(self):
        r = ap.Report()
        ap.check_id_in_library(fixture("good.so"), "good-mod", r)    # its real id
        self.assertEqual(r.warnings, [])                                          # CONTROL
        ap.check_id_in_library(fixture("good.so"), "someone-else", r)
        self.assertTrue(any("MOD_ID" in w for w in r.warnings), r.warnings)

    def test_placeholders_warn(self):
        m = manifest()
        r = ap.Report(); ap.check_placeholders(m, b"x", r)
        self.assertEqual(r.warnings, [])                                          # CONTROL: the template
        m.update(id="my-mod", summary=ap.PLACEHOLDER_SUMMARY, description=ap.PLACEHOLDER_DESCRIPTION)
        with open(os.path.join(TEMPLATE, "icon.png"), "rb") as f:
            icon = f.read()
        r = ap.Report(); ap.check_placeholders(m, icon, r)
        self.assertEqual(len(r.warnings), 3, r.warnings)
        self.assertTrue(r.ok)                                                     # reminders, not refusals

    def test_long_description_warns(self):
        m = manifest(); m["description"] = "a" * ap.DECK_DESCRIPTION_BYTES
        r = ap.Report(); ap.check_placeholders(m, b"x", r)
        self.assertEqual(r.warnings, [])                                          # CONTROL: exactly the deck's limit
        m["description"] = "\u00e9" * 300                                         # 300 characters, 600 bytes
        r = ap.Report(); ap.check_placeholders(m, b"x", r)
        self.assertEqual(len(r.warnings), 1, r.warnings)
        self.assertTrue(r.ok)

    def test_manifest_key_limit(self):
        m = manifest()
        for i in range(ap.MAX_MANIFEST_KEYS - len(m)):
            m[f"x{i}"] = "y"
        r = ap.Report(); ap.check_manifest_bytes(json.dumps(m).encode(), r)
        self.assertTrue(r.ok, r.errors)                                           # CONTROL: exactly the limit
        m["one_more"] = "y"
        r = ap.Report(); ap.check_manifest_bytes(json.dumps(m).encode(), r)
        self.assertTrue(any("keys" in e for e in r.errors), r.errors)

    def test_manifest_must_be_flat(self):
        good = json.dumps(manifest()).encode()
        r = ap.Report(); ap.check_manifest_bytes(good, r)
        self.assertTrue(r.ok, r.errors)                                            # CONTROL
        nested = good[:-1] + b', "x": {"firmware": ["ep147-0000000000000000"], "hooks": true}}'
        r = ap.Report(); ap.check_manifest_bytes(nested, r)
        self.assertTrue(any("nested object" in e for e in r.errors), r.errors)
        dup = good[:-1] + b', "hooks": true}'
        r = ap.Report(); ap.check_manifest_bytes(dup, r)
        self.assertTrue(any("repeats the key" in e for e in r.errors), r.errors)

    def test_icon_made_deck_safe(self):
        import zlib as z
        def chunk(tp, d): return struct.pack(">I", len(d)) + tp + d + struct.pack(">I", z.crc32(tp + d) & 0xffffffff)
        def png(interlace=0, extra=b""):
            raw = b"".join(b"\0" + b"\x80" * 1024 for _ in range(256))
            return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 256, 256, 8, 6, 0, 0, interlace))
                    + extra + chunk(b"IDAT", z.compress(raw)) + chunk(b"IEND", b""))
        plain = png()
        self.assertEqual(ap.icon_for_deck(plain), (plain, None))                    # CONTROL: untouched
        meta = png(extra=chunk(b"iCCP", b"p\0\0" + z.compress(b"x" * 50)) + chunk(b"tEXt", b"Software\0ed"))
        fixed, why = ap.icon_for_deck(meta)
        self.assertIsNone(why)
        self.assertEqual(fixed, plain)                                              # metadata gone, picture same
        r = ap.Report(); ap.check_icon(meta, r); self.assertTrue(r.ok, r.errors)    # fine in a mod folder
        r = ap.Report(); ap.check_icon(meta, r, strict=True); self.assertFalse(r.ok)  # not inside a package
        r = ap.Report(); ap.check_icon(png(interlace=1), r)
        self.assertTrue(any("interlacing" in e for e in r.errors), r.errors)

    def test_index_entry_assets_kb(self):
        m = {"id": "abc", "name": "A", "version": "1.0.0", "author": "x", "summary": "s",
             "firmware": ["any"], "hooks": False, "source": "https://example.com/s"}
        self.assertNotIn("assets_kb", ap.make_entry(m, "0" * 64, 1, "https://e/p", "https://e/i"))   # CONTROL
        e = ap.make_entry(m, "0" * 64, 1, "https://e/p", "https://e/i", assets_bytes=1500 * 1024 + 1)
        self.assertEqual(e["assets_kb"], 1501)
        r = ap.Report(); ap.check_repo_entry(dict(e, assets_kb="big"), r, "e"); self.assertFalse(r.ok)

    def test_reserved_ids(self):
        self.assertTrue(ap.ID_RE.match("my-mod"))                                   # CONTROL
        for bad in ("enabled", "azmod", "azstem", "az-mods", "azmods"):
            self.assertFalse(ap.ID_RE.match(bad), bad)
        self.assertTrue(ap.ID_RE.match("enabled-plus"))                             # only the exact names

    def test_settings_partition_path_warns(self):
        r = ap.Report()
        ap.check_elf(fixture("good.so"), r)
        self.assertFalse(any("/home/root/settings" in w for w in r.warnings))   # CONTROL
        r = ap.Report()
        ap.check_elf(fixture("good.so") + b"\0/home/root/settings/my.dat\0", r)
        self.assertTrue(any("/home/root/settings" in w for w in r.warnings), r.warnings)
        self.assertTrue(r.ok, r.errors)                                         # a warning only

    def test_package_refuses_a_bad_mod(self):
        with ModDir(so="export-read.so") as md:
            rc, text = run(["package", md.dir, "--out", os.path.join(md.td, "dist")])
            self.assertEqual(rc, 1)
            self.assertIn("exports read", text)
            self.assertFalse(os.path.exists(os.path.join(md.td, "dist")))

    def test_package_refuses_bad_manifest(self):
        def no_source(m):
            del m["source"]
        with ModDir(mutate=no_source) as md:
            rc, text = run(["package", md.dir, "--out", os.path.join(md.td, "dist")])
            self.assertEqual(rc, 1)

    def _write_zip(self, path, members):
        with zipfile.ZipFile(path, "w") as z:
            for n, d in members.items():
                z.writestr(n, d)

    def _members(self):
        with open(os.path.join(TEMPLATE, "azmod.json"), "rb") as f:
            mj = f.read()
        with open(os.path.join(TEMPLATE, "icon.png"), "rb") as f:
            icon = f.read()
        return {"azmod.json": mj, "hello-az.so": fixture("good.so"), "icon.png": icon}

    def test_package_members(self):
        with tempfile.TemporaryDirectory() as td:
            p = os.path.join(td, "x.azmod")
            if ap.PACKAGE_FORMAT == "zip":
                self._write_zip(p, self._members())
                r, _ = ap.validate_package(p)
                self.assertTrue(r.ok, r.errors)
                for extra in ("../evil.sh", "run.sh", "shots/a.txt", "/abs.png"):
                    mem = self._members()
                    mem[extra] = b"x"
                    self._write_zip(p, mem)
                    r, _ = ap.validate_package(p)
                    self.assertFalse(r.ok, extra)
                for extra in ("data/save.txt", "Data/x.bin"):   # data is made on the USB, never shipped
                    mem = self._members()
                    mem[extra] = b"x"
                    self._write_zip(p, mem)
                    r, _ = ap.validate_package(p)
                    self.assertFalse(r.ok, extra)
                    self.assertTrue(any("never carries data/" in e for e in r.errors), r.errors)
                mem = self._members()
                del mem["hello-az.so"]
                self._write_zip(p, mem)
                self.assertFalse(ap.validate_package(p)[0].ok)
                mem = self._members()
                mem["hello-az.so"] = fixture("export-sqlite.so")
                self._write_zip(p, mem)
                self.assertFalse(ap.validate_package(p)[0].ok)

    def test_wrong_container_format_refused(self):
        with tempfile.TemporaryDirectory() as td:
            p = os.path.join(td, "x.azmod")
            other = "tar.gz" if ap.PACKAGE_FORMAT == "zip" else "zip"
            if other == "tar.gz":
                with tarfile.open(p, "w:gz") as t:
                    for n, d in self._members().items():
                        ti = tarfile.TarInfo(n)
                        ti.size = len(d)
                        t.addfile(ti, io.BytesIO(d))
            else:
                self._write_zip(p, self._members())
            r, _ = ap.validate_package(p)
            self.assertTrue(any(f"packages are {ap.PACKAGE_FORMAT}" in e for e in r.errors),
                            r.errors)
            with open(p, "wb") as f:
                f.write(b"not a package")
            self.assertFalse(ap.validate_package(p)[0].ok)


def sample_entry(**kw):
    e = {"id": "beat-counter", "name": "Beat Counter", "version": "1.0.0",
         "author": "someone", "summary": "Counts beats.",
         "icon": "https://example.com/i.png", "package": "https://example.com/p.azmod",
         "sha256": "0" * 64, "size": 1234, "firmware": ["ep147-0123456789abcdef"],
         "hooks": True, "source": "https://github.com/someone/az-beat-counter",
         "updated": "2026-10-08"}
    e.update(kw)
    return e


class RepoIndex(unittest.TestCase):
    def test_add_create_insert_update(self):
        with tempfile.TemporaryDirectory() as td:
            idx = os.path.join(td, "azmods-repo.json")
            ep = os.path.join(td, "e.json")
            with open(ep, "w") as f:
                json.dump(sample_entry(), f)
            rc, text = run(["repo", "add", idx, ep])
            self.assertEqual(rc, 1)                      # no metadata to create it
            rc, text = run(["repo", "add", idx, ep, "--name", "Someone's mods",
                            "--maintainer", "someone",
                            "--homepage", "https://github.com/someone/az-mods"])
            self.assertEqual(rc, 0, text)
            self.assertIn("added", text)
            with open(ep, "w") as f:
                json.dump(sample_entry(id="aaa-first"), f)
            self.assertEqual(run(["repo", "add", idx, ep])[0], 0)
            with open(ep, "w") as f:
                json.dump(sample_entry(version="1.1.0"), f)
            rc, text = run(["repo", "add", idx, ep])
            self.assertIn("updated", text)
            with open(idx) as f:
                data = json.load(f)
            self.assertEqual([m["id"] for m in data["mods"]], ["aaa-first", "beat-counter"])
            self.assertEqual(data["mods"][1]["version"], "1.1.0")
            r = ap.Report()
            ap.check_repo_index(data, r)
            self.assertTrue(r.ok, r.errors)
            rc, text = run(["repo", "check", idx])
            self.assertEqual(rc, 0, text)

    def test_add_refuses_bad_entry(self):
        with tempfile.TemporaryDirectory() as td:
            idx = os.path.join(td, "azmods-repo.json")
            ep = os.path.join(td, "e.json")
            with open(ep, "w") as f:
                json.dump(sample_entry(sha256="xyz"), f)
            rc, _ = run(["repo", "add", idx, ep, "--name", "n", "--maintainer", "m",
                         "--homepage", "https://example.com"])
            self.assertEqual(rc, 1)
            self.assertFalse(os.path.exists(idx))

    def test_entry_rules(self):
        cases = [dict(package="http://x.com/p"), dict(size=0), dict(size="12"),
                 dict(updated="Oct 8"), dict(firmware=["any"], hooks=True), dict(id="X"),
                 dict(version="1.2"), dict(version="1.0.0-beta.1"), dict(min_core="2.0")]
        for kw in cases:
            r = ap.Report()
            ap.check_repo_entry(sample_entry(**kw), r, "e")
            self.assertFalse(r.ok, kw)
        r = ap.Report()
        ap.check_repo_entry(sample_entry(), r, "e")
        self.assertTrue(r.ok, r.errors)

    def test_duplicate_ids(self):
        idx = {"azmods_repo": 1, "name": "n", "maintainer": "m",
               "homepage": "https://example.com", "mods": [sample_entry(), sample_entry()]}
        r = ap.Report()
        ap.check_repo_index(idx, r)
        self.assertTrue(any("listed twice" in e for e in r.errors))

    def test_repo_index_url(self):
        u = "https://raw.githubusercontent.com/someone/az-mods/HEAD/azmods-repo.json"
        self.assertEqual(ap.repo_index_url("someone/az-mods"), u)
        self.assertEqual(ap.repo_index_url("https://github.com/someone/az-mods"), u)
        self.assertEqual(ap.repo_index_url("https://github.com/someone/az-mods.git"), u)
        self.assertEqual(ap.repo_index_url("https://example.com/x/azmods-repo.json"),
                         "https://example.com/x/azmods-repo.json")
        self.assertIsNone(ap.repo_index_url("not a repo"))


class RepoCheckDownload(unittest.TestCase):
    """The path the Mod Index CI and a deck take: fetch a repository file, download each
    package, check sha256 and size, validate. The network is replaced by a dict."""

    BASE = "https://example.com/rel/"

    def setUp(self):
        self.real_fetch = ap._fetch
        self.web = {}

        def fake(url, limit):
            if url not in self.web:
                raise OSError("HTTP Error 404: Not Found")
            return self.web[url]
        ap._fetch = fake
        self.td = tempfile.mkdtemp()
        with ModDir() as md:
            rc, text = run(["package", md.dir, "--out", self.td,
                            "--package-url", self.BASE + "hello-az-1.0.0.azmod"])
            self.assertEqual(rc, 0, text)
        with open(os.path.join(self.td, "hello-az-1.0.0.entry.json")) as f:
            self.entry = json.load(f)
        with open(os.path.join(self.td, "hello-az-1.0.0.azmod"), "rb") as f:
            self.pkg = f.read()
        with open(os.path.join(TEMPLATE, "icon.png"), "rb") as f:
            self.web[self.entry["icon"]] = f.read()
        self.web[self.entry["package"]] = self.pkg
        self.web[self.entry["source"]] = b"<html>source</html>"

    def tearDown(self):
        ap._fetch = self.real_fetch
        shutil.rmtree(self.td)

    def check(self, entry=None):
        idx = {"azmods_repo": 1, "name": "n", "maintainer": "m",
               "homepage": "https://example.com", "mods": [entry or self.entry]}
        url = "https://raw.githubusercontent.com/someone/az-mods/HEAD/azmods-repo.json"
        self.web[url] = json.dumps(idx).encode()
        r = ap.Report()
        ap.check_repo("someone/az-mods", True, r)
        return r

    def test_good_repo(self):
        r = self.check()
        self.assertTrue(r.ok, r.errors)

    def test_sha_mismatch(self):
        r = self.check(dict(self.entry, sha256="f" * 64))
        self.assertTrue(any("sha256 does not match" in e for e in r.errors), r.errors)

    def test_size_mismatch(self):
        r = self.check(dict(self.entry, size=self.entry["size"] + 1))
        self.assertTrue(any("the repository file says" in e for e in r.errors), r.errors)

    def test_index_disagrees_with_package(self):
        r = self.check(dict(self.entry, version="9.9.9"))
        self.assertTrue(any("differs from the package" in e for e in r.errors), r.errors)

    def test_bad_package_content(self):
        bad = self.pkg.replace(b"az_mod_v1_entry", b"az_mod_v1_entrx")
        self.web[self.entry["package"]] = bad
        import hashlib as h
        r = self.check(dict(self.entry, sha256=h.sha256(bad).hexdigest()))
        self.assertFalse(r.ok)            # the zip's own CRC catches the tampering
        # A well-formed package whose library breaks a rule, with a matching hash:
        p = os.path.join(self.td, "bad.azmod")
        with zipfile.ZipFile(p, "w") as z:
            with zipfile.ZipFile(io.BytesIO(self.pkg)) as src:
                for n in src.namelist():
                    z.writestr(n, fixture("export-read.so") if n.endswith(".so")
                               else src.read(n))
        with open(p, "rb") as f:
            bad = f.read()
        self.web[self.entry["package"]] = bad
        r = self.check(dict(self.entry, sha256=h.sha256(bad).hexdigest(), size=len(bad)))
        self.assertTrue(any("exports read" in e for e in r.errors), r.errors)

    def test_missing_package_and_source(self):
        del self.web[self.entry["package"]]
        del self.web[self.entry["source"]]
        r = self.check()
        self.assertTrue(any("cannot download" in e for e in r.errors), r.errors)
        self.assertTrue(any("not publicly reachable" in e for e in r.errors), r.errors)

    def test_list_check(self):
        self.check()                     # puts the repository file on the fake web
        lp = os.path.join(self.td, "azmods-repos.json")
        with open(lp, "w") as f:
            json.dump({"azmods_repos": 1, "repos": ["https://github.com/someone/az-mods"]}, f)
        rc, text = run(["list", "check", lp, "--download"])
        self.assertEqual(rc, 0, text)
        with open(lp, "w") as f:
            json.dump({"azmods_repos": 1, "repos": ["https://github.com/nobody/none"]}, f)
        rc, text = run(["list", "check", lp, "--download"])
        self.assertEqual(rc, 1, text)

    def test_list_check_refuses_an_id_two_repositories_use(self):
        """Two listed repositories publishing the same mod id: both valid on their own, refused
        together (a deck can install only one mod per id)."""
        self.check()
        mine = "https://raw.githubusercontent.com/someone/az-mods/HEAD/azmods-repo.json"
        other = "https://raw.githubusercontent.com/other/mods/HEAD/azmods-repo.json"
        self.web[other] = self.web[mine]
        lp = os.path.join(self.td, "azmods-repos.json")
        with open(lp, "w") as f:
            json.dump({"azmods_repos": 1, "repos": ["https://github.com/other/mods",
                                                    "https://github.com/someone/az-mods"]}, f)
        rc, text = run(["list", "check", lp])
        self.assertEqual(rc, 1, text)
        self.assertIn('mod id "hello-az" is used by more than one repository', text)
        idx = json.loads(self.web[mine])
        idx["mods"][0] = dict(idx["mods"][0], id="Hello-AZ")       # compared without regard to case
        self.web[other] = json.dumps(idx).encode()
        rc, text = run(["list", "check", lp])
        self.assertIn("is used by more than one repository", text)
        idx["mods"][0] = dict(idx["mods"][0], id="other-hello")    # CONTROL: a distinct id passes
        self.web[other] = json.dumps(idx).encode()
        rc, text = run(["list", "check", lp])
        self.assertEqual(rc, 0, text)
        self.assertIn("several mods are named", text)              # same name, different ids: a warning


class NewMod(unittest.TestCase):
    def test_new_from_template(self):
        with tempfile.TemporaryDirectory() as td:
            d = os.path.join(td, "beat-counter")
            rc, text = run(["new", d, "--id", "beat-counter", "--author", "someone"])
            self.assertEqual(rc, 0, text)
            self.assertTrue(os.path.isfile(os.path.join(d, "beat-counter.c")))
            self.assertFalse(os.path.exists(os.path.join(d, "hello-az.c")))
            with open(os.path.join(d, "beat-counter.c")) as f:
                self.assertIn('#define MOD_ID "beat-counter"', f.read())
            with open(os.path.join(d, "Makefile")) as f:
                self.assertIn("MOD_ID          := beat-counter", f.read())
            r = ap.Report()
            with open(os.path.join(d, "azmod.json")) as f:
                m = json.load(f)
            ap.check_manifest(m, r)
            self.assertTrue(r.ok, r.errors)
            self.assertEqual((m["id"], m["name"]), ("beat-counter", "Beat Counter"))
            # LF and UTF-8 on every system (Windows text mode would write CRLF and cp1252)
            for rel in ("azmod.json", "Makefile", "beat-counter.c", "README.md"):
                with open(os.path.join(d, rel), "rb") as f:
                    self.assertNotIn(b"\r", f.read(), rel)
            e = os.path.join(td, "e-mod")
            self.assertEqual(run(["new", e, "--id", "e-mod", "--author", "s\u00e9bastien"])[0], 0)
            self.assertEqual(ap._load_json(os.path.join(e, "azmod.json"))["author"], "s\u00e9bastien")
            self.assertEqual(run(["new", d, "--id", "beat-counter"])[0], 1)   # exists
            self.assertEqual(run(["new", d + "x", "--id", "Bad_Id"])[0], 1)


class CommunityList(unittest.TestCase):
    def check(self, lst):
        r = ap.Report()
        ap.check_repo_list(lst, r)
        return r

    def test_empty_list_is_valid(self):
        self.assertTrue(self.check({"azmods_repos": 1, "repos": []}).ok)

    def test_list_rules(self):
        self.assertTrue(self.check({"azmods_repos": 1,
                                    "repos": ["https://github.com/a/b"]}).ok)
        self.assertFalse(self.check({"repos": []}).ok)
        self.assertFalse(self.check({"azmods_repos": 1, "repos": ["a/b"]}).ok)
        self.assertFalse(self.check({"azmods_repos": 1,
                                     "repos": ["https://github.com/a/b",
                                               "https://github.com/A/B"]}).ok)
        self.assertFalse(self.check([]).ok)


if __name__ == "__main__":
    unittest.main()
