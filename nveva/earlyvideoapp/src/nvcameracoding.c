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
//! \file nvcameracoding.c
//! \brief Camera coding example implementaion
//------------------------------------------------------------------------------

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "nvcameracoding.h"

#define CAMERA_CODING_FILE "camera_coding.txt"

typedef struct _CameraCoding {
    CameraCoding *camera_coding;
    CameraCodingParameters CameraCodingParam [ECID_MAX_ID];
    NvBoolean CameraValid [ECID_MAX_ID];
    char CameraCodingString [1024];
    char *EaAssetDir;
} CameraCodingPriv;

static NvBoolean GetRect (char *String, NvRectangle *rect)
{
    if (String == NULL || (sscanf((const char*)String, "(%d,%d-%d,%d)", &rect->sLeft, &rect->sTop, &rect->sRight, &rect->sBot) != 4))
        return NV_EVA_FALSE;
    // Add verification for values
    return NV_EVA_TRUE;
}

static void LoadCameraCodingParameters (CameraCodingPriv *camera_coding_priv)
{
    FILE *fp;
    int line = 1;
    if (camera_coding_priv->EaAssetDir) {
        strcpy(camera_coding_priv->CameraCodingString,camera_coding_priv->EaAssetDir);
        strcat(camera_coding_priv->CameraCodingString,CAMERA_CODING_FILE);
    } else {
        strcpy(camera_coding_priv->CameraCodingString,CAMERA_CODING_FILE);
    }

    fp = fopen(camera_coding_priv->CameraCodingString,"rt");

    if (fp) {
        char tempString[1024], *CurrentToken;
        // Need to go through processing each line at a time
        while (fgets(tempString,1024,fp)) {
            ECamera_ID camID = ECID_NONE_SELECTED;
            CurrentToken = strtok(tempString," \n\r");
            if (CurrentToken == NULL) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }
            // Find ID
            if (strcmp(CurrentToken,"ECID_REAR_VIEW") == 0)
                camID = ECID_REAR_VIEW;
            else if (strcmp(CurrentToken,"ECID_SIDE_VIEW") == 0)
                camID = ECID_SIDE_VIEW;
            else if (strcmp(CurrentToken,"ECID_TOP_VIEW") == 0)
                camID = ECID_TOP_VIEW;
            else {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }

            CurrentToken = strtok(NULL," \n\r");
            if (CurrentToken == NULL) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }

            // Default values
            camera_coding_priv->CameraCodingParam[camID].oRenderParam.eMode = eNvDeinterlaceWEAVE;
            // Set DisplayId = 1, which is HDMI port on Drive-CX
            camera_coding_priv->CameraCodingParam[camID].oRenderParam.uDisplayId = 1;
            camera_coding_priv->CameraCodingParam[camID].oRenderParam.uDepth = 2;
            camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSIPort = eNvCSIPortAB;
            camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatYUV422;
            camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 1280;
            camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 720;
            camera_coding_priv->CameraCodingParam[camID].oCSIParam.uNumLanes = 4;

            if (!strcmp(CurrentToken, "480p.yuv422")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 720;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 480;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatYUV422;
            }
            else if (!strcmp(CurrentToken, "480p.rgb888")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 720;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 480;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatRGB888;
            }
            else if (!strcmp(CurrentToken, "576p.yuv422")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 720;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 576;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatYUV422;
            }
            else if (!strcmp(CurrentToken, "576p.rgb888")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 720;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 576;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatRGB888;
            }
            else if (!strcmp(CurrentToken, "vga.yuv422")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 480;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 240;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatYUV422;
            }
            else if (!strcmp(CurrentToken, "vga.rgb888")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 480;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 240;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatRGB888;
            }
            else if (!strcmp(CurrentToken, "720p.yuv422")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 1280;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 720;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatYUV422;
            }
            else if (!strcmp(CurrentToken, "720p.rgb888")) {
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uWidth = 1280;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.uHeight = 720;
                camera_coding_priv->CameraCodingParam[camID].oCSIParam.eCSICaptureFormat = eNvVideoFormatRGB888;
            }
            else {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "not a valid configuration\n");
            }

            CurrentToken = strtok(NULL," \n\r");
            if (CurrentToken == NULL) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }

            if (GetRect (CurrentToken, &camera_coding_priv->CameraCodingParam[camID].m_srcRect) == NV_EVA_FALSE) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }

            CurrentToken = strtok(NULL," \n\r");
            if (GetRect (CurrentToken, &camera_coding_priv->CameraCodingParam[camID].m_dstRect) == NV_EVA_FALSE) {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }

            CurrentToken = strtok(NULL," \n\r");
            if (!strcmp(CurrentToken, "d1"))
                camera_coding_priv->CameraCodingParam[camID].oRenderParam.uDisplayId = 1;
            else if (!strcmp(CurrentToken, "d2"))
                camera_coding_priv->CameraCodingParam[camID].oRenderParam.uDisplayId = 2;
            else {
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: camera coding file incorrect, parsing stopped at line %d\n",line);
                goto exit;
            }

            camera_coding_priv->CameraValid[camID] = NV_EVA_TRUE;
            line++;
        }
    }
exit:
    if (fp)
        fclose(fp);
}

CameraCoding* CameraCodingInit (U32 *uDisplayId)
{
    int i;

    CameraCodingPriv *camera_coding_priv = (CameraCodingPriv *) malloc(sizeof(CameraCodingPriv));
    if (!camera_coding_priv) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "CameraCoding Initialization failed\n");
        return NULL;
    }
    for (i = 0; i < ECID_MAX_ID; i++)
        camera_coding_priv->CameraValid[i] = NV_EVA_FALSE;
    camera_coding_priv->EaAssetDir = getenv(EA_ASSET_DIR_ENV_VAR);
    LoadCameraCodingParameters (camera_coding_priv);
    *uDisplayId = camera_coding_priv->CameraCodingParam[ECID_REAR_VIEW].oRenderParam.uDisplayId;

    return (CameraCoding *) camera_coding_priv;
}

NvResult CameraCodingRelease (CameraCoding *camera_coding)
{
    CameraCodingPriv *camera_coding_priv = (CameraCodingPriv *) camera_coding;
    if (camera_coding_priv) {
        free (camera_coding_priv);
        return RESULT_OK;
    }

    return RESULT_INVALID_POINTER;
}

NvResult CameraCodingGetCameraParameters (CameraCoding *camera_coding, ECamera_ID camera_ID, CameraCodingParameters *CameraCodingParam)
{
    CameraCodingPriv *camera_coding_priv = (CameraCodingPriv *) camera_coding;
    NvResult ne = RESULT_NOT_SUPPORTED;
    if (camera_coding_priv && (camera_ID < ECID_MAX_ID)) {
        if (camera_coding_priv->CameraValid[camera_ID]) {
            *CameraCodingParam = camera_coding_priv->CameraCodingParam[camera_ID];
            ne = RESULT_OK;
        }
    }

    return ne;
}

int CameraCodingGetMaximumCameraNumber (void)
{
    return ECID_MAX_ID;
}



