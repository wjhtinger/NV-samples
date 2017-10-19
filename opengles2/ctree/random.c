/*
 * random.c
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
// Pseudo-random number generation
//

#include "random.h"

static const double a = 16807.0;
static const double m = 2147483647.0;

static double seed = 1.0;

double
GetRandom(void)
{
    double t = a * seed;
    seed = t - m * (double)((int)(t / m));

    return seed / m;
}
