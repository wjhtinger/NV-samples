# Copyright (c) 2014-2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

# Check for WESTON_DIR is set when sourcing this file
[ -z ${WESTON_DIR} ] && echo "Error: WESTON_DIR is not set" && exit 1

# Package remote and local sources
PACKAGES_URLS="http://ports.ubuntu.com/pool/main \
               http://launchpadlibrarian.net"
PACKAGES_LOCAL_PATH=${WESTON_DIR}/apt-repos/binary


function download_package
{
    # ${1}: Package file in ${PACKAGES_URLS}

    local package_file="${1##*/}.deb"

    if [ ! -f ${package_file} ]; then
        if [ -f ${PACKAGES_LOCAL_PATH}/${package_file} ]; then
            echo -n "    Downloading ${package_file} (local)... "
            cp ${PACKAGES_LOCAL_PATH}/${package_file} .
            echo "Done"
            return 0
        else
            for u in ${PACKAGES_URLS}; do
                echo -n "    Downloading ${package_file} (remote ${u})... "
                wget ${u}/"${1}.deb" &> /dev/null && echo "Done" && return 0
                echo "Failed"
            done
        fi
    else
        echo "    Downloading ${package_file} (cached)... Done"
        return 0
    fi

    return 1
}

function download_sources
{
    # ${1}: Package file in ${PACKAGES_URLS}

    local package_file="${1##*/}"

    if [ ! -f ${package_file} ]; then
        for u in ${PACKAGES_URLS}; do
            echo -n "    Downloading ${package_file} (remote ${u})... "
            wget ${u}/${1} &> /dev/null && echo "Done" && return 0
            echo "Failed"
        done
    else
        echo "    Downloading ${package_file} (cached)... Done"
        return 0
    fi

    return 1
}

function download_dependencies
{
    # ${1}: Type of dependencies to download (binaries, sources).
    #       Defaults to 'binaries'

    local flavor=binary

    if [ ! -z ${1} ]; then
        if [ "${1}" = "binaries" ]; then
            flavor=binary
        elif [ "${1}" = "sources" ]; then
            flavor=sources
        else
            echo "Error: Unknown type of dependencies ${1}"
            exit 1
        fi
    fi

    local packages_dir="${WESTON_DIR}/Vibrante-SDK-scripts"
    local packages_all="`cat ${packages_dir}/packages-all.list | grep ${flavor} | cut -d' ' -f1`"
    local packages_arm="`cat ${packages_dir}/packages-arm-common.list | grep ${flavor} | cut -d' ' -f1`"

    [ -f ${packages_dir}/packages-${ARCH}.list ] && {
        packages_arm="${packages_arm} `cat ${packages_dir}/packages-${ARCH}.list | grep ${flavor} | cut -d' ' -f1`"
    }

    # Create ${NVIDIA_ROOT}/packages/${flavor} if needed
    [ ! -d ${NVIDIA_ROOT}/packages/${flavor} ] && {
        mkdir -p ${NVIDIA_ROOT}/packages/${flavor} || {
            echo "Error: Unable to create directory ${NVIDIA_ROOT}/packages/${flavor}" > /dev/stderr
            exit 1
        }
    }

    # Download missing packages
    echo "Entering directory ${NVIDIA_ROOT}/packages/${flavor}..."
    pushd ${NVIDIA_ROOT}/packages/${flavor} &> /dev/null

    echo "Downloading dependent packages..."
    if [ "${flavor}" = "binary" ]; then
        for p in ${packages_all}; do
            download_package "${p}" || {
                echo "Error: Unable to fetch package ${p}.deb" > /dev/stderr
                exit 1
            }
        done
        for p in ${packages_arm}; do
            download_package "${p}_${BUILD_ARCH}" ||
            download_package "${p}_${ARCH}" || {
                echo "Error: Unable to fetch package ${p}_${ARCH}.deb or" > /dev/stderr
                echo "       ${p}_${BUILD_ARCH}.deb" > /dev/stderr
                exit 1
            }
        done
    else
        for p in ${packages_all} ${packages_arm}; do
            download_sources "${p}" ||
            echo "Warning: Unable to fetch sources ${p}" > /dev/stderr
        done
    fi

    echo "Exiting directory ${PWD}..."
    popd &> /dev/null
}

function install_dependencies
{
    # Extract all packages into sysroot
    [ -d ${NVIDIA_ROOT}/packages/binary ] && {
        for p in ${NVIDIA_ROOT}/packages/binary/*.deb; do
            dpkg -x ${p} ${SYSROOT}
        done
    }

    # Fix broken symlinks
    local broken_links=`find ${SYSROOT} -type l -! -exec test -e {} \; -print`
    for l in ${broken_links}; do
        local link_dest=`readlink $l`
        # Symlinks specified with respect to root
        [ $(echo ${link_dest} | grep -c -e '^/') -ne 0 ] && {
            rm $l && ln -s ${SYSROOT}/${link_dest} $l
        }
    done

    # Check for all symlinks were fixed
    broken_links=`find ${SYSROOT} -type l -! -exec test -e {} \; -print`
    [ -n "${broken_links}" ] && {
        echo "Warning: Some symlinks are still broken, this may cause build" > /dev/stderr
        echo "         failures." > /dev/stderr
    }
}
