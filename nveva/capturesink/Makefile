# Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

INCFILES := -I$(NV_PLATFORM_DIR)/include

LDFLAGS :=  -lrt
LDFLAGS +=  -L$(NV_PLATFORM_SDK_LIB_DIR) -lnvcapturesink
LDFLAGS +=  -Wl,-rpath $(NV_PLATFORM_SDK_LIB_DIR)

CFLAGS = $(NV_PLATFORM_CFLAGS)

SOURCES := capturesinktest.c

OBJECTS = $(SOURCES:.c=.o)

EXECUTABLE = nvcapturesinktest

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $(INCFILES) -c $< -o $@

clean:
	rm -f capturesinktest.o nvcapturesinktest
