LOCAL_PATH := $(call my-dir)


include $(CLEAR_VARS)
LOCAL_MODULE := kdzwriter
LOCAL_SRC_FILES := kdzwriter.c kdz.c md5.c gpt.c
LOCAL_CFLAGS := -Wall
LOCAL_LDFLAGS := -Wl,-dynamic-linker,/sbin/linker64
LOCAL_LDLIBS := -lz
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../../
include $(BUILD_EXECUTABLE)

