#!/usr/bin/env python3
"""Audit the linked .so for dependency problems that neither the compiler nor the
linker will ever report.

Why this exists
---------------
This module builds with APP_STL := none against a vendored libc++, and its host test
suite only ever *compiles* first-party sources. That leaves two holes:

  * The Ryu incident: every source compiled clean, host tests were green, and only
    the final link noticed that std::format needed libc++'s floating-point
    conversion routines. Undefined symbols in the *shared* output are one thing;
    here the link simply failed, so we found it. The dangerous variant is a symbol
    that links but does not resolve on device.
  * A symbol living in the libc++ ABI namespace means we quietly depend on the
    system STL, which is exactly the coupling APP_STL := none is meant to exclude.

Checks performed
----------------
  1. libc++ ABI namespace leaks in the undefined set (must be 0).
  2. Any other C++ symbol left undefined (must be 0 -- everything is static).
  3. Strong undefined symbols no NEEDED library provides (must be 0).
  4. Weak undefined symbols no NEEDED library provides (informational: these are
     the usual "new API, probe it at runtime" pattern and resolve to null on old
     Android, so they are only listed, never fatal).

Usage
-----
    python scripts/verify_linked_symbols.py build/obj/local/arm64-v8a/libarc_helper.so
    python scripts/verify_linked_symbols.py <so> --ndk-home <path-to-ndk>

Without --ndk-home the NDK is located the same way the build script does it
(environment override, then the documented SDK locations, then a scan of the
existing drives -- newest release wins), so the audit resolves against the
sysroot that actually built the module.

Run it after a release build. Prefer the unstripped copy under build/obj/ --
ndk-build strips build/libs/, and a stripped .so reports "no symbols".
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Reuse the shared toolchain lookups instead of resolving the NDK here: the
# build script picks the newest NDK next to $ANDROID_NDK_HOME, and a sysroot
# from a different NDK silently turns this audit into a check of the wrong
# baseline. --ndk-home still overrides.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
import toolchain_probe  # noqa: E402

# libc++'s ABI namespace is configurable: the NDK builds it as std::__ndk1,
# upstream defaults to std::__2, older releases used std::__1, and libstdc++
# spells its own std::__cxx11. Matching only "__ndk" would go mute the moment
# anyone rebuilt the vendored copy with a different _LIBCPP_ABI_NAMESPACE.
STL_NS = re.compile(r"^(std::)?__(ndk\d+|cxx\d+|\d+)::")
UNDEF_TYPES = ("U", "w")  # llvm-nm: undefined / weak-undefined


class Toolchain:
    def __init__(self, ndk_home: Path) -> None:
        self.ndk_home = ndk_home
        bin_dir = ndk_home / "toolchains" / "llvm" / "prebuilt"
        # Host subdirectory name varies by platform; pick what actually exists.
        hosts = sorted(bin_dir.glob("*/bin")) if bin_dir.is_dir() else []
        if not hosts:
            raise SystemExit(f"no LLVM toolchain found under {bin_dir}")
        self.bin = hosts[0]
        self.sysroot = ndk_home / "toolchains" / "llvm" / "prebuilt" / hosts[0].parent.name / "sysroot"

    def run(self, name: str, *args: str) -> str:
        exe = self.bin / name
        if not exe.exists():
            raise SystemExit(f"missing tool: {exe}")
        # These are native Windows executables: hand them absolute resolved paths,
        # never the /d/... form a POSIX shell would produce.
        proc = subprocess.run(
            [str(exe), *args], capture_output=True, text=True, encoding="utf-8", errors="replace"
        )
        if proc.returncode != 0:
            raise SystemExit(f"{name} failed on {args[-1]}:\n{proc.stderr.strip()}")
        return proc.stdout

    def nm(self, *args: str) -> str:
        return self.run("llvm-nm.exe" if sys.platform == "win32" else "llvm-nm", *args)

    def readobj(self, *args: str) -> str:
        return self.run("llvm-readobj.exe" if sys.platform == "win32" else "llvm-readobj", *args)


def parse_nm(text: str, want_undefined: bool) -> list[tuple[str, str]]:
    """Return (type, name) pairs from llvm-nm output.

    Defined lines read "<address> <type> <name>"; undefined lines pad the address
    column and read "<type> <name>". rsplit from the right so both shapes land in
    the same slots -- splitting from the left silently drops every defined symbol,
    which makes the whole comparison look like a failure.
    """
    out: list[tuple[str, str]] = []
    for line in text.splitlines():
        fields = line.rsplit(None, 2)
        if len(fields) == 3:
            kind, name = fields[1], fields[2]
        elif len(fields) == 2:
            kind, name = fields[0], fields[1]
        else:
            continue
        if len(kind) != 1 or not name or name.startswith("error:"):
            continue
        if want_undefined == (kind in UNDEF_TYPES):
            out.append((kind, name))
    return out


def self_test() -> int:
    """Exercise the parsers and detectors on synthesised llvm-nm output.

    Worth having because a healthy .so reports zero on checks 1-3, and an audit
    that always says "none" is indistinguishable from one that never looks. The
    NDK's own libc++_shared.so also passes cleanly, so real ELF files are no use
    as negative fixtures -- hence the synthetic ones.
    """
    failures: list[str] = []

    def check(label: str, got: object, want: object) -> None:
        if got != want:
            failures.append(f"{label}: got {got!r}, want {want!r}")

    # Defined lines carry an address column, undefined ones do not.
    defined_out = "0000000000001234 T memcpy\n0000000000002000 W std::__ndk1::string::size()\n"
    undef_out = "                 U memcpy\n                 w memfd_create\n"
    check("defined parse", parse_nm(defined_out, False),
          [("T", "memcpy"), ("W", "std::__ndk1::string::size()")])
    check("undefined parse", parse_nm(undef_out, True),
          [("U", "memcpy"), ("w", "memfd_create")])
    check("defined not misread as undefined", parse_nm(defined_out, True), [])
    check("undefined not misread as defined", parse_nm(undef_out, False), [])

    # Guards against the original bug: splitting from the left drops every
    # three-column line, so the provided set empties and everything looks missing.
    check("three-column name kept",
          [n for _, n in parse_nm(defined_out, False)],
          ["memcpy", "std::__ndk1::string::size()"])

    for name, want_hit in [
        ("std::__ndk1::basic_string<char>", True),   # NDK libc++
        ("std::__2::vector<int>", True),             # upstream default
        ("std::__1::locale", True),                  # older libc++
        ("std::__cxx11::string", True),              # libstdc++
        ("__ndk1::locale", True),                    # inline namespace, bare
        ("std::__ndk1x::thing", False),              # not a versioned namespace
        ("myapp::__private::helper", False),         # first-party inline ns
        ("std::vector<int>", False),
        ("memcpy", False),
    ]:
        check(f"STL_NS {name}", bool(STL_NS.match(name)), want_hit)

    print("self-test:", "PASS" if not failures else "FAIL")
    for line in failures:
        print(f"    {line}")
    return 0 if not failures else 1


def needed_libraries(tc: Toolchain, so: Path) -> list[str]:
    text = tc.readobj("--needed-libs", str(so.resolve()))
    libs, inside = [], False
    for line in text.splitlines():
        if "NeededLibraries" in line:
            inside = True
            continue
        if inside:
            stripped = line.strip()
            if stripped == "]":
                break
            if stripped:
                libs.append(stripped)
    return libs


def available_api_levels(tc: Toolchain) -> list[int]:
    root = tc.sysroot / "usr" / "lib" / "aarch64-linux-android"
    return sorted(int(p.name) for p in root.iterdir() if p.is_dir() and p.name.isdigit())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("so", type=Path, nargs="?", help="path to the linked shared object (unstripped)")
    ap.add_argument("--ndk-home", type=Path, default=None, help="NDK root; defaults to $ANDROID_NDK_HOME")
    ap.add_argument("--api-level", type=int, default=21, help="APP_PLATFORM level to resolve symbols against")
    ap.add_argument("--self-test", action="store_true",
                    help="verify the parsers and detectors on synthetic input, then exit")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if args.so is None:
        ap.error("a .so path is required unless --self-test is given")

    so: Path = args.so
    if not so.is_file():
        raise SystemExit(f"no such file: {so}")

    ndk_home = Path(args.ndk_home) if args.ndk_home else toolchain_probe.find_ndk_home()
    if not ndk_home.is_dir():
        raise SystemExit(f"not a directory: {ndk_home}")

    tc = Toolchain(ndk_home.resolve())

    levels = available_api_levels(tc)
    api = args.api_level if args.api_level in levels else min(
        levels, key=lambda lv: (abs(lv - args.api_level), lv)
    )
    if api != args.api_level:
        print(f"note: API {args.api_level} not in this NDK; using {api}")

    libs = needed_libraries(tc, so)
    if not libs:
        raise SystemExit("no NEEDED libraries found -- is this a stripped or malformed ELF?")
    print(f"target        : {so.name}")
    print(f"NEEDED        : {', '.join(libs)}")
    # Printed so the baseline is auditable: a run that resolved against a
    # different NDK's sysroot looks exactly like a run against the right one.
    print(f"ndk           : {tc.ndk_home}")
    print(f"resolved at   : android-{api}")
    print()

    undefined = parse_nm(tc.nm("--undefined-only", "--demangle", str(so.resolve())), True)
    if not undefined:
        raise SystemExit(f"{so.name} reports no undefined symbols -- is it stripped?")

    provided: set[str] = set()
    lib_dir = tc.sysroot / "usr" / "lib" / "aarch64-linux-android" / str(api)
    for lib in libs:
        candidate = lib_dir / lib
        if not candidate.is_file():
            print(f"  warning: {lib} not present at API {api}, symbols from it are unverifiable")
            continue
        for _, name in parse_nm(tc.nm("--defined-only", "-g", str(candidate)), False):
            provided.add(name.split("@", 1)[0])

    stl_leaks = sorted({n for _, n in undefined if STL_NS.match(n)})
    cpp_syms = sorted({n for _, n in undefined if "::" in n and not STL_NS.match(n)})
    strong = sorted({n for k, n in undefined if k == "U"})
    weak = sorted({n for k, n in undefined if k == "w"})
    # Versioned names carry "@VERSION"; the linker matches on the base name.
    missing_strong = [n for n in strong if n.split("@", 1)[0] not in provided]
    missing_weak = [n for n in weak if n.split("@", 1)[0] not in provided]

    print(f"undefined     : {len(undefined)}  (strong {len(strong)}, weak {len(weak)})")
    print(f"provided      : {len(provided)} unique names")
    print()

    def section(title: str, items: list[str], ok_empty: bool) -> bool:
        print(f"=== {title} ===")
        if items:
            for name in items:
                print(f"    {name}")
            print(f"    -> {'FAIL' if ok_empty else 'informational'}")
        else:
            print("    none" if ok_empty else "    (empty)")
        print()
        return not items if ok_empty else True

    ok = True
    ok &= section("1. libc++ ABI namespace leaks (must be 0)", stl_leaks, True)
    ok &= section("2. other undefined C++ symbols (must be 0)", cpp_syms, True)
    ok &= section("3. strong symbols no NEEDED library provides (must be 0)", missing_strong, True)
    section("4. weak symbols not provided (runtime-probed APIs, tolerated)", missing_weak, False)

    print("VERDICT:", "PASS" if ok else "FAIL")
    if not ok and stl_leaks:
        print("  A libc++ symbol leaked out: the vendored libc++ is not fully linking.")
        print("  See docs/cpp/build-pitfalls.md -- this is the same class of bug as the Ryu one.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
