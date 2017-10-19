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

#include <stdlib.h>
#include <string.h>

#include "ogl-image.h"
#include "ogl-image-png.h"
#include "ogl-image-tga.h"

void oglImageDelete(ogl_image_t* img)
{
	if (!img)
		return;

	img->size.width = 0;
	img->size.height = 0;
	img->scanline = 0;
	img->bytes = 0;
	free(img->data);
	img->data = NULL;
}

GLvoid *oglImageGenerate(ogl_image_t *img, GLuint format, GLuint type,
			 GLuint width, GLuint height)
{
	GLuint depth, comp, size;
	GLvoid *data;

	if (!img)
		return NULL;

	size = width * height;
	if (!size)
		return NULL;

	switch (format) {
	case GL_RGBA:
		comp = 4;
		break;
	case GL_RGB:
		comp = 3;
		break;
	case GL_LUMINANCE:
	case GL_ALPHA:
		comp = 1;
		break;
	default:
		return NULL;
	}

	switch (type) {
	case GL_UNSIGNED_BYTE:
		depth = comp * sizeof(GLubyte);
		break;

	case GL_FLOAT:
		depth = comp * sizeof(GLfloat);
		break;

	default:
		return NULL;
	}

	size *= depth;

	data = malloc(size);
	if (!data)
		return NULL;

	img->data = data;
	img->bytes = size;
	img->scanline = width * depth;
	img->size.width = width;
	img->size.height = height;
	img->type = type;
	img->format = format;
	return data;
}

GLvoid *oglImageFromColorMap(ogl_image_t *img, GLuint format, GLuint type,
			     GLuint width, GLuint height,
			     GLvoid *ind, GLenum itype,
			     GLvoid *cmap, GLuint colors)
{
	GLuint pixels, depth;
	GLvoid *data;

	data = oglImageGenerate(img, format, type, width, height);
	if (!data)
		return NULL;

	pixels = img->size.width * img->size.height;
	depth = img->bytes / pixels;

	switch (itype) {
	case GL_UNSIGNED_BYTE:
		while (pixels--) {
			GLubyte i = *(GLubyte *)ind;
			ind = (GLubyte *)ind + 1;
			if (i > colors)
				return GL_FALSE;
			memcpy(data, (GLubyte *)cmap + depth * i, depth);
			data = (GLubyte *)data + depth;
		}
		break;
	case GL_UNSIGNED_SHORT:
		while (pixels--) {
			GLushort i = *(GLushort *)ind;
			ind = (GLushort *)ind + 1;
			if (i > colors)
				return GL_FALSE;

			memcpy(data, (GLubyte *)cmap + depth * i, depth);
			data = (GLubyte *)data + depth;
		}
		break;
	case GL_UNSIGNED_INT:
		while (pixels--) {
			GLuint i = *(GLuint *)ind;
			ind = (GLuint *)ind + 1;
			if (i > colors)
				return GL_FALSE;

			memcpy(data, (GLubyte *)cmap + depth * i, depth);
			data = (GLubyte *)data + depth;
		}
		break;
	default:
		break;
	}
	return data;
}

GLvoid* oglImageFromFile(ogl_image_t *img, const char *path)
{
	GLvoid *retval;

	retval = oglImageFromPNG(img, path);
	if (retval)
		return retval;

	retval = oglImageFromTGA(img, path);
	if (retval)
		return retval;

	return NULL;
}
