# Host Toolchain

For a comparison of the two standard libraries, see the [overview](index.md). This page covers why the host side is configured this way, what the prerequisites are, and how to verify a single file quickly.

## Why Host Tests Use the MSVC STL

On Windows, NDK clang++ falls back to the **MinGW target** (`x86_64-w64-windows-gnu`) by default, and thereby uses the **MinGW libstdc++ 13.2** from the NDK sysroot. That libstdc++ is older than the production libc++ and needlessly blocks features the production side already supports, such as `ranges::to`, `fold_left`, and `ranges::contains`.

With the MSVC STL, **the host tests' standard library is broader than the production one**. This creates an inverse risk: if a feature exists in the MSVC STL but not in the vendored libcxx, host tests pass while the release build breaks.

That risk is contained by **`check_production_conformance()`** at the end of `run_host_tests.py`: it compiles every first-party source file under `src/` with the real release flags (arm64, `-fno-exceptions -fno-rtti`, `-nostdinc++` pointing at the vendored libcxx). Features beyond the allowed set fail here instead of at packaging time.

On top of that, the MinGW target **cannot link at all** with the current NDK: clang's MinGW driver wants GCC's `libgcc_eh` / `libgcc_s`, which no NDK ships (under `ndk/toolchains/llvm/prebuilt/<host>/lib/x86_64-w64-windows-gnu/` there are only `libc++.a` and `libc++abi.a`). So the host side always uses the MSVC target, no exceptions.

## Build Prerequisites

`run_host_tests.py` probes for these automatically and fails with a clear error when something is missing. The key points:

- **NDK ≥ 30**. The MSVC STL `static_assert`s against clang below 20 ("expected Clang 20 or newer"); NDK 27's clang 18 is rejected outright.
- **Visual Studio** (with the C++ workload). LLVM's `lld-link.exe` is provided by VS — the NDK only ships `ld.lld` (ELF/MinGW), and linking the MSVC target requires the former.
- **Windows SDK** (UCRT headers).
- **An MSVC-built zlib**. The NDK sysroot only has MinGW `.a` files, which are an ABI mismatch. The script searches for a COFF `.lib` in the order `ZLIB_ROOT` → conda → vcpkg.

Overridable environment variables: `ANDROID_NDK_ROOT` / `ANDROID_NDK_HOME` / `ANDROID_SDK_ROOT` / `ANDROID_HOME`, `MSVC_INSTALL`, `WINDOWS_SDK_DIR`, `ZLIB_ROOT`.

All machine-specific path probing lives in `tests/toolchain_probe.py` (environment variables → documented default locations → scanning existing drive letters); scripts under `scripts/` reuse the same implementation. **Do not** hard-code yet another copy of the install paths anywhere else.

## Fast Feedback Loop: `scripts/check-one.sh`

`check_production_conformance()` checks **all** of `src/*.cpp` and takes over a minute per run. When changing a single file, use this script for a two-sided compile; results come back in seconds:

```bash
bash scripts/check-one.sh src/utils/Log.cpp src/manager/ConfigManager.cpp
```

It compiles each file twice, with flags aligned with `check_production_conformance()`:

1. **production**: arm64 + vendored libcxx + release flags (`-fno-exceptions -fno-rtti -Werror`).
2. **host**: `x86_64-pc-windows-msvc` + MSVC STL.

Output is `prod OK` / `host OK` / `FAIL` (with the first 30 lines of errors). Two pitfalls:

- `tests/stubs` is added **only** to the host include path. Its `unistd.h` would shadow the identically named header in the NDK sysroot (`-I` beats the sysroot) and hide `getpid` in production compiles.
- A few files cannot be host-compiled; the script's `HOST_SKIP` marks them `host SKIP` and runs the production side only.
  Re-verified entry by entry on 2026-08-29: **the list used to have 17 entries, of which 11 actually compile fine**,
  silently forfeiting host-side verification (including `Autoplay.cpp`, `HookManager.cpp`, `RuntimeMemory.cpp`,
  `AddressResolver.cpp`, and others). Only 6 entries remain, each annotated in the script with its reason:
  - 5 genuinely depend on Android headers (`android/asset_manager.h`, `jni.h`, `dlfcn.h`) —
    that header set needs a versioned ELF triple and Bionic's `off64_t`, unsolvable on the MSVC target.
  - 1 (`NetworkManager.cpp`) has **nothing to do with Android**: the MSVC STL rejects using
    `std::atomic_load_explicit` on a `shared_ptr` (deprecated since C++20, STL4029), `-Werror` turns that into a
    compile failure, and the production build never even sees it.
    **Do not "fix" it** — the C++20 replacement `std::atomic<std::shared_ptr<T>>` does not exist on the production side:
    the vendored libcxx only provides `std::atomic` specializations for integral / pointer / floating-point types
    (see `third_party/libcxx/include/atomic`); applying it to `shared_ptr` reports
    `_Atomic cannot be applied to ... which is not trivially copyable` (verified 2026-08-29).
    So those free functions are currently the only form that compiles on both sides; keep them.
  **This list goes stale as the toolchain changes** — after switching NDK / MSVC, re-verify it entry by entry; do not just append to it.
- Artifacts are written under `.tmp/check-one/`, **not under `build/`**: `build.ps1` wipes that tree, and a long scan would suddenly start reporting `unable to open output file` for everything partway through. The script recreates the directory before every compile, so parallel builds are unaffected.
- Do not pipe a full sweep through `| tail`: `bash scripts/check-one.sh $(find src -name '*.cpp' | sort)` covers 37 files and about 70 lines of output; truncating it means you cannot see whether the first few files were OK or FAIL.
