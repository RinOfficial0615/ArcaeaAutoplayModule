#include "utils/memory/ExecUtils.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

#include "utils/memory/ProcMaps.hpp"

namespace arc_helper::mem {

bool IsAddrInLibraryExec(uintptr_t addr, std::string_view soname) {
    if (!addr || soname.empty()) return false;

    // Per-vcall guard. The exec-range cache avoids one /proc/self/maps scan
    // per call; the mutex only serializes cache refreshes. A soname change
    // invalidates the cache and forces exactly one rescan per alternation.
    static std::array<MemRange, 64> s_exec_ranges{};
    static size_t s_exec_count = 0;
    static bool s_cached = false;
    static std::string s_soname;
    static std::mutex s_cache_mutex;

    std::scoped_lock lock(s_cache_mutex);

    if (!s_cached || std::string_view(s_soname) != soname) {
        s_soname.assign(soname.data(), soname.size());
        s_exec_count = 0;
        s_cached = ProcMaps::GetLibraryExecRanges(soname, s_exec_ranges, s_exec_count);
    }

    const auto contains = [&] {
        const std::span<MemRange> ranges(s_exec_ranges.data(), s_exec_count);
        return std::ranges::any_of(ranges, [addr](const MemRange &range) {
            return addr >= range.start && addr < range.end;
        });
    };
    if (s_cached && contains()) return true;

    // A miss can mean the library was unloaded and mapped at a new base.
    // Refresh once, but never fall back to an unrelated executable mapping.
    s_exec_count = 0;
    s_cached = ProcMaps::GetLibraryExecRanges(soname, s_exec_ranges, s_exec_count);
    return s_cached && contains();
}

} // namespace arc_helper::mem
