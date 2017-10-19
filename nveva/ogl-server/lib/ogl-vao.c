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

#include "ogl-vao.h"

void oglVAODelete(ogl_vao_t *vao)
{
	vao->count = 0;
	vao->mode = 0;
	pglDeleteVertexArrays(1, &vao->array);
	vao->array = 0;
	glDeleteBuffers(2, vao->buffers);
	vao->buffers[0] = 0;
	vao->buffers[1] = 0;
}

GLint oglVAOGenerate(ogl_vao_t *vao)
{
	ogl_attr_t attr;

	if (!vao || vao->array || vao->buffers[0] || vao->buffers[1])
		return GL_FALSE;

	glGenBuffers(2, vao->buffers);
	if (!vao->buffers[0] || !vao->buffers[1])
		return GL_FALSE;

	pglGenVertexArrays(1, &vao->array);
	if (!vao->array) {
		oglVAODelete(vao);
		return GL_FALSE;
	}

	vao->mode = 0;
	vao->count = 0;

	pglBindVertexArray(vao->array);
	glBindBuffer(GL_ARRAY_BUFFER, vao->buffers[0]);
	for (attr = OGL_ATTR_VERTEX; attr < OGL_ATTRIBUTES; attr++) {
		glDisableVertexAttribArray(attr);
	}

	pglBindVertexArray(0);
	return GL_TRUE;
}

GLint oglVAOQuad(ogl_vao_t *vao, rect_t *vertex, rect_t *texture)
{
	vec2_t v[8] = {
		{ .x = -1.0f, .y = -1.0f } , { .x = 0.0f, .y = 0.0f },
		{ .x =  1.0f, .y = -1.0f } , { .x = 1.0f, .y = 0.0f },
		{ .x = -1.0f, .y =  1.0f } , { .x = 0.0f, .y = 1.0f },
		{ .x =  1.0f, .y =  1.0f } , { .x = 1.0f, .y = 1.0f },
	};
	GLushort i[4] = { 0, 1, 2, 3 };
	GLint offset = sizeof(GLfloat) * 2;
	GLint stride = offset * 2;

	if (!vao->array || !vao->buffers[0] || !vao->buffers[1])
		return GL_FALSE;

	if (vertex) {
		v[0] = vertex->min;
		v[2].x = vertex->max.x;
		v[2].y = vertex->min.y;
		v[4].x = vertex->min.x;
		v[4].y = vertex->max.y;
		v[6] = vertex->max;
	}

	if (texture) {
		v[1] = texture->min;
		v[3].x = texture->max.x;
		v[3].y = texture->min.y;
		v[5].x = texture->min.x;
		v[5].y = texture->max.y;
		v[7] = texture->max;
	}

	pglBindVertexArray(vao->array);
	glBindBuffer(GL_ARRAY_BUFFER, vao->buffers[0]);
	glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
	glVertexAttribPointer(OGL_ATTR_VERTEX, 2, GL_FLOAT,
			      GL_FALSE, stride, OGL_PTR(0));
	glEnableVertexAttribArray(OGL_ATTR_VERTEX);
	glVertexAttribPointer(OGL_ATTR_TEXTURE0, 2, GL_FLOAT,
			      GL_FALSE, stride, OGL_PTR(offset));
	glEnableVertexAttribArray(OGL_ATTR_TEXTURE0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vao->buffers[1]);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(i), i, GL_STATIC_DRAW);
	pglBindVertexArray(0);
	vao->mode = GL_TRIANGLE_STRIP;
	vao->count = 4;

	return GL_TRUE;
}
