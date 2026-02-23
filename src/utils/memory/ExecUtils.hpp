#pragma once

#include <cstdint>
#include <string_view>

namespace arc_autoplay::mem {

bool IsAddrInLibraryExec(uintptr_t addr, std::string_view soname);

} // namespace arc_autoplay::mem
