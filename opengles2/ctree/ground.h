/*
 * ground.h
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
// Ground functions
//

#ifndef __GROUND_H
#define __GROUND_H

#include <GLES2/gl2.h>

// Size of ground mesh
#define GROUND_SIZE 10.0f

// Initialization and clean-up
void Ground_initialize(GLuint t);
void Ground_deinitialize(void);

// Rendering
int  Ground_polyCount(void);
int  Ground_sizeVBO(void);
void Ground_buildVBO(void);
void Ground_draw(int useVBO);

#endif // __GROUND_H
