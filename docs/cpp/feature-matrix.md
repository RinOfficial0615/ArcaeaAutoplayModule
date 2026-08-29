# Measured Feature Matrix

The probes live under `scripts/` and deliberately carry **no version-macro guards** — a missing feature is a compile failure. That is the only trustworthy evidence: a defined feature-test macro does not mean the feature is usable.

Run everything from the repository root. **Do not use `-o /dev/null`** — on Windows clang cannot open it and reports
`unable to open output file`, which looks like a missing feature. Likewise, do not put artifacts under `build/`
(`build.ps1` wipes that tree).

The two probes **have different purposes; do not mix them up**:

- `scripts/refactor_probe.cpp` contains only the features the current refactor actually uses and **must compile on both sides**.
  The easiest way to run it is to reuse `check-one.sh` (identical flags):
  `bash scripts/check-one.sh scripts/refactor_probe.cpp`
- `scripts/capability_probe.cpp` is the full-matrix probe that **deliberately includes non-existent features**;
  **it is guaranteed to fail on the production side** — those 7 `no member named '...'` errors are the "gap list" itself,
  and the table below is derived from them. It should come out all green on the host side. Do not treat it as a regression test.

By hand:

```bash
mkdir -p .tmp/probe

# production baseline: arm64 + vendored libcxx + release flags
"$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++" \
  --target=aarch64-linux-android21 -std=c++23 -fno-exceptions -fno-rtti \
  -Wall -Wextra -Werror -O2 -c scripts/capability_probe.cpp \
  -o .tmp/probe/probe.o \
  -nostdinc++ -isystem third_party/libcxx/include \
  -D_LIBCPP_NO_EXCEPTIONS -D_LIBCPP_NO_RTTI \
  -D_LIBCPP_ABI_NAMESPACE=_LIBCPP_ABI_NAMESPACE

# host baseline: MSVC STL
"$VS/VC/Tools/Llvm/x64/bin/clang++.exe" -std=c++23 -Wall -Wextra -Werror \
  -target x86_64-pc-windows-msvc \
  -isystem "$MSVC/VC/Tools/MSVC/<ver>/include" \
  -isystem "$WINSDK/Include/<ver>/ucrt" \
  -isystem "$WINSDK/Include/<ver>/um" \
  -isystem "$WINSDK/Include/<ver>/shared" \
  -D_CRT_SECURE_NO_WARNINGS -c scripts/capability_probe.cpp \
  -o .tmp/probe/probe-host.o
```

## The Matrix

| Feature | Standard | production libcxx 19 | host MSVC STL 145 | usable in `src/`? |
| --- | :-: | :-: | :-: | :-: |
| concepts / `requires` | 20 | ✅ | ✅ | ✅ |
| `std::span`, `std::string_view` | 20/17 | ✅ | ✅ | ✅ |
| `starts_with` / `ends_with` (`string_view`) | 20 | ✅ | ✅ | ✅ |
| ranges core (`sort` / `find` / `filter` / `transform`) | 20 | ✅ | ✅ | ✅ |
| `std::ssize`, `std::to_underlying` | 20/23 | ✅ | ✅ | ✅ |
| three-way comparison `<=>`, designated initializers | 20 | ✅ | ✅ | ✅ |
| `std::format` | 20 | ✅ | ✅ | ⚠️ needs Ryu linked, see [build pitfalls](build-pitfalls.md) |
| `std::from_chars` (integers) | 17 | ✅ | ✅ | ✅ |
| `std::from_chars` (floating-point) | 17 | ❌ | ✅ | ❌ |
| `std::expected` | 23 | ✅ | ✅ | ✅ |
| `optional` monadic operations (`and_then` / `transform` / `or_else`) | 23 | ✅ | ✅ | ✅ |
| `std::ranges::to` | 23 | ✅ | ✅ | ✅ |
| `std::ranges::fold_left` | 23 | ✅ | ✅ | ✅ |
| `std::ranges::contains` | 23 | ✅ | ✅ | ✅ |
| `std::ranges::starts_with` / `ends_with` | 23 | ✅ | ✅ | ✅ |
| `std::views::zip` | 23 | ✅ | ✅ | ✅ |
| `std::ranges::find_last` | 23 | ❌ | ✅ | ❌ |
| `std::ranges::fold_right` | 23 | ❌ | ✅ | ❌ |
| `std::views::chunk` / `slide` | 23 | ❌ | ✅ | ❌ |
| `std::views::join_with` | 23 | ❌ | ✅ | ❌ |
| `std::views::enumerate` | 23 | ❌ | ✅ | ❌ |
| `std::views::cartesian_product` | 23 | ❌ | ✅ | ❌ |

**`find_last` / `fold_right` / `chunk` / `slide` / `join_with` / `enumerate` / `cartesian_product` are forbidden in `src/`.** They compile in host tests but not in the release build. `check_production_conformance()` will catch them, but do not rely on it as the safety net.

To ask "is this element in the range" → `std::ranges::find(range, value) != range.end()`.
To iterate with an index → a plain `for (size_t i = 0; ...)`; this project's difficulty slots and config entries are small fixed-length tables, and an index loop is clearer than an `enumerate` pipeline.

> The header for `chunk_by` (`include/__ranges/chunk_by_view.h`) exists in the vendored libcxx, but `src/` does not use it and it is not part of the probe. Run the probe before using it.

## Numeric Parsing Still Uses `strtod` / `strtoll`

Do not switch to `from_chars`, for two reasons, both verified:

1. **The production side has no floating-point version.** `_LIBCPP_VERSION` is `190000` (LLVM 19), but `third_party/libcxx/include/charconv` only includes `from_chars_integral.h`; the entire `third_party/libcxx/include/__charconv/` tree **lacks** `from_chars_floating_point.h` (the floating-point `to_chars` is there, though). This is a result of the upstream `Minimize libcxx` trim. So `from_chars(double)` falls onto the `= delete`d `bool` overload and reports `call to deleted function`.
2. **The integer version does not accept a leading `+`**, while `strtoll` does. libcxx's `__sign_combinator` only matches `'-'`, which is also what the standard requires. `AffNormalizer`'s `LooksLikeInteger` explicitly handles `+`/`-` prefixes, which means `+5` can appear in AFF text — switching to `from_chars` would silently stop parsing a set of tokens.

Corollary: as long as `strtod` / `strtoll` are in use, tokens must first be copied into a `std::string` to get NUL termination, so the one allocation in `TrimWhitespace` inside `BoundedParse.hpp` is **forced**, not waste that can be deleted in passing.

## On Upgrading the Vendored libcxx

We tried upgrading to upstream master ("Update libcxx to NDK r30", LLVM 21); the conclusion is **not worth it**:

- The net gain is a single feature, `find_last`.
- `chunk` / `slide` / `join_with` / `enumerate` / `cartesian_product` were **actively deleted** upstream by `Minimize libcxx` / `Remove unused sources`; upgrading to the latest does not bring them back.
- The fork ships its own Android-specific override headers (`<stdlib.h>`, `<wchar.h>`) and **cannot be built on a Windows host** (verified: `ldiv_t` unknown, `mbstate_t` missing), so host tests cannot share it either.

Moving the release build's STL for the sake of one feature is not a good trade.
