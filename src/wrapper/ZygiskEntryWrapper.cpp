#include <jni.h>

#include <string_view>

#include <fcntl.h>
#include <unistd.h>

#include "wrapper/WrapperCommon.hpp"
#include "manager/ConfigManager.hpp"
#include "config/ScopeConfig.hpp"
#include "utils/JniUtils.hpp"
#include <zygisk.hpp>

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

namespace arc_helper {

namespace {

JNINativeMethod g_jni_method_hooks[1];

using OrigNativeLoad = jstring (*)(JNIEnv *, jclass, jstring, jobject, jobject);

jstring Runtime_nativeLoad_hook(JNIEnv *env,
                                jclass runtime_class,
                                jstring java_file_name,
                                jobject java_loader,
                                jobject caller);

// When called from zygisk's hookJniNativeMethods path, fnPtr holds the
// real Runtime.nativeLoad.  A non-capturing helper avoids the lambda-static
// bug (static [&] captures stale stack pointers after the first invocation).
inline jstring CallOrigNativeLoad(JNIEnv *env, jclass cls, jstring name,
                                  jobject loader, jobject caller) {
    const auto original = reinterpret_cast<OrigNativeLoad>(g_jni_method_hooks[0].fnPtr);
    if (!original || original == Runtime_nativeLoad_hook) {
        ARC_LOGE("Runtime.nativeLoad original pointer unavailable");
        return nullptr;
    }
    return original(env, cls, name, loader, caller);
}

bool IsTargetLibraryPath(const char *path) {
    if (!path) return false;
    const std::string_view value(path);
    const size_t separator = value.find_last_of("/\\");
    const std::string_view basename = separator == std::string_view::npos
                                          ? value
                                          : value.substr(separator + 1);
    return basename == cfg::module::kLibName;
}

jstring Runtime_nativeLoad_hook(JNIEnv *env,
                                jclass runtime_class,
                                jstring java_file_name,
                                jobject java_loader,
                                jobject caller) {
    if (!java_file_name)
        return CallOrigNativeLoad(env, runtime_class, java_file_name, java_loader, caller);

    const char *lib_name = env->GetStringUTFChars(java_file_name, nullptr);
    if (!lib_name) {
        ClearPendingJniException(env, "GetStringUTFChars(nativeLoad arg)");
        return CallOrigNativeLoad(env, runtime_class, java_file_name, java_loader, caller);
    }

    const bool is_target = IsTargetLibraryPath(lib_name);
    if (is_target) {
        jclass cls = env->FindClass(cfg::module::kRuntimeClass);
        if (cls) {
            const jint rc = env->RegisterNatives(cls, g_jni_method_hooks, 1);
            if (rc != JNI_OK) {
                ARC_LOGE("RegisterNatives restore failed: %d", rc);
            }
            ClearPendingJniException(env, "RegisterNatives(restore nativeLoad)");
            env->DeleteLocalRef(cls);
        } else {
            ClearPendingJniException(env, "FindClass(java/lang/Runtime)");
        }
    }

    auto ret = CallOrigNativeLoad(env, runtime_class, java_file_name, java_loader, caller);
    env->ReleaseStringUTFChars(java_file_name, lib_name);
    if (ret != nullptr) return ret;

    if (is_target) {
        const uintptr_t lib_base = wrapper::FindGameLibraryBase();
        if (!lib_base) {
            ARC_LOGE("Failed to locate %s base", cfg::module::kLibName);
        } else {
            ARC_LOGI("Found %s base @ %p", cfg::module::kLibName, reinterpret_cast<void *>(lib_base));
            wrapper::InitFeatures();
        }
    }

    return ret;
}

void InitJniHooks() {
    g_jni_method_hooks[0].name = "nativeLoad";
    g_jni_method_hooks[0].signature = "(Ljava/lang/String;Ljava/lang/ClassLoader;Ljava/lang/Class;)Ljava/lang/String;";
    g_jni_method_hooks[0].fnPtr = reinterpret_cast<void *>(Runtime_nativeLoad_hook);
}

bool IsModuleDisabled(Api *api, int dirfd) {
    if (!api || dirfd < 0) return false;

    auto exists = [&](const char *name) -> bool {
        const int fd = openat(dirfd, name, O_RDONLY | O_CLOEXEC);
        if (fd < 0) return false;
        close(fd);
        return true;
    };

    return exists("disable") || exists("remove");
}

} // namespace

class ArcHelperZygiskWrapper : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        if (!api || !env) return;
        api_ = api;
        env_ = env;
        InitJniHooks();
    }

    void preServerSpecialize([[maybe_unused]] ServerSpecializeArgs *args) override {
        if (!api_) return;
        api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        if (!api_ || !env_ || !args || !args->nice_name) {
            if (api_) api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        // One getModuleDir fd serves both the disable check and scope matching.
        const int module_dirfd = api_->getModuleDir();
        if (IsModuleDisabled(api_, module_dirfd)) {
            if (module_dirfd >= 0) close(module_dirfd);
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char *package_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!package_name) {
            ClearPendingJniException(env_, "GetStringUTFChars(nice_name)");
            if (module_dirfd >= 0) close(module_dirfd);
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const bool enable_module = cfg::scope::IsTargetPackage(module_dirfd, package_name);
        if (module_dirfd >= 0) close(module_dirfd);
        if (enable_module) ConfigManager::Instance().SetPackageName(package_name);
        env_->ReleaseStringUTFChars(args->nice_name, package_name);

        if (!enable_module) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        api_->hookJniNativeMethods(env_, cfg::module::kRuntimeClass, g_jni_method_hooks, 1);
        ClearPendingJniException(env_, "hookJniNativeMethods(nativeLoad)");
    }

private:
    Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
};

} // namespace arc_helper

REGISTER_ZYGISK_MODULE(arc_helper::ArcHelperZygiskWrapper)
