/*
 * tree.c
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
// Top level tree object
//

#include "nvgldemo.h"
#include "vbo.h"
#include "tree.h"
#include "branches.h"
#include "leaves.h"
#include "ground.h"
#include "buildtree.h"

// parameters to control the tree generation.
float treeParams[NUM_TREE_PARAMS] = {
    0.46f,
    0.7f,
    degToRadF(60.0f),
    0.75f,
    2.0f,
    0.1f,
    0.5f,
};

// minimum and maximum of the tree parameters.
float treeParamsMin[NUM_TREE_PARAMS] = {
    0.0f,
    0.0f,
    0.0f,
    0.0f,
    0.5f,
    0.01f,
    0.0f,
};

float treeParamsMax[NUM_TREE_PARAMS] = {
    1.0f,
    1.0f,
    degToRadF(180.0f),
    1.0f,
    5.0f,
    0.5f,
    1.0f,
};

// Variables specific to this module.
static GLboolean geometryDirty = GL_FALSE;
static GLboolean isVBO;

void
Tree_newCharacter()
{
    BuildTree_newCharacter();
    geometryDirty = GL_TRUE;
}

// Construct (or reconstruct) the tree elements.
static void
build(void)
{
    Branches_clear();
    Leaves_clear();

    BuildTree_generate();

    isVBO = useVBO;

    if (useVBO) {
        // initialize the VBO area.
        if (VBO_setup(Leaves_sizeVBO()+Branches_sizeVBO()+Ground_sizeVBO()))
        {
            Leaves_buildVBO();
            Branches_buildVBO();
            Ground_buildVBO();

            isVBO = GL_TRUE;
        } else {
            NvGlDemoLog("Unable to allocate VBO, falling back to non-VBO\n");
            isVBO = GL_FALSE;
        }
    } else {
        isVBO = GL_FALSE;
    }

    // Mark the dirty bit false.
    geometryDirty = GL_FALSE;
}

void
Tree_initialize(
    GLuint bark,
    GLuint leaf,
    GLuint leafb)
{
    VBO_init();

    // initialize the textures.
    Leaves_initialize(leaf, leafb, treeParams[TREE_PARAM_LEAF_SIZE]);
    Branches_initialize(bark);

    // this will cause the tree to be built at the first draw call.
    geometryDirty = GL_TRUE;
}

void
Tree_deinitialize(void)
{
    Leaves_deinitialize();
    Branches_deinitialize();
    if (useVBO) {
        VBO_deinit();
    }
    Tree_newCharacter();
}



void
Tree_draw(void)
{
    if (geometryDirty) {
        build();
    }
    Leaves_draw(isVBO);
    Branches_draw(isVBO);

    Ground_draw(isVBO);
}


void
Tree_setParam(
    int   param,
    float val)
{
    treeParams[param] = val;
    geometryDirty = GL_TRUE;
}

void
Tree_toggleVBO(void)
{
    if (vboInitialized) {
        useVBO = !useVBO;
        geometryDirty = GL_TRUE;
    }
}
