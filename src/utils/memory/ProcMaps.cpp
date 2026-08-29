#include "utils/memory/ProcMaps.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <ranges>
#include <string_view>
#include <vector>

#include <sys/mman.h>

namespace arc_helper::mem {
namespace {

struct PermissionRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
    int permissions = 0;
};

struct PermissionCache {
    std::vector<PermissionRange> ranges;
    std::chrono::steady_clock::time_point refreshed{};
    uint64_t generation = 0;
    bool valid = false;
};

// Runtime reads are hot in gameplay hooks. Reuse one map view for at most one
// frame, while RuntimeMemory::Protect invalidates every thread immediately.
constexpr auto kPermissionCacheLifetime = std::chrono::milliseconds(16);
std::atomic_uint64_t g_permission_cache_generation{1};
thread_local PermissionCache t_permission_cache{};

bool PathMatchesSoname(std::string_view path, std::string_view soname) {
    constexpr std::string_view kDeletedSuffix = " (deleted)";
    if (path.ends_with(kDeletedSuffix)) path.remove_suffix(kDeletedSuffix.size());
    const size_t slash = path.find_last_of('/');
    const std::string_view basename = slash == std::string_view::npos
                                          ? path
                                          : path.substr(slash + 1);
    return basename == soname;
}

// The leading set deliberately excludes '\n': the caller hands over the tail of
// a single maps line, so a newline there would be data, not padding.
std::string_view TrimSpaces(std::string_view sv) {
    constexpr std::string_view kLeading = " \t";
    constexpr std::string_view kTrailing = " \t\n\r";
    const size_t begin = sv.find_first_not_of(kLeading);
    if (begin == std::string_view::npos) return {};
    return sv.substr(begin, sv.find_last_not_of(kTrailing) - begin + 1);
}

struct MapsLine {
    uintptr_t start = 0;
    uintptr_t end = 0;
    uintptr_t offset = 0;
    int permissions = 0;
    std::string_view path;
};

bool ParseProcMapsLine(const char *line, MapsLine &out) {
    if (!line) return false;

    uintptr_t start = 0;
    uintptr_t end = 0;
    uintptr_t off = 0;
    unsigned long inode = 0;
    unsigned int dev_major = 0;
    unsigned int dev_minor = 0;
    char perm[5] = {'\0'};
    int path_off = 0;

    const int scanned = sscanf(line,
                               "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %x:%x %lu %n",
                               &start,
                               &end,
                               perm,
                               &off,
                               &dev_major,
                               &dev_minor,
                               &inode,
                               &path_off);
    if (scanned != 7) return false;

    if (perm[0] == 'r') out.permissions |= PROT_READ;
    if (perm[1] == 'w') out.permissions |= PROT_WRITE;
    if (perm[2] == 'x') out.permissions |= PROT_EXEC;

    out.start = start;
    out.end = end;
    out.offset = off;
    out.path = path_off > 0 ? TrimSpaces(std::string_view(line + path_off)) : std::string_view{};
    return true;
}

// Scans /proc/self/maps and hands each decoded line to `visit`, which returns
// false to stop early. Returns false when the file cannot be opened or the read
// ended in an error -- callers must then discard any partial results, because a
// truncated maps file would silently hide protected ranges.
template <typename Visit>
bool ScanMaps(Visit &&visit) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        MapsLine parsed;
        if (ParseProcMapsLine(line, parsed) && !visit(parsed)) break;
    }
    const bool complete = ferror(fp) == 0;
    fclose(fp);
    return complete;
}

bool RefreshPermissionCache(PermissionCache &cache, uint64_t generation) {
    cache.ranges.clear();
    if (!ScanMaps([&](const MapsLine &line) {
            if (line.end > line.start) {
                cache.ranges.push_back({line.start, line.end, line.permissions});
            }
            return true;
        })) {
        cache.valid = false;
        cache.ranges.clear();
        return false;
    }
    cache.generation = generation;
    cache.refreshed = std::chrono::steady_clock::now();
    cache.valid = true;
    return true;
}

const PermissionCache *CurrentPermissionCache() {
    const uint64_t generation = g_permission_cache_generation.load(std::memory_order_acquire);
    const auto now = std::chrono::steady_clock::now();
    if (!t_permission_cache.valid || t_permission_cache.generation != generation ||
        now - t_permission_cache.refreshed >= kPermissionCacheLifetime) {
        if (!RefreshPermissionCache(t_permission_cache, generation)) return nullptr;
    }
    return &t_permission_cache;
}

void ConsiderExecLoadBias(uintptr_t start,
                          uintptr_t off,
                          int perms,
                          uintptr_t &best_off,
                          uintptr_t &best_bias) {
    if ((perms & PROT_EXEC) == 0 || off > start || off >= best_off) return;
    best_off = off;
    best_bias = start - off;
}

bool CachedRangePermitted(uintptr_t addr, size_t len, int required_permissions) {
    if (len == 0 || addr == 0 || len > UINTPTR_MAX - addr) return false;
    const PermissionCache *cache = CurrentPermissionCache();
    if (!cache) return false;

    const uintptr_t end = addr + len;
    uintptr_t cursor = addr;
    for (const auto &range : cache->ranges) {
        if (range.end <= cursor) continue;
        if (range.start > cursor ||
            (range.permissions & required_permissions) != required_permissions) {
            return false;
        }
        cursor = std::min(end, range.end);
        if (cursor == end) return true;
    }
    return false;
}

} // namespace

// Splits on '\n' and drops the '\r' of a CRLF pair. The returned views borrow
// `text`, so they are only valid while the caller's buffer is alive.
constexpr auto LinesOf(std::string_view text) {
    return std::views::split(text, '\n') | std::views::transform([](auto &&line) {
        std::string_view view(line.begin(), line.end());
        if (view.ends_with('\r')) view.remove_suffix(1);
        return view;
    });
}

uintptr_t ProcMaps::FindLibraryBaseFromMaps(std::string_view maps_text,
                                            std::string_view soname) {
    uintptr_t best_off = std::numeric_limits<uintptr_t>::max();
    uintptr_t best_bias = 0;

    for (const std::string_view line : LinesOf(maps_text)) {
        char buf[4096];
        if (line.empty() || line.size() >= sizeof(buf)) continue;
        std::memcpy(buf, line.data(), line.size());
        buf[line.size()] = '\0';

        MapsLine parsed;
        if (!ParseProcMapsLine(buf, parsed)) continue;
        if (!PathMatchesSoname(parsed.path, soname)) continue;
        ConsiderExecLoadBias(parsed.start, parsed.offset, parsed.permissions, best_off, best_bias);
    }

    return best_bias;
}

uintptr_t ProcMaps::FindLibraryBase(std::string_view soname) {
    uintptr_t best_off = std::numeric_limits<uintptr_t>::max();
    uintptr_t best_bias = 0;
    ScanMaps([&](const MapsLine &line) {
        if (!PathMatchesSoname(line.path, soname)) return true;
        ConsiderExecLoadBias(line.start, line.offset, line.permissions, best_off, best_bias);
        return true;
    });
    return best_bias;
}

bool ProcMaps::GetLibraryExecRanges(std::string_view soname,
                                    std::array<MemRange, 64> &out_ranges,
                                    size_t &out_count) {
    out_count = 0;
    constexpr int kRequired = PROT_READ | PROT_EXEC;
    ScanMaps([&](const MapsLine &line) {
        if (out_count >= out_ranges.size()) return false;
        if ((line.permissions & kRequired) != kRequired) return true;
        if (!PathMatchesSoname(line.path, soname)) return true;
        out_ranges[out_count++] = MemRange{line.start, line.end};
        return true;
    });
    return out_count > 0;
}

bool ProcMaps::GetPermissions(uintptr_t addr, int &out_perms) {
    if (const PermissionCache *cache = CurrentPermissionCache()) {
        for (const auto &range : cache->ranges) {
            if (addr >= range.start && addr < range.end) {
                out_perms = range.permissions;
                return true;
            }
            if (range.start > addr) break;
        }
    }
    return false;
}

namespace {

bool IsRangePermitted(uintptr_t addr, size_t len, int required_perms) {
    return CachedRangePermitted(addr, len, required_perms);
}

} // namespace

bool ProcMaps::IsReadable(uintptr_t addr, size_t len) {
    return IsRangePermitted(addr, len, PROT_READ);
}

bool ProcMaps::IsWritable(uintptr_t addr, size_t len) {
    return IsRangePermitted(addr, len, PROT_WRITE);
}

bool ProcMaps::IsExecutable(uintptr_t addr) {
    int perms = 0;
    return GetPermissions(addr, perms) && ((perms & PROT_EXEC) != 0);
}

void ProcMaps::InvalidatePermissionCache() {
    g_permission_cache_generation.fetch_add(1, std::memory_order_acq_rel);
}

} // namespace arc_helper::mem
