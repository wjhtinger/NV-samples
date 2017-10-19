#!/bin/sh

###########################################################################
# Copyright (c) 2011-2014, NVIDIA CORPORATION. All rights reserved.
###########################################################################

echo "Partitioning mNAND for lifetime test, step 2 ..."

MNANDDEV=mmcblk0

if [ ! "$1" = "" ]; then
    YEARS=$1
else
    YEARS=15
fi

if [ ${YEARS} -lt 1 ] || [ ${YEARS} -gt 15 ]; then
    echo "usage: $0 [1-15 years]"
    exit 1
fi

if [ -f ${PWD}/mnand_lifetime_example.txt ]; then
    USE_FILE=${PWD}/mnand_lifetime_example.txt
else
    if [ -f /proc/boot/mnand_lifetime_example.txt ]; then
        USE_FILE=/proc/boot/mnand_lifetime_example.txt
    else
        echo "cannot locate mnand_lifetime_example.txt, aborting .."
        exit 1
    fi
fi

export LD_LIBRARY_PATH=.

umount /mnt/app
umount /mnt/maps
umount /mnt/media
umount /mnt/misc
umount /mnt/ota

echo "Formatting file system on each partition .."
mkfs.ext4 -q /dev/${MNANDDEV}p1
mkfs.ext4 -q /dev/${MNANDDEV}p2
mkfs.ext4 -q /dev/${MNANDDEV}p3
mkfs.ext4 -q /dev/${MNANDDEV}p4
mkfs.ext4 -q /dev/${MNANDDEV}p5

echo "Create mount point"
mkdir -p /mnt/app
mkdir -p /mnt/maps
mkdir -p /mnt/media
mkdir -p /mnt/misc
mkdir -p /mnt/ota

echo "Mounting each partition .."
mount -text4 /dev/${MNANDDEV}p1 /mnt/app
mount -text4 /dev/${MNANDDEV}p1 /mnt/maps
mount -text4 /dev/${MNANDDEV}p2 /mnt/media
mount -text4 /dev/${MNANDDEV}p3 /mnt/misc
mount -text4 /dev/${MNANDDEV}p4 /mnt/ota

echo "Starting lifetime test .."

mnand_lifetime_test -d /dev/${MNANDDEV} -f ${USE_FILE} -v 3 -t ${YEARS}

exit 0
