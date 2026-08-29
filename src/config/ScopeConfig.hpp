#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <unistd.h>

namespace arc_helper::cfg::scope {

inline constexpr std::array<const char *, 3> kDefaultPackages = {
    "moe.inf.arc", "moe.low.arc", "moe.low.mes"};

// Borrowing trim: scope.txt is only ever scanned, never stored, so no line has
// to be copied into a std::string just to be compared against a package name.
inline std::string_view Trim(std::string_view value) {
    size_t begin = 0, end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

inline bool Contains(std::string_view text, std::string_view package_name) {
    // An empty name must never match: the line loop below sees blank lines
    // (from trailing newlines) and would otherwise accept one.
    if (package_name.empty()) return false;
    return std::ranges::any_of(std::views::split(text, '\n'), [&](auto &&raw_line) {
        std::string_view line(raw_line.begin(), raw_line.end());
        // '#' starts a comment anywhere on the line; scope.txt has no quoting.
        if (const size_t comment = line.find('#'); comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        return Trim(line) == package_name;
    });
}

inline bool MatchesDefaultPackages(std::string_view package_name) {
    return std::ranges::any_of(kDefaultPackages, [&](const char *default_package) {
        return package_name == default_package;
    });
}

inline bool IsTargetPackage(int module_dir_fd, const char *package_name) {
    if (module_dir_fd < 0 || !package_name || package_name[0] == '\0') return false;
    constexpr size_t kMaxScopeBytes = 256 * 1024;
    std::string contents;
    contents.reserve(4096);
    const int fd = openat(module_dir_fd, "scope.txt", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return MatchesDefaultPackages(package_name);
    std::array<char, 4096> buffer{};
    bool truncated = false;
    while (contents.size() < kMaxScopeBytes) {
        const size_t remaining = kMaxScopeBytes - contents.size();
        const size_t request = std::min(remaining, buffer.size());
        const ssize_t size = read(fd, buffer.data(), request);
        if (size < 0 && errno == EINTR) continue;
        if (size < 0) {
            close(fd);
            return MatchesDefaultPackages(package_name);
        }
        if (size == 0) break;
        contents.append(buffer.data(), static_cast<size_t>(size));
    }
    if (contents.size() == kMaxScopeBytes) {
        char extra = 0;
        ssize_t size = 0;
        do {
            size = read(fd, &extra, 1);
        } while (size < 0 && errno == EINTR);
        truncated = size > 0;
    }
    close(fd);
    if (truncated) return false;
    return Contains(contents, package_name);
}

} // namespace arc_helper::cfg::scope
