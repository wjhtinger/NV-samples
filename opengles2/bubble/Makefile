
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

TARGETS += $(NV_WINSYS)/bubble

BUBBLE_OBJS :=
BUBBLE_OBJS += $(NV_WINSYS)/main.o
BUBBLE_OBJS += $(NV_WINSYS)/bubble.o
BUBBLE_OBJS += $(NV_WINSYS)/envcube.o
BUBBLE_OBJS += $(NV_WINSYS)/shape.o
BUBBLE_OBJS += $(NV_WINSYS)/shaders.o
BUBBLE_OBJS += $(NV_WINSYS)/algebra.o
INTERMEDIATES += $(BUBBLE_OBJS)

BUBBLE_SHADER_STRS :=
BUBBLE_SHADER_STRS += envcube_vert.glslvh
BUBBLE_SHADER_STRS += bubble_vert.glslvh
BUBBLE_SHADER_STRS += mesh_vert.glslvh
BUBBLE_SHADER_STRS += mouse_vert.glslvh
BUBBLE_SHADER_STRS += envcube_frag.glslfh
BUBBLE_SHADER_STRS += bubble_frag.glslfh
BUBBLE_SHADER_STRS += mesh_frag.glslfh
BUBBLE_SHADER_STRS += mouse_frag.glslfh
INTERMEDIATES += $(BUBBLE_SHADER_STRS)

BUBBLE_SHADER_BINS :=
BUBBLE_SHADER_BINS += envcube_vert.cgbin
BUBBLE_SHADER_BINS += bubble_vert.cgbin
BUBBLE_SHADER_BINS += mesh_vert.cgbin
BUBBLE_SHADER_BINS += mouse_vert.cgbin
BUBBLE_SHADER_BINS += envcube_frag.cgbin
BUBBLE_SHADER_BINS += bubble_frag.cgbin
BUBBLE_SHADER_BINS += mesh_frag.cgbin
BUBBLE_SHADER_BINS += mouse_frag.cgbin
INTERMEDIATES += $(BUBBLE_SHADER_BINS)
ifeq ($(NV_USE_EXTERN_SHADERS),1)
ifeq ($(NV_USE_BINARY_SHADERS),1)
TARGETS += $(BUBBLE_SHADER_BINS)
endif
endif

BUBBLE_SHADER_HEXS :=
BUBBLE_SHADER_HEXS += envcube_vert.cghex
BUBBLE_SHADER_HEXS += bubble_vert.cghex
BUBBLE_SHADER_HEXS += mesh_vert.cghex
BUBBLE_SHADER_HEXS += mouse_vert.cghex
BUBBLE_SHADER_HEXS += envcube_frag.cghex
BUBBLE_SHADER_HEXS += bubble_frag.cghex
BUBBLE_SHADER_HEXS += mesh_frag.cghex
BUBBLE_SHADER_HEXS += mouse_frag.cghex
INTERMEDIATES += $(BUBBLE_SHADER_HEXS)

BUBBLE_DEMOLIBS :=
BUBBLE_DEMOLIBS += ../nvtexfont/$(NV_WINSYS)/libnvtexfont2.a
BUBBLE_DEMOLIBS += ../nvgldemo/$(NV_WINSYS)/libnvgldemo.a

BUBBLE_LDLIBS :=
BUBBLE_LDLIBS += -lm
BUBBLE_LDLIBS += -lrt
BUBBLE_LDLIBS += -lpthread
BUBBLE_LDLIBS += -lEGL
BUBBLE_LDLIBS += -lGLESv2
BUBBLE_LDLIBS += ${NV_PLATFORM_WINSYS_LIBS}

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

$(NV_WINSYS)/bubble: $(BUBBLE_OBJS) $(BUBBLE_DEMOLIBS)
	$(LD) $(LDFLAGS) -o $@ $^ $(BUBBLE_LDLIBS)

ifeq ($(NV_USE_EXTERN_SHADERS),0)
ifeq ($(NV_USE_BINARY_SHADERS),1)
$(BUBBLE_OBJS) : $(BUBBLE_SHADER_HEXS)
else
$(BUBBLE_OBJS) : $(BUBBLE_SHADER_STRS)
endif
endif

define demolib-rule
$(1): FORCE
	$(MAKE) -C $$(subst $$(NV_WINSYS)/,,$$(dir $$@))
endef
$(foreach demolib,$(BUBBLE_DEMOLIBS),$(eval $(call demolib-rule,$(demolib))))

