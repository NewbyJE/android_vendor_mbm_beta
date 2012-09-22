# Use hardware GPS implementation if available.
#
ifeq ($(strip $(BOARD_USES_MBM_GPS)),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := gps.$(CM_BUILD)
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := \
	src/mbm_gps.c \
	src/nmea_reader.h \
	src/nmea_reader.c \
	src/nmea_tokenizer.h \
	src/nmea_tokenizer.c \
	src/gpsctrl/gps_ctrl.c \
	src/gpsctrl/gps_ctrl.h \
	src/gpsctrl/atchannel.c \
	src/gpsctrl/atchannel.h \
	src/gpsctrl/at_tok.c \
	src/gpsctrl/at_tok.h \
	src/gpsctrl/nmeachannel.c \
	src/gpsctrl/nmeachannel.h \
	src/gpsctrl/supl.c \
	src/gpsctrl/supl.h \
	src/gpsctrl/pgps.c \
	src/gpsctrl/pgps.h \
	src/mbm_service_handler.c \
	src/mbm_service_handler.h \
	src/gpsctrl/misc.c \
	src/gpsctrl/misc.h

LOCAL_SHARED_LIBRARIES := \
	libutils \
	libcutils \
	libdl \
	libc

LOCAL_CFLAGS := -Wall -Wextra -std=c99
#LOCAL_CFLAGS += -DLOG_NDEBUG=0
#LOCAL_CFLAGS += -DSINGLE_SHOT

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

include $(BUILD_SHARED_LIBRARY)

endif
