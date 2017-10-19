/*
 * egloutput_consumer.h
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   egloutput consumer header file
//

#ifndef _EGLOUTPUT_CONSUMER_H_
#define _EGLOUTPUT_CONSUMER_H_

#include "eglstrm_setup.h"
#include "egl_utils.h"
#include <cmdline.h>

NvMediaStatus egloutputConsumer_init(EGLDisplay display, EGLStreamKHR eglStream);


#endif
