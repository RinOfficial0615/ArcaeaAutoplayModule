#include "manager/HookManager.hpp"

#include <cinttypes>

#include "config/ModuleConfig.h"

namespace arc_autoplay {

HookManager &HookManager::Instance() {
    static HookManager manager;
    return manager;
}

uintptr_t HookManager::GetLibBase() const {
    return lib_base_;
}

bool HookManager::EnsureReady() {
    if (lib_base_) return true;

    lib_base_ = GameManager::Instance().GetOrFindGameLibBase();
    if (!lib_base_) {
        if (!logged_unready_) {
            ARC_LOGE("HookManager: %s base not ready", cfg::module::kLibName);
            logged_unready_ = true;
        }
        return false;
    }

    resolver_.SetLibBase(lib_base_);
    logged_unready_ = false;
    return true;
}

bool HookManager::ResolveAddress(uintptr_t &cached_addr,
                                 uintptr_t hint_offset,
                                 const std::array<uint8_t, 16> &sig,
                                 const char *name,
                                 bool allow_offset_if_sig_empty) {
    if (cached_addr) return true;
    if (!EnsureReady()) return false;

    if (!allow_offset_if_sig_empty || !IsAllZeros(sig)) {
        cached_addr = resolver_.ResolveBySignature(hint_offset, sig.data(), sig.size(), cfg::module::kLibName);
        if (cached_addr) {
            ARC_LOGI("Resolved %s @ %p", name, reinterpret_cast<void *>(cached_addr));
            return true;
        }

        ARC_LOGE("Failed to resolve %s by signature (offset=0x%" PRIxPTR ")", name, hint_offset);
        return false;
    }

    cached_addr = lib_base_ + hint_offset;
    if (!cached_addr) return false;
    if (!mem::ProcMaps::IsExecutable(cached_addr)) {
        ARC_LOGE("Resolved %s @ %p (offset) but not executable", name, reinterpret_cast<void *>(cached_addr));
        cached_addr = 0;
        return false;
    }

    ARC_LOGI("Resolved %s @ %p (offset-only)", name, reinterpret_cast<void *>(cached_addr));
    return true;
}

bool HookManager::InstallInlineHookImpl(uintptr_t &addr,
                                        uintptr_t hint_offset,
                                        const std::array<uint8_t, 16> &sig,
                                        void *hook_handler,
                                        const char *name,
                                        bool allow_offset_if_sig_empty) {
    if (!hook_handler) return false;
    if (!ResolveAddress(addr, hint_offset, sig, name, allow_offset_if_sig_empty)) return false;

    if (const auto *existing = FindHookRecordByHook(hook_handler); existing && existing->active) {
        return true;
    }

    void *orig = nullptr;
    if (!mem::InlineHook::InstallA64(addr, hook_handler, &orig)) {
        ARC_LOGE("Failed to hook %s @ %p", name, reinterpret_cast<void *>(addr));
        return false;
    }

    InlineHookRecord rec{};
    rec.target_addr = addr;
    rec.hook_handler = hook_handler;
    rec.orig_handler = orig;
    rec.active = true;
    inline_hooks_.push_back(rec);

    ARC_LOGI("Hooked %s @ %p", name, reinterpret_cast<void *>(addr));
    return true;
}

HookManager::InlineHookRecord *HookManager::FindHookRecordByHook(void *hook_handler) {
    for (auto &rec : inline_hooks_) {
        if (rec.hook_handler == hook_handler) return &rec;
    }
    return nullptr;
}

const HookManager::InlineHookRecord *HookManager::FindHookRecordByHook(void *hook_handler) const {
    for (const auto &rec : inline_hooks_) {
        if (rec.hook_handler == hook_handler) return &rec;
    }
    return nullptr;
}

bool HookManager::HasOriginalForHook(void *hook_handler) const {
    const auto *rec = FindHookRecordByHook(hook_handler);
    return rec && rec->active && rec->orig_handler;
}

void *HookManager::GetOriginalForHook(void *hook_handler) {
    const auto &mgr = Instance();
    const auto *rec = mgr.FindHookRecordByHook(hook_handler);
    if (!rec || !rec->active) return nullptr;
    return rec->orig_handler;
}

bool HookManager::RestoreInlineHook(void *hook_handler) {
    auto *rec = FindHookRecordByHook(hook_handler);
    if (!rec || !rec->active) return false;

    const bool ok = mem::InlineHook::RestoreA64(rec->target_addr, rec->orig_handler);
    if (ok) {
        rec->active = false;
    }
    return ok;
}

void HookManager::RestoreAllInlineHooks() {
    for (auto &rec : inline_hooks_) {
        if (!rec.active) continue;
        if (mem::InlineHook::RestoreA64(rec.target_addr, rec.orig_handler)) {
            rec.active = false;
        }
    }
}

bool HookManager::IsAllZeros(const std::array<uint8_t, 16> &sig) {
    for (uint8_t b : sig) {
        if (b != 0) return false;
    }
    return true;
}

} // namespace arc_autoplay
