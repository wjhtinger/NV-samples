#!/bin/sh

###########################################################################
# Copyright (c) 2011-2013, NVIDIA CORPORATION. All rights reserved.
###########################################################################

echo "Partitioning mNAND for lifetime test, step 1 ..."

MNANDDEV=mmcblk0

echo "Filling data onto mNAND, please wait (this can take a while) .."
#cp -V /dev/zero /dev/${MNANDDEV}

echo "Start Partitioning .."
parted -s /dev/${MNANDDEV} mklabel gpt
parted -s /dev/${MNANDDEV} mkpart app 17.4k 12GB
parted -s /dev/${MNANDDEV} mkpart maps  12GB 24GB
parted -s /dev/${MNANDDEV} mkpart media 24GB 36GB
parted -s /dev/${MNANDDEV} mkpart misc 36GB 48GB
parted -s /dev/${MNANDDEV} mkpart ota 48GB 62GB


echo "Done. Please reboot, and run step 2 ..."

exit 0
