/*
 * sky.h
 *
 * Copyright (c) 2003-2012, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// Sky functions
//

#ifndef __SKY_H
#define __SKY_H

#include <GLES2/gl2.h>

// Radius of sky texture
#define SKY_RADIUS (45.0f)

// Initialization and clean-up
void Sky_initialize(GLuint t);
void Sky_deinitialize(void);

// Rendering
void Sky_draw(void);

#endif // __SKY_H
