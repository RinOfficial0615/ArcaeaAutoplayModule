LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := arc_autoplay
LOCAL_SRC_FILES := \
    src/wrapper/ZygiskEntryWrapper.cpp \
    src/wrapper/JniEntryWrapper.cpp \
    src/features/Autoplay.cpp \
    src/features/NetworkLogger.cpp \
    src/features/NetworkBlock.cpp \
    src/manager/GameManager.cpp \
    src/manager/GameVersionManager.cpp \
    src/manager/NetworkManager.cpp \
    src/manager/HookManager.cpp \
    src/utils/memory/ProcMaps.cpp \
    src/utils/memory/AddressResolver.cpp \
    src/utils/memory/Patcher.cpp \
    src/utils/memory/InlineHook.cpp \
    src/utils/memory/ExecUtils.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/src
LOCAL_STATIC_LIBRARIES := libcxx
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)

include libcxx/Android.mk
