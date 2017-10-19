/*
 * firefly.h
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
// Firefly representation/animation
//

#ifndef __FIREFLY_H
#define __FIREFLY_H

#include "vector.h"

// Global arrays holding firefly data
extern float *fPos;
extern float *fWings;
extern float *fVel;
extern float *fColor;
extern float *fHsva;

// Firefly structure
//   Has pointers to above arrays which keeps transformation code simpler
//   (avoids allocating temp arrays)
typedef struct {
    float *pos;
    float *wings;
    float *vel;
    float *c;
    float range;
    float *hsva;
} Firefly;

// Initialization and clean-up
void Firefly_global_init(int num);
void Firefly_init(Firefly *o, int num);
void Firefly_destroy(Firefly *o);
void Firefly_global_destroy(void);

// Animation and rendering
void Firefly_move(Firefly *o);
void Firefly_draw(Firefly *o);

#endif // __FIREFLY_H
