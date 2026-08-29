# C++20/23 Usage Conventions

Which features are usable: see the [measured matrix](feature-matrix.md). This page is "how to write it once you use it".

## Adopted

| Feature | Current landing spots | Verdict |
| --- | --- | --- |
| concepts / `requires` | `ConfigManager.hpp`, `Feature.hpp` | `ConfigScalar` and `ConfigValidator` move errors from function bodies up into interface constraints. |
| `std::remove_cvref_t` / `std::same_as` / `std::predicate` | config type deduction and validators | More direct than `is_same_v` plus `static_assert` in a function body. |
| `std::in_range` | integer JSON reads | Validate first, then narrow; avoids undefined behavior. |
| `std::ranges::find_if` | `GameProfile.hpp`, `CustomChartImporter.cpp` | Fits small read-only tables; do not introduce a registry for this. |
| `std::string_view` / `std::span` | feature names, hook registration batches, config keys | Vocabulary types for read-only borrowed values; the caller owns the lifetime. |
| `std::to_underlying` | enum logging/index conversions | Replaces hand-written `static_cast`; the enum value must already be validated. |
| `std::optional` return values | `CustomChartImporter.cpp`'s `Find*` / `JsonFind` / `ReadEntryText` | Replaces "raw pointer + `nullptr` sentinel + out-parameter". |
| `and_then` / `or_else` / `transform` | same file, lookup chains | Use only for "look once more / switch source / switch type"; never to carry side effects. |
| `std::format` | `Log.cpp`, `CustomChartImporter.cpp`, `ZipArchive.cpp`, `CustomChartReportWriter.cpp`, `AffNormalizer.cpp` | Replaces `snprintf` fixed buffers, `ostringstream`, and `"a" + to_string(i) + "b"` concatenation. |
| `std::ranges::sort` / `stable_sort` / `copy` / `count_if` / `any_of` / `all_of` / `none_of` | whole codebase | Replaces the `std::` versions (drops the `.begin(), .end()` noise); see "Ranges usage" below. |
| `std::ranges::to` | `CustomChartManager.cpp` | A `filter` + `transform` pipeline materializes directly into a vector; no hand-written `push_back` loop. |
| `std::erase_if` | `HookManager.cpp` | Replaces hand-written `for (auto it = …; it != end();)` + `erase(it)`; the predicate can be "try to restore" itself. |
| `std::views::reverse` | `PatchTransaction.cpp`, `CustomChartReportWriter.cpp` | Replaces `rbegin()` / `rend()`; can join filter/transform on the same pipeline. |
| `std::views::split` | `AffNormalizer.cpp`, `ProcMaps.cpp`, `ScopeConfig.hpp` | Replaces `istringstream` + `getline`; pairs with C++23's `(It, End)` `string_view` constructor. |
| `std::views::keys` / `values` | `CustomChartAssetIndex.cpp` | Take key/value views off a map directly; saves the `.first` / `.second`. |
| `std::span` (including fixed-length `span<const T, N>`) | `AssetVirtualizer.cpp`, `ExecUtils.cpp`, `NetworkManager.cpp`, `AddressResolver.cpp`, `Sha256.cpp` | Wrap the raw `[begin, end)` pairs handed over by the game into ranges for the ranges algorithms; the fixed-length version puts the length requirement into the type. |
| `std::format_to` + `back_inserter` | `CustomChartConfig.h` | Appends into an already `reserve`d `std::string`, replacing `append(std::to_string(...))`. |
| `string_view` comparison instead of `strcmp` | `GameProfile.hpp` | Value semantics, no `<cstring>` needed; still null-check first when the pointer may be null. |

## `std::optional` Monadic Operations (C++23, P0798)

Lookup-style functions return `std::optional` throughout; never "raw pointer + `nullptr` sentinel + out-parameter". The semantics of the three operations:

| Operation | Shape | Hard requirement on the callable | On empty |
| --- | --- | --- | --- |
| `transform(f)` | `T -> U` | `U` must be a non-array object type and **must not be a reference** | short-circuits, returns `nullopt`; `f` does not run |
| `and_then(f)` | `T -> optional<U>` | must return some `std::optional` specialization; **may change the type** | short-circuits, returns `nullopt`; `f` does not run |
| `or_else(f)` | `() -> optional<T>` | must return **exactly the same** `std::optional<T>`; **may not change the type** | calls `f` and returns its result |

Corollary: if `f` inside `transform` returns an optional itself, you get `optional<optional<T>>`; that is the moment to use `and_then`.

### Pitfalls

1. **`or_else` has no "return void, do side effects only" overload.** The **P0798R8 proposal** contains one, but it **never made it into the final C++23 standard**. The standard wording is "the program is ill-formed if `remove_cvref_t<invoke_result_t<F>>` differs from `std::optional<T>`"; libstdc++ turns this into a `static_assert`:

   ```cpp
   opt.or_else([] { ARC_LOGW("miss"); });            // compile error
   opt.or_else([] { return std::nullopt; });         // compile error: cannot deduce optional<T>
   opt.or_else([]() -> std::optional<T> {            // correct: spell out the return type
       ARC_LOGW("miss");
       return std::nullopt;
   });
   ```

   This project's landing spot is `ReadEntryTextByName()`: on chain failure it appends `error = "entry missing"`, but must explicitly `return std::nullopt`.

2. **`value_or` evaluates eagerly.** `opt.value_or(ExpensiveDefault())` constructs the default value whether or not the optional has one. For laziness use `or_else`. `JsonBpmText()` uses `or_else` so that `FormatBpm(fallback)` only formats when the lookup missed.

3. **A `transform` callable must not return a reference.** Returning a pointer is fine, but do not return a pointer to a member of a temporary — `transform` only extends the lifetime to the end of the call. This project returns `const Entry *`, pointing at long-lived elements of `archive.Entries()`, which is safe.

4. **Do not use `and_then` to carry side effects.** `AddDiagnostic`, file writes, and state changes stay after `if (!x)`. Every step of the chain should be "a lookup that may fail"; otherwise the pipeline is just hiding control flow.

5. **`or_else` on an empty optional overwrites an existing error.** `ReadEntryTextByName()` calls `error.clear()` first, and inside `or_else` guards with `if (error.empty())`, so a genuine decompression failure is not wiped out by "entry missing".

### Project Conventions

- When the optional stores a `const T *`, `*opt` is a `const T *` and `**opt` is a `const T &`. When you need a reference, bind it to a local reference first (`const Entry &audio_source = **audio_entry;`); do not embed `**x` in long expressions.
- A **consumer** that already holds the optional uses `if (opt)` / `value_or` / `*`; there is no need to force chaining. Only "look once more from here" warrants `and_then`.
- Parameters take `std::optional<const Json *>` directly (`JsonString` / `JsonNumber` / `JsonBool`, etc.); call sites keep their shape while the `nullptr` checks disappear.

## Ranges Usage

- `std::ranges::find_if` replaces hand-written loops for single-element lookup. Helpers that return a **slot index** use it too — the iteration is `find_if` and the index comes from `it - begin()`, rather than keeping a counter in step with the array length (`Autoplay::FindTouchByNote` / `FindFreeTouchIndex`).
- `std::views::filter` expresses "exactly one" semantics (`OnlyEntry()` judges 0/1/many in a single pass with `begin()` / `++first == end()`). filter is lazy; it does not scan a second time just to count.
- **Lifetime**: a temporary view from `views::filter` lives only within the current full expression; **never save it across statements or put it into a container**. `archive.Entries()` returns a const reference, the filter holds a `ref_view`, and the pointers it yields still point into the archive's own vector — safe as long as you do not leave the `Archive` scope.
- **Do not** rewrite fixed-length index loops as pipelines just to use `views::zip` / `enumerate`.
- Parsing steps with error states and capacity limits do not get long pipelines; explicit loops are easier to audit for bounds.
- **When you need the value from a lookup, use the member `find`, not `ranges::contains`.** `contains` only answers "is it there", and `contains` + `at` is two lookups. `std::ranges::contains` fits projected comparisons (e.g. `contains(pages_, page, &PageRecord::address)`); for containers with their own `contains(key)` such as `std::map` / `nlohmann::json`, prefer the member version.

### A Few Recurring Idioms

**1. No need to branch on `find` returning npos.** `substr` / counting both read `npos` as "to the end":

```cpp
// truncate before the query string; without a '?' keep the whole thing
const std::string_view path = value.substr(path_start);
const size_t copied = path.substr(0, path.find_first_of("?#")).copy(out, out_size - 1);
out[copied] = '\0';
```

**2. `find_last_not_of(...) + 1` naturally handles all-whitespace input.** On all-whitespace input `find_last_not_of` returns `npos`, `npos + 1 == 0`, and `substr(0, 0)` yields an empty string:

```cpp
std::string_view TrimView(std::string_view value) {
    const size_t begin = value.find_first_not_of(kBlanks);
    if (begin == std::string_view::npos) return {};
    return value.substr(begin, value.find_last_not_of(kBlanks) - begin + 1);
}
// trimming trailing slashes works the same: raw.substr(0, raw.find_last_not_of("/\\") + 1)
```

**3. `string_view::copy` returns the number of bytes actually written**, exactly what you need to terminate a fixed-length `char[]`, saving the manual `strlen` + `min` + `memcpy` triple.

**4. `views::split` + the C++23 `(It, End)` constructor** replaces `istringstream` + `getline`:

```cpp
constexpr auto LinesOf(std::string_view text) {
    return std::views::split(text, '\n') | std::views::transform([](auto &&line) {
        std::string_view view(line.begin(), line.end());
        if (view.ends_with('\r')) view.remove_suffix(1);
        return view;
    });
}
```

**5. Keyword lookup tables** replace switch / if chains (`AffOfficialParser.cpp`): a `constexpr std::string_view kPunctuation` paired with a parallel `std::array`, indexing with `kPunctuation.find(c)`, and keyword lookup via `std::ranges::find(kKeywords, ident, &Keyword::first)`.

**6. `nlohmann::json` keys can be `std::string_view`.** Its `object_comparator_t` is the transparent `std::less<>`, and `is_usable_as_basic_json_key_type` explicitly accepts types convertible to `string_view`, so `find` / `contains` / `at` / `operator[]` never need to construct a `std::string` first:

```cpp
if (!object.contains(key)) return std::nullopt;   // key is a string_view
encoded_value = object.at(key);
```

And there is a corresponding **ban**: `std::ranges` algorithms applied to a json object iterate over the **values**, not the keys (dereferencing a `json` iterator yields `json&`), so `ranges::contains(obj, key)` compares the key against the values. Object lookups always use member functions.

**7. `std::span` replaces "pointer + length" stepping.** Fixed-length buffers put the length into the type (`std::span<const uint8_t, 64>`, `Sha256::Compress`); runtime lengths are consumed with `subspan`, saving the paired `data += n; size -= n;` two-variable bookkeeping:

```cpp
void Update(std::span<const uint8_t> data) {
    while (!data.empty()) {
        const size_t n = std::min(data.size(), block_.size() - used_);
        std::memcpy(block_.data() + used_, data.data(), n);
        data = data.subspan(n);          // one assignment, not two
    }
}
```

`first(i)` also turns "inner `for (j = 0; j < i; ++j)`" into `for (const auto &prev : items.first(i))`, reading directly as "only the accepted prefix" (`HookManager::CommitInlineHook`).

**8. `std::format_to(std::back_inserter(s), ...)` appends into an already `reserve`d buffer**, keeping intermediate results from passing through a temporary `std::string`:

```cpp
path.append(kSongsPrefix);
path.append(song_id);
std::format_to(std::back_inserter(path), "/{}.aff", difficulty);  // replaces append(std::to_string(d))
```

**9. `string_view(a) == string_view(b)` instead of `strcmp`.** A value comparison rather than a byte scan, and no `<cstring>` needed for it. The only pitfall: `std::string_view(nullptr)` is UB, so null-check first when the pointer may be null:

```cpp
inline bool GameVersionMatches(const char *actual, const char *expected) {
    return actual && expected && std::string_view(actual) == std::string_view(expected);
}
```

## `std::expected`

Config loading, importing, and dynamic symbol resolution all have the "success value / failure reason" shape. When the caller needs to distinguish errors, prefer `std::expected<T, Error>` or an equivalent project status type; do not use exceptions — the Android release build explicitly disables exceptions.

When migrating, keep existing bool call sites unchanged; adopt at an internal seam first, then unify error logging.

## Conditionally Adopted

| Feature | Applicability |
| --- | --- |
| `std::format` / `std::print` | **Already adopted in production code** (log prefixes, file names, cache paths, AFF time rewriting). The formatting core of `Log.cpp` is still `vsnprintf` (needs `%s` varargs + UTF-8-safe truncation), but every **concatenation point** has moved to `std::format`. |
| `std::mdspan` | Game layouts are a few fixed-offset fields, not matrix math; not worth it currently. |
| deducing `this` | No deep module currently needs to merge const/non-const member templates; do not rewrite Feature just to show off syntax. |
| long ranges pipelines | Use only when they clearly improve readability without hiding bounds checks; the set of usable views is in the measured matrix. |
| `std::source_location` | This project's logging requires keeping only the basename at compile time, and the macro already turns `BaseName(__FILE__)` into a path-free string. Unless the logging interface changes to take a `source_location` with verified binary overhead, keep the current macro. |
| modules, coroutines, `std::execution` | No real benefit under the NDK build, Zygisk loading, and the no-RTTI/no-exceptions constraints; not adopted. |

## Coding Rules

1. **Validate before use.** Confirm with a minimal compile probe **without version-macro guards** that a feature is usable in the **production standard library** (the vendored libcxx). A macro being defined ≠ the feature being usable; host tests passing ≠ the release build compiling.
2. Prefer vocabulary types, concepts, `constexpr`, and standard algorithms; if a construct makes bounds, error paths, or runtime layout harder to read, keep clear explicit code.
3. No registry, DI, reflection, or layers of wrappers for a single call site. Apply the deletion test: if removing the candidate module merely moves the complexity to several callers, it provides no real leverage; keep it inline instead.
4. Runtime addresses, files, ZIPs, JSON, and network responses must all be range/capacity validated before conversion or invocation. Modern syntax does not replace validation.
5. Third-party code does not participate in `-Werror`. `third_party` comes in via `-isystem` (explicitly exempted in AGENTS.md).

## References

- [P0798R8: Monadic operations for std::optional](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2021/p0798r8.html) — the proposal. Note that the `or_else` void overload it describes was **not adopted into C++23**; defer to cppreference's wording.
- [cppreference: std::optional::or_else](https://en.cppreference.com/w/cpp/utility/optional/or_else)
- [cppreference: std::optional::and_then](https://en.cppreference.com/w/cpp/utility/optional/and_then)
- [cppreference: std::optional::transform](https://en.cppreference.com/w/cpp/utility/optional/transform)
- [cppreference: C++20 library features](https://en.cppreference.com/w/cpp/20#Library_features)
- [cppreference: C++23 library features](https://en.cppreference.com/w/cpp/23#Library_features)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Android NDK documentation](https://developer.android.com/ndk)
