"""Locate the host toolchain pieces the test scripts need.

Shared by run_host_tests.py, run_arcpkg_corpus.py, scripts/check-one.sh and
scripts/verify_linked_symbols.py. Those used to carry private copies of
find_ndk_clang(); every machine-specific path therefore had to be fixed several
times over, and forgetting a copy broke a script that still looked fine. Keeping
one implementation means one place to fix.

Lookup order for everything here:

  1. Environment-variable override (ANDROID_NDK_ROOT / ANDROID_NDK_HOME /
     ANDROID_SDK_ROOT / ANDROID_HOME, MSVC_INSTALL, WINDOWS_SDK_DIR, ZLIB_ROOT).
  2. Documented default install locations.
  3. A scan of existing drive letters, which catches installs on a non-system
     drive without encoding anyone's particular layout.

Nothing in this file is specific to one machine; if a lookup cannot find what
it needs it raises SystemExit naming the variable that overrides it.

`python tests/toolchain_probe.py <name>` prints one value so shell scripts can
reuse these lookups instead of hard-coding paths of their own.
"""
from __future__ import annotations

import os
import pathlib
import re
import string
import subprocess
import sys
from functools import lru_cache


def version_key(path: pathlib.Path) -> tuple[int, ...]:
    numbers = re.findall(r"\d+", path.name)
    return tuple(int(number) for number in numbers) or (0,)


def newest_subdir(path: pathlib.Path) -> pathlib.Path:
    return max((child for child in path.iterdir() if child.is_dir()), key=version_key)


def from_env_path(value: str) -> pathlib.Path:
    """Interpret an override value, including Git Bash's /d/... spelling.

    Git Bash exports POSIX-looking paths. Handed straight to pathlib on Windows
    they never match anything, so the override is silently ignored and the
    lookup quietly falls back to whatever it finds by scanning -- the caller
    gets a working-looking answer from the wrong place. Only the leading
    "/<drive>/" form is rewritten; anything else is returned untouched.
    """
    path = pathlib.Path(value)
    if os.name != "nt" or path.exists():
        return path
    match = re.match(r"/([A-Za-z])(?:/|$)", value)
    if not match:
        return path
    drive = pathlib.Path(match.group(1).upper() + ":\\")
    if not drive.exists():
        return path
    return drive / value[match.end():].lstrip("/").replace("/", "\\")


def _iter_drive_roots() -> list[pathlib.Path]:
    """Every drive letter that currently exists.

    Used only as a last resort, after the documented defaults. The previous
    approach hard-coded a few specific drive letters, which made these lookups
    work on exactly one machine and fail everywhere else; scanning lets an
    install on D:, E:, F: ... be found without naming it.
    """
    if os.name != "nt":
        return []
    return [
        pathlib.Path(f"{letter}:\\")
        for letter in string.ascii_uppercase
        if pathlib.Path(f"{letter}:\\").exists()
    ]


def _ndk_search_roots() -> list[pathlib.Path]:
    """Where to look for NDKs, most specific first."""
    roots: list[pathlib.Path] = []
    for variable in ("ANDROID_NDK_ROOT", "ANDROID_NDK_HOME"):
        value = os.environ.get(variable)
        if value:
            roots.append(from_env_path(value))
    for variable in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        value = os.environ.get(variable)
        if value:
            sdk = from_env_path(value)
            roots.append(sdk / "ndk" if (sdk / "ndk").is_dir() else sdk)
    roots.extend(
        pathlib.Path(path)
        for path in (
            pathlib.Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "ndk",
            pathlib.Path.home() / "Android" / "Sdk" / "ndk",
            pathlib.Path("/opt/android-sdk/ndk"),
        )
    )
    roots.extend(drive / "android_sdk" / "ndk" for drive in _iter_drive_roots())
    return roots


def _iter_ndk_dirs(root: pathlib.Path):
    """Expand one search root into the NDK directories it points at.

    A root is either an NDK itself (source.properties at the top) or a folder
    holding several releases side by side. An NDK named explicitly also brings
    its siblings: build.ps1 resolves ANDROID_NDK_HOME by walking up to the
    parent and taking the highest version there, so a stale ANDROID_NDK_HOME
    still lands on the NDK that actually gets used. Matching that here keeps
    every script looking at the same NDK.
    """
    if not root.is_dir():
        return
    if (root / "source.properties").is_file():
        yield root
        yield from (sibling for sibling in root.parent.iterdir() if sibling.is_dir())
        return
    yield from (child for child in root.iterdir() if child.is_dir())


@lru_cache(maxsize=None)
def find_ndk_home() -> pathlib.Path:
    """The NDK root -- the directory holding toolchains/ and sysroot.

    For scripts that need the LLVM binutils and sysroot (verify_linked_symbols)
    rather than a compiler driver (find_ndk_clang).
    """
    candidates = [
        (version_key(ndk), ndk)
        for root in _ndk_search_roots()
        for ndk in _iter_ndk_dirs(root)
        if (ndk / "toolchains" / "llvm" / "prebuilt").is_dir()
    ]
    if not candidates:
        raise SystemExit(
            "No NDK found. Set ANDROID_NDK_ROOT (or ANDROID_NDK_HOME) to an NDK, "
            "or ANDROID_SDK_ROOT/ANDROID_HOME to an SDK with ndk/ inside."
        )
    return max(candidates, key=lambda candidate: (candidate[0], str(candidate[1])))[1]


def _clang_in(ndk: pathlib.Path) -> pathlib.Path | None:
    prebuilt = ndk / "toolchains" / "llvm" / "prebuilt"
    if not prebuilt.is_dir():
        return None
    wanted = {
        "win32": "windows-x86_64",
        "linux": "linux-x86_64",
        "darwin": "darwin-x86_64",
    }.get(sys.platform)
    hosts = [host for host in prebuilt.iterdir() if host.is_dir()]
    # Prefer this platform's host triple; fall back to whatever was shipped.
    hosts.sort(key=lambda host: host.name != wanted)
    for host in hosts:
        suffix = "clang++.exe" if host.name.startswith("windows") else "clang++"
        compiler = host / "bin" / suffix
        if compiler.is_file():
            return compiler
    return None


@lru_cache(maxsize=None)
def find_ndk_clang() -> str:
    # An override that names the compiler binary outright still wins.
    for variable in ("ANDROID_NDK_ROOT", "ANDROID_NDK_HOME"):
        value = os.environ.get(variable)
        if value:
            direct = from_env_path(value)
            if direct.is_file():
                return str(direct)

    ndk = find_ndk_home()
    compiler = _clang_in(ndk)
    if compiler is None:
        raise SystemExit(
            f"No clang++ under {ndk / 'toolchains' / 'llvm' / 'prebuilt'}. "
            "Host tests intentionally do not fall back to g++."
        )
    return str(compiler)


@lru_cache(maxsize=None)
def find_visual_studio() -> pathlib.Path:
    override = os.environ.get("MSVC_INSTALL")
    if override:
        return from_env_path(override)

    # vswhere is the supported way to enumerate installs and reports them
    # regardless of which drive they live on, so it covers non-default layouts
    # without hard-coding any drive letter here.
    vswhere = pathlib.Path(
        r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    if vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-property", "installationPath"],
            capture_output=True,
            text=True,
            check=False,
        )
        lines = result.stdout.strip().splitlines()
        if lines and pathlib.Path(lines[0]).is_dir():
            return pathlib.Path(lines[0])

    standard = pathlib.Path(r"C:\Program Files\Microsoft Visual Studio")
    if standard.is_dir():
        for edition in standard.iterdir():
            if edition.is_dir():
                return edition
    for drive in _iter_drive_roots():
        for name in ("Program Files", "Programming Tools"):
            root = drive / name / "Microsoft Visual Studio"
            if not root.is_dir():
                continue
            for edition in root.iterdir():
                if edition.is_dir():
                    return edition

    raise SystemExit(
        "Host tests build against the MSVC STL but no Visual Studio install was "
        "found. Set MSVC_INSTALL to the VS root (the folder containing VC/)."
    )


@lru_cache(maxsize=None)
def find_windows_sdk() -> pathlib.Path:
    override = os.environ.get("WINDOWS_SDK_DIR")
    candidates = [from_env_path(override)] if override else []
    candidates.extend(
        pathlib.Path(path)
        for path in (
            r"C:\Program Files (x86)\Windows Kits\10",
            r"C:\Program Files\Windows Kits\10",
        )
    )
    candidates.extend(drive / "Windows Kits" / "10" for drive in _iter_drive_roots())
    for candidate in candidates:
        if candidate.is_dir() and (candidate / "Include").is_dir():
            return candidate
    raise SystemExit(
        "Host tests need the Windows SDK (for the UCRT headers). Set "
        "WINDOWS_SDK_DIR to the 'Windows Kits/10' folder."
    )


@lru_cache(maxsize=None)
def find_msvc_zlib() -> tuple[str, ...]:
    """Locate a zlib built with the MSVC toolchain (COFF .lib, not MinGW .a)."""
    lib_names = ("zlibstatic.lib", "zlib.lib", "z.lib", "zdll.lib")
    roots: list[pathlib.Path] = []
    if os.environ.get("ZLIB_ROOT"):
        roots.append(from_env_path(os.environ["ZLIB_ROOT"]))
    for variable in ("CONDA_PREFIX", "CONDA"):
        value = os.environ.get(variable)
        if value:
            roots.append(from_env_path(value) / "Library")
    roots.extend(
        pathlib.Path(path)
        for path in (
            pathlib.Path.home() / "miniconda3" / "Library",
            pathlib.Path.home() / "anaconda3" / "Library",
        )
    )
    for drive in _iter_drive_roots():
        roots.extend(
            drive / rel
            for rel in (
                r"vcpkg\installed\x64-windows",
                r"Program Files\vcpkg\installed\x64-windows",
                r"tools\vcpkg\installed\x64-windows",
            )
        )

    for root in roots:
        include, lib = root / "include", root / "lib"
        if not (include / "zlib.h").is_file():
            continue
        for name in lib_names:
            if (lib / name).is_file():
                # -isystem, not -I: these prefixes (conda, vcpkg) often ship other
                # libraries too -- conda ships its own nlohmann/json.hpp, which
                # would otherwise shadow third_party/json. -isystem dirs are
                # searched after every -I dir and their warnings are suppressed.
                #
                # Absolute path rather than -l: for the MSVC target clang treats a
                # bare "<name>.lib" argument as a source file, not a -L lookup.
                return ("-isystem", str(include), str(lib / name))
    raise SystemExit(
        "Host tests need an MSVC-built zlib (ZipArchive.cpp uses it and the "
        "NDK sysroot only ships MinGW .a files). Set ZLIB_ROOT to a prefix "
        "with include/zlib.h and lib/zlibstatic.lib."
    )


@lru_cache(maxsize=None)
def find_msvc_llvm_bin() -> pathlib.Path:
    """Visual Studio's bundled LLVM bin/ -- the only place lld-link lives.

    NDK clang ships only ld.lld (ELF/MinGW); targeting MSVC needs lld-link.
    """
    llvm_bin = find_visual_studio() / "VC" / "Tools" / "Llvm" / "x64" / "bin"
    if not (llvm_bin / "lld-link.exe").is_file():
        raise SystemExit(f"{llvm_bin} has no lld-link.exe; install the C++ CMake/LLVM tools.")
    return llvm_bin


@lru_cache(maxsize=None)
def find_msvc_clang() -> pathlib.Path:
    """Visual Studio's own clang++, for scripts that want one compiler binary."""
    clang = find_msvc_llvm_bin() / "clang++.exe"
    if not clang.is_file():
        raise SystemExit(
            f"{clang} is missing; install the 'C++ Clang Compiler' component, or "
            "set MSVC_INSTALL to a VS root that has VC/Tools/Llvm."
        )
    return clang


@lru_cache(maxsize=None)
def msvc_include_dir() -> pathlib.Path:
    """<VS>/VC/Tools/MSVC/<newest>/include."""
    versions = find_visual_studio() / "VC" / "Tools" / "MSVC"
    if not versions.is_dir():
        raise SystemExit(f"{versions} is missing; install the C++ workload.")
    return newest_subdir(versions) / "include"


@lru_cache(maxsize=None)
def sdk_include_dir() -> pathlib.Path:
    """<Windows Kits>/10/Include/<newest>, the folder holding ucrt/ and um/."""
    return newest_subdir(find_windows_sdk() / "Include")


def zlib_include_dir() -> pathlib.Path:
    """Just the include dir from find_msvc_zlib() -- compile-only callers need
    no .lib. find_msvc_zlib() returns ("-isystem", <include>, <lib>)."""
    return pathlib.Path(find_msvc_zlib()[1])


def host_toolchain_args() -> tuple[list[str], list[str]]:
    """Compile/link args for NDK clang++ driving the MSVC STL.

    On Windows, NDK clang++ defaults to the MinGW target and therefore picks up
    the MinGW libstdc++ in the NDK sysroot. That libstdc++ is several releases
    old and silently blocks C++23 library features (ranges::to, fold_left, ...)
    that the Android build's libc++ already provides. Re-aiming the *host tests
    only* at the MSVC STL lifts that artificial ceiling.
    """
    msvc = msvc_include_dir().parent
    sdk_include = sdk_include_dir()
    sdk_lib = newest_subdir(find_windows_sdk() / "Lib")

    includes = [msvc / "include", sdk_include / "ucrt", sdk_include / "um", sdk_include / "shared"]
    libdirs = [msvc / "lib" / "x64", sdk_lib / "ucrt" / "x64", sdk_lib / "um" / "x64"]

    compile_args = [
        "-target",
        "x86_64-pc-windows-msvc",
        *[arg for path in includes for arg in ("-isystem", str(path))],
        # MSVC's STL marks the CRT's non-conforming names (fopen, getpid, ...)
        # deprecated, and warnings are errors here -- opt out explicitly.
        "-D_CRT_SECURE_NO_WARNINGS",
        "-D_CRT_NONSTDC_NO_WARNINGS",
        # stb's x86 SIMD paths call intrinsics that NDK clang does not emit when
        # targeting MSVC -- they come out as undefined symbols in lld-link, and
        # they are also the only part of stb that trips -Wmissing-braces /
        # -Wc++11-narrowing under -Werror. Disabling SIMD fixes both at once and
        # makes the rasterizer deterministic (the device build uses NEON anyway).
        "-DSTBIR_NO_SIMD",
        "-DSTBI_NO_SIMD",
    ]
    link_args = [
        "-fuse-ld=lld-link",
        "-B",
        str(find_msvc_llvm_bin()),
        *[arg for path in libdirs for arg in ("-L", str(path))],
        *find_msvc_zlib(),
    ]
    return compile_args, link_args


# Names for the shell-facing CLI below.
PROBES: dict[str, object] = {
    "ndk-home": find_ndk_home,
    "ndk-clang": find_ndk_clang,
    "visual-studio": find_visual_studio,
    "msvc-llvm-bin": find_msvc_llvm_bin,
    "msvc-clang": find_msvc_clang,
    "msvc-include": msvc_include_dir,
    "windows-sdk": find_windows_sdk,
    "sdk-include": sdk_include_dir,
    "zlib-include": zlib_include_dir,
    "zlib": find_msvc_zlib,
}


if __name__ == "__main__":
    # Lets the bash helpers (scripts/check-one.sh) reuse these lookups instead
    # of carrying their own copies of the same install paths.
    if len(sys.argv) != 2 or sys.argv[1] not in PROBES:
        raise SystemExit(f"usage: toolchain_probe.py {{{','.join(PROBES)}}}")
    value = PROBES[sys.argv[1]]()
    if isinstance(value, (list, tuple)):
        print(" ".join(str(item) for item in value))
    else:
        print(value)
