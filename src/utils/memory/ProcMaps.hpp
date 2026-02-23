#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace arc_autoplay::mem {

struct MemRange {
    uintptr_t start;
    uintptr_t end;
};

class ProcMaps {
public:
    static uintptr_t FindLibraryBase(std::string_view soname);
    static bool GetLibraryExecRanges(std::string_view soname,
                                     std::array<MemRange, 64> &out_ranges,
                                     size_t &out_count);
    static bool GetPermissions(uintptr_t addr, int &out_perms);
    static bool IsReadable(uintptr_t addr, size_t len);
    static bool IsExecutable(uintptr_t addr);
};

} // namespace arc_autoplay::mem
