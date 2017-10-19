
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

TARGETS += $(NV_WINSYS)/eglstreamcube

EGLSTREAMCUBE_OBJS :=
EGLSTREAMCUBE_OBJS += $(NV_WINSYS)/eglstreamcube.o
INTERMEDIATES += $(EGLSTREAMCUBE_OBJS)

EGLSTREAMCUBE_SHADER_STRS :=
EGLSTREAMCUBE_SHADER_STRS += cube_vert.glslvh
EGLSTREAMCUBE_SHADER_STRS += cube_frag.glslfh
EGLSTREAMCUBE_SHADER_STRS += logo_frag.glslfh
INTERMEDIATES += $(EGLSTREAMCUBE_SHADER_STRS)

EGLSTREAMCUBE_SHADER_BINS :=
EGLSTREAMCUBE_SHADER_BINS += cube_vert.cgbin
EGLSTREAMCUBE_SHADER_BINS += cube_frag.cgbin
EGLSTREAMCUBE_SHADER_BINS += logo_frag.cgbin
INTERMEDIATES += $(EGLSTREAMCUBE_SHADER_BINS)
ifeq ($(NV_USE_EXTERN_SHADERS),1)
ifeq ($(NV_USE_BINARY_SHADERS),1)
TARGETS += $(EGLSTREAMCUBE_SHADER_BINS)
endif
endif

EGLSTREAMCUBE_SHADER_HEXS :=
EGLSTREAMCUBE_SHADER_HEXS += cube_vert.cghex
EGLSTREAMCUBE_SHADER_HEXS += cube_frag.cghex
EGLSTREAMCUBE_SHADER_HEXS += logo_frag.cghex
INTERMEDIATES += $(EGLSTREAMCUBE_SHADER_HEXS)

EGLSTREAMCUBE_DEMOLIBS :=
EGLSTREAMCUBE_DEMOLIBS += ../nvgldemo/$(NV_WINSYS)/libnvgldemo.a

EGLSTREAMCUBE_LDLIBS :=
EGLSTREAMCUBE_LDLIBS += -lm
EGLSTREAMCUBE_LDLIBS += -lrt
EGLSTREAMCUBE_LDLIBS += -lpthread
EGLSTREAMCUBE_LDLIBS += -lEGL
EGLSTREAMCUBE_LDLIBS += -lGLESv2
EGLSTREAMCUBE_LDLIBS += ${NV_PLATFORM_WINSYS_LIBS}

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

$(NV_WINSYS)/eglstreamcube: $(EGLSTREAMCUBE_OBJS) $(EGLSTREAMCUBE_DEMOLIBS)
	$(LD) $(LDFLAGS) -o $@ $^ $(EGLSTREAMCUBE_LDLIBS)

ifeq ($(NV_USE_EXTERN_SHADERS),0)
ifeq ($(NV_USE_BINARY_SHADERS),1)
$(EGLSTREAMCUBE_OBJS) : $(EGLSTREAMCUBE_SHADER_HEXS)
else
$(EGLSTREAMCUBE_OBJS) : $(EGLSTREAMCUBE_SHADER_STRS)
endif
endif

define demolib-rule
$(1): FORCE
	$(MAKE) -C $$(subst $$(NV_WINSYS)/,,$$(dir $$@))
endef
$(foreach demolib,$(EGLSTREAMCUBE_DEMOLIBS),$(eval $(call demolib-rule,$(demolib))))

