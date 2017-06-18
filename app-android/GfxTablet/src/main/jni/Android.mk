LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := networkclient
LOCAL_SRC_FILES := networkclient.c
include $(BUILD_SHARED_LIBRARY)
