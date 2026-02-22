#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include "AutoplayConfig.h"
#include "AutoplayEngine.h"
#include "Log.h"
#include "MemoryUtils.h"
#include "../zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

namespace arc_autoplay {

static JNINativeMethod g_jni_method_hooks[1];

static jstring Runtime_nativeLoad_hook(JNIEnv *env,
                                       jclass runtime_class,
                                       jstring java_file_name,
                                       jobject java_loader,
                                       jobject caller) {
    auto orig = reinterpret_cast<jstring (*)(JNIEnv *, jclass, jstring, jobject, jobject)>(g_jni_method_hooks[0].fnPtr);
    jstring ret = orig ? orig(env, runtime_class, java_file_name, java_loader, caller) : nullptr;
    if (ret != nullptr) return ret;

    const char *lib_name = env->GetStringUTFChars(java_file_name, nullptr);
    if (!lib_name) return ret;

    if (strstr(lib_name, cfg::kLibName) != nullptr) {
        const uintptr_t lib_base = mem::ProcMaps::FindLibraryBase(cfg::kLibName);
        if (!lib_base) {
            ARC_LOGE("Failed to locate %s base", cfg::kLibName);
        } else {
            ARC_LOGI("Found %s base @ %p", cfg::kLibName, reinterpret_cast<void *>(lib_base));
            AutoplayEngine::Instance().OnLibLoaded(lib_base);
        }

        // Unhook nativeLoad after libcocos2dcpp is loaded.
        jclass cls = env->FindClass(cfg::kRuntimeClass);
        if (cls) {
            env->RegisterNatives(cls, g_jni_method_hooks, 1);
        }
    }

    env->ReleaseStringUTFChars(java_file_name, lib_name);
    return ret;
}

static void InitJniHooks() {
    g_jni_method_hooks[0].name = "nativeLoad";
    g_jni_method_hooks[0].signature = "(Ljava/lang/String;Ljava/lang/ClassLoader;Ljava/lang/Class;)Ljava/lang/String;";
    g_jni_method_hooks[0].fnPtr = reinterpret_cast<void *>(Runtime_nativeLoad_hook);
}

static bool IsModuleDisabled(Api *api) {
    if (!api) return false;
    const int dirfd = api->getModuleDir();
    if (dirfd < 0) return false;

    auto exists = [&](const char *name) -> bool {
        const int fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        close(fd);
        return true;
    };

    const bool disabled = exists("disable") || exists("remove");
    close(dirfd);
    return disabled;
}

class ArcAutoplayModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;
        InitJniHooks();
    }

    void preServerSpecialize([[maybe_unused]] ServerSpecializeArgs *args) override {
        api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (IsModuleDisabled(api_)) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char *package_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        const bool enable_module = AutoplayEngine::IsTargetPackage(package_name);
        if (package_name) env_->ReleaseStringUTFChars(args->nice_name, package_name);

        if (!enable_module) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        api_->hookJniNativeMethods(env_, cfg::kRuntimeClass, g_jni_method_hooks, 1);
    }

private:
    Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
};

} // namespace arc_autoplay

REGISTER_ZYGISK_MODULE(arc_autoplay::ArcAutoplayModule)
