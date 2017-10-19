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

#include <string.h>

#include "ogl-texture.h"
#include "ogl-math.h"
#include "ogl-debug.h"

void oglTextureDelete(ogl_texture_t *texture)
{
	texture->type = 0;
	texture->max.x = 0.0f;
	texture->max.y = 0.0f;

	glDeleteTextures(1, &texture->id);
	texture->id = 0;
}

GLint oglTextureGenerate(ogl_texture_t *texture)
{
	if (!texture || texture->id)
		return GL_FALSE;

	glGenTextures(1, &texture->id);
	if (!texture->id)
		return GL_FALSE;

	texture->type = 0;
	texture->max.x = 0.0f;
	texture->max.y = 0.0f;
	return GL_TRUE;
}

static void oglTextureInit(ogl_texture_t *texture, GLenum type)
{
	texture->type = type;
	glBindTexture(type, texture->id);

	glTexParameteri(type, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(type, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glTexParameterf(type, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(type, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLint oglTextureFromStream(ogl_texture_t *texture,
			       EGLDisplay dpy, EGLStreamKHR str)
{
	GLint retval;

	if (!texture || !texture->id) {
		ogl_debug("condition failed");
		return GL_FALSE;
	}

	oglTextureInit(texture, GL_TEXTURE_EXTERNAL_OES);
	retval = peglStreamConsumerGLTextureExternalKHR(dpy, str);
	if (!retval) {
		ogl_debug("condition failed");
		return GL_FALSE;
	}

	texture->max.x = 1.0f;
	texture->max.y = 1.0f;
	return GL_TRUE;
}

GLint oglTextureFromImage(ogl_texture_t *texture, ogl_image_t *img)
{
	GLint retval;
	uvec2_t ts;

	if (!texture || !texture->id) {
		ogl_debug("condition failed");
		return GL_FALSE;
	}

	if (!img || !img->size.width || !img->size.height) {
		ogl_debug("condition failed");
		return GL_FALSE;
	}

	oglTextureInit(texture, GL_TEXTURE_2D);
	ts.x = oglRoundPower2(img->size.width);
	ts.y = oglRoundPower2(img->size.height);

	glTexImage2D(GL_TEXTURE_2D, 0, img->format, ts.x , ts.y, 0,
		     img->format, img->type, NULL);

	retval = glGetError();
	if (retval)
		ogl_warn("glTexImage2D: returned %i\n", retval);

	if (img->data)
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
				img->size.width, img->size.height,
				img->format, img->type, img->data);

	texture->max.x = (GLfloat) img->size.x / ts.x;
	texture->max.y = (GLfloat) img->size.y / ts.y;
	return GL_TRUE;
}

GLint oglTextureFromFile(ogl_texture_t *texture, const char *path)
{
	ogl_image_t img;
	GLint retval;

	memset(&img, 0, sizeof(img));
	oglImageFromFile(&img, path);

	if (!img.data)
		return GL_FALSE;

	retval = oglTextureFromImage(texture, &img);
	oglImageDelete(&img);

	return retval;
}
