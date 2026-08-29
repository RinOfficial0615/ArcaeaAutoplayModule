#include "manager/HookManager.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <ranges>
#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

#include "config/ModuleConfig.h"

namespace arc_helper {

HookManager::InlineHookRegistration::InlineHookRegistration(HookManager *owner,
                                                            uintptr_t target_addr,
                                                            void *hook_handler,
                                                            const char *name,
                                                            State state)
    : owner_(owner),
      target_addr_(target_addr),
      hook_handler_(hook_handler),
      name_(name),
      state_(state) {}

HookManager::InlineHookRegistration::~InlineHookRegistration() {
    Reset();
}

HookManager::InlineHookRegistration::InlineHookRegistration(
    InlineHookRegistration &&other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      target_addr_(std::exchange(other.target_addr_, 0)),
      hook_handler_(std::exchange(other.hook_handler_, nullptr)),
      orig_handler_(std::exchange(other.orig_handler_, nullptr)),
      stub_(std::exchange(other.stub_, nullptr)),
      name_(std::exchange(other.name_, nullptr)),
      state_(std::exchange(other.state_, State::Invalid)) {}

HookManager::InlineHookRegistration &HookManager::InlineHookRegistration::operator=(
    InlineHookRegistration &&other) noexcept {
    if (this == &other) return *this;
    Reset();
    owner_ = std::exchange(other.owner_, nullptr);
    target_addr_ = std::exchange(other.target_addr_, 0);
    hook_handler_ = std::exchange(other.hook_handler_, nullptr);
    orig_handler_ = std::exchange(other.orig_handler_, nullptr);
    stub_ = std::exchange(other.stub_, nullptr);
    name_ = std::exchange(other.name_, nullptr);
    state_ = std::exchange(other.state_, State::Invalid);
    return *this;
}

void HookManager::InlineHookRegistration::Reset() {
    if (owner_ && state_ == State::Installed) {
        owner_->RollbackRegistration(*this);
    }
    owner_ = nullptr;
    target_addr_ = 0;
    hook_handler_ = nullptr;
    orig_handler_ = nullptr;
    stub_ = nullptr;
    name_ = nullptr;
    state_ = State::Invalid;
}

HookManager &HookManager::Instance() {
    static HookManager manager;
    return manager;
}

uintptr_t HookManager::GetLibBase() const {
    std::scoped_lock lock(mutation_mutex_);
    return lib_base_;
}

bool HookManager::EnsureReady() {
    std::scoped_lock lock(mutation_mutex_);
    if (lib_base_) return true;

    lib_base_ = GameManager::Instance().GetOrFindGameLibBase();
    if (!lib_base_) {
        if (!logged_unready_) {
            ARC_LOGE("%s base not ready", cfg::module::kLibName);
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
    std::scoped_lock lock(mutation_mutex_);
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

    if (hint_offset > UINTPTR_MAX - lib_base_) {
        ARC_LOGE("Offset overflow while resolving %s", name);
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

bool HookManager::ResolveSymbol(uintptr_t &cached_addr,
                                const char *library_name,
                                const char *symbol_name,
                                const char *name) {
    std::scoped_lock lock(mutation_mutex_);
    if (cached_addr) return true;
    if (!library_name || !symbol_name) return false;

#if !defined(__ANDROID__)
    ARC_LOGE("Symbol resolution for %s is only available on Android", name ? name : "unknown");
    return false;
#else
    void *handle = nullptr;
#ifdef RTLD_NOLOAD
    handle = dlopen(library_name, RTLD_NOW | RTLD_NOLOAD);
#endif
    if (!handle) handle = dlopen(library_name, RTLD_NOW);
    if (!handle) {
        const char *error = dlerror();
        ARC_LOGE("Failed to open %s for %s: %s",
                 library_name,
                 name,
                 error ? error : "unknown dlopen error");
        return false;
    }

    dlerror();
    void *symbol = dlsym(handle, symbol_name);
    const char *error = dlerror();
    if (!symbol || error) {
        ARC_LOGE("Failed to resolve symbol %s in %s for %s: %s",
                 symbol_name,
                 library_name,
                 name,
                 error ? error : "symbol not found");
        dlclose(handle);
        return false;
    }

    cached_addr = reinterpret_cast<uintptr_t>(symbol);
    symbol_handles_.push_back(handle);
    ARC_LOGI("Resolved %s via %s!%s @ %p",
             name,
             library_name,
             symbol_name,
             symbol);
    return true;
#endif
}

HookManager::InlineHookRegistration HookManager::RegisterInlineHookImpl(
    uintptr_t &addr,
    uintptr_t hint_offset,
    const std::array<uint8_t, 16> &sig,
    void *hook_handler,
    const char *name,
    bool allow_offset_if_sig_empty) {
    if (!hook_handler) return {};
    if (!ResolveAddress(addr, hint_offset, sig, name, allow_offset_if_sig_empty)) return {};
    return RegisterInlineHookAbsoluteImpl(addr,
                                          sig,
                                          hook_handler,
                                          name,
                                          allow_offset_if_sig_empty);
}

HookManager::InlineHookRegistration HookManager::RegisterInlineHookAbsoluteImpl(
    uintptr_t addr,
    const std::array<uint8_t, 16> &sig,
    void *hook_handler,
    const char *name,
    bool allow_empty_signature) {
    std::scoped_lock mutation_lock(mutation_mutex_);
    if (!RetryPendingRollbacks()) {
        ARC_LOGE("Pending rollback still active before %s", name);
        return {};
    }
    if (!ValidateHookTarget(addr, sig, hook_handler, name, allow_empty_signature)) return {};

    {
        std::shared_lock records_lock(records_mutex_);
        if (const auto *existing = FindHookRecordByHook(hook_handler)) {
            if (existing->target_addr != addr) {
                ARC_LOGE("Hook handler conflict for %s", name);
                return {};
            }
            return {this, addr, hook_handler, name, InlineHookRegistration::State::Committed};
        }
        if (FindHookRecordByTarget(addr)) {
            ARC_LOGE("Hook target conflict for %s @ %p", name, reinterpret_cast<void *>(addr));
            return {};
        }
    }

    ARC_LOGI("Registered %s @ %p", name, reinterpret_cast<void *>(addr));
    return {this, addr, hook_handler, name, InlineHookRegistration::State::Pending};
}

bool HookManager::CommitInlineHook(std::span<InlineHookRegistration> registrations) {
    std::scoped_lock mutation_lock(mutation_mutex_);
    // The index is still needed — the conflict check below only looks at the
    // entries before it — but first(i) says "already accepted" more plainly
    // than an inner `j < i` counter.
    for (const size_t i : std::views::iota(size_t{0}, registrations.size())) {
        const auto &registration = registrations[i];
        if (registration.owner_ != this ||
            (registration.state_ != InlineHookRegistration::State::Pending &&
             registration.state_ != InlineHookRegistration::State::Committed)) {
            ARC_LOGE("Invalid registration at index %zu", i);
            return false;
        }
        if (registration.state_ == InlineHookRegistration::State::Committed) continue;
        for (const auto &previous : registrations.first(i)) {
            if (previous.state_ == InlineHookRegistration::State::Committed) continue;
            if (previous.target_addr_ == registration.target_addr_ ||
                previous.hook_handler_ == registration.hook_handler_) {
                ARC_LOGE("Duplicate registration for %s", registration.name_);
                return false;
            }
        }
    }

    const size_t pending_count = static_cast<size_t>(std::ranges::count_if(
        registrations,
        [](const auto &registration) {
            return registration.state_ == InlineHookRegistration::State::Pending;
        }));
    {
        std::unique_lock records_lock(records_mutex_);
        inline_hooks_.reserve(inline_hooks_.size() + pending_count);
    }

    size_t installed_count = 0;
    for (auto &registration : registrations) {
        if (registration.state_ == InlineHookRegistration::State::Committed) continue;

        bool installed = false;
        {
            // The hook becomes observable inside InstallA64. Holding the
            // exclusive record lock makes CALL_ORIG wait until its trampoline
            // is published below.
            std::unique_lock records_lock(records_mutex_);
            void *orig = nullptr;
            void *stub = nullptr;
            installed = mem::InlineHook::InstallA64(registration.target_addr_,
                                                     registration.hook_handler_,
                                                     &orig,
                                                     &stub) &&
                        orig && stub;
            if (installed) {
                registration.orig_handler_ = orig;
                registration.stub_ = stub;
                registration.state_ = InlineHookRegistration::State::Installed;
                inline_hooks_.push_back({registration.target_addr_,
                                         registration.hook_handler_,
                                         registration.orig_handler_,
                                         registration.stub_,
                                         false});
            }
        }
        if (!installed) {
            ARC_LOGE("Failed to hook %s @ %p",
                     registration.name_,
                     reinterpret_cast<void *>(registration.target_addr_));
            for (size_t i = registrations.size(); i > 0; --i) {
                auto &installed = registrations[i - 1];
                if (installed.state_ == InlineHookRegistration::State::Installed) {
                    RollbackRegistration(installed);
                }
            }
            return false;
        }
        ++installed_count;
    }

    for (auto &registration : registrations) {
        if (registration.state_ != InlineHookRegistration::State::Installed) continue;
        registration.state_ = InlineHookRegistration::State::Committed;
        ARC_LOGI("Hooked %s @ %p",
                 registration.name_,
                 reinterpret_cast<void *>(registration.target_addr_));
    }

    ARC_LOGI("Committed %zu inline hooks", installed_count);
    return true;
}

bool HookManager::ValidateHookTarget(uintptr_t addr,
                                     const std::array<uint8_t, 16> &sig,
                                     void *hook_handler,
                                     const char *name,
                                     bool allow_empty_signature) const {
    if (!addr || !hook_handler || !mem::ProcMaps::IsExecutable(addr)) {
        ARC_LOGE("Failed to validate hook %s @ %p", name, reinterpret_cast<void *>(addr));
        return false;
    }

    const bool empty_signature = IsAllZeros(sig);
    if (empty_signature) {
        if (allow_empty_signature) return true;
        ARC_LOGE("Refusing empty signature for hook %s", name);
        return false;
    }
    if (!mem::ProcMaps::IsReadable(addr, sig.size()) ||
        std::memcmp(reinterpret_cast<const void *>(addr), sig.data(), sig.size()) != 0) {
        ARC_LOGE("Signature mismatch for hook %s @ %p", name, reinterpret_cast<void *>(addr));
        return false;
    }
    return true;
}

bool HookManager::RollbackRegistration(InlineHookRegistration &registration) {
    std::scoped_lock mutation_lock(mutation_mutex_);
    std::unique_lock records_lock(records_mutex_);
    if (registration.state_ != InlineHookRegistration::State::Installed) return true;
    if (!mem::InlineHook::RestoreA64(registration.stub_)) {
        const auto existing = std::ranges::find_if(inline_hooks_, [&](const auto &record) {
            return record.target_addr == registration.target_addr_ &&
                   record.hook_handler == registration.hook_handler_;
        });
        if (existing == inline_hooks_.end()) {
            inline_hooks_.push_back({registration.target_addr_,
                                     registration.hook_handler_,
                                     registration.orig_handler_,
                                     registration.stub_,
                                     true});
        } else {
            existing->orig_handler = registration.orig_handler_;
            existing->stub = registration.stub_;
            existing->rollback_pending = true;
        }
        ARC_LOGE("Failed to rollback %s @ %p",
                 registration.name_,
                 reinterpret_cast<void *>(registration.target_addr_));
        return false;
    }

    std::erase_if(inline_hooks_, [&](const auto &record) {
        return record.target_addr == registration.target_addr_ &&
               record.hook_handler == registration.hook_handler_;
    });

    ARC_LOGI("Rolled back %s @ %p",
             registration.name_,
             reinterpret_cast<void *>(registration.target_addr_));
    registration.orig_handler_ = nullptr;
    registration.stub_ = nullptr;
    registration.state_ = InlineHookRegistration::State::Invalid;
    return true;
}

bool HookManager::RetryPendingRollbacks() {
    std::scoped_lock mutation_lock(mutation_mutex_);
    std::unique_lock records_lock(records_mutex_);
    // The predicate is the recovery attempt: a stub that restores is erased, one
    // that fails stays recorded so the next retry can try again.
    bool restored_all = true;
    std::erase_if(inline_hooks_, [&](const InlineHookRecord &record) {
        if (!record.rollback_pending) return false;
        if (!mem::InlineHook::RestoreA64(record.stub)) {
            restored_all = false;
            return false;
        }
        ARC_LOGI("Recovered pending rollback @ %p", reinterpret_cast<void *>(record.target_addr));
        return true;
    });
    return restored_all;
}

const HookManager::InlineHookRecord *HookManager::FindHookRecordByHook(void *hook_handler) const {
    return FindHookRecord(
        [hook_handler](const auto &rec) { return rec.hook_handler == hook_handler; });
}

const HookManager::InlineHookRecord *HookManager::FindHookRecordByTarget(uintptr_t target_addr) const {
    return FindHookRecord(
        [target_addr](const auto &rec) { return rec.target_addr == target_addr; });
}

bool HookManager::HasOriginalForHook(void *hook_handler) const {
    std::shared_lock lock(records_mutex_);
    const auto *rec = FindHookRecordByHook(hook_handler);
    return rec && rec->orig_handler;
}

void *HookManager::GetOriginalForHook(void *hook_handler) {
    const auto &mgr = Instance();
    std::shared_lock lock(mgr.records_mutex_);
    const auto *rec = mgr.FindHookRecordByHook(hook_handler);
    return rec ? rec->orig_handler : nullptr;
}

bool HookManager::IsAllZeros(const std::array<uint8_t, 16> &sig) {
    return std::ranges::all_of(sig, [](uint8_t b) { return b == 0; });
}

} // namespace arc_helper
