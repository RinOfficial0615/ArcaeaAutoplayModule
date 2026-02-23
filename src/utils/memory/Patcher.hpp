#pragma once

#include <cstddef>
#include <cstdint>

namespace arc_autoplay::mem {

class Patcher {
public:
    static bool PatchBytesWithPerms(uintptr_t addr, const void *data, size_t len);
    static bool PatchU32WithPerms(uintptr_t addr, uint32_t value);
    static bool PatchA64ClearImm12(uintptr_t addr, uint32_t expected_insn);
    static bool PatchA64AbsoluteJump(uintptr_t target, uintptr_t dst);

    // Convenience patches for common tiny stubs.
    static bool PatchA64ReturnTrue(uintptr_t addr);
    static bool PatchA64ReturnFalse(uintptr_t addr);
};

} // namespace arc_autoplay::mem
