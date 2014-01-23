LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../../common.mk
include $(CLEAR_VARS)
#libhwcpllchange library
include $(CLEAR_VARS)
LOCAL_MODULE                  := libhwcpllchange
LOCAL_MODULE_PATH             := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes) \
                                 $(TOP)/hardware/qcom/display/libhwcomposer
LOCAL_SHARED_LIBRARIES        := $(common_libs) libqservice libexternal libbinder
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               := hwc_pllchange.cpp
include $(BUILD_SHARED_LIBRARY)
