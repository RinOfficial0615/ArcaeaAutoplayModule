// Capability probe: UNGUARDED use of each candidate feature.
// Unlike the feature-macro probe, this one fails to compile when a feature is
// missing, which is exactly the signal we want when choosing a baseline.
// Compile with the *production* flags (-fno-exceptions -fno-rtti) so the answer
// reflects what src/ may actually use.
#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// ---- C++20 ----
static void cxx20(std::span<const int> s, std::string_view sv) {
    (void)s;
    (void)sv.starts_with("a");
    (void)sv.ends_with("z");
    (void)std::ssize(s);
    std::vector<int> v{3, 1, 2};
    (void)std::ranges::sort(v);
    (void)std::ranges::any_of(v, [](int n) { return n > 1; });
    auto sq = v | std::views::transform([](int n) { return n * n; }) |
              std::views::filter([](int n) { return n % 2 == 0; });
    (void)sq.empty();
    (void)std::ranges::contains(v, 2);            // C++23
    (void)std::ranges::find(v, 2);
}

// ---- C++23 ranges additions ----
static void ranges23() {
    const std::vector<int> v{1, 2, 3, 4, 5, 6};
    const auto out = v | std::views::filter([](int n) { return n % 2 == 0; }) |
                     std::views::transform([](int n) { return n * 10; }) |
                     std::ranges::to<std::vector>();
    (void)out.size();
    (void)std::ranges::fold_left(v, 0, std::plus<>{});
    (void)std::ranges::fold_right(v, 0, std::plus<>{});
    (void)std::ranges::starts_with(v, std::vector<int>{1, 2});
    (void)std::ranges::ends_with(v, std::vector<int>{5, 6});
    (void)std::ranges::find_last(v, 3).begin();
    for ([[maybe_unused]] auto c : v | std::views::chunk(2)) { }
    for ([[maybe_unused]] auto c : v | std::views::slide(3)) { }
    const std::vector<std::vector<int>> nested{{1}, {2}};
    for ([[maybe_unused]] int n : nested | std::views::join_with(0)) { }
    for ([[maybe_unused]] auto [i, n] : std::views::enumerate(v)) { (void)i; (void)n; }
    const std::vector<std::string> vs{"a"};
    for ([[maybe_unused]] auto [a, b] : std::views::zip(v, vs)) { (void)a; (void)b; }
    (void)std::views::cartesian_product(v, vs);
}

// ---- std::expected ----
#include <expected>
static std::expected<int, std::string> expected_demo() {
    std::expected<int, std::string> e = 42;
    return e.and_then([](int n) -> std::expected<int, std::string> { return n * 2; })
        .transform([](int n) { return n + 1; })
        .or_else([](const std::string &) -> std::expected<int, std::string> { return 0; });
}

// ---- std::format ----
#include <format>
static std::string format_demo() { return std::format("{} {:.3f} {:>6}", 1, 2.5, "x"); }

// ---- std::print (C++23, often lagging) ----
#if defined(TRY_PRINT)
#include <print>
static void print_demo() { std::print("ok\n"); }
#endif

// ---- std::optional monadic ----
static int optional_demo() {
    const std::optional<int> o = 7;
    return o.and_then([](int n) -> std::optional<int> { return n + 1; })
        .transform([](int n) { return n * 2; })
        .or_else([] { return std::optional<int>{-1}; })
        .value_or(0);
}

int main() {
    const std::vector<int> data{1, 2, 3};
    cxx20(data, "abc");
    ranges23();
    (void)expected_demo();
    (void)format_demo();
#if defined(TRY_PRINT)
    print_demo();
#endif
    return optional_demo() == 0 ? 1 : 0;
}
