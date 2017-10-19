/*
 * tree.h
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
// Complete tree (branches + leaves) setup/rendering
//

#ifndef __TREE_H
#define __TREE_H

#include <GLES2/gl2.h>

// The parameters used to control the characteristics of the tree generated
typedef enum {
    TREE_PARAM_DEPTH,
    TREE_PARAM_BALANCE,
    TREE_PARAM_TWIST,
    TREE_PARAM_SPREAD,
    TREE_PARAM_LEAF_SIZE,
    TREE_PARAM_BRANCH_SIZE,
    TREE_PARAM_FULLNESS,
    NUM_TREE_PARAMS,
} enumTreeParams;
extern float treeParams[NUM_TREE_PARAMS];
extern float treeParamsMin[NUM_TREE_PARAMS];
extern float treeParamsMax[NUM_TREE_PARAMS];

// Initialization and clean-up
void Tree_initialize(GLuint bark, GLuint leaf, GLuint leafb);
void Tree_deinitialize(void);

// Control
void Tree_toggleVBO(void);
void Tree_setParam(int param, float val);

// Geometry setup
void Tree_newCharacter(void);
void Tree_build(void);

// Rendering
void Tree_draw(void);

#endif // __TREE_H
