# Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = nvm_eglstream

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) -I. -I../utils -Iwinintf
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   := cmdline.o
OBJS   += eglstrm_setup.o
OBJS   += gl_consumer.o
OBJS   += gl_rgbconsumer.o
OBJS   += gl_yuvconsumer.o
OBJS   += gl_producer.o
OBJS   += nvmimg_consumer.o
OBJS   += nvmimg_producer.o
OBJS   += nvmvid_consumer.o
OBJS   += nvmvid_encode.o
OBJS   += nvmvid_producer.o
OBJS   += ../utils/buffer_utils.o
OBJS   += ../utils/config_parser.o
OBJS   += ../utils/log_utils.o
OBJS   += ../utils/misc_utils.o
OBJS   += ../utils/surf_utils.o
OBJS   += ../utils/thread_utils.o
OBJS   += winintf/egl_utils.o
OBJS   += testmain.o
OBJS   += cuda_producer.o
OBJS   += cuda_consumer.o
CFLAGS += -DCUDA_SUPPORT

ifeq ($(NV_WINSYS), egldevice)
  OBJS   += winintf/win_egldevice.o
  OBJS   += egloutput_consumer.o
  CFLAGS += -DEGLOUTPUT_SUPPORT
else ifeq ($(NV_WINSYS), x11)
  OBJS   += winintf/win_x11.o
else ifeq ($(NV_WINSYS), screen)
  OBJS   += winintf/win_screen.o
  OBJS   += screen_consumer.o
else ifeq ($(NV_WINSYS), wayland)
  OBJS   += winintf/win_wayland.o
else
  $(error Unrecognized Windowing environment)
endif

ifeq ($(NV_PLATFORM_OS), QNX)
  CFLAGS += -DNVMEDIA_QNX
endif

LDLIBS := -L ../utils
LDLIBS := -lnvmedia
LDLIBS += -lnvparser
LDLIBS += -lnvtvmr
LDLIBS += -lEGL
LDLIBS += -lGLESv2
LDLIBS += -lz
LDLIBS += -lm

CFLAGS  += -D_FILE_OFFSET_BITS=64
LDLIBS += -lcuda

ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS += -pthread
  LDLIBS += -ldl
  LDLIBS += -lrt
else
  LDLIBS += -lsocket
endif

ifeq ($(NV_WINSYS), x11)
  LDLIBS += -lX11
else ifeq ($(NV_WINSYS), wayland)
  LDLIBS += -lxkbcommon
  LDLIBS += -lwayland-egl
  LDLIBS += -lwayland-client
else ifeq ($(NV_WINSYS), screen)
  LDLIBS += -lscreen
endif

default: $(NV_WINSYS)/$(TARGETS)

$(NV_WINSYS):
	mkdir -p $@

$(NV_WINSYS)/$(TARGETS): $(NV_WINSYS) $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(NV_WINSYS)/$(TARGETS)
