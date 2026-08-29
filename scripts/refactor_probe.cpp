// Refactor probe: the exact features the src/ modernization pass wants to use,
// written UNGUARDED -- if a feature is missing, the compile fails. That is the
// only trustworthy signal (a defined feature-test macro does not mean the
// feature is usable).
//
// Both baselines are easiest to run through scripts/check-one.sh, which already
// carries these exact flag sets:
//   bash scripts/check-one.sh scripts/refactor_probe.cpp
//
// Production baseline (arm64 + vendored libcxx + release flags), run from the
// repository root. clang cannot write to /dev/null on Windows, so emit an .o;
// keep it out of build/ because build.ps1 wipes that tree.
//   mkdir -p .tmp/probe
//   "$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++" \
//     --target=aarch64-linux-android21 -std=c++23 -fno-exceptions -fno-rtti \
//     -Wall -Wextra -Werror -O2 -c scripts/refactor_probe.cpp \
//     -o .tmp/probe/probe.o \
//     -nostdinc++ -isystem third_party/libcxx/include \
//     -D_LIBCPP_NO_EXCEPTIONS -D_LIBCPP_NO_RTTI \
//     -D_LIBCPP_ABI_NAMESPACE=_LIBCPP_ABI_NAMESPACE
//
// Host baseline (MSVC STL):
//   "$VS/VC/Tools/Llvm/x64/bin/clang++.exe" -std=c++23 -Wall -Wextra -Werror \
//     -target x86_64-pc-windows-msvc \
//     -isystem "$MSVC/VC/Tools/MSVC/<ver>/include" \
//     -isystem "$WINSDK/Include/<ver>/ucrt" -isystem "$WINSDK/Include/<ver>/um" \
//     -isystem "$WINSDK/Include/<ver>/shared" \
//     -D_CRT_SECURE_NO_WARNINGS -c scripts/refactor_probe.cpp \
//     -o .tmp/probe/probe-host.o

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// views::split over a string_view + the C++23 (It, End) string_view ctor.
constexpr auto Lines(std::string_view text) {
    return std::views::split(text, '\n') | std::views::transform([](auto &&line) {
        std::string_view view(line.begin(), line.end());
        if (view.ends_with('\r')) view.remove_suffix(1);
        return view;
    });
}

int CountLines(std::string_view text) {
    int total = 0;
    for (std::string_view line [[maybe_unused]] : Lines(text)) ++total;
    return total;
}

// ranges algorithms over a container.
bool UsesRangeAlgorithms(const std::vector<std::string> &items) {
    return std::ranges::contains(items, std::string("a")) &&
           std::ranges::any_of(items, [](std::string_view item) { return item.empty(); }) &&
           std::ranges::all_of(items, [](std::string_view item) { return !item.empty(); });
}

// range-based search, the replacement for find_last/enumerate-style helpers.
bool UsesRangeFind(const std::vector<std::string> &items) {
    return std::ranges::find(items, std::string("a")) != items.end();
}

// fold_left for joins.
std::string Join(const std::vector<std::string> &items) {
    return std::ranges::fold_left(items, std::string{}, [](std::string acc, std::string_view item) {
        if (!acc.empty()) acc.push_back(',');
        acc += item;
        return acc;
    });
}

// string_view / span member contains (C++23).
bool UsesContains(std::string_view text, std::span<const int> data) {
    return text.contains('(') && text.contains("arctap") && std::ranges::contains(data, 2);
}

// ranges::transform in place.
std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](char c) {
        return static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    });
    return text;
}

// ranges::to.
std::vector<int> Squares() {
    return std::views::iota(1, 5) |
           std::views::transform([](int value) { return value * value; }) |
           std::ranges::to<std::vector>();
}

// std::from_chars, INTEGRAL ONLY.
//
// The floating-point overload does NOT exist on the production side even though
// _LIBCPP_VERSION is 190000: third_party/libcxx/include/charconv pulls in
// from_chars_integral.h but there is no __charconv/from_chars_floating_point.h
// in that tree, so from_chars(double) is a deleted/ignored overload set.
// (to_chars for floating point IS present.) Number parsing therefore stays on
// strtod, which also means every token has to be copied into a std::string for
// the NUL terminator -- strtod cannot read a string_view.
//
// Second caveat: the integral overload rejects a leading '+'. libcxx's
// __sign_combinator (__charconv/from_chars_integral.h) matches '-' only, and
// that is what the standard requires. strtoll accepts '+', so the two are not
// drop-in replacements for text that may carry an explicit plus sign.
bool ParseInt64(std::string_view text, int64_t &out) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// std::atomic<std::shared_ptr<T>> is NOT available on the production side, so
// there is deliberately no probe for it: the compile dies with
//   _Atomic cannot be applied to type 'std::shared_ptr<...>'
//   which is not trivially copyable
// because third_party/libcxx specializes std::atomic only for integral,
// pointer and floating-point types (see third_party/libcxx/include/atomic).
//
// That rules out the C++20 replacement for the free std::atomic_load_explicit /
// atomic_store_explicit overloads on shared_ptr, which MSVC STL deprecates
// (STL4029, promoted to a hard failure by -Werror) but the Android build never
// sees. NetworkManager therefore keeps the free functions: they are the only
// spelling that compiles under both standard libraries.

// optional monadic operations, with the strict or_else signature.
std::optional<int> Chain(std::optional<int> value) {
    return value.and_then([](int inner) { return std::optional<int>(inner + 1); })
        .transform([](int inner) { return inner * 2; })
        .or_else([]() -> std::optional<int> { return std::nullopt; });
}

} // namespace

int main() {
    const std::vector<std::string> items{"a", "b"};
    const std::vector<int> data{1, 2, 3};
    int total = 0;
    total += CountLines("a\nb\nc");
    total += UsesRangeAlgorithms(items) ? 1 : 0;
    total += UsesRangeFind(items) ? 1 : 0;
    total += static_cast<int>(Join(items).size());
    total += UsesContains("arctap(1)", data) ? 1 : 0;
    total += static_cast<int>(Lower("AB").size());
    total += static_cast<int>(Squares().size());
    total += Chain(1).value_or(0);
    total += static_cast<int>(std::format("{:.2f}", 1.5).size());
    int64_t parsed_int = 0;
    total += ParseInt64("-42", parsed_int) ? 1 : 0;
    return total == 0 ? 1 : 0;
}
