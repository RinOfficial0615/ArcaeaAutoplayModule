#include "utils/memory/Patcher.hpp"

#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

#include "utils/memory/MemoryPrimitives.hpp"
#include "utils/memory/PatchHelpers.hpp"
#include "utils/memory/ProcMaps.hpp"

namespace arc_autoplay::mem {
namespace {

template <typename PatchFn>
inline static bool PatchWithPagePerms(uintptr_t range_start, size_t range_size, PatchFn &&patch_fn) {
    if (range_size == 0) return false;

    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return false;

    const uintptr_t page_mask = static_cast<uintptr_t>(page_size - 1);
    const uintptr_t page_start = range_start & ~page_mask;
    const uintptr_t page_end = (range_start + range_size + page_mask) & ~page_mask;
    const size_t protect_size = page_end - page_start;

    int original_perms = 0;
    if (!ProcMaps::GetPermissions(range_start, original_perms)) return false;

    if (mprotect(reinterpret_cast<void *>(page_start), protect_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }

    const bool ok = patch_fn();
    __builtin___clear_cache(reinterpret_cast<char *>(range_start),
                            reinterpret_cast<char *>(range_start + range_size));

    const bool restore_ok =
        (mprotect(reinterpret_cast<void *>(page_start), protect_size, original_perms) == 0);
    return ok && restore_ok;
}

} // namespace

bool Patcher::PatchA64AbsoluteJump(uintptr_t target, uintptr_t dst) {
    return PatchWithPagePerms(target, 16, [&]() {
        detail::WriteA64AbsoluteJumpStub(target, dst);
        return true;
    });
}

bool Patcher::PatchBytesWithPerms(uintptr_t addr, const void *data, size_t len) {
    if (!data || len == 0) return false;
    return PatchWithPagePerms(addr, len, [&]() {
        std::memcpy(reinterpret_cast<void *>(addr), data, len);
        return true;
    });
}

bool Patcher::PatchU32WithPerms(uintptr_t addr, uint32_t value) {
    return PatchBytesWithPerms(addr, &value, sizeof(value));
}

bool Patcher::PatchA64ClearImm12(uintptr_t addr, uint32_t expected_insn) {
    if (!ProcMaps::IsReadable(addr, 4)) return false;
    const uint32_t cur = Read<uint32_t>(addr);
    if (cur != expected_insn) return false;

    constexpr uint32_t kImm12Mask = 0x003FFC00u;
    const uint32_t patched = cur & ~kImm12Mask;
    return PatchU32WithPerms(addr, patched);
}

bool Patcher::PatchA64ReturnTrue(uintptr_t addr) {
    return PatchWithPagePerms(addr, 8, [&]() {
        // mov w0, #1; ret
        Write<uint32_t>(addr + 0, 0x52800020);
        Write<uint32_t>(addr + 4, 0xD65F03C0);
        return true;
    });
}

bool Patcher::PatchA64ReturnFalse(uintptr_t addr) {
    return PatchWithPagePerms(addr, 8, [&]() {
        // mov w0, #0; ret
        Write<uint32_t>(addr + 0, 0x52800000);
        Write<uint32_t>(addr + 4, 0xD65F03C0);
        return true;
    });
}

} // namespace arc_autoplay::mem
