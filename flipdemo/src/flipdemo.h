#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <nvflipper.h>
#include "bmpdata.h"

#define SOLID_GREEN ((0xFF << 24) | (0xFF << 8))
#define SOLID_BLUE  ((0xFF << 24) | (0xFF))
#define SOLID_RED   ((0xFF << 24) | (0xFF << 16))
#define BLACK       (0)
#define SOLID_WHITE (SOLID_GREEN | SOLID_BLUE | SOLID_RED)
#define MAX_DRAW    (1000)

void colorFill(unsigned int color, unsigned char *fb,
               unsigned int x, unsigned int y,
               unsigned int fill_width, unsigned int fill_height,
               unsigned int fb_pitch);

void blit(unsigned char *bitmap_data, unsigned char *fb,
          unsigned int x, unsigned int y,
          unsigned int bm_width, unsigned int bm_height,
          unsigned int fb_pitch);

