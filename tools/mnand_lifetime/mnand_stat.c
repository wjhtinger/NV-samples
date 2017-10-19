/*
 * Copyright (c) 2012-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <semaphore.h>
#include <nvmnand.h>
#include "common.h"

#define SPEED_UP_CONTROL            0.05      // 5 Percent

static int s_mlc_delay = 1000; /* MLC Delay (us) to avoid pace > target_max_pace */
static int s_slc_delay = 1000; /* SLC Delay (us) to avoid pace > target_max_pace */
static int s_current_delay = 1000; /* Current Delay (us) */
static int s_max_delay = 1000000;
static double s_slc_start_time = 0;
static double s_slc_start_avg_age = 0;
static double s_slc_last_avg_age = 0;
static double s_mlc_start_time = 0;
static double s_mlc_start_avg_age = 0;
static double s_mlc_last_avg_age = 0;

/* Verbose print used to print mnand log. */
#define VERBOSE_PRINTF(level, ...) \
    do { \
        if (g_verbosity >= (level)) \
            printf(__VA_ARGS__); \
        if (g_logFp) \
            fprintf(g_logFp,__VA_ARGS__); \
    } while (0)

#define MNAND_DELAY_STEP_UP         10000
#define MNAND_DELAY_STEP_DOWN       10000

#define BLK_TYPE_STR(type) ((type) == MNAND_MLC_BLOCK) ? "MLC" : "SLC"

double get_current_systime(void)
{
    struct timespec tp;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec + (double)tp.tv_nsec / 1000000000;
}

/*
 * Obtain current average P/E cycles and pace (expressed in P/E cycles per
 * second). If pace not within provided pace range, changes p_delay towards
 * the pace target.
 */
int get_current_age_calc_pace(mnand_chip *chip, double *cur_avg_age, double target_min_pace,
    double target_max_pace, uint16_t block_type)
{
    int blk_count;
    uint64_t cur_total_age;
    double pace = 0.0;
    double cur_time;
    double delta_t;
    double delta_age;
    double avg_age;
    double *start_time = (block_type == MNAND_MLC_BLOCK) ? &s_mlc_start_time : &s_slc_start_time;
    double *start_avg_age = (block_type == MNAND_MLC_BLOCK) ? &s_mlc_start_avg_age : &s_slc_start_avg_age;
    double *last_avg_age = (block_type == MNAND_MLC_BLOCK) ? &s_mlc_last_avg_age : &s_slc_last_avg_age;
    int *p_delay = (block_type == MNAND_MLC_BLOCK) ? &s_mlc_delay : &s_slc_delay;

    if (cur_avg_age == NULL)
        return 1;

    /* Extract current total MLC age from mNAND */
    if (mnand_extract_age_info(chip, block_type, &cur_total_age,
        &blk_count, &avg_age) != MNAND_OK)
        return 1;

    *cur_avg_age = blk_count ? (double)cur_total_age / blk_count : (double)avg_age;
    cur_time = get_current_systime();

    /* Set up startup statistics */
    if (*start_time == 0)
        *start_time = cur_time;
    if (*start_avg_age == 0)
        *start_avg_age = *cur_avg_age;
    if (*last_avg_age == 0)
        *last_avg_age = *cur_avg_age;

    /* In some cases there will be no increase in age because of disk caching */
    if (*cur_avg_age > *last_avg_age) {
        delta_t = cur_time - *start_time;
        delta_age = *cur_avg_age - *start_avg_age;
        VERBOSE_PRINTF(3, "\t===> delta_age %f, delta_t %f\n", delta_age, delta_t);

        if (delta_t)
            pace = delta_age / delta_t;

        /* Try to adjust pace to stay below max pace, but not too much below */
        if (pace > target_max_pace) {
            /* Make it go slower */
            if (*p_delay < s_max_delay) {
                *p_delay += MNAND_DELAY_STEP_UP;
                if (*p_delay > s_max_delay)
                    *p_delay = s_max_delay;
                VERBOSE_PRINTF(2, "\tSlowing down for %s, new delay %d us\n", BLK_TYPE_STR(block_type), *p_delay);
            }
        } else if (pace < (target_min_pace - (target_min_pace * SPEED_UP_CONTROL))) {
            /* Make it go faster */
            if (*p_delay > 0) {
                *p_delay -= MNAND_DELAY_STEP_DOWN;
                if (*p_delay < 0)
                    *p_delay = 0;
                VERBOSE_PRINTF(2, "\tSpeed up for %s, new delay %d us\n", BLK_TYPE_STR(block_type), *p_delay);
            }
        }
        *last_avg_age = *cur_avg_age;
        if (blk_count) {
            VERBOSE_PRINTF(3, "%d %s blocks. Current average %s age: %f. "
                "Pace %f p/e cycles/sec\n",
                blk_count, BLK_TYPE_STR(block_type), BLK_TYPE_STR(block_type),
                *last_avg_age, pace);
        } else {
            VERBOSE_PRINTF(3, "Current average %s age: %f. "
                "Pace %f p/e cycles/sec\n",
                BLK_TYPE_STR(block_type), *last_avg_age, pace);
        }
    } else {
        VERBOSE_PRINTF(1, "No age increment found for %s area, accumulate to next round.\n", BLK_TYPE_STR(block_type));
    }

    return 0;
}

void pace_delay(void)
{
    int p_delay = (s_mlc_delay > s_slc_delay) ? s_mlc_delay : s_slc_delay;

    if (p_delay > s_current_delay)
        VERBOSE_PRINTF(2, "\tSlowing down...\t");
    else if (p_delay < s_current_delay)
        VERBOSE_PRINTF(2, "\tSpeeding up...\t");

    if (p_delay != s_current_delay)
        VERBOSE_PRINTF(3, "\tNew delay %d us\n\n\n", p_delay);

    s_current_delay = p_delay;
    if (s_current_delay)
        usleep(s_current_delay);
}
