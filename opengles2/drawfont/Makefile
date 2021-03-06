
CFLAGS   = $(NV_PLATFORM_OPT)     $(NV_PLATFORM_CFLAGS)
CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  = $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_LDFLAGS)

include ../../../make/nvdefs.mk

# By default we use the following options
#   - Use native functions for window and operating system interaction
#   - Use source shaders
#   - Build shaders into the application rather than using external data files
# Any of these can be overridden with environment variables or by
#   modifying this file. Note that demo executables must be build with the
#   same settings as the demo libraries they link against.
# If you choose external shader files, you will need to copy the files
#   (.cgbin for binary, .glsl[fv] for source) to the platform along with
#   the executable.
NV_USE_BINARY_SHADERS ?= 0
NV_USE_EXTERN_SHADERS ?= 0

ifeq ($(NV_USE_BINARY_SHADERS),1)
  CPPFLAGS += -DUSE_BINARY_SHADERS
endif
ifeq ($(NV_USE_EXTERN_SHADERS),1)
  CPPFLAGS += -DUSE_EXTERN_SHADERS
endif

TARGETS += $(NV_WINSYS)/drawfont

DRAWFONT_OBJS :=
DRAWFONT_OBJS += $(NV_WINSYS)/drawfont.o
INTERMEDIATES += $(DRAWFONT_OBJS)

DRAWFONT_DEMOLIBS :=
DRAWFONT_DEMOLIBS += ../nvtexfont/$(NV_WINSYS)/libnvtexfont2.a
DRAWFONT_DEMOLIBS += ../nvgldemo/$(NV_WINSYS)/libnvgldemo.a

DRAWFONT_LDLIBS :=
DRAWFONT_LDLIBS += -lm
DRAWFONT_LDLIBS += -lrt
DRAWFONT_LDLIBS += -lpthread
DRAWFONT_LDLIBS += -lEGL
DRAWFONT_LDLIBS += -lGLESv2
DRAWFONT_LDLIBS += ${NV_PLATFORM_WINSYS_LIBS}

ifeq ($(findstring $(NV_WINSYS),egldevice screen wayland x11),)
all:
	echo Sample not supported for NV_WINSYS=
else
all: $(TARGETS)
endif

clean:
	rm -rf $(TARGETS) $(INTERMEDIATES)

.PHONY: FORCE
FORCE:

$(NV_WINSYS)/drawfont: $(DRAWFONT_OBJS) $(DRAWFONT_DEMOLIBS)
	$(LD) $(LDFLAGS) -o $@ $^ $(DRAWFONT_LDLIBS)

define demolib-rule
$(1): FORCE
	$(MAKE) -C $$(subst $$(NV_WINSYS)/,,$$(dir $$@))
endef
$(foreach demolib,$(DRAWFONT_DEMOLIBS),$(eval $(call demolib-rule,$(demolib))))

