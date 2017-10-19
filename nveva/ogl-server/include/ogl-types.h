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

#ifndef __OGL_TYPES_H__
#define __OGL_TYPES_H__

#include "ogl.h"

typedef union {
	struct {
		GLfloat f11;
		GLfloat f21;
		GLfloat f31;
		GLfloat f41;
		GLfloat f12;
		GLfloat f22;
		GLfloat f32;
		GLfloat f42;
		GLfloat f13;
		GLfloat f23;
		GLfloat f33;
		GLfloat f43;
		GLfloat f14;
		GLfloat f24;
		GLfloat f34;
		GLfloat f44;
	};
	GLfloat ptr[16];
} mat4_t;

typedef union {
	struct {
		GLfloat x;
		GLfloat y;
		GLfloat z;
		GLfloat w;
	};
	struct  {
		GLfloat r;
		GLfloat g;
		GLfloat b;
		GLfloat a;
	};
	struct  {
		GLfloat s;
		GLfloat t;
		GLfloat p;
		GLfloat q;
	};
	struct  {
		GLfloat f1;
		GLfloat f2;
		GLfloat f3;
		GLfloat f4;
	};
	GLfloat ptr[4];
} vec4_t;

typedef vec4_t tex4_t;
typedef vec4_t col4_t;
typedef vec4_t quat_t;

typedef union {
	struct  {
		GLfloat x;
		GLfloat y;
		GLfloat z;
	};
	struct  {
		GLfloat r;
		GLfloat g;
		GLfloat b;
	};
	struct  {
		GLfloat s;
		GLfloat t;
		GLfloat p;
	};
	struct  {
		GLfloat f11;
		GLfloat f22;
		GLfloat f33;
	};
	struct  {
		GLfloat f14;
		GLfloat f24;
		GLfloat f34;
	};
	struct  {
		GLfloat f1;
		GLfloat f2;
		GLfloat f3;
	};
	GLfloat ptr[3];
} vec3_t;

typedef vec3_t tex3_t;
typedef vec3_t col3_t;

typedef union {
	struct  {
		GLfloat width;
		GLfloat height;
	};
	struct  {
		GLfloat x;
		GLfloat y;
	};
	struct  {
		GLfloat s;
		GLfloat t;
	};
	struct  {
		GLfloat f1;
		GLfloat f2;
	};
	GLfloat ptr[2];
} vec2_t;

typedef vec2_t tex2_t;

typedef union {
	struct {
		GLint x;
		GLint y;
		GLint z;
		GLint w;
	};
	struct {
		GLint r;
		GLint g;
		GLint b;
		GLint a;
	};
	struct {
		GLint s;
		GLint t;
		GLint p;
		GLint q;
	};
	struct {
		GLint i1;
		GLint i2;
		GLint i3;
		GLint i4;
	};
	GLuint ptr[4];
} ivec4_t;

typedef ivec4_t itex4_t;
typedef ivec4_t icol4_t;

typedef union {
	struct {
		GLint x;
		GLint y;
		GLint z;
	};
	struct {
		GLint r;
		GLint g;
		GLint b;
	};
	struct {
		GLint s;
		GLint t;
		GLint p;
	};
	struct {
		GLint i1;
		GLint i2;
		GLint i3;
	};
	GLuint ptr[3];
} ivec3_t;

typedef ivec3_t itex3_t;
typedef ivec3_t icol3_t;

typedef union {
	struct  {
		GLint width;
		GLint height;
	};
	struct {
		GLint x;
		GLint y;
	};
	struct {
		GLint s;
		GLint t;
	};
	struct {
		GLint i1;
		GLint i2;
	};
	GLuint ptr[2];
} ivec2_t;

typedef ivec2_t itex2_t;

typedef union {
	struct {
		GLuint x;
		GLuint y;
		GLuint z;
		GLuint w;
	};
	struct {
		GLuint r;
		GLuint g;
		GLuint b;
		GLuint a;
	};
	struct {
		GLuint s;
		GLuint t;
		GLuint p;
		GLuint q;
	};
	struct {
		GLuint u1;
		GLuint u2;
		GLuint u3;
		GLuint u4;
	};
	GLuint ptr[4];
} uvec4_t;

typedef uvec4_t utex4_t;
typedef uvec4_t ucol4_t;

typedef union {
	struct {
		GLuint x;
		GLuint y;
		GLuint z;
	};
	struct {
		GLuint r;
		GLuint g;
		GLuint b;
	};
	struct {
		GLuint s;
		GLuint t;
		GLuint p;
	};
	struct {
		GLuint u1;
		GLuint u2;
		GLuint u3;
	};
	GLuint ptr[3];
} uvec3_t;

typedef uvec3_t utex3_t;
typedef uvec3_t ucol3_t;

typedef union {
	struct  {
		GLuint width;
		GLuint height;
	};
	struct {
		GLuint x;
		GLuint y;
	};
	struct {
		GLuint s;
		GLuint t;
	};
	struct {
		GLuint u1;
		GLuint u2;
	};
	GLuint ptr[2];
} uvec2_t;

typedef uvec2_t utex2_t;

typedef union {
	struct {
		GLubyte r;
		GLubyte g;
		GLubyte b;
	};
	GLubyte ptr[3];
} rgb_t;

typedef union {
	struct {
		GLubyte r;
		GLubyte g;
		GLubyte b;
		GLubyte a;
	};
	GLubyte ptr[4];
} rgba_t;

typedef union {
	struct {
		vec2_t min;
		vec2_t max;
	};
	GLfloat ptr[4];
} rect_t;

typedef union {
	struct {
		vec3_t min;
		vec3_t max;
	};
	GLfloat ptr[6];
} box_t;

#endif	/* __OGL_TYPES_H__ */
