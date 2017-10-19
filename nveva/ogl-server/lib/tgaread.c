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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tga.h"
#include "ogl-image.h"

TGA*
TGAOpen(const char *file)
{
	TGA *tga;
	FILE *fd;

	tga = (TGA*)calloc(1, sizeof(TGA));
	if (!tga) {
		TGA_ERROR(tga, TGA_OOM);
		return NULL;
	}

	fd = fopen(file, "r");
	if (!fd) {
		TGA_ERROR(tga, TGA_OPEN_FAIL);
		free(tga);
		return NULL;
	}
	tga->off = 0;
	tga->fd = fd;
	tga->last = TGA_OK;
	return tga;
}


void
TGAClose(TGA *tga)
{
	if (tga) {
		fclose(tga->fd);
		free(tga);
	}
}


tlong
__TGASeek(TGA  *tga,
	  tlong off,
	  int   whence)
{
	fseek(tga->fd, off, whence);
	tga->off = ftell(tga->fd);
	return tga->off;
}

size_t
TGARead(TGA    *tga,
	tbyte  *buf,
	size_t	size,
	size_t	n)
{
	size_t read = fread(buf, size, n, tga->fd);
	tga->off = ftell(tga->fd);
	return read;
}

int
TGAReadImage(TGA     *tga,
	     TGAData *data)
{
	if (!tga) return 0;

	if (TGAReadHeader(tga) != TGA_OK) {
		TGA_ERROR(tga, tga->last);
		return 0;
	}

	if ((data->flags & TGA_IMAGE_ID) && tga->hdr.id_len != 0) {
		if (TGAReadImageId(tga, &data->img_id) != TGA_OK) {
			data->flags &= ~TGA_IMAGE_ID;
			TGA_ERROR(tga, tga->last);
		}
	} else {
		data->flags &= ~TGA_IMAGE_ID;
	}

	if (data->flags & TGA_IMAGE_DATA) {
		data->img_data = (tbyte*)malloc(TGA_IMG_DATA_SIZE(tga));
		if (!data->img_data) {
			data->flags &= ~TGA_IMAGE_DATA;
			TGA_ERROR(tga, TGA_OOM);
			return 0;
		}

		if (TGAReadScanlines(tga, data->img_data, 0, tga->hdr.height, data->flags) != tga->hdr.height) {
			data->flags &= ~TGA_IMAGE_DATA;
			TGA_ERROR(tga, tga->last);
			return 0;
		}

		if (TGA_IS_MAPPED(tga)) {
			if (!TGAReadColorMap(tga, &data->cmap, data->flags)) {
				data->flags &= ~TGA_COLOR_MAP;
				TGA_ERROR(tga, tga->last);
				return 0;
			}
			data->flags |= TGA_COLOR_MAP;
			tga->last = TGA_OK;
		} else if (data->flags & TGA_RGB) {
			tga->last = __TGAbgr2rgb(&data->img_data,
						 TGA_IMG_DATA_SIZE(tga),
						 &tga->hdr.depth, tga->hdr.alpha);
		}
	}
	return TGA_OK;
}

int
TGAReadHeader(TGA *tga)
{
	tbyte *tmp;

	if (!tga) return 0;

	__TGASeek(tga, 0, SEEK_SET);
	if (tga->off != 0) {
		TGA_ERROR(tga, TGA_SEEK_FAIL);
		return 0;
	}

	tmp = (tbyte*)malloc(TGA_HEADER_SIZE);
	if (!tmp) {
		TGA_ERROR(tga, TGA_OOM);
		return 0;
	}

	memset(tmp, 0, TGA_HEADER_SIZE);

	if (!TGARead(tga, tmp, TGA_HEADER_SIZE, 1)) {
		free(tmp);
		TGA_ERROR(tga, TGA_READ_FAIL);
		return 0;
	}

	tga->hdr.id_len		= tmp[0];
	tga->hdr.map_t		= tmp[1];
	tga->hdr.img_t		= tmp[2];
	tga->hdr.map_first	= tmp[3] + tmp[4] * 256;
	tga->hdr.map_len	= tmp[5] + tmp[6] * 256;
	tga->hdr.map_entry	= tmp[7];
	tga->hdr.x		= tmp[8] + tmp[9] * 256;
	tga->hdr.y		= tmp[10] + tmp[11] * 256;
	tga->hdr.width		= tmp[12] + tmp[13] * 256;
	tga->hdr.height		= tmp[14] + tmp[15] * 256;
	tga->hdr.depth		= tmp[16];
	tga->hdr.alpha		= tmp[17] & 0x0f;
	tga->hdr.horz		= (tmp[17] & 0x10) ? TGA_TOP : TGA_BOTTOM;
	tga->hdr.vert		= (tmp[17] & 0x20) ? TGA_RIGHT : TGA_LEFT;

	if (tga->hdr.map_t && tga->hdr.depth != 8) {
		TGA_ERROR(tga, TGA_UNKNOWN_SUB_FORMAT);
		free(tga);
		free(tmp);
		return 0;
	}

	if (tga->hdr.depth != 8 &&
	    tga->hdr.depth != 15 &&
	    tga->hdr.depth != 16 &&
	    tga->hdr.depth != 24 &&
	    tga->hdr.depth != 32)
	{
		TGA_ERROR(tga, TGA_UNKNOWN_SUB_FORMAT);
		free(tga);
		free(tmp);
		return 0;
	}

	free(tmp);
	tga->last = TGA_OK;
	return TGA_OK;
}

int
TGAReadImageId(TGA    *tga,
	       tbyte **buf)
{
	if (!tga || tga->hdr.id_len == 0) return 0;

	__TGASeek(tga, TGA_HEADER_SIZE, SEEK_SET);
	if (tga->off != TGA_HEADER_SIZE) {
		TGA_ERROR(tga, TGA_SEEK_FAIL);
		return 0;
	}
	*buf = (tbyte*)malloc(tga->hdr.id_len);
	if (!buf) {
		TGA_ERROR(tga, TGA_OOM);
		return 0;
	}

	if (!TGARead(tga, *buf, tga->hdr.id_len, 1)) {
		free(buf);
		TGA_ERROR(tga, TGA_READ_FAIL);
		return 0;
	}

	tga->last = TGA_OK;
	return TGA_OK;
}

int
TGAReadColorMap(TGA	  *tga,
		tbyte   **buf,
		tuint32   flags)
{
	tlong n, off, read;

	if (!tga) return 0;

	n = TGA_CMAP_SIZE(tga);
	if (n <= 0) return 0;

	off = TGA_CMAP_OFF(tga);
	if (tga->off != off) __TGASeek(tga, off, SEEK_SET);
	if (tga->off != off) {
		TGA_ERROR(tga, TGA_SEEK_FAIL);
		return 0;
	}

	*buf = (tbyte*)malloc(n);
	if (!buf) {
		TGA_ERROR(tga, TGA_OOM);
		return 0;
	}

	if ((read = TGARead(tga, *buf, n, 1)) != 1) {
		TGA_ERROR(tga, TGA_READ_FAIL);
		return 0;
	}

	tga->last = flags & TGA_RGB ?
		__TGAbgr2rgb(buf, n, &tga->hdr.map_entry, tga->hdr.alpha) :
		TGA_OK;
	return read;
}

int
TGAReadRLE(TGA   *tga,
	   tbyte *buf)
{
	int head;
	char sample[4];
	tbyte k, repeat = 0, direct = 0, bytes = TGA_BYTE_DEPTH(tga->hdr.depth);
	tshort x;
	tshort width = tga->hdr.width;
	FILE *fd = tga->fd;

	if (!tga || !buf) return TGA_ERROR;

	for (x = 0; x < width; ++x) {
		if (repeat == 0 && direct == 0) {
			head = getc(fd);
			if (head == EOF) return TGA_ERROR;
			if (head >= 128) {
				repeat = head - 127;
				if (fread(sample, bytes, 1, fd) < 1)
					return TGA_ERROR;
			} else {
				direct = head + 1;
			}
		}
		if (repeat > 0) {
			for (k = 0; k < bytes; ++k) buf[k] = sample[k];
			--repeat;
		} else {
			if (fread(buf, bytes, 1, fd) < 1) return TGA_ERROR;
			--direct;
		}
		buf += bytes;
	}

	tga->last = TGA_OK;
	return TGA_OK;
}

int
__TGAbgr2rgb(tbyte **buf, size_t size, tbyte *depth, tbyte alpha)
{
	tbyte *src;
	tbyte bytes;

	if (*depth < 9)
		return TGA_OK;

	src = *buf;
	bytes = TGA_BYTE_DEPTH(*depth);
	size /= bytes;

	if (bytes == 2) {
		tbyte *dst;
		tbyte *data;
		tbyte tmp;
		tshort val;

		data = malloc(alpha ? size * 4 : size * 3);
		if (!data)
			return TGA_OOM;

		dst = data;
		if (alpha) {
			while (size--) {
				val = src[0] | src[1] << 8;

				tmp = (val >> 7) & 0xf8;
				*dst++ = tmp | tmp >> 5;
				tmp = (val >> 2) & 0xf8;
				*dst++ = tmp | tmp >> 5;
				tmp = (val << 3) & 0xf8;
				*dst++ = tmp | tmp >> 5;
				*dst++ = (val >> 15) * 255;
				src += bytes;
			}
			*depth = 32;
		} else {
			while (size--) {
				val = src[0] | src[1] << 8;

				tmp = (val >> 7) & 0xf8;
				*dst++ = tmp | tmp >> 5;
				tmp = (val >> 2) & 0xf8;
				*dst++ = tmp | tmp >> 5;
				tmp = (val << 3) & 0xf8;
				*dst++ = tmp | tmp >> 5;
				src += bytes;
			}
			*depth = 24;
		}
		free(*buf);
		*buf = data;
	} else if (bytes == 4 && !alpha) {
		tbyte *dst;
		tbyte *data;

		data = (tbyte*)malloc(size * 3);
		if (!data)
			return TGA_OOM;

		dst = data;
		while (size--) {
			*dst++ = src[2];
			*dst++ = src[1];
			*dst++ = src[0];
			src += bytes;
		}
		*depth = 24;
		free(*buf);
		*buf = data;
	} else {
		while (size--) {
			tbyte tmp = src[0];

			src[0] = src[2];
			src[2] = tmp;
			src += bytes;
		}
		*depth = bytes << 3;
	}

	return TGA_OK;
}

size_t
TGAReadScanlines(TGA	*tga,
		 tbyte  *buf,
		 size_t  sln,
		 size_t  n,
		 tuint32 flags)
{
	tlong off;
	size_t sln_size, read;
	intptr_t tmp;

	if (!tga || !buf) return 0;

	sln_size = TGA_SCANLINE_SIZE(tga);
	off = TGA_IMG_DATA_OFF(tga) + (sln * sln_size);

	if (tga->off != off) __TGASeek(tga, off, SEEK_SET);
	if (tga->off != off) {
		TGA_ERROR(tga, TGA_SEEK_FAIL);
		return 0;
	}

	buf = buf + sln * sln_size;
	tmp = sln_size;

	if (tga->hdr.vert == TGA_TOP) {
		tmp = -tmp;
		buf += (n - 1) * sln_size;
		tga->hdr.vert = TGA_BOTTOM;
	}

	if(TGA_IS_ENCODED(tga)) {
		for(read = 0; read < n; ++read) {
			if (TGAReadRLE(tga, buf) != TGA_OK)
				break;
			buf += tmp;
		}
		tga->hdr.img_t -= 8;
	} else {
		for(read = 0; read < n; ++read) {
			if (TGARead(tga, buf, sln_size, 1) != 1)
				break;
			buf += tmp;
		}
	}
	if(read != n) {
		TGA_ERROR(tga, TGA_READ_FAIL);
		return read;
	}

	tga->last = TGA_OK;
	return read;
}
