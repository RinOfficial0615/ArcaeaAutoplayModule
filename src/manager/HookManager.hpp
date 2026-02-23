#pragma once

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "manager/GameManager.hpp"
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_autoplay {

class HookManager {
public:
    static HookManager &Instance();

    uintptr_t GetLibBase() const;
    bool EnsureReady();

    bool ResolveAddress(uintptr_t &cached_addr,
                        uintptr_t hint_offset,
                        const std::array<uint8_t, 16> &sig,
                        const char *name,
                        bool allow_offset_if_sig_empty = false);

    template <typename HookFn>
    bool InstallInlineHook(uintptr_t &addr,
                           uintptr_t hint_offset,
                           const std::array<uint8_t, 16> &sig,
                           HookFn hook_fn,
                           const char *name,
                           bool allow_offset_if_sig_empty = false) {
        return InstallInlineHookImpl(addr,
                                     hint_offset,
                                     sig,
                                     reinterpret_cast<void *>(hook_fn),
                                     name,
                                     allow_offset_if_sig_empty);
    }

    template <typename FnOut>
    void ResolveFunctionPtr(uintptr_t &addr,
                            uintptr_t hint_offset,
                            const std::array<uint8_t, 16> &sig,
                            FnOut &fn_out,
                            const char *name) {
        if (!ResolveAddress(addr, hint_offset, sig, name)) return;
        if (fn_out) return;

        fn_out = reinterpret_cast<FnOut>(addr);
        ARC_LOGI("Resolved %s @ %p", name, reinterpret_cast<void *>(addr));
    }

    bool HasOriginalForHook(void *hook_handler) const;
    static void *GetOriginalForHook(void *hook_handler);

    bool RestoreInlineHook(void *hook_handler);
    void RestoreAllInlineHooks();

    template <typename RType, typename... Params>
    static RType CallOriginal(RType (*handler)(Params...), Params... params) {
        void *orig_ptr = GetOriginalForHook(reinterpret_cast<void *>(handler));
        if (!orig_ptr) {
            if constexpr (!std::is_void_v<RType>) {
                return RType{};
            } else {
                return;
            }
        }

        using FuncType = RType (*)(Params...);
        auto orig_func = reinterpret_cast<FuncType>(orig_ptr);

        if constexpr (std::is_void_v<RType>) {
            orig_func(params...);
        } else {
            return orig_func(params...);
        }
    }

private:
    struct InlineHookRecord {
        uintptr_t target_addr = 0;
        void *hook_handler = nullptr;
        void *orig_handler = nullptr;
        bool active = false;
    };

    HookManager() = default;

    bool InstallInlineHookImpl(uintptr_t &addr,
                               uintptr_t hint_offset,
                               const std::array<uint8_t, 16> &sig,
                               void *hook_handler,
                               const char *name,
                               bool allow_offset_if_sig_empty);

    InlineHookRecord *FindHookRecordByHook(void *hook_handler);
    const InlineHookRecord *FindHookRecordByHook(void *hook_handler) const;

    static bool IsAllZeros(const std::array<uint8_t, 16> &sig);

    uintptr_t lib_base_ = 0;
    mem::AddressResolver resolver_;
    bool logged_unready_ = false;

    std::vector<InlineHookRecord> inline_hooks_{};
};

} // namespace arc_autoplay

#define CALL_ORIG(func, ...) \
    arc_autoplay::HookManager::CallOriginal(func, ##__VA_ARGS__)
