#pragma once

#include <cstdint>

#include "utils/memory/MemoryPrimitives.hpp"

namespace arc_autoplay::mem::detail {

// Writes a 16-byte absolute jump sequence:
//   ldr x17, #8
//   br  x17
//   .quad dst
inline void WriteA64AbsoluteJumpStub(uintptr_t at, uintptr_t dst) {
    Write<uint32_t>(at + 0, 0x58000051);
    Write<uint32_t>(at + 4, 0xD61F0220);
    Write<uint64_t>(at + 8, static_cast<uint64_t>(dst));
}

} // namespace arc_autoplay::mem::detail
