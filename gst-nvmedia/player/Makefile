# Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

INCFILES := -I$(NV_PLATFORM_GSTREAMER_DIR)/usr/include/gstreamer-1.0
INCFILES += -I$(NV_PLATFORM_ASOUND_DIR)/usr/include/alsa
INCFILES += -I$(NV_PLATFORM_ASOUND_DIR)/usr/include
INCFILES += -I$(NV_PLATFORM_GLIB_DIR)/usr/lib/$(ARM_ARCH_DIST)/glib-2.0/include
INCFILES += -I$(NV_PLATFORM_GLIB_DIR)/usr/include/glib-2.0
INCFILES += -I$(NV_PLATFORM_LIBXML2_DIR)/usr/include/libxml2
INCFILES += -I$(NV_PLATFORM_DIR)/include
INCFILES += -I../utils
INCFILES += -I../../nvmedia/utils
ifeq ($(NV_WINSYS), wayland)
INCFILES += -I$(NV_PLATFORM_DIR)/samples/wayland/weston/protocol
endif

LDFLAGS := -L$(NV_PLATFORM_GSTREAMER_DIR)/usr/lib/$(ARM_ARCH_DIST) -lgstreamer-1.0
LDFLAGS += -L$(NV_PLATFORM_ASOUND_DIR)/usr/lib/$(ARM_ARCH_DIST) -lasound
LDFLAGS += -L$(NV_PLATFORM_LIBXML2_DIR)/usr/lib/$(ARM_ARCH_DIST) -lxml2
LDFLAGS += -L$(NV_PLATFORM_ZLIB_DIR)/usr/lib/$(ARM_ARCH_DIST) -lz
LDFLAGS += -L$(NV_PLATFORM_GLIB_DIR)/usr/lib/$(ARM_ARCH_DIST) -lglib-2.0 -lgobject-2.0 -lgmodule-2.0 -lgthread-2.0 -lm
LDFLAGS += -L$(NV_PLATFORM_PCRE_DIR)/usr/lib/$(ARM_ARCH_DIST) -lpcre
LDFLAGS += -L$(NV_PLATFORM_FFI_DIR)/usr/lib/$(ARM_ARCH_DIST) -lffi -lstdc++
LDFLAGS += -L../../nvmedia/utils -lnvtestutil_capture_input -lnvtestutil_board -lnvtestutil_i2c
LDFLAGS +=  $(NV_PLATFORM_SDK_LIB) -lpthread -ldl -lnvavtp
ifeq ($(NV_WINSYS), x11)
LDFLAGS +=  -lEGL -lX11 -lGLESv2 -lcuda
else ifeq ($(NV_WINSYS), wayland)
LDFLAGS +=  -lEGL -lxkbcommon -lwayland-egl -lwayland-client -lGLESv2 -lcuda
endif
LDFLAGS += -Wl,--unresolved-symbols=ignore-in-shared-libs
LDFLAGS += $(NV_PLATFORM_LDFLAGS)

CFLAGS = $(NV_PLATFORM_CFLAGS)
ifeq ($(NV_WINSYS), x11)
CFLAGS += -DNV_EGL_SINK
endif
ifeq ($(NV_WINSYS), wayland)
CFLAGS += -DNV_EGL_SINK -DWAYLAND
endif


SOURCES := capture.c
SOURCES += cmdline.c
SOURCES += config-parser.c
SOURCES += context-common.c
SOURCES += device-map.c
SOURCES += main.c
SOURCES += media.c
SOURCES += player-core.c
SOURCES += androida4a.c
SOURCES += usbmode.c
SOURCES += taskpool.c
SOURCES += avb_src.c
SOURCES += avb_sink.c
SOURCES += raw_socket.c

ifeq ($(NV_WINSYS), x11)
SOURCES += eglconsumer.c
SOURCES += cuda_consumer.c
SOURCES += ../utils/fdshare.c
SOURCES += ../utils/common.c
SOURCES += ../utils/grutil.c
SOURCES += ../utils/grutil_x11.c
endif

ifeq ($(NV_WINSYS), wayland)
SOURCES += eglconsumer.c
SOURCES += cuda_consumer.c
SOURCES += ../utils/fdshare.c
SOURCES += ../utils/common.c
SOURCES += ../utils/grutil.c
SOURCES += ../utils/grutil_wayland.c
SOURCES += ../../wayland/weston/protocol/ivi-application-protocol.c
endif


OBJECTS = $(SOURCES:.c=.o)

EXECUTABLE = gst-nvmedia-player

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS)  -o $@

.c.o:
	$(CC)  $(CFLAGS) $(INCFILES) -c $< -o $@

clean:
	rm -f capture.o cmdline.o config-parser.o context-common.o \
        device-map.o main.o media.o player-core.o eglconsumer.o \
        ../utils/common.o ../utils/grutil.o ../utils/grutil_x11.o \
        cuda_consumer.o avb_src.o avb_sink.o raw_socket.o ../utils/fdshare.o gst-nvmedia-player
