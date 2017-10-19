/*
 * envcube.h
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
// Environment cube description and functions
//

#ifndef __ENVCUBE_H
#define __ENVCUBE_H

typedef struct {
    unsigned int cubeTexture;
    unsigned int vbo[2];
} EnvCube;

EnvCube*
EnvCube_build(void);

void
EnvCube_destroy(
    EnvCube* e);

unsigned int
EnvCube_getCube(
    EnvCube* e);

void
EnvCube_draw(
    EnvCube* e);

#endif //__ENVCUBE_H
