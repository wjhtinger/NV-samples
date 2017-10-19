/*
 * Copyright (c) 2012-2016 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#ifndef __COMMON_TEST_H_INCLUDED
#define __COMMON_TEST_H_INCLUDED

extern FILE *g_logFp;                 // Global Log File Descriptor.
extern int g_verbosity;               // Global Verbosity Level.

/***************************************
 * Management of mNAND data extraction *
 ****************************************/

double get_current_systime(void);
int get_current_age_calc_pace(mnand_chip *chip, double *cur_avg_age, double target_min_pace,
    double target_max_pace, uint16_t block_type);
void pace_delay(void);

#endif
