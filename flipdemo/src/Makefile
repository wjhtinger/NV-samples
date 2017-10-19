# Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

DEBUG ?= no

CFLAGS = -fno-strict-aliasing -Wall -Wcast-align
CFLAGS += -Wl,--dynamic-linker=/lib/ld-linux-aarch64.so.1
CFLAGS += $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_SDK_LIB)
CFLAGS += -Iinclude $(NV_PLATFORM_SDK_INC)
LIBS = -lm -lrt -lnvflipper

ifneq ($(DEBUG),yes)
CFLAGS += -O2
else
CFLAGS += -g -DDEBUG
endif

TARGET = flipdemo

SRC = $(wildcard lib/*.c) $(TARGET).c
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
	@echo "  make clean        - remove all built binaries"
	@echo "  make help         - diplay this message"
