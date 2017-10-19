/*
 * buildtree.h
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
// Tree branch generation
//

#ifndef __BUILDTREE_H
#define __BUILDTREE_H

// Absolute maximum depth of the tree
#define BRANCH_DEPTH 31

// (Re)generate a tree.
void BuildTree_generate(void);
void BuildTree_newCharacter(void);

#endif // __BUILDTREE_H
