/*
 * Copyright (c) 2014, NVIDIA Corporation.  All Rights Reserved.
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
//! \file nvassetloader.c
//! \brief Asset Loader example implementaion
//------------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "nvassetloader.h"
#include "nvearlyappdecompression.h"

#define SPLASH_NEA "splash_nvidia.nea"
#define CAMERA_MASK_TVC_NEA "camera_mask_tvc.nea"
#define CAMERA_MASK_RVC_NEA "camera_mask_rvc.nea"
#define CAMERA_MASK_SVC_NEA "camera_mask_svc.nea"
#define CAR_IMAGE_NEA "car_image.nea"
#define WELCOME_ANIMATION_H264 "welcome_animation.264"
#define WELCOME_ANIMATION_FRAMERATE 30.0

typedef struct _AssetLoader {
    AssetLoader *asset_loader;
    char AssetLoadingString[1024];
    char *EaAssetDir;
} AssetLoaderPriv;

static char* GetAssetString (AssetLoaderPriv *asset_loader_priv, const char *input)
{
    if (asset_loader_priv->EaAssetDir) {
        strcpy (asset_loader_priv->AssetLoadingString, asset_loader_priv->EaAssetDir);
    } else {
        asset_loader_priv->AssetLoadingString[0] = '\0';
    }
    strcat (asset_loader_priv->AssetLoadingString, input);
    return asset_loader_priv->AssetLoadingString;
}

AssetLoader* AssetLoaderInit (void)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) malloc (sizeof(AssetLoaderPriv));
    if (!asset_loader_priv) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "AssetLoader Initialization failed\n");
        return NULL;
    }
    asset_loader_priv->EaAssetDir = getenv(EA_ASSET_DIR_ENV_VAR);

    return (AssetLoader *) asset_loader_priv;
}

NvResult AssetLoaderRelease (AssetLoader *asset_loader)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    if (asset_loader_priv) {
        free (asset_loader_priv);
        return RESULT_OK;
    }

    return RESULT_INVALID_POINTER;
}

NvResult AssetLoaderGetSplashParam (AssetLoader *asset_loader, AssetLoaderSplashScreenParam *SSParam)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    NeaFileHeader tempHead;
    if (asset_loader_priv && SSParam) {
        memset ((void*)SSParam, 0, sizeof(AssetLoaderSplashScreenParam));
        if (EAC_GetFileHeader (GetAssetString(asset_loader_priv, SPLASH_NEA), &tempHead) == RESULT_OK) {
            SSParam->height = tempHead.height;
            SSParam->width = tempHead.width;
        }
            return RESULT_OK;
    }
    return RESULT_INVALID_POINTER;
}

NvResult AssetLoaderCopySplashScreen (AssetLoader *asset_loader, void *SplashBuffer, U32 maxSplashScreenSize)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    if (asset_loader_priv)
        return EAC_DecompressToBuffer (GetAssetString(asset_loader_priv, SPLASH_NEA), SplashBuffer, &maxSplashScreenSize);
    return RESULT_FAIL;
}

NvResult AssetLoaderGetCameraOverlayParams (AssetLoader *asset_loader, ECamera_ID camID, CameraOverlayParam *COP)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    NeaFileHeader tempHead;
    if (asset_loader_priv && COP) {
        memset((void*)COP, 0, sizeof(CameraOverlayParam));
        switch (camID) {
            case ECID_TOP_VIEW:
                if (EAC_GetFileHeader (GetAssetString(asset_loader_priv, CAMERA_MASK_TVC_NEA), &tempHead) == RESULT_OK) {
                    COP->height = tempHead.height;
                    COP->width = tempHead.width;
                }
                break;
            case ECID_SIDE_VIEW:
                if (EAC_GetFileHeader (GetAssetString(asset_loader_priv, CAMERA_MASK_SVC_NEA), &tempHead) == RESULT_OK) {
                    COP->height = tempHead.height;
                    COP->width = tempHead.width;
                }
                break;
            case ECID_REAR_VIEW:
                if (EAC_GetFileHeader (GetAssetString(asset_loader_priv, CAMERA_MASK_RVC_NEA), &tempHead) == RESULT_OK) {
                    COP->height = tempHead.height;
                    COP->width = tempHead.width;
                }
                break;
            default:
                return RESULT_INVALID_ARGUMENT;
                break;
        }
        return RESULT_OK;
    }
    return RESULT_INVALID_POINTER;
}

NvResult AssetLoaderCopyCameraOverlay (AssetLoader *asset_loader, ECamera_ID camID, void *CameraMask, U32 maxCamMaskSize)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    if (asset_loader_priv && CameraMask) {
        if (maxCamMaskSize) {
            switch (camID) {
                case ECID_TOP_VIEW:
                    return EAC_DecompressToBuffer (GetAssetString(asset_loader_priv, CAMERA_MASK_TVC_NEA), CameraMask, &maxCamMaskSize);
                    break;
                case ECID_SIDE_VIEW:
                    return EAC_DecompressToBuffer (GetAssetString(asset_loader_priv, CAMERA_MASK_SVC_NEA), CameraMask, &maxCamMaskSize);
                    break;
                case ECID_REAR_VIEW:
                    return EAC_DecompressToBuffer (GetAssetString(asset_loader_priv, CAMERA_MASK_RVC_NEA), CameraMask, &maxCamMaskSize);
                    break;
                default:
                    return RESULT_INVALID_ARGUMENT;
                    break;
            }
        }
        return RESULT_INVALID_ARGUMENT;
    }
    return RESULT_INVALID_POINTER;
}

NvResult AssetLoaderGetCarImageParams (AssetLoader *asset_loader, CarOverlayParams *CARP)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    NeaFileHeader tempHead;
    if (asset_loader_priv && CARP) {
        memset ((void*)CARP, 0, sizeof(CarOverlayParams));
        if (EAC_GetFileHeader (GetAssetString(asset_loader_priv, CAR_IMAGE_NEA), &tempHead) == RESULT_OK)
        {
            CARP->height = tempHead.height;
            CARP->width = tempHead.width;
        }
        return RESULT_OK;
    }
    return RESULT_INVALID_POINTER;
}

NvResult AssetLoaderCopyCarImage (AssetLoader *asset_loader, void *CarImage, U32 maxCarImageSize)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    if (asset_loader_priv && CarImage) {
        if (maxCarImageSize) {
            NvResult nr = EAC_DecompressToBuffer (GetAssetString(asset_loader_priv, CAR_IMAGE_NEA), CarImage, &maxCarImageSize);
            return nr;
        }
        return RESULT_INVALID_ARGUMENT;

    }
    return RESULT_INVALID_POINTER;
}

NvResult AssetLoaderGetWelcomeAnimationParams (AssetLoader *asset_loader, WelcomeAnimationParams *WAP)
{
    AssetLoaderPriv *asset_loader_priv = (AssetLoaderPriv *) asset_loader;
    if (asset_loader_priv && WAP)
    {
        strcpy(WAP->fileName, GetAssetString(asset_loader_priv, WELCOME_ANIMATION_H264));
        WAP->frameRate = WELCOME_ANIMATION_FRAMERATE;
        return RESULT_OK;
    }
    return RESULT_FAIL;
}

