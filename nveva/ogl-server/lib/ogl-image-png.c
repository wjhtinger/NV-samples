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

#ifdef HAVE_LIBPNG
#include <stdlib.h>
#include <stdio.h>

#include <png.h>

#include "ogl-image-png.h"

GLvoid *oglImageFromPNG(ogl_image_t *img, const char *path)
{
	FILE *fp;
        unsigned char header[8];
	int width, height;
	png_byte color_type;
	png_byte bit_depth;

	png_structp png_ptr;
	png_infop info_ptr;
//	int number_of_passes;
	png_bytep *row_pointers;
	GLuint format;
	png_bytep row;

	if (!img || img->data || !path)
		return NULL;

	fp = fopen(path, "rb");
	if (!fp)
		return NULL;

        fread(header, 1, 8, fp);
        if (png_sig_cmp(header, 0, 8))
		goto out_close;

        /* initialize stuff */
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png_ptr)
		goto out_close;

        info_ptr = png_create_info_struct(png_ptr);
        if (!info_ptr)
		png_destroy_read_struct(&png_ptr, NULL, NULL);

        if (setjmp(png_jmpbuf(png_ptr)))
		goto out_destroy;

        png_init_io(png_ptr, fp);
        png_set_sig_bytes(png_ptr, 8);

        png_read_info(png_ptr, info_ptr);

        width = png_get_image_width(png_ptr, info_ptr);
	if (!width)
		goto out_destroy;

        height = png_get_image_height(png_ptr, info_ptr);
	if (!height)
		goto out_destroy;

        color_type = png_get_color_type(png_ptr, info_ptr);
        bit_depth = png_get_bit_depth(png_ptr, info_ptr);
	if (bit_depth < 8 || color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_expand(png_ptr);
	if (bit_depth == 16)
	        png_set_strip_16(png_ptr);

	switch (color_type) {
	case PNG_COLOR_TYPE_PALETTE:
		png_set_palette_to_rgb(png_ptr);
		break;
	default:
		break;
	}

//	number_of_passes = png_set_interlace_handling(png_ptr);
        png_read_update_info(png_ptr, info_ptr);
        color_type = png_get_color_type(png_ptr, info_ptr);
	switch (color_type) {
	case PNG_COLOR_TYPE_RGB_ALPHA:
		format = GL_RGBA;
		break;
	case PNG_COLOR_TYPE_RGB:
		format = GL_RGB;
		break;
	case PNG_COLOR_TYPE_GRAY_ALPHA:
		format = GL_LUMINANCE_ALPHA;
		break;
	case PNG_COLOR_TYPE_GRAY:
		format = GL_LUMINANCE;
		break;
	default:
		goto out_destroy;
	}

        /* read file */
        if (setjmp(png_jmpbuf(png_ptr)))
		goto out_destroy;

        row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
	if (!row_pointers)
		goto out_destroy;

	row = (png_bytep)oglImageGenerate(img, format, GL_UNSIGNED_BYTE,
					  width, height);
	if (!row)
		goto out_free;

	while (height--) {
                row_pointers[height] = row;
		row += img->scanline;
	}

        png_read_image(png_ptr, row_pointers);

out_free:
	free(row_pointers);
out_destroy:
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
out_close:
	fclose(fp);
	return img->data;
}
#endif	/* HAVE_LIBPNG */
