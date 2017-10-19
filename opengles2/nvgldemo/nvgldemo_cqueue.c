/*
 * nvgldemo_cqueue.c
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// This file contains the logic to implement the circular queue of indexes
//

#include "nvgldemo.h"

//rear points to the index of last element that's inserted
//front points to the index of the first element that can be deleted
static int front, rear, queue_size;

void NvGlDemoCqInitIndex(int size)
{
    front = -1;
    rear = -1;
    queue_size = size;
}

int NvGlDemoCqFull(void)
{
    if ((front == rear + 1) ||
       (front == 0 && rear == (queue_size - 1))) {
        return 1;
    }
    return 0;
}

int NvGlDemoCqEmpty(void)
{
    if (front == -1) {
       return 1;
    }
    return 0;
}

int NvGlDemoCqInsertIndex()
{
    if (NvGlDemoCqFull()) {
        return -1;
    } else {

        if (front == -1) {
            front = 0;
        }
        rear = (rear + 1) % queue_size;
        return rear;
   }
}

int NvGlDemoCqDeleteIndex()
{
    int index;

    if (NvGlDemoCqEmpty()) {
        return -1;
    } else {
        index = front;

        if (front == rear) {
            front = -1;
            rear = -1;
        } else {
            front = (front + 1) % queue_size;
        }
        return index;
    }
}
