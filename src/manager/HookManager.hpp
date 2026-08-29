#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "manager/GameManager.hpp"
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_helper {

class HookManager {
public:
    class InlineHookRegistration {
    public:
        InlineHookRegistration() = default;
        ~InlineHookRegistration();

        InlineHookRegistration(const InlineHookRegistration &) = delete;
        InlineHookRegistration &operator=(const InlineHookRegistration &) = delete;
        InlineHookRegistration(InlineHookRegistration &&other) noexcept;
        InlineHookRegistration &operator=(InlineHookRegistration &&other) noexcept;

        explicit operator bool() const { return state_ != State::Invalid; }

    private:
        friend class HookManager;

        enum class State : uint8_t {
            Invalid = 0,
            Pending,
            Installed,
            Committed,
        };

        InlineHookRegistration(HookManager *owner,
                               uintptr_t target_addr,
                               void *hook_handler,
                               const char *name,
                               State state);
        void Reset();

        HookManager *owner_ = nullptr;
        uintptr_t target_addr_ = 0;
        void *hook_handler_ = nullptr;
        void *orig_handler_ = nullptr;
        void *stub_ = nullptr;
        const char *name_ = nullptr;
        State state_ = State::Invalid;
    };

    static HookManager &Instance();

    uintptr_t GetLibBase() const;
    bool EnsureReady();

    bool ResolveAddress(uintptr_t &cached_addr,
                        uintptr_t hint_offset,
                        const std::array<uint8_t, 16> &sig,
                        const char *name,
                        bool allow_offset_if_sig_empty = false);
    bool ResolveSymbol(uintptr_t &cached_addr,
                       const char *library_name,
                       const char *symbol_name,
                       const char *name);

    template <typename HookFn>
    InlineHookRegistration RegisterInlineHook(uintptr_t &addr,
                                              uintptr_t hint_offset,
                                              const std::array<uint8_t, 16> &sig,
                                              HookFn hook_fn,
                                              const char *name,
                                              bool allow_offset_if_sig_empty = false) {
        return RegisterInlineHookImpl(addr,
                                      hint_offset,
                                      sig,
                                      reinterpret_cast<void *>(hook_fn),
                                      name,
                                      allow_offset_if_sig_empty);
    }

    template <typename HookFn>
    InlineHookRegistration RegisterInlineHookAbsolute(uintptr_t addr,
                                                      const std::array<uint8_t, 16> &sig,
                                                      HookFn hook_fn,
                                                      const char *name) {
        return RegisterInlineHookAbsoluteImpl(addr,
                                              sig,
                                              reinterpret_cast<void *>(hook_fn),
                                              name);
    }

    template <typename HookFn>
    InlineHookRegistration RegisterInlineHookSymbol(uintptr_t &addr,
                                                    const char *library_name,
                                                    const char *symbol_name,
                                                    const std::array<uint8_t, 16> &sig,
                                                    HookFn hook_fn,
                                                    const char *name) {
        if (!ResolveSymbol(addr, library_name, symbol_name, name)) return {};
        return RegisterInlineHookAbsoluteImpl(addr,
                                              sig,
                                              reinterpret_cast<void *>(hook_fn),
                                              name);
    }

    bool CommitInlineHook(std::span<InlineHookRegistration> registrations);
    bool CommitInlineHook(InlineHookRegistration &registration) {
        return CommitInlineHook(std::span<InlineHookRegistration>(&registration, 1));
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

    template <auto HookFn, typename RType, typename... Params, typename... Args>
    static RType CallOriginal(RType (*)(Params...), Args&&... args) {
        void *orig_ptr = GetOriginalForHook(reinterpret_cast<void *>(HookFn));
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
            orig_func(std::forward<Args>(args)...);
        } else {
            return orig_func(std::forward<Args>(args)...);
        }
    }

private:
    struct InlineHookRecord {
        uintptr_t target_addr = 0;
        void *hook_handler = nullptr;
        void *orig_handler = nullptr;
        void *stub = nullptr;
        bool rollback_pending = false;
    };

    HookManager() = default;

    InlineHookRegistration RegisterInlineHookImpl(uintptr_t &addr,
                                                  uintptr_t hint_offset,
                                                  const std::array<uint8_t, 16> &sig,
                                                  void *hook_handler,
                                                  const char *name,
                                                  bool allow_offset_if_sig_empty);
    InlineHookRegistration RegisterInlineHookAbsoluteImpl(
        uintptr_t addr,
        const std::array<uint8_t, 16> &sig,
        void *hook_handler,
        const char *name,
        bool allow_empty_signature = false);

    bool ValidateHookTarget(uintptr_t addr,
                            const std::array<uint8_t, 16> &sig,
                            void *hook_handler,
                            const char *name,
                            bool allow_empty_signature) const;
    bool RollbackRegistration(InlineHookRegistration &registration);
    bool RetryPendingRollbacks();

    // Both public lookups differ only in which field they match, so they share
    // one traversal; the predicate is inlined at each call site.
    template <typename Predicate>
    const InlineHookRecord *FindHookRecord(Predicate predicate) const {
        const auto it = std::ranges::find_if(inline_hooks_, predicate);
        return it != inline_hooks_.end() ? &*it : nullptr;
    }
    const InlineHookRecord *FindHookRecordByHook(void *hook_handler) const;
    const InlineHookRecord *FindHookRecordByTarget(uintptr_t target_addr) const;

    static bool IsAllZeros(const std::array<uint8_t, 16> &sig);

    uintptr_t lib_base_ = 0;
    mem::AddressResolver resolver_;
    bool logged_unready_ = false;

    mutable std::recursive_mutex mutation_mutex_{};
    mutable std::shared_mutex records_mutex_{};
    std::vector<InlineHookRecord> inline_hooks_{};
    std::vector<void *> symbol_handles_{};
};

} // namespace arc_helper

#define CALL_ORIG(func, ...) \
    arc_helper::HookManager::CallOriginal<func>(func, ##__VA_ARGS__)
