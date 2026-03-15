#pragma once

#if __has_include(<jni.h>)
#include <jni.h>
#else
#define ARC_AUTOPLAY_FWD_DECLARE_JNI 1
#endif

#include <cstdint>
#include <string>

#include "config/GameProfile.hpp"
#include "manager/HookManager.hpp"

namespace arc_autoplay {

#if ARC_AUTOPLAY_FWD_DECLARE_JNI
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

    bool IsResolved() const { return active_profile_ != nullptr; }
    const cfg::GameProfile *GetActiveProfile() const { return active_profile_; }
    cfg::GameVersionId GetActiveVersionId() const;
    const std::string &GetResolvedVersionString() const { return resolved_version_string_; }

private:
    GameVersionManager() = default;

    bool EnsureLibBase();
    const cfg::GameProfile *DetectCandidateProfile() const;
    bool MatchSetterSignature(uintptr_t offset) const;

    bool TryResolveFromString(const char *version_string);
    bool TryResolveFromGlobal(const cfg::GameProfile &profile);

    std::string ReadAppVersionString(const cfg::GameProfile &profile) const;
    std::string ReadLibcxxString(uintptr_t string_addr) const;

    void OnSetAppVersion(JNIEnv *env, jobject receiver, jstring version_string);
    void FireResolvedCallback();

    static void SetAppVersionHook(JNIEnv *env, jobject receiver, jstring version_string);

    HookManager &hook_manager_ = HookManager::Instance();
    uintptr_t lib_base_ = 0;
    uintptr_t addr_set_app_version_ = 0;

    const cfg::GameProfile *candidate_profile_ = nullptr;
    const cfg::GameProfile *active_profile_ = nullptr;

    std::string resolved_version_string_{};
    ResolvedCallback resolved_callback_ = nullptr;

    bool hook_installed_ = false;
    bool resolved_callback_fired_ = false;
};

} // namespace arc_autoplay
