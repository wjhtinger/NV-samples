/*
 * algebra.h
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
// Vector/matrix/quaternion operations used by the bubble calculations
//

#ifndef __ALGEBRA_H
#define __ALGEBRA_H

/* algebraic functions */

/* a quaternion */
typedef struct {
    float r, i, j, k;
} Quat;
/* a vector */
typedef float float3[3];
/* a matrix */
typedef float float4x4[4][4];


/* apply matrix to vector, result stored on this vector */
void vec_transform(float3 v, float4x4 m);
/* apply matrix to point, result stored on this point */
void pnt_transform(float3 v, float4x4 m);
/* inverses the matrix */
void mat_invert(float4x4 m);
/* inverses the left-upper 3x3 part of matrix */
void mat_invert_part(float4x4 m);
/* transposes the matrix */
void mat_transpose(float4x4 m);
/* an identity */
void quat_identity(Quat *a);
/* sets a quaternion */
void quat_setf3(Quat *a, float nr, float ni, float nj, float nk);
/* sets a quaternion */
void quat_setfv(Quat *a, float radians, const float3 axis);
/* multiplies quaternions, result on the first argument */
void quat_multiply(Quat *a, const Quat *b);
/* makes the matrix (used in transformations) from quaternion */
void quat_mat(float4x4 m, Quat *a);
/* a=b; */
void quat_prescribe(Quat *a, Quat *b);
/* set matrix to identity */
void mat_identity(float4x4 m);
/* muliplies matrices, the result on the first argument */
void mat_multiply(float4x4 m0, float4x4 m1);
/* simmulates glTranslatef */
void mat_translate(float4x4 m, float x, float y, float z);
/* simmulates glOrthof */
void mat_ortho(float4x4 m,
    float l, float r, float b, float t, float n, float f);
/* simmulates glFrustumf */
void mat_frustum(float4x4 m,
    float l, float r, float b, float t, float n, float f);
/* simmulates glScalef */
void mat_scale(float4x4 m, float x, float y, float z);
/* multiply vector by scalar */
void vec_scale(float3 v, const float s);
/* add vectors, the result on the first argument (a=a+b) */
void vec_add(float3 a, const float3 b);
/* substract vectors, the result on the first argument (a=a-b) */
void vec_subs(float3 a, const float3 b);
/* dot product of vectors */
float vec_dot(const float3 a, const float3 b);
/* a=b; */
void vec_prescribe(float3 a, const float3 b);

#endif //__ALGEBRA_H
