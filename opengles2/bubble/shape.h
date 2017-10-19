/*
 * shape.h
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
// Bubble shape description and functions
//

#ifndef __SHAPE_H
#define __SHAPE_H

#include "algebra.h"

#define MAX_DEPTH 64

// Bubble vertex description
typedef struct {
    float p[3];  // point
    float n[3];  // normal
    float v[3];  // velocity
    float h[3];  // home
    float a[3];  // neighborhood velocity for averaging
    int connectedness; // indicates how many triangles this vertex is
                       // connected to, as well as how many edges
} Vertex;

// Bubble edge description
typedef struct {
    short v0id;
    short v1id;
    float l; // length
} Edge;

// Triangle strip forming part of the bubble
typedef struct {
    int            numVerts;  // numTris == numVerts-2
    Vertex         **vertices;
    unsigned short *indices;
} Tristrip;

// Complete bubble description
typedef struct {
    int      numTristrips;
    Tristrip *tristrips;
    int      numEdges;
    Edge     *edges;
    int      numVerts;
    Vertex   *vertices;
    float    final_drag;
    float    initial_drag;
    float    drag;
} Shape;

// Function declarations

Shape*
Bubble_create(
    int depth);

void
Bubble_destroy(
    Shape *b);

void
Bubble_draw(
    Shape        *b,
    unsigned int cube_texture);

void
Bubble_calcNormals(
    Shape *b);

void
Bubble_pick(
    Shape        *b,
    const float3 e,
    const float3 n);

void
Bubble_calcVelocity(
    Shape *b);

void
Bubble_filterVelocity(
    Shape *b);

void
Bubble_drawVertices(
    Shape *b);

void
Bubble_drawEdges(
    Shape *b);

#endif //__SHAPE_H
