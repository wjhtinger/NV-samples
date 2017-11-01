# Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../../make/nvdefs.mk

TARGETS = libnv_extimgdev

CFLAGS   = $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CFLAGS  += -I.
CFLAGS  += -I../../utils
CFLAGS  += -I../drv
CFLAGS  += -I./maxim
CFLAGS  += -I./tpg
CFLAGS  += -D_FILE_OFFSET_BITS=64
CFLAGS  += -fPIC
ifeq ($(NV_PLATFORM_OS), QNX)
  CFLAGS += -DNVMEDIA_QNX
endif

CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)

OBJS   += dev.o
OBJS   += dev_list.o
OBJS   += dev_map.o
OBJS   += dev_property.o
OBJS   += maxim/ref_max9286_9271_ov10635.o
OBJS   += maxim/ref_max9286_9271_ov10640.o
OBJS   += maxim/c_max9286_9271_ov10640.o
OBJS   += maxim/c_max9286_9271_ov10640lsoff.o
OBJS   += maxim/d_max9286_9271_mt9v024.o
OBJS   += maxim/ref_max9286_96705_ar0231.o
OBJS   += maxim/ref_max9286_96705_ar0231_rccb.o
OBJS   += maxim/ref_max9286_96705_ov2718_xc7027.o
OBJS   += maxim/ov2718_xc7027.o
OBJS   += maxim/ref_max9288_96705_ov10635.o
OBJS   += maxim/m_max9288_96705_ar0140.o
OBJS   += tpg/tpg.o
OBJS   += ../drv/isc_max9271.o
OBJS   += ../drv/isc_max96705.o
OBJS   += ../drv/isc_max9286.o
OBJS   += ../drv/isc_max9288.o
OBJS   += ../drv/isc_ov10635.o
OBJS   += ../drv/isc_ov10640.o
OBJS   += ../drv/isc_ar0231.o
OBJS   += ../drv/isc_ar0231_rccb.o
OBJS   += ../drv/isc_ov2718.o
OBJS   += ../drv/isc_xc7027.o
OBJS   += ../../utils/log_utils.o

LDLIBS += -lnvmedia_isc

$(TARGETS).so: $(OBJS)
	$(CROSSBIN)ld -shared --soname $(TARGETS).so $^ -o $@ -L $(LDLIBS)
	$(AR) rcs $(TARGETS).a $@ $^

clean clobber:
	rm -rf $(OBJS) $(TARGETS).so $(TARGETS).a
