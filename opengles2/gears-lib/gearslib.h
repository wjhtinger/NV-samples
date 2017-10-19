/*
 * gearslib.h
 *
 * Copyright (c) 2003-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef __GEARSLIB_H
#define __GEARSLIB_H

#include <GLES2/gl2.h>

int  gearsInit(int width, int height);
void gearsResize(int width, int height);
void gearsRender(GLfloat angle);
void gearsTerm(void);

#endif // __GEARSLIB_H
