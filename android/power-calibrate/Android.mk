LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := debug
LOCAL_STATIC_LIBRARIES := 
LOCAL_SHARED_LIBRARIES :=
 
LOCAL_MODULE := power-calibrate

LOCAL_CPPFLAGS +=

LOCAL_CFLAGS += -DVERSION=\"1.0\"

LOCAL_SRC_FILES += \
	power-calibrate.c \
	perf.c \

include $(BUILD_EXECUTABLE)
