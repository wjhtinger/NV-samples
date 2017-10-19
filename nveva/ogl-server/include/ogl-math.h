/*
 * Copyright (c) 2015, NVIDIA Corporation.  All Rights Reserved.
 *
 * BY INSTALLING THE SOFTWARE THE USER AGREES TO THE TERMS BELOW.
 *
 * User agrees to use the software under carefully controlled conditions
 * and to inform all employees and contractors who have access to the software
 * that the source code of the software is confidential and proprietary
 * information of NVIDIA and is licensed to user as such.  User acknowledges
 * and agrees that protection of the source code is essential and user shall
 * retain the source code in strict confidence.  User shall restrict access to
 * the source code of the software to those employees and contractors of user
 * who have agreed to be bound by a confidentiality obligation which
 * incorporates the protections and restrictions substantially set forth
 * herein, and who have a need to access the source code in order to carry out
 * the business purpose between NVIDIA and user.  The software provided
 * herewith to user may only be used so long as the software is used solely
 * with NVIDIA products and no other third party products (hardware or
 * software).   The software must carry the NVIDIA copyright notice shown
 * above.  User must not disclose, copy, duplicate, reproduce, modify,
 * publicly display, create derivative works of the software other than as
 * expressly authorized herein.  User must not under any circumstances,
 * distribute or in any way disseminate the information contained in the
 * source code and/or the source code itself to third parties except as
 * expressly agreed to by NVIDIA.  In the event that user discovers any bugs
 * in the software, such bugs must be reported to NVIDIA and any fixes may be
 * inserted into the source code of the software by NVIDIA only.  User shall
 * not modify the source code of the software in any way.  User shall be fully
 * responsible for the conduct of all of its employees, contractors and
 * representatives who may in any way violate these restrictions.
 *
 * NO WARRANTY
 * THE ACCOMPANYING SOFTWARE (INCLUDING OBJECT AND SOURCE CODE) PROVIDED BY
 * NVIDIA TO USER IS PROVIDED "AS IS."  NVIDIA DISCLAIMS ALL WARRANTIES,
 * EXPRESS, IMPLIED OR STATUTORY, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.

 * LIMITATION OF LIABILITY
 * NVIDIA SHALL NOT BE LIABLE TO USER, USERS CUSTOMERS, OR ANY OTHER PERSON
 * OR ENTITY CLAIMING THROUGH OR UNDER USER FOR ANY LOSS OF PROFITS, INCOME,
 * SAVINGS, OR ANY OTHER CONSEQUENTIAL, INCIDENTAL, SPECIAL, PUNITIVE, DIRECT
 * OR INDIRECT DAMAGES (WHETHER IN AN ACTION IN CONTRACT, TORT OR BASED ON A
 * WARRANTY), EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGES.  THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF THE
 * ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.  IN NO EVENT SHALL NVIDIAS
 * AGGREGATE LIABILITY TO USER OR ANY OTHER PERSON OR ENTITY CLAIMING THROUGH
 * OR UNDER USER EXCEED THE AMOUNT OF MONEY ACTUALLY PAID BY USER TO NVIDIA
 * FOR THE SOFTWARE PROVIDED HEREWITH.
 */

#ifndef __OGL_MATH_H__
#define __OGL_MATH_H__

#include "ogl-types.h"
#include <math.h>

#define OGL_PI			3.14159265358979323846		/* pi */
#define OGL_PI_2		1.57079632679489661923		/* pi/2 */
#define OGL_PI_4		0.78539816339744830962		/* pi/4 */
#define OGL_1_PI		0.31830988618379067154		/* 1/pi */
#define OGL_2_PI		0.63661977236758134308		/* 2/pi */
#define OGL_2_SQRTPI		1.12837916709551257390		/* 2/sqrt(pi) */
#define OGL_SQRT2		1.41421356237309504880		/* sqrt(2) */
#define OGL_1_SQRT2		0.70710678118654752440		/* 1/sqrt(2) */
#define OGL_RAD2DEG		57.29577951308232286464772	/* 1 rad */
#define OGL_DEG2RAD		0.01745329251994329547437168	/* 1 deg */

#define OGL_DEG(r)		((r)*OGL_RAD2DEG)
#define OGL_RAD(r)		((r)*OGL_DEG2RAD)

#define OGL_FLOAT_EPS	1.0e-30f
#define OGL_FLOAT_MAX	1.0e+37f

#define OGL_MIN(a,b)	(((a) < (b)) ? (a) : (b))
#define OGL_MAX(a,b)	(((a) < (b)) ? (b) : (a))

/* Round up to the next power of two */
static inline GLuint oglRoundPower2(GLuint x)
{
	x--;
	x |= x >> 1;  // handle  2 bit numbers
	x |= x >> 2;  // handle  4 bit numbers
	x |= x >> 4;  // handle  8 bit numbers
	x |= x >> 8;  // handle 16 bit numbers
	x |= x >> 16; // handle 32 bit numbers
	x++;
	return x;
}

/* Initialize identity matrix */
static inline void oglIdentityMat4(mat4_t *out)
{
	out->f11 = 1.0f;
	out->f21 = 0.0f;
	out->f31 = 0.0f;
	out->f41 = 0.0f;
	out->f12 = 0.0f;
	out->f22 = 1.0f;
	out->f32 = 0.0f;
	out->f42 = 0.0f;
	out->f13 = 0.0f;
	out->f23 = 0.0f;
	out->f33 = 1.0f;
	out->f43 = 0.0f;
	out->f14 = 0.0f;
	out->f24 = 0.0f;
	out->f34 = 0.0f;
	out->f44 = 1.0f;
}

/* Transpose a 4x4 matrix */
static inline void oglTransposeMat4(mat4_t *inout)
{
	GLfloat tmp[4];

	tmp[0] = inout->f21;
	inout->f21 = inout->f12;
	tmp[1] = inout->f31;
	inout->f31 = inout->f13;
	tmp[2] = inout->f41;
	inout->f41 = inout->f14;

	inout->f12 = tmp[0];
	tmp[0] = inout->f32;
	inout->f32 = inout->f23;
	tmp[3] = inout->f42;
	inout->f42 = inout->f24;

	inout->f13 = tmp[1];
	inout->f23 = tmp[0];
	tmp[1] = inout->f43;
	inout->f43 = inout->f34;

	inout->f14 = tmp[2];
	inout->f24 = tmp[3];
	inout->f34 = tmp[1];
}

/* Matrix multiplication */
static inline void oglMultiplyMat4(mat4_t* out, const mat4_t* m1, const mat4_t* m2)
{
	out->f11 = m1->f11*m2->f11+m1->f12*m2->f21+m1->f13*m2->f31+m1->f14*m2->f41;
	out->f21 = m1->f21*m2->f11+m1->f22*m2->f21+m1->f23*m2->f31+m1->f24*m2->f41;
	out->f31 = m1->f31*m2->f11+m1->f32*m2->f21+m1->f33*m2->f31+m1->f34*m2->f41;
	out->f41 = m1->f41*m2->f11+m1->f42*m2->f21+m1->f43*m2->f31+m1->f44*m2->f41;

	out->f12 = m1->f11*m2->f12+m1->f12*m2->f22+m1->f13*m2->f32+m1->f14*m2->f42;
	out->f22 = m1->f21*m2->f12+m1->f22*m2->f22+m1->f23*m2->f32+m1->f24*m2->f42;
	out->f32 = m1->f31*m2->f12+m1->f32*m2->f22+m1->f33*m2->f32+m1->f34*m2->f42;
	out->f42 = m1->f41*m2->f12+m1->f42*m2->f22+m1->f43*m2->f32+m1->f44*m2->f42;

	out->f13 = m1->f11*m2->f13+m1->f12*m2->f23+m1->f13*m2->f33+m1->f14*m2->f43;
	out->f23 = m1->f21*m2->f13+m1->f22*m2->f23+m1->f23*m2->f33+m1->f24*m2->f43;
	out->f33 = m1->f31*m2->f13+m1->f32*m2->f23+m1->f33*m2->f33+m1->f34*m2->f43;
	out->f43 = m1->f41*m2->f13+m1->f42*m2->f23+m1->f43*m2->f33+m1->f44*m2->f43;

	out->f14 = m1->f11*m2->f14+m1->f12*m2->f24+m1->f13*m2->f34+m1->f14*m2->f44;
	out->f24 = m1->f21*m2->f14+m1->f22*m2->f24+m1->f23*m2->f34+m1->f24*m2->f44;
	out->f34 = m1->f31*m2->f14+m1->f32*m2->f24+m1->f33*m2->f34+m1->f34*m2->f44;
	out->f44 = m1->f41*m2->f14+m1->f42*m2->f24+m1->f43*m2->f34+m1->f44*m2->f44;
}

/* Rotation transformation */
/* vector v should be normalized */
static inline void oglRotationMat4(mat4_t *out, GLfloat a, const vec3_t* v)
{
	GLfloat s, c, c1, xs, ys, zs, tmp;

	s = sinf(a);
	c = cosf(a);
	c1 = 1.0f - c;
	xs = v->x*s;
	ys = v->y*s;
	zs = v->z*s;

	tmp = v->x*c1;
	out->f11 = v->x*tmp + c;
	out->f21 = v->y*tmp + zs;
	out->f31 = v->z*tmp - ys;
	out->f41 = 0.0f;

	tmp = v->y*c1;
	out->f12 = v->x*tmp - zs;
	out->f22 = v->y*tmp + c;
	out->f32 = v->z*tmp + xs;
	out->f42 = 0.0f;

	tmp = v->z*c1;
	out->f13 = v->x*tmp + ys;
	out->f23 = v->y*tmp - xs;
	out->f33 = v->z*tmp + c;
	out->f43 = 0.0f;

	out->f14 = 0.0f;
	out->f24 = 0.0f;
	out->f34 = 0.0f;
	out->f44 = 1.0f;
}

/* Translation transformation */
static inline void oglTranslationMat4(mat4_t* out, vec3_t *in)
{
	out->f11 = 1.0f;
	out->f21 = 0.0f;
	out->f31 = 0.0f;
	out->f41 = 0.0f;
	out->f12 = 0.0f;
	out->f22 = 1.0f;
	out->f32 = 0.0f;
	out->f42 = 0.0f;
	out->f13 = 0.0f;
	out->f23 = 0.0f;
	out->f33 = 1.0f;
	out->f43 = 0.0f;
	out->f14 = in->x;
	out->f24 = in->y;
	out->f34 = in->z;
	out->f44 = 1.0f;
}


/* Scale transformation */
static inline void oglScaleMat4(mat4_t* out, vec3_t *in)
{
	out->f11 = in->x;
	out->f21 = 0.0f;
	out->f31 = 0.0f;
	out->f41 = 0.0f;
	out->f12 = 0.0f;
	out->f22 = in->y;
	out->f32 = 0.0f;
	out->f42 = 0.0f;
	out->f13 = 0.0f;
	out->f23 = 0.0f;
	out->f33 = in->z;
	out->f43 = 0.0f;
	out->f14 = 0.0f;
	out->f24 = 0.0f;
	out->f34 = 0.0f;
	out->f44 = 1.0f;
}

/* Perspective projection transformation */

static inline void oglPerspectiveMat4(mat4_t *out, vec4_t *in)
{
	out->f11 = in->f1;
	out->f21 = 0.0f;
	out->f31 = 0.0f;
	out->f41 = 0.0f;

	out->f12 = 0.0f;
	out->f22 = in->f2;
	out->f32 = 0.0f;
	out->f42 = 0.0f;

	out->f13 = 0.0f;
	out->f23 = 0.0f;
	out->f33 = in->f3;
	out->f43 = -1.0f;

	out->f14 = 0.0f;
	out->f24 = 0.0f;
	out->f34 = in->f4;
	out->f44 = 0.0f;
}

/* a special value of far <= near
 * indicates a far clipping plane at infinity
 * 0 < fov < 180
 * aspect > 0
 * near > 0
 * far > 0
 */
static inline void oglPerspectiveFOVMat4(mat4_t *out,
					 GLfloat fovy, GLfloat aspect,
					 GLfloat near, GLfloat far)
{
	GLfloat sy, cy, tmp;
	vec4_t v;

	fovy *= 0.5f;
	sy = sinf(fovy);
	cy = cosf(fovy);

	tmp = sy * aspect;
	v.f1 = cy / tmp;
	v.f2 = cy / sy;

	if (near < far) {
		tmp = 1.0f / (near - far);
		v.f3 = (far + near) * tmp;
		v.f4 = 2.0f * far * near * tmp;
	} else {
		/* far at infinity */
		v.f3 = -1.0f;
		v.f4 = -2.0f * near;
	}

	oglPerspectiveMat4(out, &v);
}

#endif	/* __OGL_MATH_H__ */
