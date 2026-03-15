#pragma once

#include <cstring>
#include <cstdint>

#include "utils/Log.h"
#include "config/ModuleConfig.h"
#include "features/Autoplay.hpp"
#include "features/NetworkLogger.hpp"
#include "features/NetworkBlock.hpp"
#include "manager/GameManager.hpp"
#include "manager/GameVersionManager.hpp"

namespace arc_autoplay::wrapper {

inline uintptr_t FindGameLibraryBase() {
    return GameManager::Instance().GetOrFindGameLibBase();
}

inline void InitResolvedFeatures() {
    Autoplay::Instance();
    NetworkLogger::Instance();
    NetworkBlock::Instance();
}

inline bool IsTargetPackage(const char *pkg) {
    if (!pkg) return false;
    for (const char *target : cfg::module::kTargetPackages) {
        if (target && std::strcmp(pkg, target) == 0) return true;
    }
    return false;
}

inline void InitFeatures() {
    auto &version_manager = GameVersionManager::Instance();
    version_manager.SetResolvedCallback(&InitResolvedFeatures);
    version_manager.EnsureInstalled();
}

} // namespace arc_autoplay::wrapper
