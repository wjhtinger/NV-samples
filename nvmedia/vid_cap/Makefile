# Copyright (c) 2012-2016, NVIDIA Corporation.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an
# express license agreement from NVIDIA Corporation is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = nvmvid_cap

CFLAGS   := $(NV_PLATFORM_OPT)     $(NV_PLATFORM_CFLAGS) -I. -I../utils
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS    := nvmedia_capture.o
OBJS    += cmdline.o
OBJS    += ../utils/deinterlace_utils.o
OBJS    += ../utils/config_parser.o
OBJS    += ../utils/thread_utils.o
OBJS    += ../utils/misc_utils.o
OBJS    += ../utils/surf_utils.o
OBJS    += ../utils/log_utils.o

CFLAGS  += -D_FILE_OFFSET_BITS=64

ifeq ($(NV_PLATFORM_OS), QNX)
  CFLAGS += -DNVMEDIA_QNX
endif

LDLIBS  := -L ../utils
LDLIBS  += -lnvmedia
LDLIBS  += -lnvparser
LDLIBS  += -lnvtvmr
LDLIBS  += -lz
LDLIBS  += -lm
LDLIBS  += -lnvtestutil_capture_input
LDLIBS  += -lnvtestutil_board
LDLIBS  += -lnvtestutil_i2c
ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS  += -lpthread
  LDLIBS  += -lrt
endif

include ../../../make/nvdefs.mk

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clobber clean :
	rm -rf $(TARGETS) $(OBJS)

