# http://b/15193147
# Whitelist devices that are still allowed to use stlport. This will prevent any
# new devices from making the same mistakes.
STLPORT_WHITELIST := \
    deb \
    flo \
    flounder \
    fugu \
    grouper \
    hammerhead \
    mako \
    manta \
    shamu \
    tilapia \

ifneq (,$(filter $(TARGET_DEVICE),$(STLPORT_WHITELIST)))

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libstlport
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_SRC_FILES := $(TARGET_DEVICE)/$(LOCAL_MODULE).so

ifeq ($(TARGET_IS_64_BIT),true)
LOCAL_MULTILIB := both
LOCAL_SRC_FILES_32 := $(TARGET_DEVICE)/32/$(LOCAL_MODULE).so
endif

include $(BUILD_PREBUILT)

endif
