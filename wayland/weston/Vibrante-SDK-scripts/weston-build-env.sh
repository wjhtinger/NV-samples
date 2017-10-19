# Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

# Check for WESTON_DIR is set when sourcing this file
[ -z ${WESTON_DIR} ] && echo "Error: WESTON_DIR is not set" && exit 1

# Detect toolchain
function detect_toolchain
{
    if [ -z "$TOOLDIR" ]; then
        # Check for toolchain
        if [ -d "$1" ]; then
            export TOOLDIR=$1
        else
            return 1
        fi
    else
        # remove the '/' if exist at the end.
        export TOOLDIR=`echo $TOOLDIR | sed s'/[\/]$//'`
        if [ ! -d "$TOOLDIR" ]; then
            echo "Error: Toolchain directory does not exist" > /dev/stderr
            return 1
        fi
    fi
    return 0
}

# Set armhf environment
function set_armhf_env
{
    export ARCH=armhf
    export BUILD_ARCH=armhf
    export BUILD_TARGET=arm-linux-gnueabihf

    export DYNAMIC_LINKER=/lib/ld-linux-armhf.so.3

    export CFLAGS="-fno-strict-aliasing \
                   -mtune=cortex-a15 \
                   -march=armv7-a \
                   -mhard-float \
                   -mfloat-abi=hard \
                   -mfpu=neon-vfpv4 \
                   -mthumb-interwork \
                   -fgcse-after-reload \
                   -finline-functions"
    export CXXFLAGS="$CFLAGS"
    export TARGET_CFLAGS=$CFLAGS

    export TOOLCHAIN_VERSION=4.8.1
    if [ -z "$TOOLCHAIN_PREFIX" ]; then
        export TOOLCHAIN_PREFIX=arm-cortex_a15-linux-gnueabi
    fi
}

# Set arm64 environment
function set_aarch64_env
{
    export ARCH=aarch64
    export BUILD_ARCH=arm64
    export BUILD_TARGET=aarch64-linux-gnu

    export DYNAMIC_LINKER=/lib/ld-linux-aarch64.so.1

    export CFLAGS="-fno-strict-aliasing -Wall -Wcast-align"
    export CXXFLAGS="$CFLAGS"
    export TARGET_CFLAGS=$CFLAGS

    export TOOLCHAIN_VERSION=4.9
    if [ -z "$TOOLCHAIN_PREFIX" ]; then
        export TOOLCHAIN_PREFIX=aarch64-gnu-linux
    fi
}

# Set toolchain
function set_toolchain
{
    if [ -z "$VIBRANTE_TOP" ]; then
        echo "Error: Please export VIBRANTE_TOP - the directory where vibrante" > /dev/stderr
        echo "       PDK/SDK is installed and start the script!!! Note: The" > /dev/stderr
        echo "       toolchain needs to be also installed when you install the" > /dev/stderr
        echo "       Vibrante PDK/SDK. If you are building with V3Le SDK/PDK," > /dev/stderr
        echo "       please export ARCH=armhf before invoking this script" > /dev/stderr
        exit 1
    else
        # remove the '/' if exist at the end.
        export VIBRANTE_TOP=`echo $VIBRANTE_TOP | sed s'/[\/]$//'`
    fi

    # Detect the platform whether armhf (TK1 for V3L, TX1 for V3Le) or
    # arm64 (TK1-64, >=TX1 for V4L) based
    pushd $VIBRANTE_TOP &> /dev/null
    export PLATFORM=`ls -d vibrante-*-linux/ | sed -e 's/\///g'`
    popd &> /dev/null

    # Default toolchain
    local gcc_armhf="$VIBRANTE_TOP/toolchains/tegra-4.8.1-nv/usr/bin/arm-cortex_a15-linux-gnueabi"
    local gcc_aarch64="$VIBRANTE_TOP/toolchains/tegra-4.9-nv/usr/bin/aarch64-gnu-linux"

    # Toolchain overrides
    gcc_armhf=${ARMHF_TOOLCHAIN:-$gcc_armhf}
    gcc_aarch64=${AARCH64_TOOLCHAIN:-$gcc_aarch64}

    case "$PLATFORM" in
        vibrante-vcm30t124-linux)
            if ! detect_toolchain "$gcc_armhf" ; then
                # Toolchain was not detected.
                echo "Error: V3L Toolchain not detected!! Please install" > /dev/stderr
                echo "       toolchain to build!!" > /dev/stderr
                exit 1
            fi
            set_armhf_env
        ;;
        vibrante-t210ref-linux | vibrante-*-linux)
            # Check for toolchain and detect if V3Le or V4L
            if detect_toolchain "$gcc_aarch64" ; then
                # V4L toolchain detected
                set_aarch64_env
            elif detect_toolchain "$gcc_armhf"; then
                # V3Le toolchain detected
                set_armhf_env
            else
                # Toolchain was not detected.
                echo "Error: V3Le/V4L Toolchain not detected!! Please install" > /dev/stderr
                echo "       toolchain to build!!" > /dev/stderr
                exit 1
            fi
        ;;
        *)
            echo "Error: No valid Vibrante SDK/PDK installation found." > /dev/stderr
            exit 1
        ;;
    esac

    export CC=$(printf '%s/%s-%s' $TOOLDIR $TOOLCHAIN_PREFIX gcc)
    export CXX=$(printf '%s/%s-%s' $TOOLDIR $TOOLCHAIN_PREFIX g++)

    export TOOLCHAIN_SYSROOT="$TOOLDIR/../../sysroot"
    export SYSROOT=${SYSROOT:-$VIBRANTE_TOP/weston_sysroot_$ARCH}
    export NVIDIA_ROOT=$SYSROOT
}

# Set build environment
function set_build_env
{
    export WESTON_BUILD_DIR=${WESTON_BUILD_DIR:-$WESTON_DIR/build}
    export LIBINPUT_DIR=$WESTON_DIR/libinput
    export LIBINPUT_BUILD_DIR=${LIBINPUT_BUILD_DIR:-$LIBINPUT_DIR/build}
    export IVI_EXTENSION_DIR=$WESTON_DIR/wayland-ivi-extension
    export IVI_EXTENSION_BUILD_DIR=${IVI_EXTENSION_BUILD_DIR:-$IVI_EXTENSION_DIR/build}
    export SDK_TOPDIR=$WESTON_DIR/../../..
    export TOOLCHAIN=$SDK_TOPDIR/../toolchains

    if [ -z "$VIBRANTE_TOP" ]; then
        export VIBRANTE_TOP=${WESTON_DIR}/../../../..
    else
        # remove the '/' if exist at the end.
        export VIBRANTE_TOP=`echo $VIBRANTE_TOP | sed s'/[\/]$//'`
    fi

    set_toolchain

    export EGL_CFLAGS="-I$SDK_TOPDIR/include"
    export EGL_LIBS="-L$SDK_TOPDIR/lib-target -lEGL -lGLESv2 \
                     -lm -lpthread -ldl -lrt"
    export EGL_TESTS_CFLAGS="${EGL_CFLAGS}"
    export EGL_TESTS_LIBS="${EGL_LIBS}"

    export COMPOSITOR_CFLAGS="-I$SDK_TOPDIR/include"
    export COMPOSITOR_LIBS="-L$SDK_TOPDIR/lib-target -lwayland-server \
                            -lpixman-1 -lxkbcommon"

    export WAYLAND_COMPOSITOR_CFLAGS="-I$SDK_TOPDIR/include"
    export WAYLAND_COMPOSITOR_LIBS="-L$SDK_TOPDIR/lib -lwayland-server \
                                    -lwayland-egl -lwayland-cursor"

    export WAYLAND_CLIENT_CFLAGS="-I$SDK_TOPDIR/include ${EGL_CFLAGS}"
    export WAYLAND_CLIENT_LIBS="-L$SDK_TOPDIR/lib -lwayland-client \
                                -lwayland-cursor -lwayland-egl -lxkbcommon \
                                -lcairo ${EGL_LIBS}"

    export TEST_CLIENT_CFLAGS=${WAYLAND_CLIENT_CFLAGS}
    export TEST_CLIENT_LIBS=${WAYLAND_CLIENT_LIBS}
    export SIMPLE_CLIENT_CFLAGS=${WAYLAND_CLIENT_CFLAGS}
    export SIMPLE_CLIENT_LIBS=${WAYLAND_CLIENT_LIBS}
    export SIMPLE_EGL_CLIENT_CFLAGS=${WAYLAND_CLIENT_CFLAGS}
    export SIMPLE_EGL_CLIENT_LIBS="${WAYLAND_CLIENT_LIBS} \
                                   -lilmCommon \
                                   -lilmClient \
                                   -lilmControl"
    export CLIENT_CFLAGS=${WAYLAND_CLIENT_CFLAGS}
    export CLIENT_LIBS=${WAYLAND_CLIENT_LIBS}
    export SERVER_CFLAGS=${WAYLAND_CLIENT_CFLAGS}
    export SERVER_LIBS=${WAYLAND_CLIENT_LIBS}
    export WESTON_INFO_CFLAGS=${WAYLAND_CLIENT_CFLAGS}
    export WESTON_INFO_LIBS=${WAYLAND_CLIENT_LIBS}
    export WESTON_NATIVE_BACKEND="drm-backend.so"

    export WESTON_INSTALL_DIR=${WESTON_INSTALL_DIR:-$SYSROOT/weston_install}
    export WESTON_PREFIX_DIR=${WESTON_PREFIX_DIR:-/usr}
    export LIBINPUT_INSTALL_DIR=${LIBINPUT_INSTALL_DIR:-$WESTON_INSTALL_DIR}
    export LIBINPUT_PREFIX_DIR=${LIBINPUT_PREFIX_DIR:-$WESTON_PREFIX_DIR}

    export LIBDRM_LIBS="-ldrm -lrt"

    export WAYLAND_SCANNER_CFLAGS="does not"
    export WAYLAND_SCANNER_LIBS="matter"

    export ACLOCAL="aclocal -I $SYSROOT/usr/share/aclocal"

    # Ensure we don't find packages from the host system.
    export PKG_CONFIG_DIR=
    export PKG_CONFIG_LIBDIR=$SYSROOT/usr/lib/$BUILD_TARGET/pkgconfig
    export PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR}:$SYSROOT/usr/share/pkgconfig
    export PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR}:$SYSROOT/usr/lib/pkgconfig
    export PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR}:$WESTON_INSTALL_DIR$WESTON_PREFIX_DIR/lib/pkgconfig
    export PKG_CONFIG_LIBDIR=${PKG_CONFIG_LIBDIR}:$LIBINPUT_INSTALL_DIR$LIBINPUT_PREFIX_DIR/lib/pkgconfig
    export PKG_CONFIG_PATH=$PKG_CONFIG_LIBDIR
    export PKG_CONFIG_SYSROOT_DIR=$SYSROOT

    export wayland_scanner=$WESTON_DIR/tools/wayland-scanner

    export CPPFLAGS=" $CFLAGS \
      -DWAYLAND \
      -I$SDK_TOPDIR/include \
      -I$SYSROOT/usr/include \
      -I$SYSROOT/usr/include/$BUILD_TARGET \
      -I$SYSROOT/usr/include/cairo \
      -I$SYSROOT/usr/include/glib-2.0 \
      -I$SYSROOT/usr/include/pixman-1 \
      -I$SYSROOT/usr/include/libdrm \
      -I$SYSROOT/usr/include/libpng12 \
      -I$LIBINPUT_INSTALL_DIR$LIBINPUT_PREFIX_DIR/include \
      -I$IVI_EXTENSION_DIR/ivi-layermanagement-api/ilmCommon/include \
      -I$IVI_EXTENSION_DIR/ivi-layermanagement-api/ilmClient/include \
      -I$IVI_EXTENSION_DIR/ivi-layermanagement-api/ilmControl/include \
      -I$WESTON_DIR/src \
      -I$WESTON_DIR/shared \
      -I$WESTON_DIR/ivi-shell \
      --sysroot=$SYSROOT"

    export CFLAGS="$CPPFLAGS"
    export CXXFLAGS="$CPPFLAGS"

    export LDFLAGS=" \
      -L$SYSROOT/lib \
      -L$SYSROOT/lib/$BUILD_TARGET \
      -L$SYSROOT/usr/lib \
      -L$SYSROOT/usr/lib/$BUILD_TARGET \
      -L$SDK_TOPDIR/lib-target \
      -L$IVI_EXTENSION_BUILD_DIR/ivi-layermanagement-api/ilmCommon \
      -L$IVI_EXTENSION_BUILD_DIR/ivi-layermanagement-api/ilmClient \
      -L$IVI_EXTENSION_BUILD_DIR/ivi-layermanagement-api/ilmControl \
      -L$LIBINPUT_INSTALL_DIR$LIBINPUT_PREFIX_DIR/lib \
      -Wl,--dynamic-linker=$DYNAMIC_LINKER \
      -Wl,-rpath-link=$SYSROOT/lib \
      -Wl,-rpath-link=$SYSROOT/lib/$BUILD_TARGET \
      -Wl,-rpath-link=$SYSROOT/usr/lib \
      -Wl,-rpath-link=$SYSROOT/usr/lib/$BUILD_TARGET \
      -Wl,-rpath-link=$SDK_TOPDIR/lib-target \
      -Wl,-rpath-link=$IVI_EXTENSION_BUILD_DIR/ivi-layermanagement-api/ilmCommon \
      -Wl,-rpath-link=$IVI_EXTENSION_BUILD_DIR/ivi-layermanagement-api/ilmClient \
      -Wl,-rpath-link=$IVI_EXTENSION_BUILD_DIR/ivi-layermanagement-api/ilmControl \
      -Wl,-rpath-link=$LIBINPUT_INSTALL_DIR$LIBINPUT_PREFIX_DIR/lib \
      -Wl,-rpath=$LIBINPUT_PREFIX_DIR/lib \
      -Wl,--sysroot=$SYSROOT"
}

# Create a clean build sysroot
function create_build_sysroot
{
    [ ! -d $TOOLCHAIN_SYSROOT ] && {
        echo "Error: Unable to find toolchain sysroot $TOOLCHAIN_SYSROOT" > /dev/stderr
        exit 1
    }

    mkdir -p $SYSROOT
    cp -rf "$TOOLCHAIN_SYSROOT/." "$SYSROOT/"

    # Formerly, we also packaged generated protocol files, which interfere with
    # the build, so remove them if they exists
    rm -f $WESTON_DIR/protocol/*.[ch]
    rm -f $IVI_EXTENSION_DIR/protocol/*.[ch]
}

# Remove build output directories
function remove_build_directories
{
    [ -d $WESTON_BUILD_DIR ] && rm -rf $WESTON_BUILD_DIR
    [ -d $LIBINPUT_BUILD_DIR ] && rm -rf $LIBINPUT_BUILD_DIR
    [ -d $IVI_EXTENSION_BUILD_DIR ] && rm -rf $IVI_EXTENSION_BUILD_DIR
}

# Remove build sysroot
function remove_build_sysroot
{
    [ -d $SYSROOT ] && sudo rm -rf $SYSROOT
}
