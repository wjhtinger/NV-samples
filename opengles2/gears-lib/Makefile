
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

TARGETS += $(NV_WINSYS)/libgears.a

GEARSLIB_OBJS :=
GEARSLIB_OBJS += $(NV_WINSYS)/gearslib.o
INTERMEDIATES += $(GEARSLIB_OBJS)

GEARSLIB_SHADER_STRS :=
GEARSLIB_SHADER_STRS += gears_vert.glslvh
GEARSLIB_SHADER_STRS += gears_frag.glslfh
INTERMEDIATES += $(GEARSLIB_SHADER_STRS)

GEARSLIB_SHADER_BINS :=
GEARSLIB_SHADER_BINS += gears_vert.cgbin
GEARSLIB_SHADER_BINS += gears_frag.cgbin
INTERMEDIATES += $(GEARSLIB_SHADER_BINS)
ifeq ($(NV_USE_EXTERN_SHADERS),1)
ifeq ($(NV_USE_BINARY_SHADERS),1)
TARGETS += $(GEARSLIB_SHADER_BINS)
endif
endif

GEARSLIB_SHADER_HEXS :=
GEARSLIB_SHADER_HEXS += gears_vert.cghex
GEARSLIB_SHADER_HEXS += gears_frag.cghex
INTERMEDIATES += $(GEARSLIB_SHADER_HEXS)

GEARSLIB_DEMOLIBS :=

GEARSLIB_LDLIBS :=

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

$(NV_WINSYS)/libgears.a: $(NV_WINSYS)/libgears.a($(GEARSLIB_OBJS))

ifeq ($(NV_USE_EXTERN_SHADERS),0)
ifeq ($(NV_USE_BINARY_SHADERS),1)
$(GEARSLIB_OBJS) : $(GEARSLIB_SHADER_HEXS)
else
$(GEARSLIB_OBJS) : $(GEARSLIB_SHADER_STRS)
endif
endif

define demolib-rule
$(1): FORCE
	$(MAKE) -C $$(subst $$(NV_WINSYS)/,,$$(dir $$@))
endef
$(foreach demolib,$(GEARSLIB_DEMOLIBS),$(eval $(call demolib-rule,$(demolib))))

