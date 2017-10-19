# Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = nvmvid_play

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) -I. -I../utils -I $(NV_WINSYS)
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   := videodemo.o
OBJS   += cmdline.o
OBJS   += ../utils/deinterlace_utils.o
OBJS   += ../utils/log_utils.o
OBJS   += ../utils/misc_utils.o
OBJS   += ../utils/surf_utils.o

LDLIBS := -lnvmedia
LDLIBS += -lnvparser
LDLIBS += -lnvtvmr
LDLIBS += -lz
LDLIBS += -lm

ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS  += -lrt
endif

ifeq ($(NV_PLATFORM_OS), QNX)
  OBJS   += screen/screen_init.o
  LDLIBS += -lscreen
endif

CFLAGS  += -D_FILE_OFFSET_BITS=64

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(TARGETS)
