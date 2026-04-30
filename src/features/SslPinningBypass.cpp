#include "features/SslPinningBypass.hpp"

#include "manager/GameVersionManager.hpp"
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_autoplay {

SslPinningBypass &SslPinningBypass::Instance() {
    static SslPinningBypass instance;
    return instance;
}

SslPinningBypass::SslPinningBypass() {
    if constexpr (!cfg::module::kDisableSslPinsEnabled) return;
    EnsurePatched();
}

void SslPinningBypass::EnsurePatched() {
    if (patched_) return;
    if (!RefreshLibBase()) return;

    auto &vm = GameVersionManager::Instance();
    vm.EnsureInstalled();

    const auto *profile = vm.GetActiveProfile();
    if (!profile) return;

    const auto &pins = profile->ssl_pins;
    if (!pins.skip_cbz || !pins.tail_call) {
        ARC_LOGE("SslPinningBypass: no patch offsets for this version");
        return;
    }

    const uintptr_t addr_a = lib_base_ + pins.skip_cbz;
    const uintptr_t addr_b = lib_base_ + pins.tail_call;

    if (!mem::ProcMaps::IsReadable(addr_a, 4) || !mem::ProcMaps::IsReadable(addr_b, 4)) {
        ARC_LOGE("SslPinningBypass: patch addresses not readable");
        return;
    }

    // Patch A: CBZ X20, skip → B skip
    mem::Write<uint32_t>(addr_a, 0x14000007);
    // Patch B: BL sub_XXXX → NOP
    mem::Write<uint32_t>(addr_b, 0xD503201F);

    patched_ = true;
    ARC_LOGI("SslPinningBypass: patched @ %p / %p",
             reinterpret_cast<void *>(addr_a),
             reinterpret_cast<void *>(addr_b));
}

} // namespace arc_autoplay
