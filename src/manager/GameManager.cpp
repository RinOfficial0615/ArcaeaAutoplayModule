#include "manager/GameManager.hpp"

#include "utils/MemoryUtils.hpp"
#include "config/ModuleConfig.h"

namespace arc_autoplay {

GameManager &GameManager::Instance() {
    static GameManager manager;
    return manager;
}

uintptr_t GameManager::GetGameLibBase() const {
    return game_lib_base_.load(std::memory_order_relaxed);
}

uintptr_t GameManager::GetOrFindGameLibBase() {
    const uintptr_t cached = GetGameLibBase();
    if (cached) return cached;

    const uintptr_t found = mem::ProcMaps::FindLibraryBase(cfg::module::kLibName);
    if (found) {
        game_lib_base_.store(found, std::memory_order_relaxed);
    }
    return found;
}

} // namespace arc_autoplay
