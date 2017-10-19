/*
 * grutil_x11.h
 *
 * Copyright (c) 2013, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Exposes X11 display system objects which demonstrate X11-specific features.

#ifndef __GRUTIL_X11_H
#define __GRUTIL_X11_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

// Platform-specific state info
struct GrUtilPlatformState
{
    Display* XDisplay;
    int      XScreen;
    Window   XWindow;
};

#endif // __GRUTIL_X11_H
