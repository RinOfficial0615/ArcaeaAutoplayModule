# Repository Guidelines

## Project Structure & Module Organization

First-party C++23 code lives in `src/`: entry points in `src/wrapper/`, runtime coordination in `src/manager/`, user-facing behavior in `src/features/`, version profiles and game layouts in `src/game/`, feature knobs in `src/config/`, reusable helpers in `src/utils/`. Host tests live in `tests/`, packaging files in `module/`, build/analysis scripts in `scripts/`, and notes in `docs/`. Detailed layouts: `docs/project-structure.md`, `docs/architecture/`, `docs/offsets/`.

Dependencies are git submodules under `third_party/`. **Never edit a submodule** — `git submodule update --init --recursive` wipes local changes, and that command is in our own docs. To change a dependency's behavior, add a module in the top-level `Android.mk` that compiles it differently (see how `build.ps1` patches LSPlt into a throwaway `build/generated/` copy).

## Reuse Before Implementing

**Do not reinvent the wheel.** Search `src/utils/`, `third_party/`, and the Internet first, then reuse established parsing, hooking, hashing, archive, and memory components.

## Modern C++ and Design Rules

- Target C++23, but **the usable feature set is set by the standard library, not the compiler**. `src/` is built against the vendored `third_party/libcxx` (LLVM 19 fork, `-fno-exceptions -fno-rtti`); host tests compile the same sources against MSVC STL. **Write `src/` against the vendored libcxx column.** Never infer availability from a clang version number — run the probes in `scripts/` (`capability_probe.cpp`, `refactor_probe.cpp`; they deliberately carry no version guards, so a missing feature fails the build) or check `docs/cpp/feature-matrix.md`.
- Before using a new library feature, add it to `scripts/refactor_probe.cpp` and compile both sides: `sh scripts/check-one.sh <file>` or the conformance sweep inside `tests/run_host_tests.py`.
- Do not over-design. Apply the deletion test and introduce a new module, seam, adapter, or abstraction only when it removes real duplication or gives callers a smaller, deeper interface.
- Host tests use the NDK clang++ with `-target x86_64-pc-windows-msvc`. **Do not switch them back to a MinGW target** — clang's MinGW driver needs GCC's `libgcc_eh`/`libgcc_s`, which no NDK ships, so the link cannot succeed.

## Machine-Specific Paths

Toolchain locations (NDK, Visual Studio, Windows SDK, zlib) are discovered at runtime, never hard-coded. **All discovery lives in `tests/toolchain_probe.py`** — it is shared by `run_host_tests.py`, `run_arcpkg_corpus.py`, `scripts/check-one.sh`, and `scripts/verify_linked_symbols.py`, so there is exactly one place to fix. Override with environment variables when the default search misses: `ANDROID_NDK_ROOT` / `ANDROID_NDK_HOME` / `ANDROID_SDK_ROOT` / `ANDROID_HOME`, `MSVC_INSTALL`, `WINDOWS_SDK_DIR`, `ZLIB_ROOT`. Run `python tests/toolchain_probe.py <name>` (see `PROBES` in that file) to see what any of them resolves to.

Do not write absolute paths, drive letters, usernames, or toolchain version directories into scripts, docs, or tests.

## Build, Test, and Development Commands

- `git submodule update --init --recursive` initializes the pinned dependencies.
- `./build.ps1` produces a debug arm64 build; it requires NDK r30 or newer and picks the newest NDK under the resolved search root. NDK r29 and older are rejected.
- `./build.ps1 --rel` builds, strips, and packages `build/ArcHelperModule.zip`.
- `./build.ps1 --rebuild --rel` forces a clean release build. `build.ps1 --help` lists the rest.
- `python tests/run_host_tests.py` compiles every host test with the newest installed NDK `clang++` and validates hashing, JSON, logging, hooks, memory patching, AFF normalization, chart importing, image resampling, and a full-source production-conformance sweep. A clean run prints 21 green lines in about 2.5 minutes; fewer lines means it died partway. **Hard prerequisite:** at least 7 sample packages under the sibling `ArcCreate` repository (`../ArcCreate/arcpkg-samples/*.arcpkg` and `../ArcCreate/raw-zip-samples/*.zip`) — the script exits otherwise. `g++` compatibility is not a test target.
- `python tests/run_arcpkg_corpus.py [dir]` imports real `.arcpkg` / `.zip` packages (default `.tmp/arcpkg`) and reports per-chart diagnostics. Set `ARC_OFFICIAL_APK` to a local Arcaea Infinity APK to also run the official AFF grammar pass; without it that pass is reported as skipped. There is no in-repo default for the APK because it is not redistributable.
- `python tests/verify_profile.py` checks pinned offsets for 6.16.2c, 6.16.8c, 7.0.0c, 7.0.1c, and 7.0.255c against `../<version>/libcocos2dcpp.so`, verifying each binary's exact SHA-256, the `setAppVersion` / `__cxa_throw` dynamic symbols, function prologues, and runtime patch sites. It also verifies the FMOD provider's `loadBGM` symbol if `.tmp/arcaea_6.16.2c_arc_helper_diag.apk` is present. Versions whose `.so` is missing are reported as skipped, not failed.
- `bash scripts/check-one.sh <src files...>` compile-checks individual files against both standard libraries in seconds.
- `python scripts/verify_linked_symbols.py <unstripped .so>` audits the built module's remaining external dependencies; pass `build/obj/local/arm64-v8a/libarc_helper.so` (the `build/libs/` copy is stripped and is rejected). Run it after any change that touches templates or cross-TU symbols.
- `python scripts/port_match.py` locates relocated ARM64 functions across game versions when porting offsets. See its module docstring for the workflow.
- Notes on the C++ baseline, toolchain, and the four build blind spots: `docs/cpp/index.md`.

## Coding Style & Naming Conventions

Use four-space indentation and follow the surrounding C++ style. Types and public methods use `PascalCase`; local variables and private fields use `snake_case`, with private members ending in `_`; constants use `kPascalCase`. Pair implementation files as `Name.hpp` and `Name.cpp`, keep code in `namespace arc_helper`, and prefer bounded parsing plus explicit validation for untrusted runtime data. The build disables exceptions and RTTI, so use status values and error strings instead. Treat warnings as failures (`-Wall -Wextra -Werror`).

## Testing Guidelines

Add focused host coverage for portable logic, naming new files `*_host_test.cpp`. Pure algorithms that only touch caller-supplied memory can be lifted from an anonymous namespace into a header-only `inline` function to gain host coverage — see `src/utils/memory/ByteScanner.hpp`; functions that read live game objects cannot be host-tested and need manual review plus a production compile. For hook profiles or layout changes, verify signatures, offsets, struct assertions, the matching game binary hash, and an arm64 release build. No coverage percentage is enforced; regressions in parsing, archive safety, configuration defaults, and unsupported-version behavior require tests.

## Commit & Pull Request Guidelines

History favors short, imperative subjects, sometimes with `feat:` or `fix:` prefixes. Keep each commit scoped. Pull requests should describe affected game versions and features, list commands run, link relevant issues, and include device logs or screenshots for runtime-visible behavior. Document new offsets and never commit game binaries, generated `build/` output, or personal `config.json` files.
