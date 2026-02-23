#include "utils/memory/InlineHook.hpp"

#include <cstring>

#include <sys/mman.h>

#include "utils/memory/MemoryPrimitives.hpp"
#include "utils/memory/PatchHelpers.hpp"
#include "utils/memory/Patcher.hpp"

namespace arc_autoplay::mem {
namespace {

constexpr size_t kTrampSize = 64;

} // namespace

bool InlineHook::InstallA64(uintptr_t target, void *hook_fn, void **orig_fn_out) {
    if (!hook_fn || !orig_fn_out) return false;

    void *tramp = mmap(nullptr,
                       kTrampSize,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1,
                       0);
    if (tramp == MAP_FAILED) return false;

    memcpy(tramp, reinterpret_cast<void *>(target), 16);

    const uintptr_t tramp_jmp = reinterpret_cast<uintptr_t>(tramp) + 16;
    detail::WriteA64AbsoluteJumpStub(tramp_jmp, target + 16);
    __builtin___clear_cache(reinterpret_cast<char *>(tramp),
                            reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(tramp) + 32));

    if (!Patcher::PatchA64AbsoluteJump(target, reinterpret_cast<uintptr_t>(hook_fn))) {
        munmap(tramp, kTrampSize);
        return false;
    }

    *orig_fn_out = tramp;
    return true;
}

bool InlineHook::RestoreA64(uintptr_t target, void *orig_fn) {
    if (!target || !orig_fn) return false;

    const bool ok = Patcher::PatchBytesWithPerms(target, orig_fn, 16);
    (void)munmap(orig_fn, kTrampSize);
    return ok;
}

} // namespace arc_autoplay::mem
