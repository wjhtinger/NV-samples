# Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

MY_DIR=$(cd "$(dirname "$0")" && pwd)
WESTON_DIR=${MY_DIR}/..

# Check for required arguments to be specified
[ -z ${1} ] && echo "Error: Undefined target ABI (valid values: ARMv7-gnueabihf, aarch64)" && exit 1
[ -z ${2} ] && echo "Error: Undefined source repo folder" && exit 1
[ -z ${3} ] && echo "Error: Undefined destination repo folder" && exit 1

if [ "${1}" = "ARMv7-gnueabihf" ]; then
    ARCH=armhf
    BUILD_ARCH=armhf
elif [ "${1}" = "aarch64" ]; then
    ARCH=aarch64
    BUILD_ARCH=arm64
else
    echo "Error: Unknown target ABI ${1}" && exit 1
fi

# Fetch packages lists
PKG_DIR="${WESTON_DIR}/Vibrante-SDK-scripts"
PKG_ALL="`cat ${PKG_DIR}/packages-all.list | grep binary | cut -d' ' -f1`"
PKG_ARM="`cat ${PKG_DIR}/packages-arm-common.list | grep binary | cut -d' ' -f1`"

[ -f ${PKG_DIR}/packages-${ARCH}.list ] && {
    PKG_ARM="${PKG_ARM} `cat ${PKG_DIR}/packages-${ARCH}.list | grep binary | cut -d' ' -f1`"
}

# Copy required packages
mkdir -p ${3}
for p in ${PKG_ALL}; do
    pkg_file="${p##*/}.deb"
    cp ${2}/${pkg_file} ${3}/${pkg_file} || {
        echo "Error: No such file ${2}/${pkg_file}" > /dev/stderr
        exit 1
    }
done
for p in ${PKG_ARM}; do
    pkg_file_1="${p##*/}_${BUILD_ARCH}.deb"
    pkg_file_2="${p##*/}_${ARCH}.deb"
    cp ${2}/${pkg_file_1} ${3}/${pkg_file_1} ||
    cp ${2}/${pkg_file_2} ${3}/${pkg_file_2} || {
        echo "Error: No such file ${2}/${pkg_file_1} nor ${2}/${pkg_file_2}" > /dev/stderr
        exit 1
    }
done

exit 0
