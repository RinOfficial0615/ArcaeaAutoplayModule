# Build Pitfalls

Three layers of blind spots, each subtler than the last: **compiling ≠ linking ≠ loading ≠ building correctly**. The first two have ready-made checks, the third needs one extra real build, and the fourth can only be judged by artifact size.

## Layer 1: Compiling ≠ Linking (`std::format` Must Come with Ryu)

`check_production_conformance()` only compiles and never links, so it cannot catch this one.

**Symptom**: as soon as `src/` contains a single use of `std::format`, the release build fails at link time with four undefined symbols:

```
ld.lld: error: undefined symbol: std::_LIBCPP_ABI_NAMESPACE::__d2s_buffered_n(char*, char*, double, chars_format)
ld.lld: error: undefined symbol: std::_LIBCPP_ABI_NAMESPACE::__d2exp_buffered_n(char*, char*, double, unsigned int)
ld.lld: error: undefined symbol: std::_LIBCPP_ABI_NAMESPACE::__d2fixed_buffered_n(char*, char*, double, unsigned int)
ld.lld: error: undefined symbol: std::_LIBCPP_ABI_NAMESPACE::__f2s_buffered_n(char*, float, chars_format)
>>> referenced by ryu.h:... / libarc_helper.so.lto.o
```

**Root cause**: `__visit_format_arg` performs a **runtime switch on the type** held by the `basic_format_arg` variant, so it instantiates the visitor for every alternative of the variant, including `double` and `float`. That means `formatter<double>::format` / `formatter<float>::format` **are instantiated whether or not you ever pass a floating-point value**, and they in turn reference Ryu's four entry points. The type is a runtime value, so LTO cannot eliminate those branches.

Two pieces of measured evidence:

1. `Log.cpp` / `ZipArchive.cpp` / `CustomChartReportWriter.cpp`, which only format integers and strings, pull in these symbols all the same.
2. After changing both `AffNormalizer::FormatFloat` (`{:.2f}`) and `CustomChartImporter::FormatBpm` (`{:g}`) to `snprintf`, the link **still failed**. So "use fewer floating-point arguments" is not a way around it.

**The fix must not land inside the submodule.** In the vendored libcxx's own `Android.mk`, `libcxx_sources` only globs `src/*.cpp` and `src/filesystem/*.cpp`, missing `src/ryu/*.cpp` (the implementation files have always been in the tree, just never compiled in). The most intuitive change is to add one line to that `Android.mk` — **do not do that**: `third_party/libcxx` is a git submodule, and the hints in `docs/` and `run_host_tests.py` tell people to run `git submodule update --init --recursive`; one run of that command wipes the fix. The project's own convention says the same: `build.ps1` reads "Keep the pinned LSPlt submodule clean... patch is applied to a disposable build copy", and `patches/lsplt-live-plt.patch` takes exactly that route.

The correct place is the top-level `Android.mk`: create a separate `libcxx_ryu` static library module that compiles those three files, and add it to `arc_helper`'s `LOCAL_STATIC_LIBRARIES`. The compile flags need to mirror the ones `third_party/libcxx/Android.mk` uses for its own sources (`_LIBCPP_BUILDING_LIBRARY`, `_LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS`, etc.) — these are libc++ internals; with the wrong flags the exported symbols will not match the declarations the rest of the module sees.

**Cost**: the three `.cpp` files total 93,054 bytes of source, but after LTO only `__d2s_buffered_n`(3312) + `__d2exp_buffered_n`(3276) + `__d2fixed_buffered_n`(2384) = **8,972 bytes** remain in the final `.so`; `__f2s_buffered_n` gets inlined away. That is **0.71%** of the stripped `.so` (1,261,784 bytes).

**One thing you do not need to worry about**: `src/charconv.cpp` (floating-point `to_chars`) also references these four symbols, but because nobody uses the entire call chain, LTO eliminates it completely — no trace of `charconv` shows up in the link error, and there is no need to change the configuration for it.

### Run One Real Build Before Every Release

Changes that touch template instantiation, static initialization, or cross-translation-unit symbols are not covered by compile checks alone:

```powershell
.\build.ps1 --rel
```

`--ndk-home <path>` is **usually not needed**. By default `build.ps1` first reads `ANDROID_NDK_HOME`; if unset it falls back to
`ndk/` under `ANDROID_SDK_ROOT` / `ANDROID_HOME`; then it **takes that path's parent as the search root and picks the
highest version among the sibling NDKs**. So it does not matter if `ANDROID_NDK_HOME` points at an old version —
verified: with it pointing at r27 and r30 among the siblings, the script still picks r30.

Only one situation genuinely needs `--ndk-home`: **the target NDK is not among those siblings**. And there is an inverse
trap when passing it: an explicit value is treated as the sole candidate and the upward search for a newer version no longer
happens — passing any r29-or-older directory fails outright with "Selected NDK is ... (<= r29)". To let the script
choose by itself, pass the `ndk/` root.
Both the default (DEBUG) and `--rel` modes have been verified.

## Layer 2: Build Success ≠ Build Correctness (Discard `.o` When Switching Modes)

**When switching from DEBUG to `--rel`, ndk-build does not rebuild the `.o` files.** Verified: right after a DEBUG build, a
`--rel` run produced only 3 new `.o` files out of 150; the other 147 reused the DEBUG artifacts and the build finished in
25 seconds. The reason is that `APP_CPPFLAGS` carries `-flto`, so the `.o` files hold bitcode, and ndk-build's dependency
check cannot notice that the optimization level changed.

The consequence is not an error but a **silently half-optimized `.so`**:

| Build | stripped `.so` |
| --- | --- |
| Reusing the DEBUG `.o` files | 1,512,696 bytes |
| Discarding the `.o` files and building from scratch | **1,261,784 bytes** |

A difference of **250,912 bytes (+19.9%)** — the mixed-in `-O0` code hides inside that 20%. Such a package installs and runs
normally, so no check will ever raise an alarm.

**`build.ps1` now handles this automatically.** It records the current mode into `build/.last-build-mode`; if the next build
finds that the mode changed, it deletes `build/obj` before handing over to ndk-build. A few implementation trade-offs:

- The marker is written **after strip and before packaging**, not at the very end of the script. The last step deletes
  `build/module_tmp` and can fail for reasons like file locks or permissions; if the marker were written after it, it would
  never land and the guard would be dead.
- If the deletion fails, the script **exits with an error** instead of continuing — continuing is exactly what produces the
  half-optimized package; better to have a human delete it by hand.
- Repeated builds in the same mode are **unaffected**: verified, 145 of 147 `.o` files were reused in 15 seconds
  (31 seconds from scratch).

How to tell whether you have been bitten: **look at the artifact size, not the build time** — from-scratch and reused-`.o`
builds differ by only a few seconds, invisible to the eye.
Correct release values: stripped `.so` **1,261,784**, zip **612,629**.

## Layer 3: Linking ≠ Loading (Symbol Audit)

The previous section was "the linker will complain"; this section is the kind **even the linker does not complain about**.

Under the premise of `APP_STL := none` + the vendored libc++, the `.so`'s external dependencies should be only the NDK
platform libraries. But three things are reported by no existing step in the pipeline:

1. A symbol falls inside libc++'s ABI namespace — meaning we are in fact depending on a system STL, exactly the coupling
   `APP_STL := none` is meant to exclude. The Ryu incident is its close relative: that one exploded at link time and so was
   found; the "links fine, explodes at load" variant is far subtler.
2. A strong symbol is not provided by any NEEDED library — the link let it through because of a lax policy like
   `-Wl,--allow-shlib-undefined`, and it fails at `dlopen` time on a real device.
3. Someone changed the library list in `Android.mk` and the dependency graph drifted quietly.

`scripts/verify_linked_symbols.py` checks exactly this; run it once after building:

```bash
python scripts/verify_linked_symbols.py build/obj/local/arm64-v8a/libarc_helper.so
```

When `--ndk-home` is omitted it goes through `tests/toolchain_probe.py`, the same resolution as `build.ps1` (environment
variables → documented default locations → drive scan, highest version wins), so even on multi-NDK machines it lands on the
one the build actually used. The report header still prints an `ndk :` line for cross-checking.

**You must pass the unstripped copy** (`build/obj/local/...`). ndk-build strips the `build/libs/...` copy; the script refuses
it outright with a hint of "is it stripped?" instead of giving you a fake PASS.

It does four things: read the `.so`'s NEEDED list → take those libraries' exported symbols from the matching API directory of
the sysroot → check each undefined symbol against them → classify into "libc++ leakage / other C++ symbols / missing strong
symbols / missing weak symbols". The first three must be 0; the fourth is informational only.

### Current Baseline (release, complete from-scratch build, android-21)

| Item | Value |
| --- | --- |
| NEEDED | `libandroid libc libdl liblog libm libz` |
| Undefined symbols | **153** (148 strong + 5 weak) |
| libc++ ABI namespace leakage | **0** |
| Other undefined C++ symbols | **0** |
| Strong symbols with no provider | **0** |

Running the same checks on a DEBUG build gives 159 (153 strong + 6 weak); both PASS. The difference comes entirely from the
compiler and is normal:

- DEBUG alone has `__fgets_chk` / `__memmove_chk` / `__memset_chk` / `__strcpy_chk` / `__strlcpy_chk` / `__strrchr_chk` /
  `__assert2`; at the corresponding positions release has `fgets` / `read` / `strlcpy` / `vsnprintf` — i.e. the fortify
  `__*_chk` variants get inlined back into plain calls plus bounds checks under `-O2`.
- `memfd_create` / `strchr` / `write` appear only in DEBUG; they are unused paths eliminated by LTO.

The 4 symbols unique to `release` are exactly that non-fortify group above; no dependency appeared out of thin air.

Those 5 weak symbols are standard new-API version gating, not a problem: `__cxa_thread_atexit_impl` (libc++'s `thread_local`
destructors) and `sig{addset,emptyset,fillset,ismember}64` (the latter four come from `libarc_helper_ext.a`). Verified that
they **do not exist** in API 21's `libc.so` and do exist in API 36, so the weak references are a necessary safeguard: if the
runtime cannot find them, it falls back to the old path. Not one is referenced directly by first-party `src/` code; all come
from third-party static libraries.

The script ships `--self-test`, which uses synthetic `llvm-nm` output to verify that the parser and regexes themselves are
intact — worth keeping, because for a healthy artifact the first three items are **always 0**, and a check that only ever
prints "none" is indistinguishable from a check that has never seen data. The NDK's own `libc++_shared.so` also passes
cleanly, so with no real positive/negative samples available, synthetic input is all there is.

> A pitfall we hit: `llvm-nm` is a native Windows program; feeding it a Git Bash path like `/d/...` reports "No such file or
> directory", so the script routes everything through `Path.resolve()`. Also, `llvm-nm`'s output format is **three columns
> for defined lines (address + type + name) and two columns for undefined lines**; splitting left to right discards every
> defined symbol — the first implementation fell into exactly this, manifesting as the fake failure "platform libraries
> provide 0 symbols, 159 all MISSING". The script handles it with `rsplit(None, 2)`, and the self-test has a corresponding
> regression case.
