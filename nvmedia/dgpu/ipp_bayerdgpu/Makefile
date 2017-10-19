# Copyright (c) 2013-2017, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

NV_WINSYS = egldevice

include ../../../../make/nvdefs.mk

TARGETS = nvmipp_bayerdgpu



CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) -I. -I../../utils -Iwinintf
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   += ipp.o
OBJS   += cmdline.o
OBJS   += main.o
OBJS   += interop.o
OBJS   += eglstrm_setup.o
OBJS   += img_producer.o
OBJS   += cuda_consumer.o
OBJS   += ../../utils/buffer_utils.o
OBJS   += ../../utils/config_parser.o
OBJS   += ../../utils/log_utils.o
OBJS   += ../../utils/misc_utils.o
OBJS   += ../../utils/surf_utils.o
OBJS   += ../../utils/thread_utils.o
OBJS   += winintf/egl_utils.o
OBJS   += winintf/egl_utilsdGPU.o
OBJS   += winintf/win_egldevice.o

LDLIBS := -L ../../utils
LDLIBS := -lnvmedia
LDLIBS  += -lnvmedia_isc
LDLIBS  += -lnvmedia_ipp
LDLIBS  += -lnv_extimgdev
LDLIBS  += -lnv_embstatsplugin
LDLIBS += -lEGL
LDLIBS += -lGLESv2
LDLIBS += -lcuda


CFLAGS  += -D_FILE_OFFSET_BITS=64


ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS += -pthread
  LDLIBS += -ldl
  LDLIBS += -lrt
else
  LDLIBS += -lsocket
endif


$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(TARGETS)
