/*
 * gears_frag.glslf
 *
 * Copyright (c) 2007-2014, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

varying lowp vec3 col_var;

void main()
{
    gl_FragColor = vec4(col_var, 1.0);
}
