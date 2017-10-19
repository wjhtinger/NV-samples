/*
 * screen.h
 *
 * Copyright (c) 2003-2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// Top level scene control/rendering
//

#ifndef __SCREEN_H
#define __SCREEN_H

#include <GLES2/gl2.h>

int  Screen_initialize(float d, GLsizei w, GLsizei h, GLboolean fpsFlag);
void Screen_resize(GLsizei w, GLsizei h);
void Screen_deinitialize(void);

void Screen_setDemoParams(void);
void Screen_setSmallTex(void);
void Screen_setNoSky(void);
void Screen_setNoMenu(void);

void Screen_draw(void);
void Screen_callback(int key, int x, int y);

GLboolean Screen_isFinished(void);

#endif // __SCREEN_H
