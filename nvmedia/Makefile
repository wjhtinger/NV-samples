# Copyright (c) 2013-2016, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

SUBDIRS := eglstream img_cap img_cc \
	img_enc img_usbcap ipp_raw ipp_yuv \
	misc_util vid_cap vid_enc \
	vid_play vid_render

.PHONY: all clean $(SUBDIRS)

all clean: $(SUBDIRS)

$(SUBDIRS):
	test ! -r $@/Makefile || make -C $@ ${MAKECMDGOALS}
