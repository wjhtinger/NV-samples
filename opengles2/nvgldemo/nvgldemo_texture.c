/*
 * nvgldemo_texture.c
 *
 * Copyright (c) 2010-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// This file illustrates how to load an image file into a texture.
//   Only uncompressed RGB/RGBA TGA files are supported.
//

#include "nvgldemo.h"
#include "GLES2/gl2.h"

static int failedImageIndex = -1;
// Load a set of TGA files as a texture.
//   Returns the ID, and leaves it bound to current texture unit.
unsigned int
NvGlDemoLoadTgaFromBuffer(
    unsigned int target,
    unsigned int count,
    unsigned char** buffer)
{
    unsigned char* filedata = NULL;
    unsigned int facetarget=0;
    unsigned int width, height, bpp, format, pot;
    unsigned int cubewidth=0, cubeheight=0, cubeformat=0;
    unsigned char* body;
    unsigned char* temp;
    unsigned int i, j, k;
    GLuint id;

    // Validate inputs
    switch (target) {

        case GL_TEXTURE_2D:
            facetarget = GL_TEXTURE_2D;
            if (count != 1) {
                NvGlDemoLog("Unexpected file count (%d) for 2D texture\n",
                            count);
                return 0;
            }
            break;

        case GL_TEXTURE_CUBE_MAP:
            facetarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
            if (count != 6) {
                NvGlDemoLog("Unexpected file count (%d) for cube map\n",
                            count);
                return 0;
            }
            break;

        default:
            NvGlDemoLog("Unsupported texture target 0x(%04x)\n", target);
            return 0;
    }

    // Create and bind texture
    glGenTextures(1, &id);
    glBindTexture(target, id);

    // Load each face
    for (k=0; k<count; k++) {

        // Load file
        filedata = buffer[k];

        // Parse header
        if ((filedata[1] != 0) || (filedata[2] != 2)) {
            NvGlDemoLog("Cannot parse image %d\n.", k);
            NvGlDemoLog("  Only uncompressed tga files are supported");
            failedImageIndex = k;
            goto fail;
        }
        width  = ((unsigned int)filedata[13] << 8)
               |  (unsigned int)filedata[12];
        height = ((unsigned int)filedata[15] << 8)
               |  (unsigned int)filedata[14];
        bpp    =  (unsigned int)filedata[16] >> 3;
        format = (bpp == 4) ? GL_RGBA : GL_RGB;
        pot    = ((width & (width-1))==0) && ((height & (height-1))==0);

        // For cubemaps, validate size/format
        if (target == GL_TEXTURE_CUBE_MAP) {

            if (width != height) {
                NvGlDemoLog("Texture %d is not square (%d x %d)\n",
                            k, width, height);
                failedImageIndex = k;
                goto fail;
            }

            if (k == 0) {
                cubewidth  = width;
                cubeheight = height;
                cubeformat = format;
            } else if ((width  != cubewidth)  ||
                       (height != cubeheight) ||
                       (format != cubeformat)) {
                NvGlDemoLog("Texture %d does not match texture 0 on this cube\n"
                            "  (%d,%d,0x%04x) vs (%d,%d,0x%04x)\n",
                            k,
                            width, height, format,
                            cubewidth, cubeheight, cubeformat);
                failedImageIndex = k;
                goto fail;
            }

        }

        // Get image data and convert BGRA to RGBA
        temp = body = filedata + 18;
        for (i = 0; i < height; i++) {
            for(j = 0; j < width; j++) {
                unsigned char c;

                c = temp[0];
                temp[0] = temp[2];
                temp[2] = c;

                temp += bpp;
            }
        }

        // Upload data
        glTexImage2D(facetarget+k, 0, format, width, height, 0,
                     format, GL_UNSIGNED_BYTE, body);

    }

    // Set texture parameters and generate mipmaps if appropriate
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (pot) {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
        glGenerateMipmap(target);
    } else {
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }

    // Clean up and return
    return id;

    fail:
    if (id) glDeleteTextures(1, &id);
    return 0;
}

// Load a set of TGA files as a texture.
// Returns the ID, and leaves it bound to current texture unit.
unsigned int
NvGlDemoLoadTga(
    unsigned int target,
    unsigned int count,
    const char** names)
{
    unsigned int filesize;
    unsigned char **buffer = (unsigned char**) malloc(count * sizeof(unsigned char*));
    unsigned int k;
    GLuint id = 0;

    for (k=0; k<count; k++) {

        buffer[k] = (unsigned char*)NvGlDemoLoadFile(names[k], &filesize);
        if (filesize == 0 || !buffer[k]) {
            goto finish;
        }
    }

    id = NvGlDemoLoadTgaFromBuffer(target, count, buffer);

    if (id == 0) {
        NvGlDemoLog("File %s failed\n", names[failedImageIndex]);
    }

    // Clean up and return
    finish:
    for (k=0; k<count && buffer[k]; k++) {
        FREE(buffer[k]);
    }
    FREE(buffer);

    return id;

}
