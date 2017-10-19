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

#ifndef __TGA_H
#define __TGA_H

#include <inttypes.h>

/* __FILE__ and __LINE__ are gcc specific */
#ifndef __FILE__
# define __FILE__ "unknown"
#endif
#ifndef __LINE__
# define __LINE__ 0
#endif

/* sections */
#define TGA_IMAGE_ID	0x01
#define TGA_IMAGE_INFO	0x02
#define TGA_IMAGE_DATA	0x04
#define TGA_COLOR_MAP	0x08
/* RLE */
#define TGA_RLE_ENCODE  0x10

/* color format */
#define TGA_RGB		0x20
#define TGA_BGR		0x40

/* orientation */
#define TGA_BOTTOM	0x0
#define TGA_TOP		0x1
#define	TGA_LEFT	0x0
#define	TGA_RIGHT	0x1

/* version info */
#define LIBTGA_VER_MAJOR	1
#define LIBTGA_VER_MINOR	0
#define LIBTGA_VER_PATCH	1
#define LIBTGA_VER_STRING	"1.0.1"

/* error codes */
enum {  TGA_OK = 0,		/* success */
	TGA_ERROR,
	TGA_OOM,		/* out of memory */
	TGA_OPEN_FAIL,
	TGA_SEEK_FAIL,
	TGA_READ_FAIL,
	TGA_WRITE_FAIL,
	TGA_UNKNOWN_SUB_FORMAT  /* invalid bit depth */
};

typedef uint32_t	tuint32;
typedef uint16_t	tuint16;
typedef uint8_t		tuint8;

typedef tuint8		tbyte;
typedef tuint16		tshort;
typedef tuint32		tlong;

typedef struct _TGAHeader	TGAHeader;
typedef struct _TGAData		TGAData;
typedef struct _TGA		TGA;


typedef void (*TGAErrorProc)(TGA*, int);


/* TGA image header */
struct _TGAHeader {
	tbyte	id_len;		/* image id length */
	tbyte	map_t;		/* color map type */
	tbyte	img_t;		/* image type */
	tshort	map_first;	/* index of first map entry */
	tshort	map_len;	/* number of entries in color map */
	tbyte	map_entry;	/* bit-depth of a cmap entry */
	tshort	x;		/* x-coordinate */
	tshort	y;		/* y-coordinate */
	tshort	width;		/* width of image */
	tshort	height;		/* height of image */
	tbyte	depth;		/* pixel-depth of image */
	tbyte	alpha;		/* alpha bits */
	tbyte	horz;		/* horizontal orientation */
	tbyte	vert;		/* vertical orientation */
};

/* TGA image data */
struct _TGAData {
	tbyte	*img_id;	/* image id */
	tbyte	*cmap;		/* color map */
	tbyte	*img_data;	/* image data */
	tuint32	flags;
};

/* TGA image handle */
struct _TGA {
	FILE*		fd;		/* file stream */
	tlong		off;		/* current offset in file*/
	int		last;		/* last error code */
	TGAHeader	hdr;		/* image header */
	TGAErrorProc	error;		/* user-defined error proc */
};


#ifdef __cplusplus
	extern "C" {
#endif

TGA* TGAOpen(const char *name);

size_t TGARead(TGA *tga, tbyte  *buf, size_t  size, size_t  n);

int TGAReadRLE(TGA *tga, tbyte *buf);

int TGAReadHeader(TGA *tga);

int TGAReadImageId(TGA *tga, tbyte **id);

int TGAReadColorMap(TGA *tga, tbyte **cmap, tuint32 flags);

size_t TGAReadScanlines(TGA *tga, tbyte *buf, size_t sln, size_t n, tuint32 flags);

int TGAReadImage(TGA *tga, TGAData *data);

int __TGAbgr2rgb(tbyte **buf, size_t size, tbyte *depth, tbyte alpha);

tlong __TGASeek(TGA *tga, tlong off, int whence);

void TGAClose(TGA *tga);

#ifdef __cplusplus
	}
#endif

#define TGA_HEADER_SIZE		18
#define TGA_BYTE_DEPTH(depth)	(((depth) + 7) >> 3)
#define TGA_CMAP_SIZE(tga)	((tga)->hdr.map_len * TGA_BYTE_DEPTH((tga)->hdr.map_entry))
#define TGA_CMAP_OFF(tga)	(TGA_HEADER_SIZE + (tga)->hdr.id_len)
#define TGA_IMG_DATA_OFF(tga)	(TGA_HEADER_SIZE + (tga)->hdr.id_len + TGA_CMAP_SIZE(tga))
#define TGA_IMG_DATA_SIZE(tga)	((tga)->hdr.width * (tga)->hdr.height * TGA_BYTE_DEPTH((tga)->hdr.depth))
#define TGA_SCANLINE_SIZE(tga)	((tga)->hdr.width * TGA_BYTE_DEPTH((tga)->hdr.depth))

#define TGA_IS_MAPPED(tga)	((tga)->hdr.map_t == 1)
#define TGA_IS_ENCODED(tga)	((tga)->hdr.img_t > 8 && (tga)->hdr.img_t < 12)

#define TGA_ERROR(tga, code) \
if((tga) && (tga)->error) (tga)->error(tga, code);\
if(tga) (tga)->last = code\


#endif /* __TGA_H */
