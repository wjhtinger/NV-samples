# Copyright (c) 2015-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

include ../../../../make/nvapedefs.mk

PLUGIN = libnvaecfx
TARGET_ELF = $(PLUGIN).elf
TARGET_LIB = $(PLUGIN).a
TARGET_MAP = $(PLUGIN).map
TARGET_BIN = $(PLUGIN).bin
TARGET_DBG = $(PLUGIN).debug.lst
TARGET_SIZ = $(PLUGIN).size
TARGET_SYM = $(PLUGIN).sym
TARGET_LST = $(PLUGIN).lst
TARGET_STK = $(PLUGIN).stack

TARGETS = \
	$(TARGET_ELF) \
	$(TARGET_LIB) \
	$(TARGET_MAP) \
	$(TARGET_BIN) \
	$(TARGET_DBG) \
	$(TARGET_SIZ) \
	$(TARGET_SYM) \
	$(TARGET_LST) \
	$(TARGET_STK)

INCFILES := -I$(NV_PLATFORM_APE_COMMON_INC)
INCFILES += -I$(NV_PLATFORM_APE_NVFX_INC)
INCFILES += -I$(NV_PLATFORM_APE_LK_INC)
INCFILES += -I./include
INCFILES += -I./speex-1.2rc1/include
INCFILES += -I./speex-1.2rc1/libspeex

CFLAGS := $(NV_PLATFORM_APE_CFLAGS)
CFLAGS += -DHAVE_CONFIG_H=1
CFLAGS += -DPLUGIN_NAME=$(PLUGIN)

SOURCES := aec.c
SOURCES += speex-1.2rc1/libspeex/fftwrap.c
SOURCES += speex-1.2rc1/libspeex/filterbank.c
SOURCES += speex-1.2rc1/libspeex/kiss_fft.c
SOURCES += speex-1.2rc1/libspeex/kiss_fftr.c
SOURCES += speex-1.2rc1/libspeex/mdf.c
SOURCES += speex-1.2rc1/libspeex/preprocess.c

LIBS := $(NV_PLATFORM_APE_TARGET_LIBS)/libnvfx.a

LDFLAGS := -Map=$(TARGET_MAP)
LDFLAGS += -e _app_ex_$(PLUGIN)

OBJECTS = $(SOURCES:.c=.o)

all: $(SOURCES) $(TARGETS)

# build a ld -r style combined object
$(TARGET_ELF): $(OBJECTS)
	$(SIZE) -t --common $(sort $(OBJECTS))
	$(LD) -T $(NV_PLATFORM_APE_LINKER_SCRIPT) \
		-r $(LDFLAGS) $(OBJECTS) $(LIBS) $(LIBGCC) -o $@
	$(OBJCOPY) --keep-global-symbol=_app_ex_$(PLUGIN) $@

$(TARGET_LIB): $(OBJECTS)
	$(AR) rcs $(NV_PLATFORM_APE_TARGET_LIBS)/$(TARGET_LIB) $(sort $(OBJECTS))

$(TARGET_BIN): $(TARGET_ELF)
	$(SIZE) $<
	$(OBJCOPY) --remove-section=".text.vectortable" -O binary $< $@

$(TARGET_DBG): $(TARGET_ELF)
	$(OBJDUMP) -Mreg-names-raw -S $< | $(CPPFILT) > $@

$(TARGET_SIZ): $(TARGET_ELF)
	$(NM) -S --size-sort $< > $@

$(TARGET_SYM): $(TARGET_ELF)
	$(OBJDUMP) -t $< | $(CPPFILT) > $@

$(TARGET_LST): $(TARGET_ELF)
	$(OBJDUMP) -Mreg-names-raw -d $< | $(CPPFILT) > $@

$(TARGET_STK): $(TARGET_ELF)
	$(NV_PLATFORM_APE_STACK_USAGE) < $< | sort -n -k 1 -r > $@

.c.o:
	$(CC) $(CFLAGS) $(INCFILES) -c $< -o $@

# clean
clean clobber:
	rm -rf $(OBJECTS) $(TARGETS)
