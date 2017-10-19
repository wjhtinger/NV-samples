# Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../../make/nvdefs.mk

TARGETS = nvrm_gputhermal

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CFLAGS   += -I.
CFLAGS   += -DNV_SDK_BUILD
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   := nvrm_gputhermal.o

LDLIBS += -lnvrm_gpu -lnvrm

CFLAGS  += -D_FILE_OFFSET_BITS=64

include ../../../../make/nvdefs.mk

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(TARGETS)
