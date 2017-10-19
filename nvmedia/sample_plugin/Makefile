# Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = libnv_sampleplugin

CFLAGS   = $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CFLAGS  += -I.
CFLAGS  += -I../utils

CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)

OBJS   += sample_plugin.o
OBJS   += plugin_config.o
OBJS   += ../utils/log_utils.o

$(TARGETS).so: $(OBJS)
	$(CROSSBIN)ld -shared --soname $(TARGETS).so $^ -o $@

clean clobber:
	rm -rf $(OBJS) $(TARGETS).so
