/*
 * nvtexfont-priv.h
 *
 * Copyright (c) 2003 - 2012 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 *
 * This software is based upon texfont, with consent from Mark J. Kilgard,
 * provided under the following terms:
 *
 * Copyright (c) Mark J. Kilgard, 1997.
 *
 * This program is freely distributable without licensing fees  and is
 * provided without guarantee or warrantee expressed or  implied. This
 * program is -not- in the public domain.
 */

typedef struct {
    unsigned short c;       /* Potentially support 16-bit glyphs. */
    unsigned char width;
    unsigned char height;
    signed char xoffset;
    signed char yoffset;
    signed char advance;
    char dummy;           /* Space holder for alignment reasons. */
    short x;
    short y;
} NVTexfontGlyphInfo;

struct _NVTexfontRasterFont {
    GLuint texobj;
    int tex_width;
    int tex_height;
    int max_ascent;
    int max_descent;
    int num_glyphs;
    int min_glyph;
    int range;
    unsigned char *teximage;
    NVTexfontGlyphInfo *tgi;
    int *lut;
    GLfloat *st;
    GLshort *vert;
};

struct _NVTexfontVectorFont {
    unsigned char *vertices;
    int *offsets;
	int *counts;
	unsigned char *widths;
    int vertNo;

	GLboolean antialias;
    GLuint vbo;
};

typedef struct {
    float r, g, b;
    float scaleX, scaleY;
    float x, y;
} nvtexfontState;


/* byte swap a 32-bit value */
#define SWAPL(x, n) { \
                 n = ((char *) (x))[0];\
                 ((char *) (x))[0] = ((char *) (x))[3];\
                 ((char *) (x))[3] = n;\
                 n = ((char *) (x))[1];\
                 ((char *) (x))[1] = ((char *) (x))[2];\
                 ((char *) (x))[2] = n; }

/* byte swap a short */
#define SWAPS(x, n) { \
                 n = ((char *) (x))[0];\
                 ((char *) (x))[0] = ((char *) (x))[1];\
                 ((char *) (x))[1] = n; }


#define VERT_ARRAY 0
#define TEX_ARRAY 1

#define FONT_HEIGHT_RATIO (1.0f/33.0f)

extern void initShaders(void);
extern void deinitShaders(void);

extern GLuint rasterProg, vectorProg;
extern char *lastError;
