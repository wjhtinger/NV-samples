/*
 * array.c
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

#include "nvgldemo.h"
#include "array.h"

////////////////////////////////////////////////////////////
// maintains an array of element (of same type) in a contiguous buffer.


// initial size of the array.
// We pick one that is large enough for the initial conditions and some
// growth.
static const int initBuffSize = 20000;

// how much the array buffer grows every time it exceeds the limit.
static const int buffSizeGrowth = 2;


// The constructor and destructor.
// elemsize is the byte size of the element to be stored in this array,
// i.e. sizeof(element type).
Array*
Array_new(
    int elemsize)
{
    Array *o = (Array*)MALLOC(sizeof(Array));
    Array_init(o, elemsize);
    return o;
}

void
Array_delete(
    Array *o)
{
    Array_destroy(o);
    FREE(o);
}

// Besides constructor and destructor, this object has two more,
//   which simply do not allocate and deallocate the object itself.
void
Array_init(
    Array *o,
    int   elemsize)
{
    o->elemSize = elemsize;
    o->elemCount = 0;
    o->buffer = NULL;
    o->buffSize = 0;
}

void
Array_destroy(
    Array *o)
{
    if (o->buffer) { FREE(o->buffer); }
}


// Set the size to zero, but keep the buffer as is.
void
Array_clear(
    Array *o)
{
    o->elemCount = 0;
}


// Access to the element specified by the index.
void*
Array_get(
    Array *o,
    int   i)
{
    ASSERT(i >= 0);
    ASSERT(i < o->elemCount);

    return (char*)o->buffer + i * o->elemSize;
}


// Add an element at the end, extending the buffer space as needed.
void
Array_push(
    Array *o,
    void  *elem)
{
    // Make sure we have enough room in the buffer.
    if (o->buffSize < o->elemSize * (o->elemCount + 1))
    {
        // decide how much to allocate.
        if (o->buffSize == 0)
        {
            o->buffSize = o->elemSize * initBuffSize;
        }
        else
        {
            o->buffSize = o->buffSize * buffSizeGrowth;
        }

        o->buffer = REALLOC(o->buffer, o->buffSize);
        ASSERT(o->buffer);
    }

    // write the element at the end location.
    MEMCPY((char*)o->buffer + o->elemCount * o->elemSize, elem, o->elemSize);
    o->elemCount++;
}


// Remove the last element, but do not shrink the buffer.
void
Array_pop(
    Array *o)
{
    ASSERT(o->elemCount > 0);

    // simply ignore the last element.
    o->elemCount--;
}
