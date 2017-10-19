# Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = nvmimg_cc

CFLAGS   = $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) -I. -I../utils
CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  = $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   := capture.o
OBJS   += capture_status.o
OBJS   += check_version.o
OBJS   += cmdline.o
OBJS   += composite.o
OBJS   += display.o
OBJS   += grp_activate.o
OBJS   += runtime_settings.o
OBJS   += i2cCommands.o
OBJS   += main.o
OBJS   += parser.o
OBJS   += save.o
OBJS   += sensor_info.o
OBJS   += sensorInfo_ov10640.o
OBJS   += sensorInfo_ar0231.o
OBJS   += ../utils/log_utils.o
OBJS   += ../utils/misc_utils.o
OBJS   += ../utils/surf_utils.o
OBJS   += ../utils/thread_utils.o

LDLIBS  := -L ../utils
LDLIBS  += -lnvmedia
LDLIBS  += -lnvmedia_isc
LDLIBS  += -lnvrawfile_interface
LDLIBS  += -lnvtvmr
LDLIBS  += -lz
LDLIBS  += -lm
LDLIBS  += -lnvtestutil_board
LDLIBS  += -lnvtestutil_i2c

CFLAGS  += -D_FILE_OFFSET_BITS=64

ifeq ($(NV_PLATFORM_OS), Linux)
    LDLIBS  += -lpthread
endif

ifeq ($(NV_PLATFORM_OS), QNX)
  CFLAGS += -DNVMEDIA_QNX
endif

include ../../../make/nvdefs.mk

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(TARGETS)
