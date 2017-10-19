/*
 * vbo.h
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
// Optional functions for rendering with VBOs
//

#ifndef __VBO_H
#define __VBO_H

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

// Uncomment to enable tracking counts of vertices rendered
//#define DUMP_VERTS_PER_SEC
#ifdef DUMP_VERTS_PER_SEC
extern int   vertCount;
#define glDrawArrays(mode, start, count)                     \
                    vertCount += (count);                    \
                    glDrawArrays(mode, start, count)
#define glDrawElements(mode, count, type, indices)           \
                    vertCount += (count);                    \
                    glDrawElements(mode, count, type, indices)
#endif

// All objects share a single VBO
#define VBO_NAME 1

// Macro to align elements properly when packed into the VBO
#define VBO_ALIGNMENT 4
#define VBO_align(size) ((size + VBO_ALIGNMENT - 1) & ~(VBO_ALIGNMENT - 1))

// Flags indicating VBO is requested and properly initialized
extern int   vboInitialized;
extern int   useVBO;

// Initialization and clean-up
GLboolean    VBO_init  (void);
void         VBO_deinit(void);
unsigned long VBO_alloc (int size);
GLboolean    VBO_setup (int size);

#endif // __VBO_H
