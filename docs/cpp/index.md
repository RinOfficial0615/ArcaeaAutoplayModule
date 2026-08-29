# Modern C++ Notes (C++20 and Later)

This repository builds with **C++23** (`APP_CPPFLAGS` in `Application.mk`), but the boundary of usable features is not the standard version — it is the **standard library implementation**. This documentation set records the C++20/23 patterns actually in use, the measured feature matrix, and "which features must not be touched".

## The Two Standard Libraries

| Purpose | Compiler | Standard library | Notes |
| --- | --- | --- | --- |
| **Android release build** | NDK arm64 clang | **vendored `third_party/libcxx`** (`topjohnwu/libcxx`, LLVM 19, based on NDK r28) | `APP_STL := none` in `Application.mk`; the top-level `Android.mk` includes the submodule's own `Android.mk` and links it statically |
| **host tests** | the same NDK clang++ (21) | **MSVC STL** (v145, the same one VS 2026 ships with its clang 22) | `tests/run_host_tests.py` points there with `-target x86_64-pc-windows-msvc` |

> **What determines usable features is the standard library version, not the compiler version.** The same NDK clang 21 paired with MinGW libstdc++, NDK libc++, or MSVC STL yields three different usable sets. Before adopting any new library feature, run the feature probes first; never infer availability from a clang version number.

**Conclusion: when writing `src/`, the "vendored libcxx" column is authoritative.**

## Pages

| Document | Content |
| --- | --- |
| [toolchain.md](toolchain.md) | Why host tests use the MSVC STL, build prerequisites and environment-variable overrides, the single-file fast feedback loop |
| [feature-matrix.md](feature-matrix.md) | How to run the feature probes, the measured matrix, the forbidden list, evaluating an upgrade of the vendored libcxx |
| [build-pitfalls.md](build-pitfalls.md) | Compiling ≠ linking (`std::format` / Ryu), switching build modes must discard `.o` files, linking ≠ loading (symbol audit) |
| [usage.md](usage.md) | Adopted features, `optional` monadic operations, Ranges idioms, `expected`, coding rules |
