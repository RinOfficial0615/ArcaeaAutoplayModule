#include "manager/GameVersionManager.hpp"

#include <array>
#include <cstring>

#include "config/ModuleConfig.h"
#include "utils/Log.h"
#include "utils/MemoryUtils.hpp"

namespace arc_autoplay {
namespace {

constexpr size_t kMaxVersionStringLen = 64;

inline constexpr std::array<uint8_t, 16> kSig_SetAppVersion = {
    0xFF, 0x43, 0x02, 0xD1,
    0xFD, 0x7B, 0x04, 0xA9,
    0xF9, 0x2B, 0x00, 0xF9,
    0xF8, 0x5F, 0x06, 0xA9,
};

void ClearPendingJniException(JNIEnv *env, const char *where) {
    if (!env || !env->ExceptionCheck()) return;
    ARC_LOGE("GameVersionManager: JNI exception at %s", where ? where : "unknown");
    env->ExceptionClear();
}

std::string CopyJniString(JNIEnv *env, jstring value) {
    if (!env || !value) return {};

    const char *utf8 = env->GetStringUTFChars(value, nullptr);
    if (!utf8) {
        ClearPendingJniException(env, "GetStringUTFChars(setAppVersion)");
        return {};
    }

    std::string copy(utf8);
    env->ReleaseStringUTFChars(value, utf8);
    return copy;
}

} // namespace

GameVersionManager &GameVersionManager::Instance() {
    static GameVersionManager manager;
    return manager;
}

void GameVersionManager::SetResolvedCallback(ResolvedCallback callback) {
    resolved_callback_ = callback;
    FireResolvedCallback();
}

cfg::GameVersionId GameVersionManager::GetActiveVersionId() const {
    return active_profile_ ? active_profile_->id : cfg::GameVersionId::kUnknown;
}

bool GameVersionManager::EnsureLibBase() {
    if (lib_base_) return true;
    if (!hook_manager_.EnsureReady()) return false;

    lib_base_ = hook_manager_.GetLibBase();
    return lib_base_ != 0;
}

bool GameVersionManager::MatchSetterSignature(uintptr_t offset) const {
    const uintptr_t addr = lib_base_ + offset;
    return mem::ProcMaps::IsReadable(addr, kSig_SetAppVersion.size()) &&
           std::memcmp(reinterpret_cast<const void *>(addr),
                       kSig_SetAppVersion.data(),
                       kSig_SetAppVersion.size()) == 0;
}

const cfg::GameProfile *GameVersionManager::DetectCandidateProfile() const {
    if (!lib_base_) return nullptr;

    for (const auto &profile : cfg::kSupportedGameProfiles) {
        if (MatchSetterSignature(profile.version_probe.set_app_version)) {
            return &profile;
        }
    }
    return nullptr;
}

std::string GameVersionManager::ReadLibcxxString(uintptr_t string_addr) const {
    if (!string_addr || !mem::ProcMaps::IsReadable(string_addr, 1)) return {};

    const uint8_t first = mem::Read<uint8_t>(string_addr);
    if ((first & 1u) == 0) {
        const size_t size = static_cast<size_t>(first >> 1);
        if (size == 0) return {};
        if (size > 22 || !mem::ProcMaps::IsReadable(string_addr + 1, size)) return {};
        return std::string(reinterpret_cast<const char *>(string_addr + 1), size);
    }

    if (!mem::ProcMaps::IsReadable(string_addr + 16, sizeof(uintptr_t))) return {};
    const size_t size = mem::Read<size_t>(string_addr + 8);
    const uintptr_t data = mem::Read<uintptr_t>(string_addr + 16);
    if (!data || size == 0 || size > kMaxVersionStringLen) return {};
    if (!mem::ProcMaps::IsReadable(data, size)) return {};
    return std::string(reinterpret_cast<const char *>(data), size);
}

std::string GameVersionManager::ReadAppVersionString(const cfg::GameProfile &profile) const {
    if (!lib_base_) return {};
    return ReadLibcxxString(lib_base_ + profile.version_probe.app_version_string);
}

bool GameVersionManager::TryResolveFromString(const char *version_string) {
    if (!version_string || version_string[0] == '\0') return false;

    const cfg::GameProfile *profile = cfg::FindGameProfileByVersionString(version_string);
    if (!profile) {
        ARC_LOGE("GameVersionManager: unsupported appVersion '%s'", version_string);
        return false;
    }

    if (active_profile_ == profile && resolved_version_string_ == version_string) {
        FireResolvedCallback();
        return true;
    }

    active_profile_ = profile;
    candidate_profile_ = profile;
    resolved_version_string_ = version_string;
    ARC_LOGI("GameVersionManager: resolved version %s", resolved_version_string_.c_str());
    FireResolvedCallback();
    return true;
}

bool GameVersionManager::TryResolveFromGlobal(const cfg::GameProfile &profile) {
    const std::string version = ReadAppVersionString(profile);
    return TryResolveFromString(version.c_str());
}

bool GameVersionManager::EnsureInstalled() {
    if (IsResolved()) {
        FireResolvedCallback();
        return true;
    }
    if (!EnsureLibBase()) return false;

    if (!candidate_profile_) {
        candidate_profile_ = DetectCandidateProfile();
        if (candidate_profile_) {
            ARC_LOGI("GameVersionManager: matched candidate profile %s", candidate_profile_->version_name);
        } else {
            ARC_LOGE("GameVersionManager: failed to match any supported profile");
            return false;
        }
    }

    if (TryResolveFromGlobal(*candidate_profile_)) return true;
    if (hook_installed_) return false;

    hook_installed_ = hook_manager_.InstallInlineHook(addr_set_app_version_,
                                                      candidate_profile_->version_probe.set_app_version,
                                                      kSig_SetAppVersion,
                                                      SetAppVersionHook,
                                                      "Java_low_moe_AppActivity_setAppVersion");
    if (!hook_installed_) {
        ARC_LOGE("GameVersionManager: failed to install setAppVersion hook");
        return false;
    }

    return TryResolveFromGlobal(*candidate_profile_);
}

void GameVersionManager::OnSetAppVersion(JNIEnv *env, jobject receiver, jstring version_string) {
    const std::string version_copy = CopyJniString(env, version_string);
    CALL_ORIG(SetAppVersionHook, env, receiver, version_string);

    if (TryResolveFromString(version_copy.c_str())) return;
    if (candidate_profile_) {
        TryResolveFromGlobal(*candidate_profile_);
    }
}

void GameVersionManager::FireResolvedCallback() {
    if (!active_profile_ || !resolved_callback_ || resolved_callback_fired_) return;
    resolved_callback_fired_ = true;
    resolved_callback_();
}

void GameVersionManager::SetAppVersionHook(JNIEnv *env, jobject receiver, jstring version_string) {
    Instance().OnSetAppVersion(env, receiver, version_string);
}

} // namespace arc_autoplay
