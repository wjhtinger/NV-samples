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
#include <stdio.h>
#include <string.h>

#include "tga.h"

#include "ogl-debug.h"
#include "ogl-image-tga.h"

GLvoid *oglImageFromTGA(ogl_image_t *img, const char *path)
{
	TGA *tga;
	TGAData data;
	tbyte depth;
	GLenum format;

	if (!img || img->data || !path)
		return NULL;

	tga = TGAOpen(path);
	if (!tga)
		return NULL;

	memset(&data, 0, sizeof(data));
	data.flags = TGA_IMAGE_DATA | TGA_RGB;
	TGAReadImage(tga, &data);
	if (tga->last != TGA_OK)
		goto out;

	if (data.flags & TGA_COLOR_MAP)
		depth = tga->hdr.map_entry;
	else
		depth = tga->hdr.depth;

	switch (depth) {
	case 8:
		format = GL_LUMINANCE;
		break;
	case 32:
		format = GL_RGBA;
		break;
	case 24:
		format = GL_RGB;
		break;
	default:
		goto out;
	}

	if (data.flags & TGA_COLOR_MAP) {
		GLvoid *rows;
		GLenum type;

		depth = (tga->hdr.depth + 7) >> 3;
		switch (depth) {
		case sizeof(GLubyte):
			type = GL_UNSIGNED_BYTE;
			break;
		case sizeof(GLushort):
			type = GL_UNSIGNED_SHORT;
			break;
		case sizeof(GLuint):
			type = GL_UNSIGNED_INT;
			break;
		default:
			goto out;
		}
		rows = oglImageFromColorMap(img, format, GL_UNSIGNED_BYTE,
					    tga->hdr.width, tga->hdr.height,
					    data.img_data, type,
					    data.cmap, tga->hdr.map_len);
		if (!rows)
			goto out;
	} else {
		img->size.width = tga->hdr.width;
		img->size.height = tga->hdr.height;
		img->bytes = TGA_IMG_DATA_SIZE(tga);
		img->scanline = TGA_SCANLINE_SIZE(tga);
		img->type = GL_UNSIGNED_BYTE;
		img->format = format;
		img->data = data.img_data;
		data.img_data = NULL;
	}

out:
	free(data.img_data);
	free(data.cmap);
	TGAClose(tga);
	return img->data;
}

