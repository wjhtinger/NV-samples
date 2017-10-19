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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "nvevaapp.h"

#define LAYERGRAPHICS_DEPTH 1

#define \
    KERNEL_LOG(msg) { \
       FILE *fp; \
       fp = fopen ("/dev/kmsg", "a+"); \
       if (fp) { \
          fprintf (fp, msg); \
          fclose (fp); \
       } \
   }


typedef struct _EarlyVideoAppPriv {
//    EarlyVideoApp *earlyvideoapp;
    NvThread     *Thread;
    FNCmdProc     fnDispatchTable [EVA_NumCmds];
    EVA_InternalState CurrentState;
    int ShutdownRequest;
    int Quit;
    NvQueue  *CommandQueue;
    CNvCaptureVideoSink *CaptureVideoSink;
    AssetLoader *AssetLoader;
    CameraCoding *CameraCoding;
    WelcomeAnimation *WelcomeAnimation;
    CaptureVideoSinkCallback *CaptureVideoSinkCallback;
    EarlyVideoCoding *EarlyVideoCoding;
    EVC_StartupEmblem StartupEmblem;
    EVC_StartupType StartupType;
    U32 uDisplayId;
    VideoConnection *VideoConnection;
    NvLayerGraphics *LayeredGraphics;
    SurfaceHandle SplashSurface;
    SurfaceHandle BlackSurface;
    int FirstSplash;
    SurfaceHandle CameraMaskSurface [ECID_MAX_ID];
    ECamera_ID CameraSelcted;
    int CaptureFailed;
    int ShutdownFailures;
    NvEvent *AssetsGrapicsReady;
    U32 CaptureErrorCount;
    CaptureError CaptureErrors [EVA_MAX_CAPTURE_ERRORS];
    void (* ThreadProc) (EarlyVideoApp *earlyvideoapp);
} EarlyVideoAppPriv;

static struct timespec oStartTime, oFirstFrameTime;

static void OnCaptureError (void *pClient)
{
    NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Capture Error\n");
}

static void OnRenderFirstFrame (void *pClient)
{
    unsigned long long uTimeStart, uTimeEnd;
    uTimeStart = (oStartTime.tv_sec * 1000000000LL) + oStartTime.tv_nsec;
    clock_gettime (CLOCK_MONOTONIC, &oFirstFrameTime);
    uTimeEnd = (oFirstFrameTime.tv_sec * 1000000000LL) + oFirstFrameTime.tv_nsec;
    uTimeEnd = (uTimeEnd-uTimeStart)/1000000;

    KERNEL_LOG ("First Camera Frame displayed\n");
    printf("First Frame Display took %llu milliseconds \n",uTimeEnd);
}

static void OnRenderFirstWAFrame (void *pClient)
{
    unsigned long long uTimeStart, uTimeEnd;
    uTimeStart = (oStartTime.tv_sec * 1000000000LL) + oStartTime.tv_nsec;
    clock_gettime (CLOCK_MONOTONIC, &oFirstFrameTime);
    uTimeEnd = (oFirstFrameTime.tv_sec * 1000000000LL) + oFirstFrameTime.tv_nsec;
    uTimeEnd = (uTimeEnd-uTimeStart)/1000000;

    KERNEL_LOG ("First Welcome Animation Frame displayed\n");
    printf("First Welcome Animation Frame Display took %llu milliseconds \n",uTimeEnd);
}

static ENvCaptureErrorType OnCaptureFrame (void *pClient, NvCaptureSurfaceMap oNvCaptureSurfaceMap)
{
    // Needs to be filled.
    return eDisplayPictureSuccess;
}

static NvResult CaptureVideoSinkCallbackOnStartCaptureEnd (void *client, NvResult result)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) client;
    if (earlyvideoapp_priv)
        return RESULT_OK;
    else {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "StartCaptureEnd Callback failed\n");
        return RESULT_FAIL;
    }
}


static NvResult CaptureVideoSinkCallbackOnStopCaptureEnd (void *client, NvResult result)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) client;
    if (earlyvideoapp_priv)
        return RESULT_OK;
    else {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "StopCaptureEnd Callback failed\n");
        return RESULT_FAIL;
    }
}


static NvResult OnQuit (EarlyVideoApp *earlyvideoapp, EVA_Task *Task)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    earlyvideoapp_priv->Quit = 1;
    if (earlyvideoapp_priv->CurrentState != EVA_DONE) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Error - OnQuit called and current state is not EVA_DONE\n");
    }

    return RESULT_OK;
}


static NvResult OnStatusGet (EarlyVideoApp *earlyvideoapp, EVA_Task *Task)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    switch (earlyvideoapp_priv->CurrentState) {
        case EVA_INIT:
            Task->Param.StatusGet.EVA_Status = EVA_IDLE;
            break;
        case EVA_CAMERA_DISPLAY:
            if (earlyvideoapp_priv->CameraSelcted > ECID_TOP_VIEW) // Should never be here
            {
                *(Task->Param.StatusGet.EVA_Status) = EVA_STATE_ERROR;
            }
            else {
                if (earlyvideoapp_priv->CaptureFailed)
                      *(Task->Param.StatusGet.EVA_Status) = (EEVA_Status)
                        ((int)EVA_CAPTURE_FAILURE_CAMERA_RVC + (int)earlyvideoapp_priv->CameraSelcted);
                else
                    *(Task->Param.StatusGet.EVA_Status) = (EEVA_Status) (((int) EVA_DISPLAY_CAMERA_RVC+ (int) earlyvideoapp_priv->CameraSelcted));
            }
            break;
        case EVA_SPLASH_SCREEN_DISPLAY:
            *(Task->Param.StatusGet.EVA_Status) = EVA_DISPLAY_SPLASH_SCREEN;
            break;
        case EVA_WELCOME_ANIMATION:
            *(Task->Param.StatusGet.EVA_Status) = EVA_DISPLAY_WELCOME_ANIMATION;
            break;
        case EVA_DONE:
            if (earlyvideoapp_priv->ShutdownFailures) {
                *(Task->Param.StatusGet.EVA_Status) = EVA_SHUTDOWN_FAILURES;
            } else {
                *(Task->Param.StatusGet.EVA_Status) = EVA_SHUTDOWN_OK;
            }
            break;
        default:
            *(Task->Param.StatusGet.EVA_Status) = EVA_STATE_ERROR;
            break;
    }
    return RESULT_OK;
}

static NvResult OnLogGet (EarlyVideoApp *earlyvideoapp, EVA_Task *Task)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    U32 charCount = 0;
    U32 entryCount = 0;
    char tempBuffer[80];
    const char *lookUpCameraID[] =
    {"RVC","SVC","TVC"};

    if (Task->Param.LogGet.Log) {
        *Task->Param.LogGet.Log = '\0';
        while (charCount < Task->Param.LogGet.maxLogLength && entryCount < earlyvideoapp_priv->CaptureErrorCount) {
            charCount += sprintf (tempBuffer,"%s 0x%08X\n",lookUpCameraID [earlyvideoapp_priv->CaptureErrors [entryCount].CamID], 0);
            entryCount++;
            if (charCount < Task->Param.LogGet.maxLogLength)
                strcat (Task->Param.LogGet.Log, tempBuffer);

        }
        return RESULT_OK;
    }
    return RESULT_INVALID_ARGUMENT;
}


static NvResult OnPictureSettings (EarlyVideoApp *earlyvideoapp, EVA_Task *Task)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    switch (earlyvideoapp_priv->CurrentState) {
        case EVA_CAMERA_DISPLAY:
            return CNvCaptureVideoSinkPictureControl (earlyvideoapp_priv->CaptureVideoSink, Task->Param.PictureSettings.Brightness,
                Task->Param.PictureSettings.Contrast,
                Task->Param.PictureSettings.Saturation,
                Task->Param.PictureSettings.Hue);
            break;
        default:
            WelcomeAnimationPictureControl (earlyvideoapp_priv->WelcomeAnimation, Task->Param.PictureSettings.Brightness,
                Task->Param.PictureSettings.Contrast,
                Task->Param.PictureSettings.Saturation,
                Task->Param.PictureSettings.Hue);
            break;
    }
    return RESULT_OK;
}

static NvResult DisplayCameraOverlay (EarlyVideoApp *earlyvideoapp, ECamera_ID CamID)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    if (NvEvaUtilShowSurface (earlyvideoapp_priv->LayeredGraphics, earlyvideoapp_priv->CameraMaskSurface [CamID]) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: LayeredGraphics::ShowSurface failed\n");
        return RESULT_FAIL;
    }
    return RESULT_OK;
}

static NvResult DisplayCamera (EarlyVideoApp *earlyvideoapp, StateMachineRequest *smr)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    NvCaptureConfig Config;
    CameraCodingParameters CamParam;
    ECamera_ID CamID = *((ECamera_ID *)(smr->Data));
//    U32 i = 0;

    if (CameraCodingGetCameraParameters (earlyvideoapp_priv->CameraCoding, CamID,  &CamParam) == RESULT_OK) {
        VideoConnectionBuild (earlyvideoapp_priv->VideoConnection, (VC_SourceID) CamID, VCSINKID_SINK1);
        Config.CaptureTimeout = 120; // Over two frame times
        Config.CSIParam = CamParam.oCSIParam;
        Config.RenderParam = CamParam.oRenderParam;
        Config.Callback.OnCaptureError = &OnCaptureError;
        Config.Callback.OnRenderFirstFrame = &OnRenderFirstFrame;
        Config.Callback.OnCaptureFrame = &OnCaptureFrame;

        if (CNvCaptureVideoSinkConfigureCapture (earlyvideoapp_priv->CaptureVideoSink, &Config) != RESULT_OK) {
            NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: Failed to configure capture\n");
        }

        // if src/dest windows valid
        if ((CamParam.m_srcRect.sTop < CamParam.m_srcRect.sBot) && (CamParam.m_srcRect.sLeft < CamParam.m_srcRect.sRight) &&
            (CamParam.m_dstRect.sTop < CamParam.m_dstRect.sBot) && (CamParam.m_dstRect.sLeft < CamParam.m_dstRect.sRight)) {
                CNvCaptureVideoSinkVideoCropAndZoom (earlyvideoapp_priv->CaptureVideoSink, &CamParam.m_srcRect, &CamParam.m_dstRect);
        }

        CNvCaptureVideoSinkPictureControl (earlyvideoapp_priv->CaptureVideoSink, 0, 1, 1, 0);
        CNvCaptureVideoSinkStartCapture (earlyvideoapp_priv->CaptureVideoSink);

        // Reset to false
        earlyvideoapp_priv->CaptureFailed = 0;

        if ( DisplayCameraOverlay (earlyvideoapp_priv, CamID) != RESULT_OK) {
        }

        earlyvideoapp_priv->CameraSelcted = CamID;
    }

    return RESULT_OK;
}

static NvResult RemoveCameraDisplay (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;

    // Stop Capture (this is taking longer than it should and will be fixed)
    CNvCaptureVideoSinkStopCapture (earlyvideoapp_priv->CaptureVideoSink);

    VideoConnectionRemove (earlyvideoapp_priv->VideoConnection, (VC_SourceID)earlyvideoapp_priv->CameraSelcted, VCSINKID_SINK1);

    earlyvideoapp_priv->CameraSelcted = ECID_NONE_SELECTED;
    return RESULT_OK;
}

static NvResult DisplaySplashScreen (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    SurfaceHandle tempHandle = NULL;

    if (earlyvideoapp_priv->FirstSplash && (earlyvideoapp_priv->StartupType == EVC_ST_BLACK || earlyvideoapp_priv->StartupType == EVC_ST_SPLASH)) {
        if (earlyvideoapp_priv->StartupType == EVC_ST_SPLASH) {
        tempHandle = earlyvideoapp_priv->SplashSurface;
        }
        else {
            tempHandle = earlyvideoapp_priv->BlackSurface;
        }
    }
    else if (earlyvideoapp_priv->StartupEmblem == EVC_SE_SPLASH) {
        tempHandle = earlyvideoapp_priv->SplashSurface;
    }
    else {
        tempHandle = earlyvideoapp_priv->BlackSurface;
    }

    earlyvideoapp_priv->FirstSplash = 0;

    if (NvEvaUtilShowSurface (earlyvideoapp_priv->LayeredGraphics, tempHandle) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: LayeredGraphics::ShowSurface failed\n");
        return RESULT_FAIL;
    }
    return RESULT_OK;
}

static NvResult RemoveSplashScreen (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    if (NvEvaUtilShowSurface (earlyvideoapp_priv->LayeredGraphics, NULL) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: LayeredGraphics::ShowSurface failed\n");
        return RESULT_FAIL;
    }
    return RESULT_OK;
}


static NvResult DisplayWelcomeAnimation (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    WelcomeAnimationParams wap;
    wap.uDisplayId = earlyvideoapp_priv->uDisplayId;
    // Is this necessary ?
    VideoConnectionBuild (earlyvideoapp_priv->VideoConnection, VCSID_INTERNAL_VIDEO, VCSINKID_SINK1);

    AssetLoaderGetWelcomeAnimationParams (earlyvideoapp_priv->AssetLoader, &wap);
    WelcomeAnimationCreate (earlyvideoapp_priv->WelcomeAnimation, &wap);
    WelcomeAnimationSetState (earlyvideoapp_priv->WelcomeAnimation, WA_PLAYING);
    return RESULT_OK;
}

static NvResult RemoveWelcomeAnimation (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    WelcomeAnimationSetState (earlyvideoapp_priv->WelcomeAnimation, WA_DONE);
    // Is this necessary ?
    VideoConnectionRemove (earlyvideoapp_priv->VideoConnection, VCSID_INTERNAL_VIDEO, VCSINKID_SINK1);
    KERNEL_LOG ("Welcome Animation done \n");
    return RESULT_OK;
}

static void CleanUp (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;

    if (earlyvideoapp_priv->CaptureVideoSink) {
        CNvCaptureVideoSinkDisconnect (earlyvideoapp_priv->CaptureVideoSink);
        CNvCaptureVideoSinkRelease (earlyvideoapp_priv->CaptureVideoSink);
        earlyvideoapp_priv->CaptureVideoSink = NULL;
    }
    if (earlyvideoapp_priv->AssetLoader) {
        AssetLoaderRelease (earlyvideoapp_priv->AssetLoader);
        earlyvideoapp_priv->AssetLoader = NULL;
    }

    if (earlyvideoapp_priv->CameraCoding) {
        CameraCodingRelease (earlyvideoapp_priv->CameraCoding);
        earlyvideoapp_priv->CameraCoding = NULL;
    }

    if (earlyvideoapp_priv->WelcomeAnimation) {
        WelcomeAnimationRelease (earlyvideoapp_priv->WelcomeAnimation);
        earlyvideoapp_priv->WelcomeAnimation = NULL;
    }

    if (earlyvideoapp_priv->EarlyVideoCoding) {
        EarlyVideoCodingRelease (earlyvideoapp_priv->EarlyVideoCoding);
        earlyvideoapp_priv->EarlyVideoCoding = NULL;
    }

    if (earlyvideoapp_priv->VideoConnection) {
        VideoConnectionRelease (earlyvideoapp_priv->VideoConnection);
        earlyvideoapp_priv->VideoConnection = NULL;
    }

    if (earlyvideoapp_priv->LayeredGraphics) {
        NvEvaUtilDestroyLayerGraphics (earlyvideoapp_priv->LayeredGraphics);
        earlyvideoapp_priv->LayeredGraphics = NULL;
    }

    SetEarlyVideoInterface (earlyvideoapp_priv->CaptureVideoSinkCallback, NULL);
    earlyvideoapp_priv->CameraSelcted = ECID_NONE_SELECTED;
    earlyvideoapp_priv->ShutdownFailures = 0;
    earlyvideoapp_priv->CaptureFailed = 0;
}

static NvResult StateMachine (EarlyVideoApp *earlyvideoapp, StateMachineRequest *smr)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    NvResult returnValue = RESULT_OK; // EVA_OK

    switch (earlyvideoapp_priv->CurrentState)
    {
    case EVA_INIT:
        switch(smr->stateRequest)
        {
        case EVA_SR_DISPLAY_SPLASH_SCREEN:
            returnValue = DisplaySplashScreen (earlyvideoapp_priv);
            if (returnValue == RESULT_OK) {
                earlyvideoapp_priv->CurrentState = EVA_SPLASH_SCREEN_DISPLAY;
            }
            break;

        case EVA_SR_DISPLAY_WELCOME_ANIMATION:
            returnValue = DisplayWelcomeAnimation (earlyvideoapp_priv);
            if (returnValue == RESULT_OK) {
                earlyvideoapp_priv->CurrentState = EVA_WELCOME_ANIMATION;
            }
            break;
        case EVA_SR_DISPLAY_CAMERA:
            returnValue = DisplayCamera (earlyvideoapp_priv, smr);
            if (returnValue == RESULT_OK) {
                earlyvideoapp_priv->CurrentState = EVA_CAMERA_DISPLAY;
            }
            break;
        case EVA_SR_LOG_CAPTURE_FAILURE:
            NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Error: Capture failure from EVA_INIT\n");
            break;
        case EVA_SR_SHUTDOWN:
            earlyvideoapp_priv->ShutdownRequest = 1;
            earlyvideoapp_priv->CurrentState = EVA_DONE;
            break;
        default:
            break;
        }
        break;
    case EVA_CAMERA_DISPLAY:
        switch(smr->stateRequest)
        {
            case EVA_SR_DISPLAY_SPLASH_SCREEN:
                returnValue = DisplaySplashScreen (earlyvideoapp_priv);
                RemoveCameraDisplay (earlyvideoapp_priv);
                earlyvideoapp_priv->CurrentState = EVA_SPLASH_SCREEN_DISPLAY;
                break;
            case EVA_SR_DISPLAY_WELCOME_ANIMATION:
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Warning: Welcome Animation Play request ignored due to active camera\n");
                break;
            case EVA_SR_DISPLAY_CAMERA:
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Error: Camera display request when camera is active\n");
                break;
            case EVA_SR_LOG_CAPTURE_FAILURE:
                // Only record first failure until camera reselected
                if (earlyvideoapp_priv->CaptureErrorCount < EVA_MAX_CAPTURE_ERRORS && earlyvideoapp_priv->CaptureFailed == 0) {
                    earlyvideoapp_priv->CaptureErrors [earlyvideoapp_priv->CaptureErrorCount].CamID = earlyvideoapp_priv->CameraSelcted;
                    earlyvideoapp_priv->CaptureErrors [earlyvideoapp_priv->CaptureErrorCount].errorInfo = 0x0; // Maybe set to time in milliseconds
                    earlyvideoapp_priv->CaptureErrorCount++;
                }
                earlyvideoapp_priv->CaptureFailed = 1;
                break;
            case EVA_SR_SHUTDOWN:
                // Leave any camera connection up, sink lives beyone EVA
                earlyvideoapp_priv->ShutdownRequest = 1;
                earlyvideoapp_priv->CurrentState = EVA_DONE;
                break;
            default:
                break;
        }
        break;
    case EVA_SPLASH_SCREEN_DISPLAY:
        switch(smr->stateRequest)
        {
            case EVA_SR_DISPLAY_SPLASH_SCREEN:
                break;
            case EVA_SR_DISPLAY_WELCOME_ANIMATION:
                returnValue = DisplayWelcomeAnimation (earlyvideoapp_priv);
                if (returnValue == RESULT_OK) {
                    RemoveSplashScreen (earlyvideoapp_priv);
                    earlyvideoapp_priv->CurrentState = EVA_WELCOME_ANIMATION;
                }
                break;
            case EVA_SR_DISPLAY_CAMERA:
                returnValue = DisplayCamera(earlyvideoapp_priv, smr);
                if (returnValue == RESULT_OK) {
                    earlyvideoapp_priv->CurrentState = EVA_CAMERA_DISPLAY;
                }
            break;
            case EVA_SR_SHUTDOWN:
                earlyvideoapp_priv->ShutdownRequest = 1;
                earlyvideoapp_priv->CurrentState = EVA_DONE;
                break;
            default:
                break;
        }
        break;

    case EVA_WELCOME_ANIMATION:
        switch(smr->stateRequest)
        {
            case EVA_SR_DISPLAY_SPLASH_SCREEN:
            case EVA_SR_DISPLAY_WELCOME_ANIMATION_DONE:
                returnValue = DisplaySplashScreen (earlyvideoapp_priv);
                RemoveWelcomeAnimation (earlyvideoapp_priv);
                earlyvideoapp_priv->CurrentState = EVA_SPLASH_SCREEN_DISPLAY;
                break;
            case EVA_SR_DISPLAY_WELCOME_ANIMATION:
                // Ignore already playing
                break;
            case EVA_SR_DISPLAY_CAMERA:
                DisplaySplashScreen (earlyvideoapp_priv);
                RemoveWelcomeAnimation (earlyvideoapp_priv);
                earlyvideoapp_priv->CurrentState = EVA_SPLASH_SCREEN_DISPLAY;
                returnValue = DisplayCamera (earlyvideoapp_priv, smr);
                if (returnValue == RESULT_OK) {
                    earlyvideoapp_priv->CurrentState = EVA_CAMERA_DISPLAY;
                }
            break;
            case EVA_SR_SHUTDOWN:
                earlyvideoapp_priv->ShutdownRequest = 1;
                // Assumption is that overlay of last image
                // of splashscreen is present
                earlyvideoapp_priv->CurrentState = EVA_DONE;
                break;
            default:
                break;
        }
        break;
    case EVA_DONE:
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Warning: State change request while in EVA_DONE\n");
        break;
    }

    return returnValue;
}

static NvResult OnStateSet (EarlyVideoApp *earlyvideoapp, EVA_Task *Task)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    StateMachineRequest smr;
    smr.result = RESULT_OK;
    smr.stateRequest = Task->Param.SetState.StateRequest;
    smr.Data = &Task->Param.SetState.Data;

    StateMachine (earlyvideoapp_priv, &smr);
    return smr.result;
}

static NvResult OnCameraSelect (EarlyVideoApp *earlyvideoapp, EVA_Task *Task)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    StateMachineRequest smr;

    if (earlyvideoapp_priv->CameraSelcted != ECID_NONE_SELECTED) {
        // Switch to Splash
        smr.stateRequest = EVA_SR_DISPLAY_SPLASH_SCREEN;
        if (StateMachine (earlyvideoapp_priv, &smr) != RESULT_OK) {
            return RESULT_FAIL;
        }
    }
    if (Task->Param.CameraSelect.cameraID != ECID_NONE_SELECTED) {
        smr.stateRequest = EVA_SR_DISPLAY_CAMERA;
        smr.Data = &Task->Param.CameraSelect.cameraID;
        return StateMachine (earlyvideoapp_priv, &smr);
    }
    return RESULT_OK;
}

static NvResult QueueCommandAndWait (EarlyVideoApp *earlyvideoapp, EVA_Task *evaTask)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    NvEvent *Event = NULL;
    NvResult returnValue = RESULT_FAIL;

    NvEventCreate (&Event, 0, 0);
    evaTask->Event = Event;
    if (earlyvideoapp_priv->CommandQueue)
      NvQueuePut (earlyvideoapp_priv->CommandQueue, (U8 *)(evaTask), NV_TIMEOUT_INFINITE);
    NvEventWait (Event, NV_TIMEOUT_INFINITE);
    NvEventDestroy (Event);
    if (evaTask->ne == &returnValue)
        return returnValue;
    return RESULT_OK;
}

static NvResult QueueCommandAsync (EarlyVideoApp *earlyvideoapp, EVA_Task *evaTask)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    NvResult returnValue = RESULT_FAIL;

    evaTask->Event = NULL;
    if (earlyvideoapp_priv->CommandQueue)
      NvQueuePut (earlyvideoapp_priv->CommandQueue, (U8 *)(evaTask), NV_TIMEOUT_INFINITE);

    return returnValue;
}

// Incorrect code to display splash (since it would remove camera if in view)
static NvResult SplashShow (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;;
    // Fill in parameters
    evaTask.Param.SetState.StateRequest = EVA_SR_DISPLAY_SPLASH_SCREEN;
    evaTask.Cmd = StateSet;
    evaTask.ne = &ne;
    // Queue and wait for command to finish
    QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
    // Return value
    return ne;
}

EarlyVideoApp* EarlyVideoAppInit (void)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) malloc (sizeof(EarlyVideoAppPriv));
    if (!earlyvideoapp_priv) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "EarlyVideoApp Initialization failed\n");
        return NULL;
    }

    U32 i;
    earlyvideoapp_priv->ShutdownRequest = 0;
    earlyvideoapp_priv->Quit = 0;
    earlyvideoapp_priv->CurrentState = EVA_INIT;
    earlyvideoapp_priv->CaptureVideoSink = NULL;
    earlyvideoapp_priv->AssetLoader = NULL;
    earlyvideoapp_priv->CameraCoding = NULL;
    earlyvideoapp_priv->WelcomeAnimation = NULL;
    earlyvideoapp_priv->StartupEmblem = EVC_SE_SPLASH;
    earlyvideoapp_priv->StartupType = EVC_ST_SPLASH;
    earlyvideoapp_priv->VideoConnection = NULL;
    earlyvideoapp_priv->LayeredGraphics = NULL;
    earlyvideoapp_priv->AssetsGrapicsReady = NULL;
    earlyvideoapp_priv->CaptureErrorCount = 0;
    earlyvideoapp_priv->CameraSelcted = ECID_NONE_SELECTED;

    earlyvideoapp_priv->fnDispatchTable [EVA_Quit] = &OnQuit;
    earlyvideoapp_priv->fnDispatchTable [StateSet] = &OnStateSet;
    earlyvideoapp_priv->fnDispatchTable [StatusGet] = &OnStatusGet;
    earlyvideoapp_priv->fnDispatchTable [LogGet] = &OnLogGet;
    earlyvideoapp_priv->fnDispatchTable [CameraSelect] = &OnCameraSelect;
    earlyvideoapp_priv->fnDispatchTable [PictureSettings] = &OnPictureSettings;
    NvQueueCreate (&earlyvideoapp_priv->CommandQueue, EVA_MAX_COMMANDS, sizeof(EVA_Task));
    NvEventCreate (&earlyvideoapp_priv->AssetsGrapicsReady, 0, 0);

    earlyvideoapp_priv->SplashSurface = NULL;
    earlyvideoapp_priv->BlackSurface = NULL;
    earlyvideoapp_priv->FirstSplash = 1;
    for (i = 0; i < ECID_MAX_ID; i++) {
        earlyvideoapp_priv->CameraMaskSurface[i] = NULL;
    }
    earlyvideoapp_priv->ThreadProc = &EarlyVideoAppThreadProc;

    earlyvideoapp_priv->CaptureVideoSinkCallback = (CaptureVideoSinkCallback *) malloc (sizeof(CaptureVideoSinkCallbackPriv));
    SetEarlyVideoInterface (earlyvideoapp_priv->CaptureVideoSinkCallback, earlyvideoapp_priv);
    CaptureVideoSinkCallbackPriv *callback_priv = (CaptureVideoSinkCallbackPriv *) earlyvideoapp_priv->CaptureVideoSinkCallback;
//    earlyvideoapp_priv->CaptureVideoSinkCallback->OnCaptureCallback = &OnCapture;
    callback_priv->startcaptureend_callback = &CaptureVideoSinkCallbackOnStartCaptureEnd;
    callback_priv->stopcaptureend_callback = &CaptureVideoSinkCallbackOnStopCaptureEnd;

    return (EarlyVideoApp *) earlyvideoapp_priv;
}

NvResult EarlyVideoAppDestroy (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;

    if (earlyvideoapp_priv) {
        NvEventDestroy (earlyvideoapp_priv->AssetsGrapicsReady);
        if (earlyvideoapp_priv->Thread)
          NvThreadDestroy (earlyvideoapp_priv->Thread);
        CleanUp (earlyvideoapp_priv);
        NvQueueDestroy (earlyvideoapp_priv->CommandQueue);
        free (earlyvideoapp_priv);
        return RESULT_OK;
    }

    return RESULT_FAIL;
}


// Used to inform Early Video Application to start shutdown.  Early Video can also have a forced shutdown (where currently configured camera will be disabled) which is needed for cases where HMI wishes to playback a file while early video is still active.
NvResult EVA_StateSet (EarlyVideoApp *earlyvideoapp, EEVA_State EVA_State)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    NvResult ne = RESULT_OK;;
    EVA_Task evaTask;
    // Fill in parameters
    if (EVA_State == EVA_SHUTDOWN)
        evaTask.Param.SetState.StateRequest = EVA_SR_SHUTDOWN;
    else
        return RESULT_INVALID_ARGUMENT;

    evaTask.Cmd = StateSet;
    evaTask.ne = &ne;
    if (earlyvideoapp_priv) {
        // Queue and wait for command to finish
        QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}

// Can be used to kick off welcome animation
NvResult WelcomeAnimationPlay (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;;
    // Fill in parameters
    evaTask.Param.SetState.StateRequest = EVA_SR_DISPLAY_WELCOME_ANIMATION;
    evaTask.Cmd = StateSet;
    evaTask.ne = &ne;
    if (earlyvideoapp_priv) {
        // Queue and wait for command to finish
        QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}

// Can be used to kick off welcome animation
NvResult WelcomeAnimationDone (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;;
    // Fill in parameters
    evaTask.Param.SetState.StateRequest = EVA_SR_DISPLAY_WELCOME_ANIMATION_DONE;
    evaTask.Cmd = StateSet;
    evaTask.ne = NULL;
    if (earlyvideoapp_priv) {
        // Queue command but dont wait for it to finish
        QueueCommandAsync (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}


// Notify Early Video App of video capture failure
NvResult VideoCaptureFail (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;;
    // Fill in parameters
    evaTask.Param.SetState.StateRequest = EVA_SR_LOG_CAPTURE_FAILURE;
    evaTask.Cmd = StateSet;
    evaTask.ne = NULL;
    if (earlyvideoapp_priv) {
        // Queue command but dont wait for it to finish
        QueueCommandAsync (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}

// Not Implemented
NvResult StartCaptureEndCallback (void *earlyvideoapp, NvResult nr)
{
    return RESULT_OK;
}
// Not Implemented
NvResult StopCaptureEndCallback (void *earlyvideoapp, NvResult nr)
{
    return RESULT_OK;
}

// Used to get EVA status
EEVA_Status EVA_StatusGet (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;
    EEVA_Status EVA_Status;
    // Fill in parameters
    evaTask.Param.StatusGet.EVA_Status = &EVA_Status;
    evaTask.Cmd = StatusGet;
    evaTask.ne = &ne;
    if (earlyvideoapp_priv) {
        // Queue and wait for command to finish
        QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
    }
    // Return value
    return EVA_Status;
}

// Used to retrieve log information from EVA
NvResult EVA_LogGet (EarlyVideoApp *earlyvideoapp, char *Log, U32 MaxLogLength)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;
    // Fill in parameters
    evaTask.Param.LogGet.maxLogLength = MaxLogLength;
    evaTask.Param.LogGet.Log = Log;
    evaTask.Cmd = LogGet;
    evaTask.ne = &ne;
    if (earlyvideoapp_priv) {
        // Queue and wait for command to finish
        QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}

// Selects camera to be displayed.  Camera mapping must match the camera coding mapping.
NvResult EVA_CameraSelect (EarlyVideoApp *earlyvideoapp, ECamera_ID camID)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;
    // Fill in parameters
    evaTask.Param.CameraSelect.cameraID = camID;
    evaTask.Cmd = CameraSelect;
    evaTask.ne = &ne;
    if (earlyvideoapp_priv) {
        // Queue and wait for command to finish
        QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}

// These controls are for adjustments to the picture.  These are mapped to the currently active video
NvResult EVA_PictureControl (EarlyVideoApp *earlyvideoapp, float Brightness, float Contrast, float Saturation, float Hue)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task evaTask;
    NvResult ne = RESULT_OK;
    // Fill in parameters
    evaTask.Param.PictureSettings.Brightness = Brightness;
    evaTask.Param.PictureSettings.Contrast = Contrast;
    evaTask.Param.PictureSettings.Saturation = Saturation;
    evaTask.Param.PictureSettings.Hue = Hue;
    evaTask.Cmd = PictureSettings;
    evaTask.ne = &ne;
    if (earlyvideoapp_priv) {
        // Queue and wait for command to finish
        QueueCommandAndWait (earlyvideoapp_priv, &evaTask);
        return ne;
    }
    return RESULT_FAIL;
}

void EarlyVideoAppThreadProc (EarlyVideoApp *earlyvideoapp)
{
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    EVA_Task Task;
    NvResult ne;

    earlyvideoapp_priv->CaptureVideoSink = CNvCaptureVideoSinkInit ();
    if (earlyvideoapp_priv->CaptureVideoSink == NULL) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: CaptureVideoSink CreateInstance failed\n");
    }

    if (CNvCaptureVideoSinkConnect (earlyvideoapp_priv->CaptureVideoSink, earlyvideoapp_priv->CaptureVideoSinkCallback) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: CaptureVideoSink Connect failed\n");
    }

    WelcomeAnimationCallback welcomeanimationcallback;
    welcomeanimationcallback.OnDoneCallback = WelcomeAnimationDone;
    welcomeanimationcallback.OnFirstFrameCallback = OnRenderFirstWAFrame;
    earlyvideoapp_priv->WelcomeAnimation = WelcomeAnimationInit (earlyvideoapp_priv, welcomeanimationcallback);
    if (earlyvideoapp_priv->WelcomeAnimation == NULL) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: WelcomeAnimation CreateInstance failed\n");
    }

    // Wait at most 200 ms for asset loading
    if (NvEventWait (earlyvideoapp_priv->AssetsGrapicsReady, 200) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Warning: Assets not loaded in 200ms\n");
    }

    while (!earlyvideoapp_priv->Quit && !earlyvideoapp_priv->ShutdownRequest) {
        if (NvQueueGet (earlyvideoapp_priv->CommandQueue, (U8*)&Task, NV_TIMEOUT_INFINITE) == RESULT_OK) {
            // Call function for processing
            ne = earlyvideoapp_priv->fnDispatchTable[Task.Cmd] (earlyvideoapp_priv, &Task);
            if (Task.ne) {
                Task.ne = &ne;
            }
            if (Task.Event) {
                NvEventSet (Task.Event);
            }
        }
        else {
            // Maybe gather statistics
        }
    }
}

int EVA_main (EarlyVideoApp *earlyvideoapp, S32 argc, char* argv[])
{
    KERNEL_LOG ("Loaded Early Video App\n");

    clock_gettime (CLOCK_MONOTONIC, &oStartTime);
    EarlyVideoAppPriv *earlyvideoapp_priv = (EarlyVideoAppPriv *) earlyvideoapp;
    // Spawn Processing Thread some init happens there
    NvThreadCreate (&earlyvideoapp_priv->Thread, (void *) earlyvideoapp_priv->ThreadProc, earlyvideoapp_priv, NV_THREAD_PRIORITY_NORMAL);

    // Do other init here
    earlyvideoapp_priv->AssetLoader = AssetLoaderInit ();
    if (earlyvideoapp_priv->AssetLoader == NULL) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: AssetLoader CreateInstance failed\n");
    }

    U32 uDisplayId;
    earlyvideoapp_priv->CameraCoding = CameraCodingInit (&uDisplayId);
    if (earlyvideoapp_priv->CameraCoding == NULL) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: CameraCoding CreateInstance failed\n");
    }
    earlyvideoapp_priv->uDisplayId = uDisplayId;

    earlyvideoapp_priv->EarlyVideoCoding = EarlyVideoCodingInit (argc, argv);
    if (earlyvideoapp_priv->EarlyVideoCoding == NULL) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: EarlyVideoCoding CreateInstance failed\n");
    }

    if (EarlyVideoCodingEVACodingGetParam (earlyvideoapp_priv->EarlyVideoCoding, EVA_CPID_STARTUP_EMBLEM, (void*)&earlyvideoapp_priv->StartupEmblem) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: EarlyVideoCoding EVACodingGetParam failed\n");
    }

    if (EarlyVideoCodingEVACodingGetParam (earlyvideoapp_priv->EarlyVideoCoding, EVA_CPID_STARTUP_TYPE, (void*)&earlyvideoapp_priv->StartupType) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: EarlyVideoCoding EVACodingGetParam failed\n");
    }

    if (EarlyVideoCodingEVACodingGetParam (earlyvideoapp_priv->EarlyVideoCoding, EVA_CPID_DISPLAY_ID, (void*)&earlyvideoapp_priv->uDisplayId) != RESULT_OK) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: EarlyVideoCoding EVACodingGetParam failed\n");
    }

    earlyvideoapp_priv->VideoConnection = VideoConnectionInit ();
    if (earlyvideoapp_priv->VideoConnection == NULL) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: VideoConnection CreateInstance failed\n");
    }

    earlyvideoapp_priv->LayeredGraphics = NvEvaUtilCreateLayerGraphics (earlyvideoapp_priv->uDisplayId, LAYERGRAPHICS_DEPTH);
    if (!earlyvideoapp_priv->LayeredGraphics)
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "ERROR: LayeredGraphics CreateInstance failed\n");

    U32 screen_width = earlyvideoapp_priv->LayeredGraphics->uWidth;
    U32 screen_height = earlyvideoapp_priv->LayeredGraphics->uHeight;

    // Example load of asset to surface
    {
        U8 *tempBuffer;
        U8 *tempCarImageBuff = NULL;
        SurfaceHandle pTempSurf = NULL;
        CarOverlayParams carop;
        CameraOverlayParam camOverParam;
        AssetLoaderSplashScreenParam ssp;
        U32 i;
        int clearSurface;

        // Note surface is always size of screen
        tempBuffer = (U8*) malloc ( screen_width * screen_height * 4 * sizeof(char));

        // Load splash if coded
        if ((earlyvideoapp_priv->StartupEmblem == EVC_SE_SPLASH || earlyvideoapp_priv->StartupType == EVC_ST_SPLASH) && (AssetLoaderGetSplashParam (earlyvideoapp_priv->AssetLoader, &ssp) == RESULT_OK)) {
            // Load splash to buffer
            if (AssetLoaderCopySplashScreen (earlyvideoapp_priv->AssetLoader, tempBuffer, ssp.width * ssp.height * 4) != RESULT_OK)
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to load splash screen\n");
            else {
                // Create splash surface
                clearSurface = 1;
                if (ssp.width == screen_width && ssp.height == screen_height)
                    clearSurface = 0;
                if (NvEvaUtilCreateSurface (earlyvideoapp_priv->LayeredGraphics, &pTempSurf, (clearSurface) ? NV_EVA_TRUE : NV_EVA_FALSE) != RESULT_OK) {
                    NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to create splash surface\n");
                }
                else {
                    if (pTempSurf && NvEvaUtilCopyToSurface (earlyvideoapp_priv->LayeredGraphics, tempBuffer, ssp.width, ssp.height, ssp.width*4, pTempSurf, 0, 0) != RESULT_OK)
                        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to copy splash to surface\n");
                    earlyvideoapp_priv->SplashSurface = pTempSurf;
                }
            }
        }

       // Create black surface if coded
        if ((earlyvideoapp_priv->StartupEmblem == EVC_SE_BLACK) || (earlyvideoapp_priv->StartupType == EVC_ST_BLACK)) {
            int *pTempBuffer = (int *)tempBuffer;
            if (NvEvaUtilCreateSurface (earlyvideoapp_priv->LayeredGraphics, &pTempSurf, NV_EVA_TRUE) != RESULT_OK)
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to Create black Surface\n");
            for (i = 0; i < screen_width * screen_height; i++, pTempBuffer++)
                *pTempBuffer = 0xFF000000;
            if (pTempSurf && tempBuffer && (NvEvaUtilCopyToSurface (earlyvideoapp_priv->LayeredGraphics, tempBuffer, screen_width, screen_height, screen_width * 4, pTempSurf, 0, 0) != RESULT_OK))
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to copy black to surface\n");
            earlyvideoapp_priv->BlackSurface = pTempSurf;
        }

        // Load car image;
        if (AssetLoaderGetCarImageParams (earlyvideoapp_priv->AssetLoader, &carop) == RESULT_OK) {
            tempCarImageBuff = (U8*) malloc (carop.width*carop.height*4*sizeof(char));
            if (AssetLoaderCopyCarImage (earlyvideoapp_priv->AssetLoader, tempCarImageBuff, carop.width*carop.height*4) != RESULT_OK)
                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to copy car image to buffer\n");
        }

        // Load Camera Masks if coded
        for (i = 0; i < ECID_MAX_ID; i++) {
            clearSurface = 1;
            if (AssetLoaderGetCameraOverlayParams (earlyvideoapp_priv->AssetLoader, (ECamera_ID)i, &camOverParam) == RESULT_OK) {
                pTempSurf = NULL;
                if (camOverParam.width && camOverParam.height) {
                    if (AssetLoaderCopyCameraOverlay (earlyvideoapp_priv->AssetLoader, (ECamera_ID)i, tempBuffer, screen_width * screen_height * 4) == RESULT_OK) {
                        if (camOverParam.width == screen_width && camOverParam.height == screen_height)
                            clearSurface = 0;
                        if (NvEvaUtilCreateSurface (earlyvideoapp_priv->LayeredGraphics, &pTempSurf, (clearSurface) ? NV_EVA_TRUE : NV_EVA_FALSE) != RESULT_OK)
                            NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to create camera mask \n");
                        else {
                            // Copy mask to surface
                            if (pTempSurf && (NvEvaUtilCopyToSurface (earlyvideoapp_priv->LayeredGraphics, tempBuffer, camOverParam.width, camOverParam.height, camOverParam.width*4, pTempSurf, 0, 0) != RESULT_OK))
                                NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to copy camera mask to surface\n");
                            if (carop.height && carop.width) {
                                U32 offSetX = (screen_width - carop.width)/2;
                                U32 offSetY = (screen_height - carop.height)/2;
                                if (pTempSurf && (NvEvaUtilCopyToSurface (earlyvideoapp_priv->LayeredGraphics, tempCarImageBuff, carop.width, carop.height, carop.width*4, pTempSurf, offSetX, offSetY) != RESULT_OK))
                                    NVTRACE (__FILE__, __FUNCTION__, __LINE__, "Failed to copy camera mask to surface\n");
                            }
                            earlyvideoapp_priv->CameraMaskSurface[i] = pTempSurf;
                        }
                    }
                }
            }
        }
        free (tempBuffer);
    }

    NvEventSet (earlyvideoapp_priv->AssetsGrapicsReady);
    // Depending on startup type
    switch (earlyvideoapp_priv->StartupType) {
        case EVC_ST_SPLASH:
        case EVC_ST_BLACK:
            SplashShow (earlyvideoapp_priv);
            break;
        case EVC_ST_WELCOME_ANIMATION:
            WelcomeAnimationPlay (earlyvideoapp_priv);
            break;
        case EVC_ST_RVC:
            EVA_CameraSelect (earlyvideoapp_priv, ECID_REAR_VIEW);
            break;
        case EVC_ST_SVC:
            EVA_CameraSelect (earlyvideoapp_priv, ECID_SIDE_VIEW);
            break;
        case EVC_ST_TVC:
            EVA_CameraSelect (earlyvideoapp_priv, ECID_TOP_VIEW);
            break;
        default:
            break;
    }
    return 0;
}


void  SetEarlyVideoInterface (CaptureVideoSinkCallback *capturevideosinkcallback, EarlyVideoApp *earlyvideoapp)
{
    CaptureVideoSinkCallbackPriv *capturevideosinkcallback_priv = (CaptureVideoSinkCallback *) capturevideosinkcallback;
    if (capturevideosinkcallback_priv)
        capturevideosinkcallback_priv->client = earlyvideoapp;
}


static EarlyVideoApp *pEarlyVideoApp = NULL;

EarlyVideoApp* GetEarlyVideoApp(void)
{
    return pEarlyVideoApp;
}

void EarlyVideoAppCreate (void)
{
    static int inConstructor = 0;
    if (pEarlyVideoApp == NULL && inConstructor == 0) {
        inConstructor = 1;
        pEarlyVideoApp = EarlyVideoAppInit ();
        inConstructor = 0;
    }
}

void EarlyVideoAppFinish (void)
{
    if (pEarlyVideoApp != NULL) {
        EarlyVideoApp *pTemp = pEarlyVideoApp;
        pEarlyVideoApp = NULL;
        EarlyVideoAppDestroy (pTemp);
    }
}
