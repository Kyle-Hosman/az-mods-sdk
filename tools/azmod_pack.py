#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""azmod-pack: build, validate and package mods for AZ-MODS.

    azmod-pack new      DIR --id ID [--name NAME] start a mod from the template
    azmod-pack build    [MODDIR] [-- MAKE ARGS]  build in the SDK container, then validate
    azmod-pack image                             build (or rebuild) the SDK container
    azmod-pack validate MODDIR | FILE.azmod      check manifest, library and icon
    azmod-pack package  [MODDIR] [--github OWNER/REPO | --package-url URL] [--tag TAG] [--out DIR]
    azmod-pack repo add   REPO.json ENTRY.json [--name ... --maintainer ... --homepage ...]
    azmod-pack repo check REPO.json|OWNER/REPO|URL [--download]
    azmod-pack list check AZMODS-REPOS.json [--download]

Python 3.8+, standard library only. `validate` runs every check the deck's core runs
before it loads a mod, plus stricter ones (only one export, allowed libraries, symbol
versions) that the core leaves to dlopen. A mod that passes here is not refused on the
deck for any of these reasons. See docs/GUIDELINES.md for why each rule exists.
"""
import argparse
import datetime
import hashlib
import io
import json
import os
import re
import shutil
import struct
import shlex
import ssl
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
import zlib

SDK_VERSION = "1.2.3"   # = the AZ-MODS release whose core it matches; the template pins v<this>
ABI = 1

# The package container format. One place to change it.
PACKAGE_FORMAT = "zip"            # "zip" or "tar.gz"

# ---- the deck's runtime, measured on the deck's own root filesystem -------------------
# The highest symbol version the deck provides for each versioned library. A mod that
# needs anything newer fails to load ("version not found") inside EP147.
VERSION_CEILINGS = {
    "GLIBC": (2, 29),       # glibc 2.29
    "GLIBCXX": (3, 4, 25),  # libstdc++.so.6.0.25 (GCC 8)
    "CXXABI": (1, 3, 11),
    "GCC": (7, 0, 0),       # libgcc_s.so.1
}

# Libraries a mod may depend on (DT_NEEDED). Everything else is refused: either the deck
# does not have it, or (X11, Xext, FreeType, ALSA) the core owns it and a mod must go
# through the core's services instead.
ALLOWED_NEEDED = {
    "libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1",
    "libgcc_s.so.1", "libstdc++.so.6", "ld-linux-aarch64.so.1",
}

ENTRY_SYMBOL = "az_mod_v1_entry"

# Symbols the GNU toolchain exports from every shared library. Harmless: nothing looks
# them up by name across libraries.
BOILERPLATE_EXPORTS = {
    "_init", "_fini", "_edata", "_end", "__end__", "__bss_start", "__bss_start__",
    "_bss_end__", "__bss_end__",
}

# Names the core interposes (or must stay the only owner of). A mod exporting one of
# these could take over the deck's I/O for the whole process.
FORBIDDEN_EXPORTS = {"read", "write", "open", "open64", "openat", "close", "ioctl", "recv",
                     "recvfrom", "recvmsg", "sendmsg", "select", "poll", "ppoll", "mmap",
                     "mprotect", "dlopen", "dlsym", "fopen"}
FORBIDDEN_EXPORT_PREFIXES = ("sqlite3_",)

MAX_SO_BYTES = 16 * 1024 * 1024
# assets/: files a mod ships beside its code (samples, images, presets). They stay on the USB
# stick and are read through the core's assets_dir(); the deck never copies them to its own
# storage. Same limits as the deck's installer: 256 files and 16 MB unpacked for the whole
# package, safe names, at most two folders inside assets/.
MAX_PKG_FILES = 256
MAX_UNPACKED = 16 * 1024 * 1024
ASSETS_WARN = 4 * 1024 * 1024
_ASSET_NAME = re.compile(r"^assets/(?:[A-Za-z0-9_-][A-Za-z0-9._-]*/){0,2}[A-Za-z0-9_-][A-Za-z0-9._-]*$")


def check_assets(names_sizes, report):
    """names_sizes: [(member name, size)] for assets/... Refusals for what the deck would refuse,
    a warning when the assets get big."""
    total, seen = 0, {}
    for name, size in names_sizes:
        if len(name) > 128:
            report.error(f"{name}: the path is {len(name)} characters; the deck takes up to 128")
        elif name.count("/") > 3:
            report.error(f"{name}: assets/ can hold folders two levels deep; move this file up a level")
        elif not _ASSET_NAME.match(name):
            report.error(f"{name}: rename it using letters, digits, '.', '_' and '-' (no spaces), "
                         "not starting with '.'")
        # the USB stick is FAT32, where Kick.wav and kick.wav are the same file
        if name.lower() in seen:
            report.error(f"{name} and {seen[name.lower()]} differ only in case; on the USB stick they "
                         "would be one file. Rename one of them")
        seen.setdefault(name.lower(), name)
        total += size
    if total > ASSETS_WARN:
        report.warn(f"assets/ is {total // 1024} KB: fine on the USB stick, but DJs installing from the deck download it "
                    "over WiFi. Consider fetching big or optional files "
                    "into data_dir() from your mod instead")
    return total
MAX_MANIFEST_BYTES = 16383        # the core reads at most this much of azmod.json
MAX_MANIFEST_KEYS = 48            # and remembers this many keys (it refuses a file with more)
MAX_PACKAGE_BYTES = 32 * 1024 * 1024
ICON_SIZE = (256, 256)

# a mod id: 3-32 of a-z 0-9 -, and not a name the community folder or AZ-MODS itself uses
# (the same list in the deck core and fetcher)
ID_RE = re.compile(r"^(?!(?:enabled|azmod|azstem|az-mods|azmods)$)[a-z0-9-]{3,32}$")
# version and min_core: MAJOR.MINOR.PATCH, numbers only, each 0-9999. The deck compares
# exactly that (an update is offered only for a higher number); a suffix such as -beta.1
# is refused there, so it is refused here.
SEMVER_RE = re.compile(r"^(0|[1-9]\d{0,3})\.(0|[1-9]\d{0,3})\.(0|[1-9]\d{0,3})$")
FW_TAG_RE = re.compile(r"^ep147-[0-9a-f]{16}$")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
CATEGORIES = {"visual", "controls", "audio", "tools"}

DEFAULT_IMAGE = "az-mods-sdk:1"
SDK_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Report:
    """Collects errors (refusals) and warnings (allowed, but worth fixing)."""

    def __init__(self):
        self.errors = []
        self.warnings = []

    def error(self, msg):
        self.errors.append(msg)

    def warn(self, msg):
        self.warnings.append(msg)

    def extend(self, other, prefix=""):
        self.errors += [prefix + e for e in other.errors]
        self.warnings += [prefix + w for w in other.warnings]

    @property
    def ok(self):
        return not self.errors

    def print(self, what, out=None):
        out = out or sys.stdout
        for w in self.warnings:
            print(f"  warning: {w}", file=out)
        for e in self.errors:
            print(f"  REFUSED: {e}", file=out)
        print(f"{what}: {'OK' if self.ok else 'REFUSED'}", file=out)


# =========================================================================================
# ELF checks
# =========================================================================================

class ElfError(Exception):
    pass


def _vtuple(s):
    return tuple(int(x) for x in s.split("."))


class Elf:
    """A minimal ELF64 little-endian reader that sees what the dynamic loader sees: it
    reads the program headers and PT_DYNAMIC, never the (optional, forgeable) section
    headers."""

    PT_LOAD, PT_DYNAMIC = 1, 2
    DT_NULL, DT_NEEDED, DT_HASH, DT_STRTAB, DT_SYMTAB = 0, 1, 4, 5, 6
    DT_STRSZ, DT_SYMENT, DT_RPATH, DT_TEXTREL = 10, 11, 15, 22
    DT_INIT_ARRAYSZ, DT_RUNPATH, DT_FLAGS = 27, 29, 30
    DT_GNU_HASH, DT_VERNEED, DT_VERNEEDNUM = 0x6FFFFEF5, 0x6FFFFFFE, 0x6FFFFFFF

    def __init__(self, data):
        self.d = data
        if len(data) < 64 or data[:4] != b"\x7fELF":
            raise ElfError("not an ELF file")
        self.ei_class, self.ei_data = data[4], data[5]
        if self.ei_class != 2:
            raise ElfError("not a 64-bit ELF (the deck is 64-bit aarch64)")
        if self.ei_data != 1:
            raise ElfError("not little-endian")
        (self.e_type, self.e_machine, _ver, self.e_entry, self.e_phoff, _shoff, _flags,
         _ehsize, self.e_phentsize, self.e_phnum) = struct.unpack_from("<HHIQQQIHHH", data, 16)
        self.phdrs = []
        if self.e_phentsize != 56 or self.e_phoff + 56 * self.e_phnum > len(data):
            raise ElfError("program headers are missing or truncated")
        for i in range(self.e_phnum):
            p_type, p_flags, p_off, p_vaddr, _pa, p_filesz, p_memsz, _al = struct.unpack_from(
                "<IIQQQQQQ", data, self.e_phoff + 56 * i)
            self.phdrs.append((p_type, p_off, p_vaddr, p_filesz, p_memsz))
            # The loader maps p_filesz bytes from p_off. A segment that runs past the end
            # of the file faults (SIGBUS) when touched: inside EP147, that is a crash.
            if p_type in (self.PT_LOAD, self.PT_DYNAMIC) and p_off + p_filesz > len(data):
                raise ElfError("the file is truncated (a segment runs past its end)")
        self._dyn = None

    def off(self, vaddr, need=1):
        for p_type, p_off, p_vaddr, p_filesz, _ in self.phdrs:
            if p_type == self.PT_LOAD and p_vaddr <= vaddr and vaddr + need <= p_vaddr + p_filesz:
                o = p_off + (vaddr - p_vaddr)
                if o + need <= len(self.d):
                    return o
        raise ElfError(f"address 0x{vaddr:x} is outside the file")

    def dynamic(self):
        if self._dyn is not None:
            return self._dyn
        dyn = [p for p in self.phdrs if p[0] == self.PT_DYNAMIC]
        if not dyn:
            raise ElfError("no dynamic section (a mod must be a shared library)")
        _, p_off, _, p_filesz, _ = dyn[0]
        if p_off + p_filesz > len(self.d):
            raise ElfError("dynamic section is truncated")
        out = []
        for o in range(p_off, p_off + p_filesz - 15, 16):
            tag, val = struct.unpack_from("<qQ", self.d, o)
            if tag == self.DT_NULL:
                break
            out.append((tag, val))
        self._dyn = out
        return out

    def dget(self, tag, default=None):
        for t, v in self.dynamic():
            if t == tag:
                return v
        return default

    def dall(self, tag):
        return [v for t, v in self.dynamic() if t == tag]

    def string(self, idx):
        strtab = self.dget(self.DT_STRTAB)
        strsz = self.dget(self.DT_STRSZ)
        if strtab is None or strsz is None or idx >= strsz:
            raise ElfError("bad string table reference")
        o = self.off(strtab + idx)
        end = self.d.find(b"\0", o, o + (strsz - idx))
        if end < 0:
            raise ElfError("unterminated string in the string table")
        return self.d[o:end].decode("utf-8", "replace")

    def needed(self):
        return [self.string(v) for v in self.dall(self.DT_NEEDED)]

    def _symcount(self):
        h = self.dget(self.DT_HASH)
        if h is not None:
            return struct.unpack_from("<I", self.d, self.off(h + 4, 4))[0]
        gh = self.dget(self.DT_GNU_HASH)
        if gh is None:
            raise ElfError("no symbol hash table")
        o = self.off(gh, 16)
        nbuckets, symoffset, bloom_size, _shift = struct.unpack_from("<IIII", self.d, o)
        if nbuckets > 1 << 20 or bloom_size > 1 << 20:
            raise ElfError("implausible symbol hash table")
        b_off = o + 16 + 8 * bloom_size
        buckets = struct.unpack_from(f"<{nbuckets}I", self.d, b_off) if nbuckets else ()
        last = max(buckets) if buckets else 0
        if last < symoffset:
            return symoffset
        c_off = b_off + 4 * nbuckets
        idx = last
        for _ in range(1 << 20):
            (chain,) = struct.unpack_from("<I", self.d, c_off + 4 * (idx - symoffset))
            if chain & 1:
                return idx + 1
            idx += 1
        raise ElfError("symbol hash chain does not terminate")

    def dynsyms(self):
        """[(name, bind, type, vis, defined)]"""
        symtab = self.dget(self.DT_SYMTAB)
        if symtab is None:
            raise ElfError("no dynamic symbol table")
        n = self._symcount()
        if n > 1 << 18:
            raise ElfError("implausible dynamic symbol count")
        out = []
        base = self.off(symtab, 24 * n) if n else 0
        for i in range(n):
            st_name, st_info, st_other, st_shndx, _v, _s = struct.unpack_from(
                "<IBBHQQ", self.d, base + 24 * i)
            if i == 0:
                continue
            out.append((self.string(st_name), st_info >> 4, st_info & 0xF, st_other & 3,
                        st_shndx != 0))
        return out

    def exports(self):
        # bind: GLOBAL=1, WEAK=2, GNU_UNIQUE=10; visibility DEFAULT=0, PROTECTED=3
        return [(name, typ) for name, bind, typ, vis, defined in self.dynsyms()
                if defined and bind in (1, 2, 10) and vis in (0, 3)]

    def version_needs(self):
        """[(library, version_name)] from DT_VERNEED."""
        vn = self.dget(self.DT_VERNEED)
        num = self.dget(self.DT_VERNEEDNUM, 0)
        out = []
        if vn is None:
            return out
        o = self.off(vn, 16)
        for _ in range(min(num, 64)):
            _ver, cnt, file_, aux, nxt = struct.unpack_from("<HHIII", self.d, o)
            lib = self.string(file_)
            a = o + aux
            for _ in range(min(cnt, 256)):
                _h, _f, _other, name, anext = struct.unpack_from("<IHHII", self.d, a)
                out.append((lib, self.string(name)))
                if not anext:
                    break
                a += anext
            if not nxt:
                break
            o += nxt
        return out


def check_elf(data, report):
    """Refusals and warnings for one mod library, as bytes."""
    if len(data) > MAX_SO_BYTES:
        report.error(f"library is {len(data)} bytes; the limit is {MAX_SO_BYTES}")
        return
    if b"/home/root/settings" in data:
        report.warn("the library names /home/root/settings, the deck's settings partition. It is "
                    "small (61 MB) and shared with the deck's own settings, and filling it has broken "
                    "the BPM display before. Use cfg_get/cfg_set for settings and data_dir() (on the "
                    "USB stick) for files")
    try:
        elf = Elf(data)
        if elf.e_machine != 183:
            report.error(f"built for machine type {elf.e_machine}, not aarch64 (183)")
            return
        if elf.e_type != 3:
            report.error("not a shared library (ELF type is not ET_DYN); link with -shared")
            return
        # The core reads the dynamic symbols through the section headers, so a library
        # stripped of them (sstrip and similar) is refused on the deck.
        shoff, = struct.unpack_from("<Q", data, 40)
        shentsize, shnum = struct.unpack_from("<HH", data, 58)
        if not shoff or shentsize != 64 or shoff + 64 * shnum > len(data):
            report.error("the ELF section table is missing or damaged (do not strip it with "
                         "sstrip; plain `strip` is fine)")
            return
        if not any(p[0] == Elf.PT_DYNAMIC for p in elf.phdrs) or elf.dget(Elf.DT_SYMTAB) is None:
            report.error("not a shared library (no dynamic symbols); link with -shared")
            return

        flags = elf.dget(Elf.DT_FLAGS, 0)
        if elf.dget(Elf.DT_TEXTREL) is not None or flags & 0x4:
            report.error("has text relocations; compile every file with -fPIC")

        for lib in elf.needed():
            if lib not in ALLOWED_NEEDED:
                why = ("the core owns the display" if re.match(r"lib(X|freetype|asound)", lib)
                       else "it is not on the deck's list of libraries mods may use")
                report.error(f"depends on {lib}: not allowed ({why})")
        if elf.dget(Elf.DT_RPATH) is not None or elf.dget(Elf.DT_RUNPATH) is not None:
            report.error("has an RPATH/RUNPATH; mods may not load libraries from other paths")

        for lib, ver in elf.version_needs():
            m = re.match(r"^([A-Z]+)_([0-9.]+)$", ver)
            if not m:
                continue
            fam, num = m.group(1), m.group(2)
            ceil = VERSION_CEILINGS.get(fam)
            if ceil is None:
                continue
            try:
                if _vtuple(num) > ceil:
                    report.error(f"needs {ver} from {lib}; the deck has up to "
                                 f"{fam}_{'.'.join(map(str, ceil))}. Build in the SDK container")
            except ValueError:
                pass

        exports = elf.exports()
        names = {n for n, _ in exports}
        entry = [t for n, t in exports if n == ENTRY_SYMBOL]
        if not entry:
            report.error(f"does not export {ENTRY_SYMBOL} (did the source include azmod-mod.h "
                         "and define it?)")
        elif entry[0] != 2:
            report.error(f"{ENTRY_SYMBOL} is exported but is not a function")
        for n in sorted(names):
            if n == ENTRY_SYMBOL or n in BOILERPLATE_EXPORTS:
                continue
            if n in FORBIDDEN_EXPORTS or n.startswith(FORBIDDEN_EXPORT_PREFIXES):
                report.error(f"exports {n}: only the core may define this symbol")
            else:
                report.error(f"exports {n}: only {ENTRY_SYMBOL} may be exported "
                             "(build with -fvisibility=hidden)")

        ctors = elf.dget(Elf.DT_INIT_ARRAYSZ, 0) // 8
        if ctors > 1:
            report.warn(f"has {ctors - 1} constructor(s) beyond the compiler's own; "
                        "do setup in init(), not in a constructor")
    except (ElfError, struct.error) as e:
        report.error(f"cannot read the library: {e}")


# =========================================================================================
# Manifest, icon, repository file
# =========================================================================================

def _is_https(v):
    return isinstance(v, str) and re.match(r"^https://[^\s/]+\.[^\s/]+(/\S*)?$", v) is not None


def _str(report, obj, key, maxlen, required=True, where="azmod.json"):
    v = obj.get(key)
    if v is None:
        if required:
            report.error(f"{where}: missing \"{key}\"")
        return None
    if not isinstance(v, str) or not v.strip():
        report.error(f"{where}: \"{key}\" must be a non-empty string")
        return None
    if len(v) > maxlen:
        report.error(f"{where}: \"{key}\" is {len(v)} characters; the limit is {maxlen}")
    return v


def check_firmware(report, fw, hooks, where):
    if not isinstance(fw, list) or not fw:
        report.error(f"{where}: \"firmware\" must be a non-empty list")
        return
    if "any" in fw:
        if fw != ["any"]:
            report.error(f"{where}: \"firmware\" is either [\"any\"] or a list of ep147 tags, "
                         "not both")
        if hooks is True:
            report.error(f"{where}: a mod for \"any\" firmware may not claim hooks "
                         "(\"hooks\": true needs exact firmware tags)")
        return
    for t in fw:
        if not isinstance(t, str) or not FW_TAG_RE.match(t):
            report.error(f"{where}: firmware tag {t!r} is not of the form ep147-<16 hex>")
    if len(set(fw)) != len(fw):
        report.error(f"{where}: \"firmware\" lists a tag twice")


MANIFEST_KEYS = {"azmod", "id", "name", "version", "author", "summary", "description",
                 "license", "source", "api", "firmware", "hooks", "min_core", "category"}


def check_manifest(m, report):
    w = "azmod.json"
    if not isinstance(m, dict):
        report.error(f"{w}: must be a JSON object")
        return
    if m.get("azmod") != 1:
        report.error(f"{w}: \"azmod\" must be 1")
    i = m.get("id")
    if not isinstance(i, str) or not ID_RE.match(i):
        report.error(f"{w}: \"id\" must be 3 to 32 characters of a-z, 0-9 and -")
    _str(report, m, "name", 47)
    v = _str(report, m, "version", 23)
    if v and not SEMVER_RE.match(v):
        report.error(f"{w}: \"version\" {v!r} is not MAJOR.MINOR.PATCH")
    _str(report, m, "author", 47)
    _str(report, m, "summary", 119)
    _str(report, m, "description", 8000, required=False)
    _str(report, m, "license", 64)
    src = m.get("source")
    if src is None:
        report.error(f"{w}: missing \"source\" (a public URL to the mod's source is required)")
    elif not _is_https(src):
        report.error(f"{w}: \"source\" must be a public https:// URL")
    if m.get("api") != ABI:
        report.error(f"{w}: \"api\" must be {ABI} (this SDK's ABI)")
    hooks = m.get("hooks")
    if not isinstance(hooks, bool):
        report.error(f"{w}: \"hooks\" must be true or false")
    check_firmware(report, m.get("firmware"), hooks, w)
    mc = m.get("min_core")
    if mc is not None and (not isinstance(mc, str) or not SEMVER_RE.match(mc)):
        report.error(f"{w}: \"min_core\" must be MAJOR.MINOR.PATCH")
    cat = m.get("category")
    if cat is not None and cat not in CATEGORIES:
        report.error(f"{w}: \"category\" must be one of {sorted(CATEGORIES)}")
    for k in sorted(set(m) - MANIFEST_KEYS):
        report.warn(f"{w}: unknown key \"{k}\" (ignored by the deck)")


def check_manifest_bytes(raw, report):
    """Checks on the file itself, for the core's small JSON reader."""
    if len(raw) > MAX_MANIFEST_BYTES:
        report.error(f"azmod.json is {len(raw)} bytes; the deck reads at most "
                     f"{MAX_MANIFEST_BYTES}. Shorten the description")
    # the deck reads ONE flat object: no nested objects, no repeated keys (it refuses either)
    dups = []
    def pairs(kv):
        keys = [k for k, _ in kv]
        dups.extend(k for k in set(keys) if keys.count(k) > 1)
        return dict(kv)
    try:
        top = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs)
    except (ValueError, UnicodeDecodeError):
        top = None
    if dups:
        report.error(f"azmod.json repeats the key {sorted(dups)[0]!r}; the deck refuses that")
    if isinstance(top, dict) and len(top) > MAX_MANIFEST_KEYS:
        report.error(f"azmod.json has {len(top)} keys; the deck reads at most {MAX_MANIFEST_KEYS}")
    if isinstance(top, dict) and any(isinstance(v, dict) for v in top.values()):
        report.error("azmod.json holds a nested object; the deck reads one flat object only")
    if isinstance(top, dict) and any(isinstance(v, list) and any(not isinstance(x, str) for x in v)
                                     for v in top.values()):
        report.error("azmod.json: lists may only hold text")
    if b"\\u" in raw:
        report.warn("azmod.json uses \\u escapes, which the deck shows as typed; write the "
                    "characters directly (the file is UTF-8)")


# The deck decodes icons inside its own program, so it accepts only plain PNGs (community_icons.c
# cmi_png_ok): 8-bit RGB, RGBA or palette, not interlaced, at most 512 x 512, and only these
# chunks. Metadata chunks an image editor adds (color profiles, text, EXIF) are left out of the
# package for you: the picture itself is untouched.
ICON_CHUNKS = {b"IHDR", b"PLTE", b"tRNS", b"sRGB", b"gAMA", b"pHYs", b"cHRM", b"IDAT", b"IEND"}


def icon_for_deck(data):
    """(bytes the deck accepts, None) or (None, the reason it cannot be made acceptable)."""
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None, "not a PNG file"
    out, off, ihdr = [data[:8]], 8, False
    while off + 12 <= len(data):
        n = struct.unpack(">I", data[off:off + 4])[0]
        t = data[off + 4:off + 8]
        if off + 12 + n > len(data):
            return None, "the file is cut short or damaged"
        body = data[off + 8:off + 8 + n]
        if (zlib.crc32(t + body) & 0xffffffff) != struct.unpack(">I", data[off + 8 + n:off + 12 + n])[0]:
            return None, f"the {t.decode('latin-1')} chunk is damaged (bad checksum)"
        if t == b"IHDR":
            w, h, depth, ct, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ct not in (2, 3, 6):
                return None, "save it as an 8-bit RGBA (or RGB) PNG; 16-bit and grayscale are not shown"
            if interlace:
                return None, "save it without interlacing (\"progressive\" / \"Adam7\" off)"
            if w > 512 or h > 512:
                return None, "it is larger than 512 x 512"
            ihdr = True
        if t in ICON_CHUNKS:
            out.append(data[off:off + 12 + n])
        if t == b"IEND":
            return (b"".join(out), None) if ihdr else (None, "no image header")
        off += 12 + n
    return None, "the file is cut short (no IEND)"


def check_icon(data, report, name="icon.png", strict=False):
    if data is None:
        report.error(f"{name}: missing (a 256x256 PNG is required)")
        return
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        report.error(f"{name}: not a PNG file")
        return
    w, h = struct.unpack(">II", data[16:24])
    if (w, h) != ICON_SIZE:
        report.error(f"{name}: is {w}x{h}; it must be {ICON_SIZE[0]}x{ICON_SIZE[1]}")
    if len(data) > 512 * 1024:
        report.warn(f"{name}: {len(data)} bytes is large for an icon; aim for under 100 KB")
    deck, why = icon_for_deck(data)
    if why:
        report.error(f"{name}: the deck cannot show it: {why}")
    elif strict and deck != data:
        report.error(f"{name}: holds metadata chunks the deck refuses (package it with azmod-pack)")


REPO_ENTRY_KEYS = {"id", "name", "version", "author", "summary", "icon", "package", "sha256",
                   "size", "firmware", "hooks", "source", "updated", "category", "license",
                   "min_core", "description", "assets_kb"}


def check_repo_entry(e, report, where):
    if not isinstance(e, dict):
        report.error(f"{where}: must be a JSON object")
        return
    i = e.get("id")
    if not isinstance(i, str) or not ID_RE.match(i):
        report.error(f"{where}: \"id\" must be 3 to 32 characters of a-z, 0-9 and -")
    for k, n in (("name", 47), ("author", 47), ("summary", 119)):
        _str(report, e, k, n, where=where)
    _str(report, e, "description", 8000, where=where, required=False)
    if "assets_kb" in e and not (isinstance(e["assets_kb"], int) and not isinstance(e["assets_kb"], bool) and 0 <= e["assets_kb"] <= 1 << 22):
        report.error(f"{where}: \"assets_kb\" must be a whole number of KB")
    v = _str(report, e, "version", 23, where=where)
    if v and not SEMVER_RE.match(v):
        report.error(f"{where}: \"version\" {v!r} is not MAJOR.MINOR.PATCH")
    mc = e.get("min_core")
    if mc is not None and (not isinstance(mc, str) or not SEMVER_RE.match(mc)):
        report.error(f"{where}: \"min_core\" must be MAJOR.MINOR.PATCH (a deck skips the entry otherwise)")
    for k in ("icon", "package", "source"):
        if not _is_https(e.get(k)):
            report.error(f"{where}: \"{k}\" must be an https:// URL")
    if not isinstance(e.get("sha256"), str) or not SHA256_RE.match(e["sha256"]):
        report.error(f"{where}: \"sha256\" must be 64 lowercase hex characters")
    s = e.get("size")
    if not isinstance(s, int) or isinstance(s, bool) or not 0 < s <= MAX_PACKAGE_BYTES:
        report.error(f"{where}: \"size\" must be the package size in bytes "
                     f"(1..{MAX_PACKAGE_BYTES})")
    hooks = e.get("hooks")
    if not isinstance(hooks, bool):
        report.error(f"{where}: \"hooks\" must be true or false")
    check_firmware(report, e.get("firmware"), hooks, where)
    u = e.get("updated")
    if not isinstance(u, str) or not DATE_RE.match(u):
        report.error(f"{where}: \"updated\" must be YYYY-MM-DD")
    cat = e.get("category")
    if cat is not None and cat not in CATEGORIES:
        report.error(f"{where}: \"category\" must be one of {sorted(CATEGORIES)}")
    for k in sorted(set(e) - REPO_ENTRY_KEYS):
        report.warn(f"{where}: unknown key \"{k}\"")


def check_repo_index(idx, report):
    w = "azmods-repo.json"
    if not isinstance(idx, dict):
        report.error(f"{w}: must be a JSON object")
        return
    if idx.get("azmods_repo") != 1:
        report.error(f"{w}: \"azmods_repo\" must be 1")
    _str(report, idx, "name", 64, where=w)
    _str(report, idx, "maintainer", 64, where=w)
    if not _is_https(idx.get("homepage")):
        report.error(f"{w}: \"homepage\" must be an https:// URL")
    mods = idx.get("mods")
    if not isinstance(mods, list):
        report.error(f"{w}: \"mods\" must be a list")
        return
    seen = set()
    for n, e in enumerate(mods):
        where = f"{w}: mods[{n}]" + (f" ({e.get('id')})" if isinstance(e, dict) else "")
        check_repo_entry(e, report, where)
        if isinstance(e, dict):
            if e.get("id") in seen:
                report.error(f"{where}: id is listed twice")
            seen.add(e.get("id"))


# =========================================================================================
# Packages
# =========================================================================================

def _package_members(path):
    """{name: bytes} for a package file, whichever container format it uses."""
    with open(path, "rb") as f:
        head = f.read(4)
    members = {}
    if head[:4] == b"PK\x03\x04":
        fmt = "zip"
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                if info.file_size > MAX_PACKAGE_BYTES:
                    raise ValueError(f"{info.filename} is too large")
                members[info.filename] = z.read(info)
    elif head[:2] == b"\x1f\x8b":
        fmt = "tar.gz"
        with tarfile.open(path, "r:gz") as t:
            for m in t.getmembers():
                if m.isdir():
                    continue
                if not m.isfile():
                    raise ValueError(f"{m.name} is a link or special file")
                if m.size > MAX_PACKAGE_BYTES:
                    raise ValueError(f"{m.name} is too large")
                members[m.name] = t.extractfile(m).read()
    else:
        raise ValueError("not a zip or tar.gz file")
    return fmt, members


def _safe_member(name):
    return (name and not name.startswith("/") and "\\" not in name
            and ".." not in name.split("/") and re.match(r"^[A-Za-z0-9._/-]+$", name))


def validate_package(path):
    r = Report()
    size = os.path.getsize(path)
    if size > MAX_PACKAGE_BYTES:
        r.error(f"package is {size} bytes; the limit is {MAX_PACKAGE_BYTES}")
        return r, None
    try:
        fmt, members = _package_members(path)
    except (ValueError, zipfile.BadZipFile, tarfile.TarError, OSError, EOFError) as e:
        r.error(f"cannot read the package: {e}")
        return r, None
    if fmt != PACKAGE_FORMAT:
        r.error(f"package is a {fmt}; .azmod packages are {PACKAGE_FORMAT}")
    for name in members:
        if not _safe_member(name):
            r.error(f"package member {name!r} has an unsafe path")
    if "azmod.json" not in members:
        r.error("package has no azmod.json at its root")
        return r, None
    try:
        m = json.loads(members["azmod.json"].decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as e:
        r.error(f"azmod.json is not valid JSON: {e}")
        return r, None
    check_manifest_bytes(members["azmod.json"], r)
    check_manifest(m, r)
    mid = m.get("id") if isinstance(m, dict) else None
    if isinstance(mid, str):
        so = members.get(f"{mid}.so")
        if so is None:
            r.error(f"package has no {mid}.so at its root")
        else:
            check_elf(so, r)
            check_id_in_library(so, mid, r)
        check_icon(members.get("icon.png"), r, strict=True)   # inside a package: exactly what the deck gets
        allowed = {"azmod.json", f"{mid}.so", "icon.png", "LICENSE"}
        check_assets([(n, len(d)) for n, d in members.items() if n.startswith("assets/")], r)
        if len(members) > MAX_PKG_FILES:
            r.error(f"the package has {len(members)} files; the deck installs at most {MAX_PKG_FILES}")
        if sum(len(d) for d in members.values()) > MAX_UNPACKED:
            r.error("the package unpacks to more than 16 MB; the deck refuses it")
        for name in sorted(members):
            if name in allowed or name.startswith("assets/"):
                continue
            if name.split("/")[0].lower() == "data":
                r.error(f"package member {name!r}: a package never carries data/; the deck makes "
                        "MODS/community-mods/<id>/data/ for the mod and keeps it across updates")
                continue
            if name.startswith("shots/") and name.endswith(".png") and name.count("/") == 1:
                check_icon_like_png(members[name], r, name)
                continue
            r.error(f"package member {name!r} is not allowed (only azmod.json, {mid}.so, "
                    "icon.png, LICENSE, shots/*.png and assets/...)")
    return r, m


PLACEHOLDER_SUMMARY = "Describe in one line what your mod does."
DECK_DESCRIPTION_BYTES = 599   # what the deck's Community Mods page shows of a description
PLACEHOLDER_DESCRIPTION = ("Describe what your mod changes on the deck, and anything it does that a "
                           "DJ would want to know before installing it.")


def check_placeholders(m, icon_bytes, report):
    """Warnings for what `new` leaves to fill in (never refusals)."""
    if not isinstance(m, dict):
        return
    if m.get("summary") == PLACEHOLDER_SUMMARY:
        report.warn("the summary is still the placeholder from `new`: write one line on what your "
                    "mod does (it shows under its name in Browse)")
    if m.get("description") == PLACEHOLDER_DESCRIPTION:
        report.warn("the description is still the placeholder from `new`: say what your mod changes "
                    "on the deck")
    d = m.get("description")
    if isinstance(d, str) and len(d.encode("utf-8")) > DECK_DESCRIPTION_BYTES:
        report.warn(f"the description is {len(d.encode('utf-8'))} bytes: the deck's mod page shows the "
                    f"first {DECK_DESCRIPTION_BYTES} as one paragraph (the app shows all of it). Put what a DJ "
                    "needs first")
    tpl = os.path.join(SDK_ROOT, "template", "icon.png")
    if icon_bytes and m.get("id") != "hello-az" and os.path.isfile(tpl) and _read(tpl) == icon_bytes:
        report.warn("icon.png is still the template's: give your mod its own so DJs can tell it "
                    "apart in Browse")


def placeholder_problems(m, icon_bytes):
    """What `new` left unfilled that DJs would see: refused when packaging for publishing."""
    out = []
    if not isinstance(m, dict):
        return out
    if m.get("summary") == PLACEHOLDER_SUMMARY:
        out.append("the summary in azmod.json is still the placeholder from `new`")
    if m.get("description") == PLACEHOLDER_DESCRIPTION:
        out.append("the description in azmod.json is still the placeholder from `new`")
    tpl = os.path.join(SDK_ROOT, "template", "icon.png")
    if icon_bytes and m.get("id") != "hello-az" and os.path.isfile(tpl) and _read(tpl) == icon_bytes:
        out.append("icon.png is still the template's")
    return out


def check_id_in_library(so_bytes, mid, report):
    """The deck refuses a library whose descriptor id differs from azmod.json. The id is a
    string in the library, so its absence almost always means MOD_ID and azmod.json disagree."""
    if isinstance(mid, str) and (mid.encode() + b"\0") not in so_bytes:
        report.warn(f"the library does not contain the id \"{mid}\" from azmod.json; check MOD_ID "
                    "in your code matches (the deck refuses a mismatch)")


def check_icon_like_png(data, report, name):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        report.error(f"{name}: not a PNG file")


def find_mod_files(moddir):
    """Resolve (manifest, so_path, icon_path, license_path, shots) for a mod directory."""
    mpath = os.path.join(moddir, "azmod.json")
    with open(mpath, "rb") as f:
        m = json.loads(f.read().decode("utf-8"))
    mid = m.get("id") if isinstance(m, dict) else None
    so = None
    if isinstance(mid, str):
        for cand in (os.path.join(moddir, "out", f"{mid}.so"), os.path.join(moddir, f"{mid}.so")):
            if os.path.isfile(cand):
                so = cand
                break
    icon = os.path.join(moddir, "icon.png")
    lic = os.path.join(moddir, "LICENSE")
    shots_dir = os.path.join(moddir, "shots")
    shots = sorted(os.path.join(shots_dir, f) for f in os.listdir(shots_dir)
                   if f.endswith(".png")) if os.path.isdir(shots_dir) else []
    return (m, so, icon if os.path.isfile(icon) else None,
            lic if os.path.isfile(lic) else None, shots)


# files Windows makes by itself in folders; they are hidden there, but stay visible if the
# folder is copied to a Mac or Linux, so they are left out by name as well
_OS_JUNK = {"desktop.ini", "thumbs.db", "ehthumbs.db"}


def _hidden(dp, name):
    if name.startswith(".") or name.lower() in _OS_JUNK:
        return True
    # Windows hides files with an attribute, not a leading dot
    attrs = getattr(os.stat(os.path.join(dp, name)), "st_file_attributes", 0)
    return bool(attrs & 0x6)        # FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM


def find_assets(moddir):
    """[(member name 'assets/...', path)] for every file under <moddir>/assets, sorted;
    hidden files (.DS_Store, desktop.ini, Thumbs.db and the like) are left out."""
    root = os.path.join(moddir, "assets")
    out = []
    for dp, dns, fns in os.walk(root):
        dns[:] = sorted(d for d in dns if not _hidden(dp, d))
        for f in sorted(fns):
            if _hidden(dp, f):
                continue
            p = os.path.join(dp, f)
            out.append(("assets/" + os.path.relpath(p, root).replace(os.sep, "/"), p))
    return out


def validate_dir(moddir):
    r = Report()
    try:
        m, so, icon, lic, shots = find_mod_files(moddir)
    except FileNotFoundError:
        r.error("no azmod.json in the mod directory")
        return r, None
    except (ValueError, UnicodeDecodeError) as e:
        r.error(f"azmod.json is not valid JSON: {e}")
        return r, None
    check_manifest_bytes(_read(os.path.join(moddir, "azmod.json")), r)
    check_manifest(m, r)
    mid = m.get("id") if isinstance(m, dict) else "?"
    if so is None:
        r.error(f"no {mid}.so in out/ or the mod directory (run: azmod-pack build)")
    else:
        with open(so, "rb") as f:
            sob = f.read()
        check_elf(sob, r)
        check_id_in_library(sob, mid, r)
    check_placeholders(m, _read(icon) if icon else None, r)
    if icon is None:
        check_icon(None, r)
    else:
        with open(icon, "rb") as f:
            check_icon(f.read(), r)
    if lic is None:
        r.warn("no LICENSE file; add one so people know how they may use your code")
    for s in shots:
        with open(s, "rb") as f:
            check_icon_like_png(f.read(), r, os.path.relpath(s, moddir))
    assets = find_assets(moddir)
    check_assets([(n, os.path.getsize(p)) for n, p in assets], r)
    return r, m


def validate_path(path):
    if os.path.isdir(path):
        return validate_dir(path)
    return validate_package(path)


_ZIP_TIME = (1980, 1, 1, 0, 0, 0)


def write_package(files, out_path):
    """files: [(member name, bytes)]. Deterministic: sorted, fixed times and modes, and
    zip members STORED (not deflated, whose output can vary with the zlib version), so the
    same inputs give the same sha256 on every machine."""
    files = sorted(files)
    tmp = out_path + ".tmp"
    if PACKAGE_FORMAT == "zip":
        with zipfile.ZipFile(tmp, "w", zipfile.ZIP_STORED) as z:
            for name, data in files:
                info = zipfile.ZipInfo(name, date_time=_ZIP_TIME)
                info.compress_type = zipfile.ZIP_STORED
                info.create_system = 3
                info.external_attr = (0o100755 if name.endswith(".so") else 0o100644) << 16
                z.writestr(info, data)
    else:
        raw = io.BytesIO()
        with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as t:
            for name, data in files:
                ti = tarfile.TarInfo(name)
                ti.size, ti.mtime, ti.uid, ti.gid = len(data), 0, 0, 0
                ti.uname = ti.gname = ""
                ti.mode = 0o755 if name.endswith(".so") else 0o644
                t.addfile(ti, io.BytesIO(data))
        import gzip
        with open(tmp, "wb") as f:
            with gzip.GzipFile(filename="", mode="wb", fileobj=f, mtime=0, compresslevel=9) as g:
                g.write(raw.getvalue())
    os.replace(tmp, out_path)


def package_ext():
    return ".azmod"


def make_entry(m, sha, size, package_url, icon_url, updated=None, assets_bytes=0):
    e = {
        "id": m["id"], "name": m["name"], "version": m["version"], "author": m["author"],
        "summary": m["summary"], "icon": icon_url, "package": package_url,
        "sha256": sha, "size": size, "firmware": m["firmware"], "hooks": m["hooks"],
        "source": m["source"],
        "updated": updated or datetime.date.today().isoformat(),
    }
    for k in ("category", "license", "min_core", "description"):   # description: the app and deck show it
        if k in m:
            e[k] = m[k]
    if assets_bytes:   # the deck says "Keeps N MB of files on your USB stick" before install
        e["assets_kb"] = (assets_bytes + 1023) // 1024
    return e


def cmd_package(args):
    moddir = os.path.abspath(args.moddir)
    r, m = validate_dir(moddir)
    r.print(f"validate {moddir}")
    if not r.ok:
        return 1
    m, so, icon, lic, shots = find_mod_files(moddir)
    if args.github or args.package_url:
        problems = placeholder_problems(m, _read(icon))
        if problems:
            for p in problems:
                print(f"  REFUSED: {p}", file=sys.stderr)
            print("package for publishing: REFUSED. DJs see these in Browse before they install; "
                  "fill them in, or package without --github / --package-url to test locally.",
                  file=sys.stderr)
            return 1
    icon_bytes = icon_for_deck(_read(icon))[0] or _read(icon)   # metadata chunks left out (validated above)
    files = [("azmod.json", _read(os.path.join(moddir, "azmod.json"))),
             (f"{m['id']}.so", _read(so)), ("icon.png", icon_bytes)]
    if lic:
        files.append(("LICENSE", _read(lic)))
    files += [("shots/" + os.path.basename(s), _read(s)) for s in shots]
    files += [(n, _read(p)) for n, p in find_assets(moddir)]

    out_dir = os.path.abspath(args.out or os.path.join(moddir, "dist"))
    os.makedirs(out_dir, exist_ok=True)
    base = f"{m['id']}-{m['version']}"
    pkg = os.path.join(out_dir, base + package_ext())
    write_package(files, pkg)
    icon_name = base + "-icon.png"
    with open(os.path.join(out_dir, icon_name), "wb") as f:
        f.write(icon_bytes)

    pr, _ = validate_package(pkg)
    if not pr.ok:
        pr.print(f"validate {pkg}")
        return 1
    data = _read(pkg)
    sha = hashlib.sha256(data).hexdigest()
    if args.package_url:
        pkg_url = args.package_url
        icon_url = args.icon_url or pkg_url.rsplit("/", 1)[0] + "/" + icon_name
    elif args.github:
        tag = args.tag or f"{m['id']}-{m['version']}"
        rel = f"https://github.com/{args.github}/releases/download/{tag}"
        pkg_url, icon_url = f"{rel}/{os.path.basename(pkg)}", f"{rel}/{icon_name}"
    else:
        pkg_url = icon_url = None
    print(f"package: {pkg}")
    print(f"sha256:  {sha}")
    print(f"size:    {len(data)}")
    if pkg_url:
        entry = make_entry(m, sha, len(data), pkg_url, icon_url,
                           assets_bytes=sum(os.path.getsize(p) for _, p in find_assets(moddir)))
        ep = os.path.join(out_dir, base + ".entry.json")
        with open(ep, "w", encoding="utf-8", newline="\n") as f:
            json.dump(entry, f, indent=2)
            f.write("\n")
        print(f"entry:   {ep}")
        print(json.dumps(entry, indent=2))
    else:
        print("(pass --github OWNER/REPO or --package-url URL to get an azmods-repo.json entry)")
    return 0


def _read(p):
    with open(p, "rb") as f:
        return f.read()


# =========================================================================================
# build
# =========================================================================================

DOCKER_HELP = {
    "missing": "Docker is not installed (or not on PATH). Install Docker Desktop (macOS, "
               "Windows) or Docker Engine (Linux), then run this again. "
               "See docs/02-setup.md.",
    "daemon": "Docker is installed but not running. Start Docker Desktop (or "
              "`sudo systemctl start docker` on Linux) and run this again.",
    "arm64": "This machine cannot run arm64 containers yet. On Linux, install the arm64 "
             "emulator once:\n    docker run --privileged --rm tonistiigi/binfmt --install arm64\n"
             "Docker Desktop (macOS, Windows) includes it already. See docs/02-setup.md.",
}


def _docker_ok():
    """Return None when Docker can build and run linux/arm64 images, else a help key."""
    if shutil.which("docker") is None:
        return "missing"
    if subprocess.run(["docker", "info"], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL).returncode != 0:
        return "daemon"
    return None


def _run_tee(cmd):
    """Run cmd, show its output as it comes, and also return it (to recognize known errors)."""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    seen = []
    for raw in p.stdout:
        line = raw.decode("utf-8", "replace")
        sys.stdout.write(line)
        sys.stdout.flush()
        seen.append(line)
    return p.wait(), "".join(seen)


IMAGE_LABEL = "io.github.kyle-hosman.az-mods-sdk.docker"


def docker_dir_hash():
    """sha256 over docker/: every file's path and bytes, in sorted order. The image carries
    it as a label, so build can tell an image made from this SDK's docker/ from any other."""
    root = os.path.join(SDK_ROOT, "docker")
    h = hashlib.sha256()
    for dp, dns, fns in os.walk(root):
        dns.sort()
        for f in sorted(fns):
            p = os.path.join(dp, f)
            h.update(os.path.relpath(p, root).replace(os.sep, "/").encode() + b"\0")
            with open(p, "rb") as fh:
                h.update(hashlib.sha256(fh.read()).digest())
    return h.hexdigest()


def image_build_cmd(image):
    return ["docker", "build", "--platform=linux/arm64", "--label",
            f"{IMAGE_LABEL}={docker_dir_hash()}", "-t", image, os.path.join(SDK_ROOT, "docker")]


def _build_image(image):
    return subprocess.run(image_build_cmd(image)).returncode


def _image_label(image):
    """The image's docker/ hash label: None if there is no such image, "" if it has none."""
    r = subprocess.run(["docker", "image", "inspect", "-f",
                        "{{ index .Config.Labels \"" + IMAGE_LABEL + "\" }}", image],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        return None
    v = r.stdout.decode().strip()
    return "" if v in ("", "<no value>") else v


def cmd_image(args):
    why = _docker_ok()
    if why:
        print(DOCKER_HELP[why], file=sys.stderr)
        return 1
    print("+ " + " ".join(image_build_cmd(args.image)), flush=True)
    rc = _build_image(args.image)
    if rc:
        print("\nThe container did not build. If the log above mentions 'exec format "
              "error':\n" + DOCKER_HELP["arm64"], file=sys.stderr)
    return rc


def _has_cxx_sources(moddir):
    """True when the mod has C++ sources (.cpp or .cc) outside out/ and dist/."""
    for dp, dns, fns in os.walk(moddir):
        dns[:] = [d for d in dns if d not in ("out", "dist") and not d.startswith(".")]
        if any(f.endswith((".cpp", ".cc")) for f in fns):
            return True
    return False


def cmd_build(args):
    moddir = os.path.abspath(args.moddir)
    if not os.path.isfile(os.path.join(moddir, "Makefile")):
        print(f"{moddir} has no Makefile: point build at your mod's folder", file=sys.stderr)
        return 1
    why = _docker_ok()
    if why:
        print(DOCKER_HELP[why], file=sys.stderr)
        return 1
    image = args.image
    want = docker_dir_hash()
    got = _image_label(image)
    if got is None:
        print(f"building the SDK container {image} (first time only)...",
              flush=True)
        rc = _build_image(image)
        if rc:
            print("\nThe container did not build. If the log above mentions 'exec format "
                  "error':\n" + DOCKER_HELP["arm64"], file=sys.stderr)
            return rc
    elif got != want and not args.any_image:
        # an image from another SDK version (or built by hand) may lack what this SDK's
        # Makefile needs, or build different bytes: never reuse it silently
        print(f"{image} was not built from this SDK's docker/ folder (an SDK update changed it, "
              f"or the image was built by hand). Rebuild the SDK image:\n"
              f"    {' '.join(image_build_cmd(image))}\n"
              f"or: azmod-pack image", file=sys.stderr)
        return 1
    elif got != want and _has_cxx_sources(moddir) and subprocess.run(
            ["docker", "run", "--rm", "--platform=linux/arm64", image, "sh", "-c",
             "command -v g++ >/dev/null"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        print(f"{image} has no C++ compiler, and this mod has C++ sources", file=sys.stderr)
        return 1
    include = os.path.abspath(args.include or os.path.join(SDK_ROOT, "include"))
    # Docker Desktop only sees folders it shares (your home folder by default). An unshared
    # folder mounts EMPTY, which make reports as "no makefile found": say what it means.
    seen = subprocess.run(["docker", "run", "--rm", "--platform=linux/arm64",
                           "-v", f"{moddir}:/mod", image, "test", "-f", "/mod/Makefile"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not seen:
        print(f"Docker cannot see {moddir}. Move your mod under your home folder, or add "
              "its location in Docker Desktop (Settings > Resources > File sharing).",
              file=sys.stderr)
        return 1
    cmd = ["docker", "run", "--rm", "--platform=linux/arm64",
           "-v", f"{moddir}:/mod", "-v", f"{include}:/sdk/include:ro", "-w", "/mod",
           image, "make", "-C", "/mod", "AZ_SDK_INCLUDE=/sdk/include"] + args.make_args
    print("+ " + " ".join(cmd), flush=True)
    rc, out = _run_tee(cmd)
    if rc:
        if "exec format error" in out:
            print("\n" + DOCKER_HELP["arm64"], file=sys.stderr)
        else:
            print("\nThe build failed; the compiler's messages are above.", file=sys.stderr)
        return rc
    if args.no_validate:
        return 0
    r, _ = validate_dir(moddir)
    r.print(f"validate {moddir}")
    return 0 if r.ok else 1


def cmd_new(args):
    if not ID_RE.match(args.id):
        print("--id must be 3 to 32 characters of a-z, 0-9 and -, and not a reserved name (enabled, azmod, azstem, az-mods, azmods)", file=sys.stderr)
        return 1
    dest = os.path.abspath(args.dir)
    if os.path.exists(dest):
        print(f"{dest} already exists", file=sys.stderr)
        return 1
    src = os.path.join(SDK_ROOT, "template")
    shutil.copytree(src, dest, ignore=shutil.ignore_patterns("out", "dist", "__pycache__"))
    os.rename(os.path.join(dest, "hello-az.c"), os.path.join(dest, f"{args.id}.c"))
    name = args.name or args.id.replace("-", " ").title()
    for rel in ("azmod.json", "Makefile", f"{args.id}.c", os.path.join(".github", "workflows",
                                                                       "release.yml")):
        p = os.path.join(dest, rel)
        with open(p, encoding="utf-8", newline="") as f:
            text = f.read()
        text = text.replace("hello-az", args.id).replace("Hello AZ", name)
        if rel == "Makefile":
            # the template's paths are relative to template/ inside the SDK: point them at the
            # SDK from where the new mod is (absolute when no relative path exists)
            try:
                sdk = os.path.relpath(SDK_ROOT, dest)
            except ValueError:                          # another drive on Windows
                sdk = SDK_ROOT
            sdk = sdk.replace(os.sep, "/")
            text = text.replace("AZ_SDK_INCLUDE  ?= ../include", f"AZ_SDK_INCLUDE  ?= {sdk}/include")
            text = text.replace("AZ_SDK          ?= ..\n", f"AZ_SDK          ?= {sdk}\n")
        # keep the template's LF: Windows text mode would write CRLF (and a different package)
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(text)
    with open(os.path.join(dest, "README.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {name}\n\nAn AZ-MODS community mod for the XDJ-AZ.\n\n"
                "Describe what it does, how to use it, and anything it changes on the deck.\n")
    mp = os.path.join(dest, "azmod.json")
    m = _load_json(mp)
    m.update({"version": "0.1.0", "author": args.author or "your-name",
              "summary": PLACEHOLDER_SUMMARY, "description": PLACEHOLDER_DESCRIPTION,
              "source": f"https://github.com/{args.author or 'your-name'}/{args.id}"})
    _write_json(mp, m)
    print(f"created {dest}")
    if os.name == "nt":
        # a line to paste into PowerShell: the .cmd wrapper, quoted paths, and & to run a
        # quoted command
        tool = os.path.join(SDK_ROOT, "tools", "azmod-pack.cmd")
        line = subprocess.list2cmdline([tool, "build", dest])
        if line.startswith('"'):
            line = "& " + line
    else:
        line = " ".join(shlex.quote(a) for a in (os.path.join(SDK_ROOT, "tools", "azmod-pack"), "build", dest))
    print(f"next: edit {args.id}.c, azmod.json and icon.png, then build it:\n  {line}")
    return 0


def cmd_validate(args):
    rc = 0
    for p in args.paths:
        r, _ = validate_path(p)
        r.print(f"validate {p}")
        rc |= 0 if r.ok else 1
    return rc


# =========================================================================================
# repository indexes
# =========================================================================================

def _load_json(path):
    with open(path, "rb") as f:
        return json.loads(f.read().decode("utf-8"))


def _write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=2, ensure_ascii=False)
        f.write("\n")
    os.replace(tmp, path)


def cmd_repo_add(args):
    if args.entry.endswith(package_ext()):
        print("pass the .entry.json that `azmod-pack package` wrote next to the package",
              file=sys.stderr)
        return 1
    entry = _load_json(args.entry)
    r = Report()
    check_repo_entry(entry, r, f"entry {args.entry}")
    if not r.ok:
        r.print("entry")
        return 1
    if os.path.exists(args.index):
        idx = _load_json(args.index)
    else:
        if not (args.name and args.maintainer and args.homepage):
            print(f"{args.index} does not exist; pass --name, --maintainer and --homepage "
                  "to create it", file=sys.stderr)
            return 1
        idx = {"azmods_repo": 1, "name": args.name, "maintainer": args.maintainer,
               "homepage": args.homepage, "mods": []}
    mods = [e for e in idx.get("mods", []) if not (isinstance(e, dict)
                                                   and e.get("id") == entry["id"])]
    action = "updated" if len(mods) != len(idx.get("mods", [])) else "added"
    mods.append(entry)
    mods.sort(key=lambda e: e.get("id", "") if isinstance(e, dict) else "")
    idx["mods"] = mods
    r = Report()
    check_repo_index(idx, r)
    if not r.ok:
        r.print(args.index)
        return 1
    _write_json(args.index, idx)
    print(f"{action} {entry['id']} {entry['version']} in {args.index}")
    return 0


_CAFILE = None   # --cafile: an extra CA to trust, for a test server with its own certificate


def _fetch(url, limit):
    req = urllib.request.Request(url, headers={"User-Agent": f"azmod-pack/{SDK_VERSION}"})
    ctx = None
    if _CAFILE:
        ctx = ssl.create_default_context()      # the system's CAs, plus this one
        ctx.load_verify_locations(cafile=_CAFILE)
    with urllib.request.urlopen(req, timeout=30, context=ctx) as resp:
        data = resp.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"{url} is larger than {limit} bytes")
    return data


def repo_index_url(ref):
    """owner/repo, a github.com repo URL, or a direct https URL to an azmods-repo.json."""
    m = re.match(r"^(?:https://github\.com/)?([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\.git)?/?$",
                 ref)
    if m and not ref.endswith(".json"):
        return f"https://raw.githubusercontent.com/{m.group(1)}/{m.group(2)}/HEAD/azmods-repo.json"
    if ref.startswith("https://"):
        return ref
    return None


def check_repo(ref, download, report):
    """Checks one repository; returns its parsed azmods-repo.json, or None if it is unreadable."""
    if os.path.isfile(ref):
        idx_bytes = _read(ref)
    else:
        url = repo_index_url(ref)
        if url is None:
            report.error(f"{ref}: not owner/repo, a github.com URL or an https URL")
            return None
        try:
            idx_bytes = _fetch(url, 1024 * 1024)
        except Exception as e:  # noqa: BLE001 - report every network failure the same way
            report.error(f"{ref}: cannot fetch {url}: {e}")
            return None
    try:
        idx = json.loads(idx_bytes.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as e:
        report.error(f"{ref}: azmods-repo.json is not valid JSON: {e}")
        return None
    check_repo_index(idx, report)
    if not download or not isinstance(idx, dict) or not isinstance(idx.get("mods"), list):
        return idx
    import tempfile
    for e in idx["mods"]:
        if not isinstance(e, dict) or not _is_https(e.get("package")):
            continue
        where = f"{e.get('id')} {e.get('version')}"
        if _is_https(e.get("source")):
            try:
                _fetch(e["source"], 4 * 1024 * 1024)
            except Exception as ex:  # noqa: BLE001
                report.error(f"{where}: the source URL {e['source']} is not publicly "
                             f"reachable: {ex}")
        try:
            data = _fetch(e["package"], MAX_PACKAGE_BYTES)
        except Exception as ex:  # noqa: BLE001
            report.error(f"{where}: cannot download {e['package']}: {ex}")
            continue
        if len(data) != e.get("size"):
            report.error(f"{where}: package is {len(data)} bytes, the repository file says {e.get('size')}")
        if hashlib.sha256(data).hexdigest() != e.get("sha256"):
            report.error(f"{where}: package sha256 does not match the repository file")
        with tempfile.TemporaryDirectory() as td:
            p = os.path.join(td, "pkg.azmod")
            with open(p, "wb") as f:
                f.write(data)
            pr, m = validate_package(p)
        report.extend(pr, prefix=f"{where}: ")
        if isinstance(m, dict):
            for k in ("id", "version", "firmware", "hooks", "source"):
                if m.get(k) != e.get(k):
                    report.error(f"{where}: \"{k}\" in the repository file differs from the package's "
                                 "azmod.json")
        try:
            icon = _fetch(e["icon"], 1024 * 1024)
            check_icon(icon, report, f"{where}: icon")
        except Exception as ex:  # noqa: BLE001
            report.error(f"{where}: cannot download the icon {e.get('icon')}: {ex}")
    return idx


def _use_cafile(args):
    global _CAFILE
    _CAFILE = getattr(args, "cafile", None)


def cmd_repo_check(args):
    _use_cafile(args)
    r = Report()
    check_repo(args.ref, args.download, r)
    r.print(f"repo {args.ref}")
    return 0 if r.ok else 1


def check_repo_list(lst, report):
    """The Mod Index's azmods-repos.json."""
    w = "azmods-repos.json"
    if not isinstance(lst, dict) or lst.get("azmods_repos") != 1:
        report.error(f"{w}: must be an object with \"azmods_repos\": 1")
        return []
    repos = lst.get("repos")
    if not isinstance(repos, list):
        report.error(f"{w}: \"repos\" must be a list")
        return []
    for k in sorted(set(lst) - {"azmods_repos", "repos"}):
        report.warn(f"{w}: unknown key \"{k}\"")
    seen = set()
    for r in repos:
        if not isinstance(r, str) or not re.match(
                r"^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$", r):
            report.error(f"{w}: {r!r} must be https://github.com/OWNER/REPO")
            continue
        if r.lower() in seen:
            report.error(f"{w}: {r} is listed twice")
        seen.add(r.lower())
    return [r for r in repos if isinstance(r, str)]


def check_list_ids(indexes, report):
    """Mod ids across the listed repositories: [(ref, azmods-repo.json), ...] in list order.
    An id names a mod's folder on the stick and its settings and files on the deck, so two mods
    with one id cannot both be installed: every id on the list is unique, compared without regard
    to case. The same display name under different ids is allowed (a warning)."""
    by_id, by_name = {}, {}
    for ref, idx in indexes:
        mods = idx.get("mods") if isinstance(idx, dict) else None
        for e in mods if isinstance(mods, list) else []:
            if not isinstance(e, dict) or not isinstance(e.get("id"), str):
                continue
            by_id.setdefault(e["id"].lower(), []).append((ref, e["id"]))
            if isinstance(e.get("name"), str) and e["name"].strip():
                by_name.setdefault(e["name"].strip().casefold(), []).append((ref, e["id"], e["name"]))
    for seen in by_id.values():
        if len({r for r, _ in seen}) > 1:
            where = ", ".join(f"{r} ({i})" for r, i in seen)
            report.error(f"mod id \"{seen[0][1]}\" is used by more than one repository: {where}; "
                         "every id in the Mod Index must be unique")
    for seen in by_name.values():
        if len({i.lower() for _, i, _ in seen}) > 1:
            where = ", ".join(f"{i} in {r}" for r, i, _ in seen)
            report.warn(f"several mods are named \"{seen[0][2]}\" ({where}); the deck tells them "
                        "apart only by author")


def cmd_list_check(args):
    _use_cafile(args)
    r = Report()
    try:
        lst = _load_json(args.path)
    except (OSError, ValueError, UnicodeDecodeError) as e:
        r.error(f"{args.path}: {e}")
        r.print(args.path)
        return 1
    repos = check_repo_list(lst, r)
    r.print(args.path)
    rc = 0 if r.ok else 1
    indexes = []
    for ref in repos:
        rr = Report()
        idx = check_repo(ref, args.download, rr)
        rr.print(f"repo {ref}")
        rc |= 0 if rr.ok else 1
        if idx is not None:
            indexes.append((ref, idx))
    ri = Report()
    check_list_ids(indexes, ri)
    ri.print("mod ids across the list")
    rc |= 0 if ri.ok else 1
    return rc


# =========================================================================================

def main(argv=None):
    ap = argparse.ArgumentParser(prog="azmod-pack", description=__doc__.split("\n")[0],
                                 epilog="Run `azmod-pack COMMAND --help` for a command's options. "
                                        "The guide: docs/README.md in the SDK.")
    ap.add_argument("--version", action="version", version=f"azmod-pack {SDK_VERSION}")
    sub = ap.add_subparsers(dest="cmd", required=True)

    n = sub.add_parser("new", help="start a new mod from the template")
    n.add_argument("dir", help="the new mod's folder (must not exist yet)")
    n.add_argument("--id", required=True,
                   help="3-32 characters of a-z, 0-9 and -; unique, and don't change it later (see the guide)")
    n.add_argument("--name", help="display name (default: from the id)")
    n.add_argument("--author", help="your GitHub user name")
    n.set_defaults(func=cmd_new)

    b = sub.add_parser("build", help="build the mod in the SDK container, then validate it")
    b.add_argument("moddir", nargs="?", default=".", help="the mod's folder (default: .)")
    b.add_argument("--image", default=DEFAULT_IMAGE,
                   help="container image tag (built from docker/ if missing)")
    b.add_argument("--include", help="directory holding azmod-mod.h (default: the SDK's)")
    b.add_argument("--no-validate", action="store_true", help="build only, skip the checks")
    b.add_argument("--any-image", action="store_true",
                   help="use --image even if it was not built from this SDK's docker/")
    b.add_argument("make_args", nargs="*", help="extra make arguments (after --)")
    b.set_defaults(func=cmd_build)

    im = sub.add_parser("image", help="build (or rebuild) the SDK container from docker/")
    im.add_argument("--image", default=DEFAULT_IMAGE, help="container image tag")
    im.set_defaults(func=cmd_image)

    v = sub.add_parser("validate", help="check a mod directory or an .azmod package")
    v.add_argument("paths", nargs="+", metavar="MODDIR|FILE.azmod")
    v.set_defaults(func=cmd_validate)

    p = sub.add_parser("package", help="make <id>-<version>.azmod and its entry in the repository file")
    p.add_argument("moddir", nargs="?", default=".", help="the mod's folder, already built (default: .)")
    p.add_argument("--out", help="output directory (default: MODDIR/dist)")
    p.add_argument("--github", metavar="OWNER/REPO", help="repo whose releases host the package")
    p.add_argument("--tag", help="release tag (default: <id>-<version>)")
    p.add_argument("--package-url", help="full https URL the package will be served from")
    p.add_argument("--icon-url", help="full https URL of the icon (default: next to the package)")
    p.set_defaults(func=cmd_package)

    r = sub.add_parser("repo", help="work with an azmods-repo.json")
    rs = r.add_subparsers(dest="repo_cmd", required=True)
    ra = rs.add_parser("add", help="insert or update an entry")
    ra.add_argument("index", metavar="REPO.json", help="your repository file (azmods-repo.json)")
    ra.add_argument("entry", help="the .entry.json written by `package`")
    ra.add_argument("--name", help="repository name, to create a new repository file")
    ra.add_argument("--maintainer", help="your name, to create a new repository file")
    ra.add_argument("--homepage", help="https page about your mods, to create a new repository file")
    ra.set_defaults(func=cmd_repo_add)
    rc = rs.add_parser("check", help="validate a repository file (and with --download, every package)")
    rc.add_argument("ref", help="a local file, OWNER/REPO, or an https URL")
    rc.add_argument("--download", action="store_true",
                    help="also download every package and check its sha256, size and contents")
    rc.add_argument("--cafile", metavar="PEM",
                    help="also trust this CA certificate (a test server with its own, e.g. serve-local.py)")
    rc.set_defaults(func=cmd_repo_check)

    lc = sub.add_parser("list", help="work with the Mod Index (azmods-repos.json)")
    ls = lc.add_subparsers(dest="list_cmd", required=True)
    lcc = ls.add_parser("check", help="validate the list and every repository on it")
    lcc.add_argument("path", help="azmods-repos.json")
    lcc.add_argument("--download", action="store_true",
                     help="also download and validate every package")
    lcc.add_argument("--cafile", metavar="PEM",
                    help="also trust this CA certificate (a test server with its own, e.g. serve-local.py)")
    lcc.set_defaults(func=cmd_list_check)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
