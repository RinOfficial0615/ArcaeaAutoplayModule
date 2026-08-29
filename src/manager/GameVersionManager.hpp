#pragma once

#if __has_include(<jni.h>)
#include <jni.h>
#else
#define ARC_HELPER_FWD_DECLARE_JNI 1
#endif

#include <cstdint>
#include <mutex>
#include <string>

#include "game/GameProfile.hpp"
#include "manager/HookManager.hpp"

namespace arc_helper {

#if ARC_HELPER_FWD_DECLARE_JNI
struct _JNIEnv;
using JNIEnv = _JNIEnv;

struct _jobject;
struct _jstring;
using jobject = _jobject *;
using jstring = _jstring *;
#endif

class GameVersionManager {
public:
    using ResolvedCallback = void (*)();

    static GameVersionManager &Instance();

    void SetResolvedCallback(ResolvedCallback callback);

    bool EnsureInstalled();

    bool IsResolved() const;
    const cfg::GameProfile *GetActiveProfile() const;
    cfg::GameVersionId GetActiveVersionId() const;
    std::string GetResolvedVersionString() const;

private:
    GameVersionManager() = default;

    bool EnsureLibBase();

    bool TryResolveFromString(const char *version_string);
    bool TryResolveFromGlobal(const cfg::GameProfile &profile);
    bool ResolveFromKnownProfiles();

    std::string ReadAppVersionString(const cfg::GameProfile &profile) const;
    std::string ReadLibcxxString(uintptr_t string_addr) const;

    void OnSetAppVersion(JNIEnv *env, jobject receiver, jstring version_string);
    void FireResolvedCallback();

    static void SetAppVersionHook(JNIEnv *env, jobject receiver, jstring version_string);

    HookManager &hook_manager_ = HookManager::Instance();
    mutable std::recursive_mutex mutex_{};
    uintptr_t lib_base_ = 0;
    uintptr_t resolved_setter_addr_ = 0;

    const cfg::GameProfile *active_profile_ = nullptr;

    std::string resolved_version_string_{};
    ResolvedCallback resolved_callback_ = nullptr;

    bool hook_installed_ = false;
    bool resolved_callback_fired_ = false;
};

} // namespace arc_helper
