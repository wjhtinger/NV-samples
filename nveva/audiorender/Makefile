# Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

INCFILES := -I$(NV_PLATFORM_DIR)/include

LDFLAGS :=  -L$(NV_PLATFORM_SDK_LIB_DIR) -lnvaudiorender
LDFLAGS +=  -L$(NV_PLATFORM_ASOUND_DIR)/usr/lib/$(ARM_ARCH_DIST) -lasound
LDFLAGS +=  -Wl,-rpath $(NV_PLATFORM_SDK_LIB_DIR)

CFLAGS = $(NV_PLATFORM_CFLAGS)

SOURCES := audiorender.c

OBJECTS = $(SOURCES:.c=.o)

EXECUTABLE = nvaudiorender

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $(INCFILES) -c $< -o $@

clean:
	rm -f audiorender.o nvaudiorender
