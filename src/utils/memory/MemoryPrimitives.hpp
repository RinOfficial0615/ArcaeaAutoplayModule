#pragma once

#include <cstdint>

namespace arc_autoplay::mem {

template <typename T>
inline T Read(uintptr_t addr) {
    return *reinterpret_cast<T *>(addr);
}

template <typename T>
inline void Write(uintptr_t addr, T value) {
    *reinterpret_cast<T *>(addr) = value;
}

} // namespace arc_autoplay::mem
