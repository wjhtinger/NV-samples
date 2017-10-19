# Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

export WESTON_DIR=$(cd "$(dirname "$0")" && pwd)

source ${WESTON_DIR}/Vibrante-SDK-scripts/weston-build-env.sh
source ${WESTON_DIR}/Vibrante-SDK-scripts/weston-package-mgr.sh

function print_usage
{
    echo ""
    echo "Usage:"
    echo "  ./Vibrante-SDK-build-weston.sh"
    echo "  ./Vibrante-SDK-build-weston.sh clean|clean-all"
    echo "  ./Vibrante-SDK-build-weston.sh download-dependencies"
    echo ""
    echo "Description:"
    echo "  Run Vibrante-SDK-build-weston.sh script without arguments to build"
    echo "  both Weston and IVI extension. 'clean' argument will make the script"
    echo "  to clean last build output files. 'clean-all' will make the script"
    echo "  to also remove configuration files and sysroot directory."
    echo ""
    echo "  To only download weston dependencies (both binaries and sources),"
    echo "  run the script with 'download-dependencies' argument. Resulting"
    echo "  files will be downloaded under <SYSROOT>/packages/."
    echo ""
    echo "  Environment variables SYSROOT, WESTON_BUILD_DIR, WESTON_INSTALL_DIR,"
    echo "  WESTON_PREFIX_DIR, LIBINPUT_BUILD_DIR, LIBINPUT_PREFIX_DIR,"
    echo "  LIBINPUT_INSTALL_DIR and IVI_EXTENSION_BUILD_DIR will take default"
    echo "  values if they are not set by user."
    echo ""
    echo "  Toolchains used by the script can be overriden by setting variables"
    echo "  ARMHF_TOOLCHAIN and/or AARCH64_TOOLCHAIN. E.g.:"
    echo "      export ARMHF_TOOLCHAIN=<VIBRANTE_TOP>/toolchains/tegra-4.8.1-nv/usr/bin/arm-cortex_a15-linux-gnueabi"
    echo "      export AARCH64_TOOLCHAIN=<VIBRANTE_TOP>/toolchains/tegra-4.9-nv/usr/bin/aarch64-gnu-linux"
    echo "      ./Vibrante-SDK-build-weston.sh"
    echo ""
}

function host_checks
{
    # Check for autoconf
    [ -z `which autoconf` ] && {
        echo "Error: Please install autoconf utility on the host machine" > /dev/stderr
        echo "       (e.g: sudo apt-get install autoconf)" > /dev/stderr
        exit 1
    }

    # Check for libtool
    [ -z `which libtool` ] && {
        echo "Error: Please install libtool utility on the host machine" > /dev/stderr
        echo "       (e.g: sudo apt-get install libtool)" > /dev/stderr
        exit 1
    }

    # Check for pkg-config
    [ -z `which pkg-config` ] && {
        echo "Error: Please install pkg-config utility on the host machine" > /dev/stderr
        echo "       (e.g: sudo apt-get install pkg-config)" > /dev/stderr
        exit 1
    }

    # Check for cmake
    [ -z `which cmake` ] && {
        echo "Error: Please install cmake utility on the host machine" > /dev/stderr
        echo "       (e.g: sudo apt-get install cmake)" > /dev/stderr
        exit 1
    }

    # Check for wayland-scanner runs
    [ `$WESTON_DIR/tools/wayland-scanner 2>&1 | grep -c 'usage: ./scanner'` = 1 ] || {
        echo "Error: Unable to run wayland-scanner. Please check 32-bit" > /dev/stderr
        echo "       libc6 and libexpat1 compatibility libraries are" > /dev/stderr
        echo "       properly installed on the host machine" > /dev/stderr
        echo "       (e.g: sudo apt-get install libc6:i386 libexpat1:i386)" > /dev/stderr
        exit 1
    }
}

function build_libinput
{
    # Create output directory
    [ ! -d ${LIBINPUT_BUILD_DIR} ] && {
        mkdir -p ${LIBINPUT_BUILD_DIR} || {
            echo "Error: Unable to create directory ${LIBINPUT_BUILD_DIR}" > /dev/stderr
            exit 1
        }
    }

    echo "Entering directory ${LIBINPUT_BUILD_DIR}..."
    pushd ${LIBINPUT_BUILD_DIR} &> /dev/null

    # Configure
    ${LIBINPUT_DIR}/autogen.sh --disable-libwacom \
                               --prefix=${LIBINPUT_PREFIX_DIR} \
                               --build=i686-pc-linux_gnu        \
                               --host=$BUILD_TARGET || exit 1

    # Build
    make -j`grep processor /proc/cpuinfo | wc -l` || exit 1

    echo "Exiting directory ${PWD}..."
    popd &> /dev/null
}

function install_libinput
{
    echo "Entering directory ${LIBINPUT_BUILD_DIR}..."
    pushd ${LIBINPUT_BUILD_DIR} &> /dev/null

    # Install under ${LIBINPUT_INSTALL_DIR}
    sudo make DESTDIR=${LIBINPUT_INSTALL_DIR} install || exit 1

    # Patch libinput.pc file
    sudo sed -e "s@${SYSROOT}@@g" -i ${LIBINPUT_INSTALL_DIR}${LIBINPUT_PREFIX_DIR}/lib/pkgconfig/libinput.pc

    echo "Exiting directory ${PWD}..."
    popd &> /dev/null
}

function clean_libinput
{
    [ -d ${LIBINPUT_BUILD_DIR} ] && {
        echo "Entering directory ${LIBINPUT_BUILD_DIR}..."
        pushd ${LIBINPUT_BUILD_DIR} &> /dev/null

        # Clean
        [ -f Makefile ] && make clean

        echo "Exiting directory ${PWD}..."
        popd &> /dev/null
    }
}

function build_weston
{
    # ${1}: If weston_only, build weston without examples
    #       If all, build weston and clients

    local weston_only=0
    case "${1}" in
        weston_only)
            weston_only=1
        ;;
        all)
        ;;
        *)
            echo "Error: Bad build_weston argument ${1}" > /dev/stderr
            exit 1
        ;;
    esac

    # Create output directory
    [ ! -d ${WESTON_BUILD_DIR} ] && {
        mkdir -p ${WESTON_BUILD_DIR} || {
            echo "Error: Unable to create directory ${WESTON_BUILD_DIR}" > /dev/stderr
            exit 1
        }
    }

    echo "Entering directory ${WESTON_BUILD_DIR}..."
    pushd ${WESTON_BUILD_DIR} &> /dev/null

    # Configure
    local options="--prefix=${WESTON_PREFIX_DIR} \
                   --build=i686-pc-linux_gnu     \
                   --host=$BUILD_TARGET          \
                   --disable-rpi-compositor      \
                   --disable-xwayland            \
                   --disable-fbdev-compositor    \
                   --disable-headless-compositor \
                   --disable-wcap-tools"
    if [ ${weston_only} -eq 1 ]; then
        options="${options}                      \
                 --disable-drm-compositor        \
                 --disable-ivi-shell             \
                 --disable-fullscreen-shell      \
                 --disable-wayland-compositor    \
                 --disable-x11-compositor        \
                 --disable-simple-egl-clients"
    fi
    ${WESTON_DIR}/autogen.sh ${options} || exit 1

    # Build
    make -j`grep processor /proc/cpuinfo | wc -l` || exit 1

    echo "Exiting directory ${PWD}..."
    popd &> /dev/null
}

function install_weston
{
    echo "Entering directory ${WESTON_BUILD_DIR}..."
    pushd ${WESTON_BUILD_DIR} &> /dev/null

    # Install under ${WESTON_INSTALL_DIR}
    sudo make DESTDIR=${WESTON_INSTALL_DIR} install || exit 1

    # Patch weston.pc file
    sudo sed -e "s@${SYSROOT}@@g" -i ${WESTON_INSTALL_DIR}${WESTON_PREFIX_DIR}/lib/pkgconfig/weston.pc

    echo "Exiting directory ${PWD}..."
    popd &> /dev/null
}

function clean_weston
{
    [ -d ${WESTON_BUILD_DIR} ] && {
        echo "Entering directory ${WESTON_BUILD_DIR}..."
        pushd ${WESTON_BUILD_DIR} &> /dev/null

        # Clean
        [ -f Makefile ] && make clean

        echo "Exiting directory ${PWD}..."
        popd &> /dev/null
    }
}

function build_ivi_extension
{
    # Create output directory
    [ ! -d ${IVI_EXTENSION_BUILD_DIR} ] && {
        mkdir -p ${IVI_EXTENSION_BUILD_DIR} || {
            echo "Error: Unable to create directory ${IVI_EXTENSION_BUILD_DIR}" > /dev/stderr
            exit 1
        }
    }

    echo "Entering directory ${IVI_EXTENSION_BUILD_DIR}..."
    pushd ${IVI_EXTENSION_BUILD_DIR} &> /dev/null

    # Generate toolchain.cmake file
    cat ${WESTON_DIR}/Vibrante-SDK-scripts/toolchain.cmake.template |
    sed -e "s@###_C_COMPILER_###@${CC}@g" \
        -e "s@###_CXX_COMPILER_###@${CXX}@g" \
        -e "s@###_ROOT_PATH_###@${SYSROOT}@g" \
        -e "s@###_WAYLAND_SCANNER_###@${wayland_scanner}@g" > toolchain.cmake

    # Patch weston.pc file
    local new_prefix=`echo ${WESTON_INSTALL_DIR} | sed "s@${SYSROOT}@@g"`${WESTON_PREFIX_DIR}
    sudo sed -e "s@^prefix=.*@prefix=${new_prefix}@g" -i ${WESTON_INSTALL_DIR}${WESTON_PREFIX_DIR}/lib/pkgconfig/weston.pc

    # Configure
    cmake -DCMAKE_TOOLCHAIN_FILE=./toolchain.cmake ${IVI_EXTENSION_DIR} || exit 1

    # Build
    make -j`grep processor /proc/cpuinfo | wc -l` || exit 1

    echo "Exiting directory ${PWD}..."
    popd &> /dev/null
}

function clean_ivi_extension
{
    [ -d ${IVI_EXTENSION_BUILD_DIR} ] && {
        echo "Entering directory ${IVI_EXTENSION_BUILD_DIR}..."
        pushd ${IVI_EXTENSION_BUILD_DIR} &> /dev/null

        # Clean
        [ -f Makefile ] && make clean

        echo "Exiting directory ${PWD}..."
        popd &> /dev/null
    }
}


#########################
###   Main
#########################

host_checks

set_build_env

if [ -z ${1} ]; then
    create_build_sysroot

    download_dependencies binaries
    install_dependencies

    # Build and install libinput as it is required by weston
    build_libinput
    install_libinput

    # Build and install the minimum weston components required by ivi-extension
    build_weston weston_only
    install_weston

    # Build ivi-extension and weston with all its clients
    build_ivi_extension
    build_weston all
    install_weston

    echo "Build and install succeedded. Done."
else
    case "${1}" in
        help)
            print_usage
        ;;
        download-dependencies)
            download_dependencies binaries
            download_dependencies sources
        ;;
        clean)
            clean_weston
            clean_libinput
            clean_ivi_extension
        ;;
        clean-all)
            clean_weston
            clean_libinput
            clean_ivi_extension
            remove_build_directories
            remove_build_sysroot
        ;;
        *)
            echo "Error: Unknown argument ${1}" > /dev/stderr
            print_usage
            exit 1
        ;;
    esac
fi
