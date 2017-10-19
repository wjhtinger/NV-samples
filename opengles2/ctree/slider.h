/*
 * slider.h
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
// Control sliders
//

#ifndef __SLIDER_H
#define __SLIDER_H

#include <GLES2/gl2.h>
#include "vector.h"

// Slider description
typedef struct {
    GLboolean visible;

    GLboolean focus;
    float     left, right, bottom, top;
    float     minimum, maximum, value;
    float     knobPos;
    GLboolean mouseDown;

    float3    rodColor;
    float2    rodVertices[12];
    float3    rodColors[12];

    float3    knobColor;

    float3    knobColors[5];
    float2    knobVertices[8];
} Slider;

// Initialization and clean-up
Slider *Slider_new(float minimum, float maximum, float init);
void Slider_delete(Slider *o);
void Slider_setCB (Slider *o, void (*CB)(float));
void Slider_setPos(Slider *o,
                   float left, float right, float bottom, float top);

// Control
GLboolean Slider_setValue(Slider *o, float val);
float Slider_getValue(Slider *o);
void Slider_select(Slider *o, GLboolean sel);

// Rendering
void Slider_draw(Slider *o);

#endif // __SLIDER_H
