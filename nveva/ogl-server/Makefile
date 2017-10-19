# Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

DEBUG ?= no
HACK_SCREEN_SIZE ?= yes
LIBPNG ?= no

CFLAGS = -fno-strict-aliasing -Wall -Wcast-align
CFLAGS += -Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1
CFLAGS += $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_SDK_LIB)
CFLAGS += -Iinclude $(NV_PLATFORM_SDK_INC)
LIBS = -lm -lrt -lpthread -ldrm -lEGL -lGLESv2 -lwayland-client -lwayland-egl
ifeq ($(LIBPNG),yes)
LIBS += -l:libpng12.so.0
CFLAGS += -DHAVE_LIBPNG
endif

ifneq ($(DEBUG),yes)
CFLAGS += -O2
else
CFLAGS += -g -DDEBUG
endif

# HACK full screen size for EGL consumer stream
ifeq ($(HACK_SCREEN_SIZE),yes)
CFLAGS += -DHACK_SCREEN_SIZE
endif
TARGET = ogl-server

SRC = $(wildcard lib/*.c) $(TARGET).cc
HDR = $(wildcard include/*/*.h)

all: $(TARGET)

$(TARGET): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LIBS)
ifneq ($(DEBUG),yes)
	$(STRIP) $@
endif

clean :
	-$(RM) $(TARGET)

help:
	@echo "  make              - build $(TARGET) binary"
	@echo "  make DEBUG=yes    - build with debug enabled"
	@echo "  make LIBPNG=no    - build without libpng support"
	@echo "  make clean        - remove all built binaries"
	@echo "  make help         - diplay this message"
