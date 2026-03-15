#include <jni.h>

#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include "wrapper/WrapperCommon.hpp"
#include "zygisk.hpp"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

namespace arc_autoplay {

namespace {

JNINativeMethod g_jni_method_hooks[1];

void ClearPendingJniException(JNIEnv *env, const char *where) {
    if (!env || !env->ExceptionCheck()) return;
    ARC_LOGE("JNI exception at %s", where ? where : "unknown");
    env->ExceptionClear();
}

jstring Runtime_nativeLoad_hook(JNIEnv *env,
                                jclass runtime_class,
                                jstring java_file_name,
                                jobject java_loader,
                                jobject caller) {
    static auto invoke_orig = [&]() {
        auto orig = reinterpret_cast<jstring (*)(JNIEnv *, jclass, jstring, jobject, jobject)>(g_jni_method_hooks[0].fnPtr);
        return orig(env, runtime_class, java_file_name, java_loader, java_loader);
    };

    if (!java_file_name) return invoke_orig();
    const char *lib_name = env->GetStringUTFChars(java_file_name, nullptr);
    if (!lib_name) {
        ClearPendingJniException(env, "GetStringUTFChars(nativeLoad arg)");
        return invoke_orig();
    }

    bool is_target = std::strstr(lib_name, cfg::module::kLibName) != nullptr;
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
    
    auto ret = invoke_orig();
    if (ret != nullptr) return ret; // nativeLoad failed

    if (is_target) {
        const uintptr_t lib_base = wrapper::FindGameLibraryBase();
        if (!lib_base) {
            ARC_LOGE("Failed to locate %s base", cfg::module::kLibName);
        } else {
            ARC_LOGI("Found %s base @ %p", cfg::module::kLibName, reinterpret_cast<void *>(lib_base));
            wrapper::InitFeatures();
        }
    }

    env->ReleaseStringUTFChars(java_file_name, lib_name);
    return ret;
}

void InitJniHooks() {
    g_jni_method_hooks[0].name = "nativeLoad";
    g_jni_method_hooks[0].signature = "(Ljava/lang/String;Ljava/lang/ClassLoader;Ljava/lang/Class;)Ljava/lang/String;";
    g_jni_method_hooks[0].fnPtr = reinterpret_cast<void *>(Runtime_nativeLoad_hook);
}

bool IsModuleDisabled(Api *api) {
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

} // namespace

class ArcAutoplayZygiskWrapper : public zygisk::ModuleBase {
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

        if (IsModuleDisabled(api_)) {
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const char *package_name = env_->GetStringUTFChars(args->nice_name, nullptr);
        if (!package_name) {
            ClearPendingJniException(env_, "GetStringUTFChars(nice_name)");
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        const bool enable_module = wrapper::IsTargetPackage(package_name);
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

} // namespace arc_autoplay

REGISTER_ZYGISK_MODULE(arc_autoplay::ArcAutoplayZygiskWrapper)
