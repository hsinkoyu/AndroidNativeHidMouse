LOCAL_PATH:= $(call my-dir)

# android_hid_mouse_test
# =========================================
include $(CLEAR_VARS)
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES:= \
    android_hid_mouse_test.c

LOCAL_MODULE := android_hid_mouse_test
LOCAL_SHARED_LIBRARIES := libc
include $(BUILD_EXECUTABLE)
