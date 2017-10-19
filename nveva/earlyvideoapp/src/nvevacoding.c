/*
 * Copyright (c) 2014-2015, NVIDIA Corporation.  All Rights Reserved.
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

//------------------------------------------------------------------------------
//! \file nvevacoding.c
//! \brief Example implementation for Early Video Coding
//------------------------------------------------------------------------------


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "nvevacoding.h"

#define STARTUP_TYPE "-StartupType="
#define STARTUP_EMBLEM "-StartupEmblem="
#define DISPLAY_ID "-DisplayId="

// Extract an argument from the list
static const char* argExtract (S32* argc, char * argv[], S32 index)
{
    char * val = argv[index];
    S32   j;
    for (j = index + 1; j < *argc; ++j)
        argv[j-1] = argv[j];
    (*argc)--;
    return val;
}

static NvResult ProcessCommandLineParameters (EarlyVideoCoding *early_video_coding, S32 *argc, char * argv[])
{
    EarlyVideoCodingPriv *earlyvideo_coding_priv = (EarlyVideoCodingPriv *) early_video_coding;
    S32  i;
    // Parse input parameters
    for (i = 1; i < *argc; /*nop*/) {
        if (!strncmp ((const char*)argv[i], STARTUP_EMBLEM, strlen(STARTUP_EMBLEM))) {
            argv[i] += strlen ("-StartupEmblem=");
            if (argv[i][0] == 0) argExtract (argc, argv, i);
            if (i == *argc) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding: ProcessCommandLineParameters: Missing parameter for %s\n", STARTUP_EMBLEM);
                continue;
            }
            if (!strncmp (argv[i], "BLACK", strlen("BLACK"))) {
                earlyvideo_coding_priv->StartupEmblem = EVC_SE_BLACK;
            } else if (!strncmp (argv[i], "SPLASH", strlen("SPLASH"))) {
                earlyvideo_coding_priv->StartupEmblem = EVC_SE_SPLASH;
            } else {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding: ProcessCommandLineParameters: Unrecognized value for %s\n", STARTUP_EMBLEM);
            }
        } else if (!strncmp ((const char*)argv[i], STARTUP_TYPE, strlen(STARTUP_TYPE))) {
            argv[i] += strlen(STARTUP_TYPE);
            if (argv[i][0] == 0) argExtract (argc, argv, i);
            if (i == *argc) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding: ProcessCommandLineParameters: Missing parameter for %s\n", STARTUP_TYPE);
                continue;
            }
            if (!strncmp (argv[i], "BLACK", strlen("BLACK"))) {
                earlyvideo_coding_priv->StartupType = EVC_ST_BLACK;
            } else if (!strncmp (argv[i], "SPLASH", strlen("SPLASH"))) {
                earlyvideo_coding_priv->StartupType = EVC_ST_SPLASH;
            } else if (!strncmp (argv[i], "WA", strlen("WA"))) {
                earlyvideo_coding_priv->StartupType = EVC_ST_WELCOME_ANIMATION;
            } else if (!strncmp (argv[i], "RVC", strlen("RVC"))) {
                earlyvideo_coding_priv->StartupType = EVC_ST_RVC;
            } else if (!strncmp (argv[i], "SVC", strlen("SVC"))) {
                earlyvideo_coding_priv->StartupType = EVC_ST_SVC;
            } else if (!strncmp (argv[i], "TVC", strlen("TVC"))) {
                earlyvideo_coding_priv->StartupType = EVC_ST_TVC;
            } else {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding: ProcessCommandLineParameters: Unrecognized value for %s\n", STARTUP_TYPE);
            }
        } else if (!strncmp ((const char*)argv[i], DISPLAY_ID, strlen(DISPLAY_ID))) {
            U32 uDisplayId = 0;
            argv[i] += strlen(DISPLAY_ID);
            if (argv[i][0] == 0) argExtract (argc, argv, i);
            if (i == *argc) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding: ProcessCommandLineParameters: Missing parameter for %s\n", DISPLAY_ID);
                continue;
            }
            sscanf(argv[i],"%u\n", &uDisplayId);
            if (uDisplayId < 0 || uDisplayId > 2) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding: ProcessCommandLineParameters: Unrecognized value for %s\n", DISPLAY_ID);
            }

            earlyvideo_coding_priv->uDisplayId = uDisplayId;
        }
        else {
            i++;
        }
    }
    return RESULT_OK;
}

EarlyVideoCoding* EarlyVideoCodingInit (S32 option1, char* option2[])
{
    EarlyVideoCodingPriv *earlyvideo_coding_priv = (EarlyVideoCodingPriv *) malloc (sizeof(EarlyVideoCodingPriv));
    if (!earlyvideo_coding_priv) {
      NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoCoding Initialization failed\n");
      return NULL;
    }

    earlyvideo_coding_priv->StartupEmblem = EVC_SE_SPLASH;
    earlyvideo_coding_priv->StartupType = EVC_ST_SPLASH;

    if (ProcessCommandLineParameters (earlyvideo_coding_priv, &option1, option2) != RESULT_OK)
      NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: Failed to process command line parameters for Camera Coding\n");

    return (EarlyVideoCoding *) earlyvideo_coding_priv;
}


NvResult EarlyVideoCodingEVACodingGetParam (EarlyVideoCoding *early_video_coding, EVC_CodingParamID CodingParamID, void *Value)
{
    EarlyVideoCodingPriv *earlyvideo_coding_priv = (EarlyVideoCodingPriv *) early_video_coding;
    NvResult ne = RESULT_INVALID_ARGUMENT;
    if (earlyvideo_coding_priv && Value) {
        switch (CodingParamID) {
            case EVA_CPID_STARTUP_EMBLEM:
                *((EVC_StartupEmblem *) Value) = earlyvideo_coding_priv->StartupEmblem;
                ne = RESULT_OK;
                break;
            case EVA_CPID_STARTUP_TYPE:
                *((EVC_StartupType *) Value) = earlyvideo_coding_priv->StartupType;
                ne = RESULT_OK;
                break;
            case EVA_CPID_DISPLAY_ID:
                 *((U32 *)Value) = earlyvideo_coding_priv->uDisplayId;
                 ne = RESULT_OK;
                 break;
            default:
                break;
        }
    }
    return ne;
}

NvResult EarlyVideoCodingRelease (EarlyVideoCoding *early_video_coding)
{
    EarlyVideoCodingPriv *earlyvideo_coding_priv = (EarlyVideoCodingPriv *) early_video_coding;
    if (earlyvideo_coding_priv) {
        free (earlyvideo_coding_priv);
        return RESULT_OK;
    }
    return RESULT_INVALID_POINTER;
}
