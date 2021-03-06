LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../common.mk

LOCAL_MODULE                  := libsdmutils
LOCAL_MODULE_PATH_32          := $(TARGET_OUT_VENDOR)/lib
LOCAL_MODULE_PATH_64          := $(TARGET_OUT_VENDOR)/lib64
LOCAL_MODULE_TAGS             := optional
LOCAL_PROPRIETARY_MODULE      := true
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_CFLAGS                  := -DLOG_TAG=\"SDM\" $(common_flags)
LOCAL_SRC_FILES               := debug.cpp \
                                 rect.cpp \
                                 sys.cpp \
                                 formats.cpp \
                                 utils.cpp

include $(BUILD_SHARED_LIBRARY)

SDM_HEADER_PATH := ../../include
include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO         := $(common_header_export_path)/sdm/utils
LOCAL_COPY_HEADERS             = $(SDM_HEADER_PATH)/utils/constants.h \
                                 $(SDM_HEADER_PATH)/utils/debug.h \
                                 $(SDM_HEADER_PATH)/utils/formats.h \
                                 $(SDM_HEADER_PATH)/utils/locker.h \
                                 $(SDM_HEADER_PATH)/utils/rect.h \
                                 $(SDM_HEADER_PATH)/utils/sys.h \
                                 $(SDM_HEADER_PATH)/utils/utils.h
include $(BUILD_COPY_HEADERS)
