LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := arc_autoplay
LOCAL_SRC_FILES := \
    src/ArcAutoplayModule.cpp \
    src/AutoplayEngine.cpp \
    src/MemoryUtils.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/src
LOCAL_STATIC_LIBRARIES := libcxx
LOCAL_LDLIBS += -llog
include $(BUILD_SHARED_LIBRARY)

include libcxx/Android.mk
