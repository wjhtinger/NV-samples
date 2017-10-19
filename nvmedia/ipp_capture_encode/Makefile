# Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = nvmipp_capture_encode

CFLAGS   = $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CFLAGS  += -I.
CFLAGS  += -Iimage_encoder
CFLAGS  += -Istreaming_engine
CFLAGS  += -Istreaming_engine/capture
CFLAGS  += -Istreaming_engine/isp
CFLAGS  += -I../utils
CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  = $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   := image_encoder/image_encoder.o
OBJS   += streaming_engine/capture/capture.o
OBJS   += streaming_engine/isp/isp.o
OBJS   += streaming_engine/stream_top.o
OBJS   += check_version.o
OBJS   += cmdline.o
OBJS   += display.o
OBJS   += err_handler.o
OBJS   += image_encoder_top.o
OBJS   += staging.o
OBJS   += main.o
OBJS   += op_stream_handler.o
OBJS   += writer.o
OBJS   += ../utils/config_parser.o
OBJS   += ../utils/log_utils.o
OBJS   += ../utils/misc_utils.o
OBJS   += ../utils/thread_utils.o

LDLIBS  := -L ../utils
LDLIBS  += -lnv_extimgdev
LDLIBS  += -lnv_sampleplugin
LDLIBS  += -lnvmedia
LDLIBS  += -lnvmedia_ipp
LDLIBS  += -lnvmedia_acp
LDLIBS  += -lnvmedia_isc
LDLIBS  += -lnvparser
LDLIBS  += -lnvtvmr
LDLIBS  += -lz
LDLIBS  += -lm

CFLAGS  += -D_FILE_OFFSET_BITS=64

ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS  += -lpthread
  LDLIBS  += -lrt
endif

ifeq ($(NV_PLATFORM_OS), QNX)
  CFLAGS += -DNVMEDIA_QNX
endif

include ../../../make/nvdefs.mk

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(TARGETS)
