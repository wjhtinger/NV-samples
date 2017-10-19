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

#include "ogl-shader.h"
#include "ogl-debug.h"

GLint oglCompileShader(GLuint shaderID, char** log)
{
	GLint retval = GL_FALSE;
	GLint len = 0;

	glCompileShader(shaderID);

	/* Check shader */
	glGetShaderiv(shaderID, GL_COMPILE_STATUS, &retval);
	if (log) {
		*log = NULL;
		glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &len);
		if (len) {
			*log = (char *)malloc(len);
			if (*log) {
				**log = 0;
				glGetShaderInfoLog(shaderID, len, NULL, *log);
			}
		}
	}
	return retval;
}

GLint oglLinkProgram(GLuint programID, GLuint *shaderID, int num, char** log)
{
	GLint retval = GL_FALSE;
	GLint len = 0;
	int i;

	for (i = 0; i < num; i++)
		glAttachShader(programID, shaderID[i]);

	glLinkProgram(programID);

	/* Check the program */
	glGetProgramiv(programID, GL_LINK_STATUS, &retval);
	if (log) {
		*log = NULL;
		glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &len);
		if (len) {
			*log = (char *)malloc(len);
			if (*log) {
				**log = 0;
				glGetProgramInfoLog(programID, len, NULL, *log);
			}
		}
	}

	return retval;
}

GLuint oglCreateProgram(const char *vss, const char *fss,
			const ogl_location_t *attr, ogl_location_t *unif)
{
	GLuint pr = 0;
	GLuint sh[2];
	int vsslen, fsslen;
	GLint retval;
	char *log;

	if (!vss || !fss) {
		ogl_debug("condition failed");
		return 0;
	}

	vsslen = strlen(vss);
	if (!vsslen) {
		ogl_debug("condition failed");
		return 0;
	}

	fsslen = strlen(fss);
	if (!fsslen) {
		ogl_debug("condition failed");
		return 0;
	}

	sh[0] = glCreateShader(GL_VERTEX_SHADER);
	if (!sh[0]) {
		ogl_debug("condition failed");
		return 0;
	}

	glShaderSource(sh[0], 1, &vss, &vsslen);
	retval = oglCompileShader(sh[0], &log);
	if (log)
		ogl_debug("vertex shader log:\n%s", log);

	if (!retval) {
		ogl_debug("condition failed");
		goto out_vs;
	}

	sh[1] = glCreateShader(GL_FRAGMENT_SHADER);
	if (!sh[1]) {
		ogl_debug("condition failed");
		goto out_vs;
	}

	glShaderSource(sh[1], 1, &fss, &fsslen);
	retval = oglCompileShader(sh[1], &log);
	if (log)
		ogl_debug("fragment shader log:\n%s", log);

	if (!retval) {
		ogl_debug("condition failed");
		goto out_fs;
	}

	pr = glCreateProgram();
	if (!pr) {
		ogl_debug("condition failed");
		goto out_fs;
	}

	if (attr) {
		while (attr->name) {
			glBindAttribLocation(pr, attr->location, attr->name);
			retval = glGetError();
			if (retval) {
				ogl_error("condition failed");
				goto out_pr;
			}
			attr++;
		}
	}

	retval = oglLinkProgram(pr, sh, 2, &log);
	if (log)
		ogl_debug("program link:\n%s", log);

	if (!retval) {
		ogl_debug("condition failed");
		goto out_pr;
	}

	if (unif) {
		while (unif->name) {
			unif->location = glGetUniformLocation(pr, unif->name);
			unif++;
		}
	}

	goto out_fs;

out_pr:
	glDeleteProgram(pr);
	pr = 0;
out_fs:
	glDeleteShader(sh[1]);
out_vs:
	glDeleteShader(sh[0]);
	return pr;
}

