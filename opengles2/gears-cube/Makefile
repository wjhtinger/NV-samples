
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

TARGETS += $(NV_WINSYS)/gearscube

GEARSCUBE_OBJS :=
GEARSCUBE_OBJS += $(NV_WINSYS)/gearscube.o
INTERMEDIATES += $(GEARSCUBE_OBJS)

GEARSCUBE_SHADER_STRS :=
GEARSCUBE_SHADER_STRS += cube_vert.glslvh
GEARSCUBE_SHADER_STRS += cube_frag.glslfh
INTERMEDIATES += $(GEARSCUBE_SHADER_STRS)

GEARSCUBE_SHADER_BINS :=
GEARSCUBE_SHADER_BINS += cube_vert.cgbin
GEARSCUBE_SHADER_BINS += cube_frag.cgbin
INTERMEDIATES += $(GEARSCUBE_SHADER_BINS)
ifeq ($(NV_USE_EXTERN_SHADERS),1)
ifeq ($(NV_USE_BINARY_SHADERS),1)
TARGETS += $(GEARSCUBE_SHADER_BINS)
endif
endif

GEARSCUBE_SHADER_HEXS :=
GEARSCUBE_SHADER_HEXS += cube_vert.cghex
GEARSCUBE_SHADER_HEXS += cube_frag.cghex
INTERMEDIATES += $(GEARSCUBE_SHADER_HEXS)

GEARSCUBE_DEMOLIBS :=
GEARSCUBE_DEMOLIBS += ../gears-lib/$(NV_WINSYS)/libgears.a
GEARSCUBE_DEMOLIBS += ../nvgldemo/$(NV_WINSYS)/libnvgldemo.a

GEARSCUBE_LDLIBS :=
GEARSCUBE_LDLIBS += -lm
GEARSCUBE_LDLIBS += -lrt
GEARSCUBE_LDLIBS += -lpthread
GEARSCUBE_LDLIBS += -lEGL
GEARSCUBE_LDLIBS += -lGLESv2
GEARSCUBE_LDLIBS += ${NV_PLATFORM_WINSYS_LIBS}

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

$(NV_WINSYS)/gearscube: $(GEARSCUBE_OBJS) $(GEARSCUBE_DEMOLIBS)
	$(LD) $(LDFLAGS) -o $@ $^ $(GEARSCUBE_LDLIBS)

ifeq ($(NV_USE_EXTERN_SHADERS),0)
ifeq ($(NV_USE_BINARY_SHADERS),1)
$(GEARSCUBE_OBJS) : $(GEARSCUBE_SHADER_HEXS)
else
$(GEARSCUBE_OBJS) : $(GEARSCUBE_SHADER_STRS)
endif
endif

define demolib-rule
$(1): FORCE
	$(MAKE) -C $$(subst $$(NV_WINSYS)/,,$$(dir $$@))
endef
$(foreach demolib,$(GEARSCUBE_DEMOLIBS),$(eval $(call demolib-rule,$(demolib))))

