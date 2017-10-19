/*
 * Copyright (c) 2012-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software and related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

//#include <stdio.h>
#include <unistd.h>
#include "flipdemo.h"

/* \color       color to fill
 * \fb          pointer to framebuffer
 * \x           x pixels from the left the leftmost pixels of the color should be placed
 * \y           y pixels from the bottom the bottommost pixels pf the color should be placed
 * \fill_width  width in pixels of fill
 * \fill_height height in pixels of fill
 * \fb_pitch    framebuffer pitch
 */
void colorFill(unsigned int color, unsigned char *fb,
               unsigned int x, unsigned int y,
               unsigned int fill_width, unsigned int fill_height,
               unsigned int fb_pitch)
{
    unsigned int i = 0, j = 0;
    unsigned int *pixel = NULL;
    const int bpp = 4;

    if (fill_height > y) {
        printf("Invalid fill_height\n");
        return;
    }

    for (i = y - fill_height; i < y; i++) {
        pixel = (unsigned int*)(fb + (x * bpp) + (i * fb_pitch));
        for (j = 0; j < fill_width; j++) {
            *pixel++ = color;
        }
    }
}

/* \bitmap_data pointer to bmp image data
 * \fb          pointer to framebuffer
 * \x           x pixels from the left the leftmost pixels of the image should be placed
 * \y           y pixels from the top the bottommost pixels of the image should be placed
 * \bm_width    width in pixels of the bitmap image
 * \bm_height   height in pixels of the bitmap image
 * \fb_pitch    framebuffer pitch
 */
void blit(unsigned char *bitmap_data, unsigned char *fb,
          unsigned int x, unsigned int y,
          unsigned int bm_width, unsigned int bm_height,
          unsigned int fb_pitch)
{
    unsigned int i = 0, j = 0;
    unsigned char *RGBA = NULL;

    if (bm_height > y) {
        printf("Invalid bm_height\n");
        return;
    }

    if (y < 1) {
        printf("Invalid y value passed to blit");
        return;
    }

    for (i = y - 1; (i >= (y-bm_height)) && (i <= (y-1)); i--) {
        RGBA = fb + (x*4) + (i * fb_pitch);
        for (j = 0; j < bm_width; j++) {
            RGBA[0] = bitmap_data[2];
            RGBA[1] = bitmap_data[1];
            RGBA[2] = bitmap_data[0];
            RGBA[3] = 0xFF;
            RGBA += 4;
            bitmap_data += 3;
        }
    }
}

static int populate_framebuf(void *buffer, unsigned int fb_width,
        unsigned int fb_height, unsigned int fb_pitch)
{
    static unsigned char init = 0;
    unsigned char *bmp[4] = {germany, checkengine, mercedes, tegra};
    static unsigned toggle_counter = 0;

    toggle_counter++;
    toggle_counter %= 16;

    if (!init) {
        printf("Framebuffer Dimensions %d x %d\n", fb_width, fb_height);
        init = 1;
    }

    /* Clear the screen */
    colorFill(BLACK, (unsigned char *)buffer, 0, fb_height, fb_width,
              fb_height, fb_pitch);

    /* Border */
    colorFill(SOLID_WHITE, (unsigned char *)buffer, 0, ((fb_height >> 1) - 70),
              fb_width, 5, fb_pitch);

    /* Color Bar */
    colorFill(BLACK, (unsigned char *)buffer, 0, ((fb_height >> 1) + 50),
              fb_width, 100, fb_pitch);

    /* Border */
    colorFill(SOLID_WHITE, (unsigned char *)buffer, 0, ((fb_height >> 1) + 75),
              fb_width, 5, fb_pitch);

    if (toggle_counter & (1 << 0)) {
        blit(bmp[0], (unsigned char *)buffer, (fb_width >> 3) - 50,
             ((fb_height >> 1) + 50), 100, 100, fb_pitch);
    }
    if (toggle_counter & (1 << 1)) {
        blit(bmp[1], (unsigned char *)buffer, (fb_width >> 3) * 3 - 50,
             ((fb_height >> 1) + 50), 100, 100, fb_pitch);
    }
    if (toggle_counter & (1 << 2)) {
        blit(bmp[2], (unsigned char *)buffer, (fb_width >> 3) * 5 - 50,
             ((fb_height >> 1) + 50), 100, 100, fb_pitch);
    }
    if (toggle_counter & (1 << 3)) {
        blit(bmp[3], (unsigned char *)buffer, (fb_width >> 3) * 7 - 50,
             ((fb_height >> 1) + 50), 100, 100, fb_pitch);
    }

    return 1;
}

int main(int argc, char *argv[])
{
    void *buffer[2];
    unsigned int fb_width;
    unsigned int fb_height;
    unsigned int fb_pitch;
    unsigned int width = 640;
    unsigned int height = 400;
    unsigned int dst_x = 0;
    unsigned int dst_y = 0;
    int display_head = 1;   /* Second display head */
    int display_window = 1; /* WIN_B */
    int window_depth = 0;   /* Topmost hardware layer */
    int i;

#if defined(__INTEGRITY)
    /* Wait for notification to start */
    while (1) {
            Object obj;
            if (RequestResource(&obj, "__nvidia_dispinit",
                            "!systempassword") == Success) {
                Close(obj);
                break;
            }
            usleep(100000);
    }
#endif

    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-disp") == 0) {
            if (++i == argc) {
                break;
            }
            display_head = atoi(argv[i]);
        } else if (strcmp(argv[i], "-window") == 0) {
            if (++i == argc) {
                break;
            }
            display_window = atoi(argv[i]);
        }
    }

    if (nvInitFlipper(display_head, display_window, window_depth,
                      nv_flipper_format_A8R8G8B8, width, height)) {
        return -1;
    }

    getArgs(buffer, &fb_width, &fb_height, &fb_pitch);

    i = 0;

    while (populate_framebuf(buffer[i], fb_width, fb_height, fb_pitch)) {
        if(!nvflip(i, dst_x, dst_y)) {
            nvDeinitFlipper();
            return -1;
        }
        i = !i;
        sleep(1);
    }

    nvDeinitFlipper();
    return 0;
}
