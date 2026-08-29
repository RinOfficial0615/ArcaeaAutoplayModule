#include "utils/memory/AddressResolver.hpp"

#include <array>
#include <cstring>
#include <limits>
#include <span>

#include "utils/memory/ByteScanner.hpp"
#include "utils/memory/ProcMaps.hpp"

namespace arc_helper::mem {

uintptr_t AddressResolver::ResolveBySignature(uintptr_t hint_offset,
                                              const uint8_t *sig,
                                              size_t sig_len,
                                              std::string_view soname) const {
    if (!lib_base_ || !sig || sig_len == 0) return 0;

    if (hint_offset > std::numeric_limits<uintptr_t>::max() - lib_base_) return 0;
    const uintptr_t hint = lib_base_ + hint_offset;
    if (ProcMaps::IsReadable(hint, sig_len) &&
        memcmp(reinterpret_cast<void *>(hint), sig, sig_len) == 0) {
        return hint;
    }

    std::array<MemRange, 64> exec_ranges{};
    size_t exec_count = 0;
    if (!ProcMaps::GetLibraryExecRanges(soname, exec_ranges, exec_count)) return 0;

    return FindUniqueBytesInRanges(std::span<const MemRange>(exec_ranges.data(), exec_count),
                                   sig, sig_len, 4, nullptr);
}

} // namespace arc_helper::mem
