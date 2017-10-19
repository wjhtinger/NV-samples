/*
 * array.h
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
// Generic array abstraction
//

#ifndef __ARRAY_H
#define __ARRAY_H

// Data structure
typedef struct {
    int  elemCount;
    int  elemSize;
    int  buffSize;
    void *buffer;
} Array;

// Initialization and clean-up
//   (New/delete malloc and free the Array struct, where init/destroy don't.)
Array* Array_new(int elemsize);
void Array_delete(Array *o);
void Array_init(Array *o, int elemsize);
void Array_destroy(Array *o);
void Array_clear(Array *o);

// Access functions
//   (We can random read the array, but add or delete only the last item.)
void Array_push(Array *o, void *elem);
void *Array_get(Array *o, int i);
void Array_pop(Array *o);

#endif // ARRAY_H
