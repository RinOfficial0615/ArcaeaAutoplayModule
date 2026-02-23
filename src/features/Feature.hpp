#pragma once

#include <cstdint>

#include "manager/HookManager.hpp"

namespace arc_autoplay {

class Feature {
public:
    virtual ~Feature() = default;

    bool IsReady() const { return lib_base_ != 0; }

protected:
    Feature() : hook_manager_(HookManager::Instance()) {
        hook_manager_.EnsureReady();
        lib_base_ = hook_manager_.GetLibBase();
    }

    bool RefreshLibBase() {
        if (lib_base_) return true;
        hook_manager_.EnsureReady();
        lib_base_ = hook_manager_.GetLibBase();
        return lib_base_ != 0;
    }

    HookManager &hook_manager_;
    uintptr_t lib_base_ = 0;
};

} // namespace arc_autoplay
