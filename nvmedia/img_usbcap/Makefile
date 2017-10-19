# Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = nvmimg_usbcap

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) -I. -I../utils -Iwinintf
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   += capture.o
OBJS   += cmdline.o
OBJS   += gl_consumer.o
OBJS   += img_producer.o
OBJS   += interop.o
OBJS   += main.o
OBJS   += process2d.o
OBJS   += usb_utils.o
OBJS   += eglstrm_setup.o
OBJS   += ../utils/log_utils.o
OBJS   += ../utils/thread_utils.o
OBJS   += ../utils/misc_utils.o
OBJS   += winintf/egl_utils.o

ifeq ($(NV_WINSYS), egldevice)
  OBJS   += winintf/win_egldevice.o
else ifeq ($(NV_WINSYS), x11)
  OBJS   += winintf/win_x11.o
else ifeq ($(NV_WINSYS), wayland)
  OBJS   += winintf/win_wayland.o
else
  $(error Unrecognized Windowing environment)
endif

LDLIBS := -L ../utils
LDLIBS := -lnvmedia
LDLIBS += -lEGL
LDLIBS += -lGLESv2
ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS += -pthread
  LDLIBS += -ldl
endif

ifeq ($(NV_WINSYS), x11)
  LDLIBS += -lX11
else ifeq ($(NV_WINSYS), wayland)
  LDLIBS += -lxkbcommon
  LDLIBS += -lwayland-egl
  LDLIBS += -lwayland-client
else ifeq ($(NV_WINSYS), screen)
  $(error Unrecognized Windowing environment)
endif

default: $(NV_WINSYS)/$(TARGETS)

$(NV_WINSYS):
	mkdir -p $@

$(NV_WINSYS)/$(TARGETS): $(NV_WINSYS) $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(NV_WINSYS)/$(TARGETS)
