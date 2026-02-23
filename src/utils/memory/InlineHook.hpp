#pragma once

#include <cstdint>

namespace arc_autoplay::mem {

class InlineHook {
public:
    static bool InstallA64(uintptr_t target, void *hook_fn, void **orig_fn_out);
    static bool RestoreA64(uintptr_t target, void *orig_fn);
};

} // namespace arc_autoplay::mem
