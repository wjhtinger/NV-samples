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
//! \file nvwelcomeanimation.c
//! \brief Example implementation of Welcome Animation player interface.
//------------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "nvwelcomeanimation.h"

//! Welcome Animation Interface
typedef struct {
    WelcomeAnimation *welcome_animation;
    char FileName[256];
    float FrameRate;
    float Brightness;
    float Contrast;
    float Saturation;
    float Hue;
    WelcomeAnimationState State;
    NvEvent  *VideoDoneEvent;
    NvStartupAnimation *StartupAnimation;
    void *earlyvideoapp;
    WelcomeAnimationCallback welcomeanimationcallback;
} WelcomeAnimationPriv;

WelcomeAnimation* WelcomeAnimationInit (void *earlyvideoapp, WelcomeAnimationCallback callback)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) malloc (sizeof(WelcomeAnimationPriv));
    if (!welcome_animation_priv) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Welcome Animation Initialization failed\n");
        return NULL;
    }

    welcome_animation_priv->FrameRate = 30.0;
    welcome_animation_priv->Brightness = 0;
    welcome_animation_priv->Contrast = 1;
    welcome_animation_priv->Saturation = 1;
    welcome_animation_priv->Hue = 1;
    welcome_animation_priv->State = WA_IDLE;
    welcome_animation_priv->StartupAnimation = NULL;
    welcome_animation_priv->VideoDoneEvent = NULL;
    welcome_animation_priv->earlyvideoapp = earlyvideoapp;
    welcome_animation_priv->welcomeanimationcallback = callback;

    NvEventCreate (&welcome_animation_priv->VideoDoneEvent, 0, 0);
    if (!welcome_animation_priv->VideoDoneEvent) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "WelcomeAnimation: Failed to create VideoDoneEvent");
    }

    memset (welcome_animation_priv->FileName, 0, 256);

    return (WelcomeAnimation *) welcome_animation_priv;
}

NvResult WelcomeAnimationRelease (WelcomeAnimation *welcome_animation)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) welcome_animation;
    if (welcome_animation_priv) {
        if (welcome_animation_priv->StartupAnimation)
            NvStartupAnimationDestroy (welcome_animation_priv->StartupAnimation);
        if (welcome_animation_priv->VideoDoneEvent) {
            NvEventDestroy (welcome_animation_priv->VideoDoneEvent);
        }

        welcome_animation_priv->StartupAnimation = NULL;
        welcome_animation_priv->Brightness = welcome_animation_priv->Hue = 0;
        welcome_animation_priv->Contrast = welcome_animation_priv->Saturation = 1;
        welcome_animation_priv->State = WA_IDLE;
        free (welcome_animation_priv);
        return RESULT_OK;
    }

    return RESULT_INVALID_POINTER;
}

static void WelcomeAnimationOnEnd (void *pClient)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) pClient;

    if (welcome_animation_priv) {
        if (welcome_animation_priv->welcomeanimationcallback.OnDoneCallback && welcome_animation_priv->earlyvideoapp) {
            welcome_animation_priv->welcomeanimationcallback.OnDoneCallback (welcome_animation_priv->earlyvideoapp);
        }

        if (welcome_animation_priv->VideoDoneEvent){
            NvEventSet (welcome_animation_priv->VideoDoneEvent);
        }
    }
}

static void WelcomeAnimationOnRenderFirstFrame (void *pClient)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) pClient;

    if (welcome_animation_priv) {
        if (welcome_animation_priv->welcomeanimationcallback.OnFirstFrameCallback && welcome_animation_priv->earlyvideoapp) {
            welcome_animation_priv->welcomeanimationcallback.OnFirstFrameCallback (welcome_animation_priv->earlyvideoapp);
        }
    }
}

NvResult WelcomeAnimationWaitOnVideoDone (WelcomeAnimation *welcome_animation)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) welcome_animation;
    NvResult nr = RESULT_FAIL;

    if (welcome_animation_priv) {
        if (welcome_animation_priv->VideoDoneEvent){
            nr = NvEventWait (welcome_animation_priv->VideoDoneEvent, NV_TIMEOUT_INFINITE);
        }
        if (IsSucceeded(nr)){
            return RESULT_OK;
        }
    }

    return RESULT_FAIL;
}

NvResult WelcomeAnimationCreate (WelcomeAnimation *welcome_animation, WelcomeAnimationParams *WAP)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) welcome_animation;
    NvSAParams WAParams;
    NvSACallback Callback;

    if (!welcome_animation_priv || welcome_animation_priv->StartupAnimation) {
        return RESULT_FAIL;
    }

    Callback.OnError = 0;
    Callback.OnEnd = WelcomeAnimationOnEnd;
    Callback.OnRenderFirstFrame = WelcomeAnimationOnRenderFirstFrame;

    WAParams.uDepth = 2;
    WAParams.uDisplayId = WAP->uDisplayId;
    WAParams.pClient = welcome_animation_priv;
    WAParams.pCallback = &Callback;

    memcpy (welcome_animation_priv->FileName, WAP->fileName, sizeof(WAP->fileName));
    welcome_animation_priv->FrameRate = WAP->frameRate;

    welcome_animation_priv->StartupAnimation  = NvStartupAnimationCreate (&WAParams);
    if (!welcome_animation_priv->StartupAnimation) {
        return RESULT_FAIL;
    }

    return NvStartupAnimationColorControl (welcome_animation_priv->StartupAnimation, welcome_animation_priv->Brightness, welcome_animation_priv->Contrast, welcome_animation_priv->Saturation, welcome_animation_priv->Hue);
}

NvResult WelcomeAnimationPictureControl (WelcomeAnimation *welcome_animation, float Brightness, float Contrast, float Saturation, float Hue)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) welcome_animation;
    if (welcome_animation_priv && welcome_animation_priv->StartupAnimation) {
        return NvStartupAnimationColorControl (welcome_animation_priv->StartupAnimation, Brightness, Contrast, Saturation, Hue);
    }

    welcome_animation_priv->Brightness = Brightness;
    welcome_animation_priv->Contrast = Contrast;
    welcome_animation_priv->Saturation = Saturation;
    welcome_animation_priv->Hue = Hue;

    return RESULT_OK;
}

NvResult WelcomeAnimationSetState (WelcomeAnimation *welcome_animation, WelcomeAnimationState state)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) welcome_animation;
    NvResult nr = RESULT_FAIL;

    if (welcome_animation_priv && welcome_animation_priv->StartupAnimation) {
        switch (state) {
            case WA_IDLE:
            case WA_DONE:
                nr = NvStartupAnimationStop (welcome_animation_priv->StartupAnimation);
                NvStartupAnimationDestroy (welcome_animation_priv->StartupAnimation);
                welcome_animation_priv->StartupAnimation = NULL;
                break;
            case WA_PLAYING:
                nr = NvStartupAnimationPlay (welcome_animation_priv->StartupAnimation, welcome_animation_priv->FileName, welcome_animation_priv->FrameRate);
                break;
            case WA_FADE:
                nr = RESULT_OK;
                break;
            default:
                break;
        }
    }

    if (IsSucceeded(nr)) {
        welcome_animation_priv->State = state;
    }

    return nr;
}

NvResult WelcomeAnimationGetState (WelcomeAnimation *welcome_animation, WelcomeAnimationState *State)
{
    WelcomeAnimationPriv *welcome_animation_priv = (WelcomeAnimationPriv *) welcome_animation;

    if (welcome_animation_priv && State) {
        *State = welcome_animation_priv->State;
        return RESULT_OK;
    }

    return RESULT_FAIL;
}
