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
//! \file nvcapturevideosink.c
//! \brief Capture sink implementation (just wrapper)
//------------------------------------------------------------------------------

#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include "nvcapturevideosink.h"

#define NV_CAPTURE_MAX_TASKS 32

typedef struct _CNvCaptureVideoSinkPriv {
    CNvCaptureVideoSink *capturevideosink;
    PFNCmdProc fnDispatchTable[NumCmds];
    NvThread *Thread;
    NvQueue  *Queue;
    U32 CapRefCount;
    float Saturation;
    float Brightness;
    float Contrast;
    float Hue;
    NvRectangle Src;
    NvRectangle Dst;
    NvCaptureSink *CaptureSink;
    NvCaptureSinkParam *CaptureParams;
    void (* ThreadProc) (CNvCaptureVideoSink *capturevideosink);
} CNvCaptureVideoSinkPriv;

static void OnStartCaptureEnd (void *pClient, NvResult nr) {
        CaptureVideoSinkCallbackPriv *Callback = (CaptureVideoSinkCallbackPriv *) pClient;
        if (Callback->startcaptureend_callback)
            Callback->startcaptureend_callback (Callback->client, nr);
        else
            NVTRACE (__FILE__, __FUNCTION__, __LINE__, "No StartCaptureEndCallback exists\n");
}

static void OnStopCaptureEnd (void *pClient, NvResult nr) {
        CaptureVideoSinkCallbackPriv *Callback = (CaptureVideoSinkCallbackPriv *) pClient;
        if (Callback->stopcaptureend_callback)
            Callback->stopcaptureend_callback (Callback->client, nr);
        else
            NVTRACE (__FILE__, __FUNCTION__, __LINE__, "No StartCaptureEndCallback exists\n");
}

static int OnQuit (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    if (capturevideosink_priv->CaptureSink && capturevideosink_priv->CapRefCount == 0) {
        NvCaptureSinkDestroy (capturevideosink_priv->CaptureSink);
        capturevideosink_priv->CaptureSink = NULL;
        if (capturevideosink_priv->CaptureParams) {
            if (capturevideosink_priv->CaptureParams->pCallback) {
                free (capturevideosink_priv->CaptureParams->pCallback);
            }
            capturevideosink_priv->CaptureParams->pCallback = NULL;
            free (capturevideosink_priv->CaptureParams);
        }
        capturevideosink_priv->CaptureParams = NULL;
    }
    return 1;
}

static int OnConnect (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvCaptureSinkCSIParam *CSIParam;
    NvCaptureSinkRenderParam *RenderParam;

    if (Task->pnr){
        *(Task->pnr) = RESULT_OK;
    }

    if (!capturevideosink_priv->CaptureParams) {
        capturevideosink_priv->CaptureParams =  (NvCaptureSinkParam *) malloc (sizeof(NvCaptureSinkParam));
        if (!capturevideosink_priv->CaptureParams) {
            if (Task->pnr) {
                *(Task->pnr) = RESULT_FAIL;
                return 0;
            }
        }
        memset(capturevideosink_priv->CaptureParams, 0, sizeof(NvCaptureSinkParam));

        capturevideosink_priv->CaptureParams->pCallback = (NvCaptureSinkCallback *) malloc (sizeof(NvCaptureSinkCallback));
        if (!capturevideosink_priv->CaptureParams->pCallback) {
            if (Task->pnr) {
                free (capturevideosink_priv->CaptureParams);
                capturevideosink_priv->CaptureParams = NULL;
                *(Task->pnr) = RESULT_FAIL;
                return 0;
            }
        }

        memset (capturevideosink_priv->CaptureParams->pCallback, 0, sizeof(NvCaptureSinkCallback));

        CSIParam = &capturevideosink_priv->CaptureParams->oCSIParam;
        memset(CSIParam, 0, sizeof(NvCaptureSinkCSIParam));

        RenderParam = &capturevideosink_priv->CaptureParams->oRenderParam;
        memset(RenderParam, 0, sizeof(NvCaptureSinkRenderParam));

        capturevideosink_priv->CaptureParams->uCaptureTimeout = 120;
        capturevideosink_priv->CaptureParams->pClient = Task->pCallback;
    }

    capturevideosink_priv->CapRefCount++;
    return 0;
}

static int OnDisconnect (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    capturevideosink_priv->CapRefCount--;

    if (capturevideosink_priv->CaptureSink && capturevideosink_priv->CapRefCount == 0) {
        NvCaptureSinkDestroy (capturevideosink_priv->CaptureSink);
        if (capturevideosink_priv->CaptureParams) {
            if (capturevideosink_priv->CaptureParams->pCallback) {
                free (capturevideosink_priv->CaptureParams->pCallback);
            }
            free (capturevideosink_priv->CaptureParams);
        }
        capturevideosink_priv->CaptureSink = NULL;
        capturevideosink_priv->CaptureParams = NULL;
    }

    if (Task->pnr) {
        *(Task->pnr) = RESULT_OK;
    }

    return 0;
}

static int OnStartCapture (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvResult nr = RESULT_FAIL;

    if (!capturevideosink_priv->CaptureSink && capturevideosink_priv->CaptureParams) {
        capturevideosink_priv->CaptureSink = NvCaptureSinkCreate (capturevideosink_priv->CaptureParams);
    }

    if (capturevideosink_priv->CaptureSink) {
        NvCaptureSinkColorControl (capturevideosink_priv->CaptureSink, capturevideosink_priv->Brightness, capturevideosink_priv->Contrast, capturevideosink_priv->Saturation, capturevideosink_priv->Hue);
        NvCaptureSinkVideoCropAndZoom (capturevideosink_priv->CaptureSink, capturevideosink_priv->Src, capturevideosink_priv->Dst);
        nr = NvCaptureSinkStart (capturevideosink_priv->CaptureSink);
    }

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }
    if (capturevideosink_priv->CaptureParams)
        OnStartCaptureEnd (capturevideosink_priv->CaptureParams->pClient, nr);
    return 0;
}

static int OnStopCapture (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvResult nr = RESULT_FAIL;

    if (capturevideosink_priv->CaptureSink) {
        nr = NvCaptureSinkDestroy (capturevideosink_priv->CaptureSink);
        capturevideosink_priv->Src.sLeft = capturevideosink_priv->Src.sTop = -1;
        capturevideosink_priv->Dst.sLeft = capturevideosink_priv->Dst.sTop = -1;
        capturevideosink_priv->Src.sRight = capturevideosink_priv->Src.sBot = -1;
        capturevideosink_priv->Dst.sRight = capturevideosink_priv->Dst.sBot = -1;
        capturevideosink_priv->Saturation = capturevideosink_priv->Contrast = 1;
        capturevideosink_priv->Brightness = capturevideosink_priv->Hue = 0;
        capturevideosink_priv->CaptureSink = NULL;
    }

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    if (capturevideosink_priv->CaptureParams)
        OnStopCaptureEnd (capturevideosink_priv->CaptureParams->pClient, nr);
    return 0;
}

static int OnQueryCaptureEnabled (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvBoolean Enabled = NV_EVA_FALSE;
    NvResult nr = RESULT_FAIL;

    if (capturevideosink_priv->CaptureSink)
        nr = NvCaptureSinkQueryCaptureEnabled (capturevideosink_priv->CaptureSink, &Enabled);

    *(Task->pEnabled) = (Enabled == NV_EVA_TRUE) ? 1: 0;

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    return 0;
}

static int OnQueryPictureControl (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    float Brightness = 0, Contrast = 0, Saturation = 0, Hue = 0;
    NvResult nr = RESULT_FAIL;

    if (capturevideosink_priv->CaptureSink)
        nr = NvCaptureSinkQueryColorControl (capturevideosink_priv->CaptureSink, &Brightness, &Contrast, &Saturation, &Hue);

    *(Task->pBrightness) = Brightness;
    *(Task->pContrast) = Contrast;
    *(Task->pSaturation) = Saturation;
    *(Task->pHue) = Hue;

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    return 0;
}

static int OnPictureControl (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvResult nr = RESULT_FAIL;

    if (capturevideosink_priv->CaptureSink) {
        nr = NvCaptureSinkColorControl (capturevideosink_priv->CaptureSink, Task->fBrightness, Task->fContrast, Task->fSaturation, Task->fHue);
    }
    else {
        capturevideosink_priv->Brightness = Task->fBrightness;
        capturevideosink_priv->Contrast = Task->fContrast;
        capturevideosink_priv->Saturation = Task->fSaturation;
        capturevideosink_priv->Hue = Task->fHue;
        nr = RESULT_OK;
    }

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    return 0;
}

static int OnVideoCropAndZoom (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvResult nr = RESULT_FAIL;

    if (capturevideosink_priv->CaptureSink) {
        nr = NvCaptureSinkVideoCropAndZoom (capturevideosink_priv->CaptureSink, Task->Src, Task->Dst);
    }
    else {
        memcpy (&capturevideosink_priv->Src, &Task->Src, sizeof(NvRectangle));
        memcpy (&capturevideosink_priv->Dst, &Task->Dst, sizeof(NvRectangle));
        nr = RESULT_OK;
    }

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    return 0;
}

static int OnConfigureCapture (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvResult nr = RESULT_OK;

    if (capturevideosink_priv->CaptureParams) {
        capturevideosink_priv->CaptureParams->uCaptureTimeout = Task->Config.CaptureTimeout;
        capturevideosink_priv->CaptureParams->oCSIParam = Task->Config.CSIParam;
        capturevideosink_priv->CaptureParams->oRenderParam = Task->Config.RenderParam;
        memcpy (capturevideosink_priv->CaptureParams->pCallback, &Task->Config.Callback, sizeof(NvCaptureSinkCallback));
    }
    else {
        nr = RESULT_FAIL;
    }

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    return 0;
}

static int OnCaptureEnable (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvResult nr = RESULT_FAIL;

    if (capturevideosink_priv->CaptureSink)
        nr = NvCaptureSinkEnable (capturevideosink_priv->CaptureSink, Task->Enable);

    if (Task->pnr) {
        *(Task->pnr) = nr;
    }

    return 0;
}

/*static void OnCapture (void *pClient) {
        CaptureVideoSinkCallback *Callback = (CaptureVideoSinkCallback *) pClient;
        CaptureVideoSinkCallbackOnCapture ();
}*/

CNvCaptureVideoSink* CNvCaptureVideoSinkInit ()
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) malloc (sizeof(CNvCaptureVideoSinkPriv));
    if (!capturevideosink_priv) {
        NVTRACE (__FILE__, __FUNCTION__, __LINE__, "CaptureVideoSink Initialization failed\n");
        return NULL;
    }

    capturevideosink_priv->fnDispatchTable [Quit] = &OnQuit;
    capturevideosink_priv->fnDispatchTable [Connect] = &OnConnect;
    capturevideosink_priv->fnDispatchTable [Disconnect] = &OnDisconnect;
    capturevideosink_priv->fnDispatchTable [StartCapture] = &OnStartCapture;
    capturevideosink_priv->fnDispatchTable [StopCapture] = &OnStopCapture;
    capturevideosink_priv->fnDispatchTable [QueryCaptureEnabled] = &OnQueryCaptureEnabled;
    capturevideosink_priv->fnDispatchTable [QueryPictureControl] = &OnQueryPictureControl;
    capturevideosink_priv->fnDispatchTable [PictureControl] = &OnPictureControl;
    capturevideosink_priv->fnDispatchTable [VideoCropAndZoom] = &OnVideoCropAndZoom;
    capturevideosink_priv->fnDispatchTable [ConfigureCapture] = &OnConfigureCapture;
    capturevideosink_priv->fnDispatchTable [CaptureEnable] = &OnCaptureEnable;

    NvQueueCreate (&capturevideosink_priv->Queue, NV_CAPTURE_MAX_TASKS, sizeof(CNvCaptureVideoSinkTask));
    capturevideosink_priv->CapRefCount = 0;
    capturevideosink_priv->Saturation = 1;
    capturevideosink_priv->Brightness = 0;
    capturevideosink_priv->Contrast = 1;
    capturevideosink_priv->Hue = 1;
    capturevideosink_priv->Src.sLeft = capturevideosink_priv->Src.sTop = -1;
    capturevideosink_priv->Dst.sLeft = capturevideosink_priv->Dst.sTop = -1;
    capturevideosink_priv->Src.sRight = capturevideosink_priv->Src.sBot = -1;
    capturevideosink_priv->Dst.sRight = capturevideosink_priv->Dst.sBot = -1;
    capturevideosink_priv->CaptureSink = NULL;
    capturevideosink_priv->CaptureParams = NULL;
    capturevideosink_priv->ThreadProc = &CNvCaptureVideoSinkThreadProc;

    NvThreadCreate (&capturevideosink_priv->Thread, (void *) capturevideosink_priv->ThreadProc, capturevideosink_priv, NV_THREAD_PRIORITY_NORMAL);

    return (CNvCaptureVideoSink *) capturevideosink_priv;
}

void CNvCaptureVideoSinkRelease (CNvCaptureVideoSink *capturevideosink)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;

    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQuit (capturevideosink_priv);
        NvThreadDestroy (capturevideosink_priv->Thread);
        NvQueueDestroy (capturevideosink_priv->Queue);
        free (capturevideosink_priv);
    }
}

static void CNvCaptureVideoSinkQueueTaskAndWait (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    NvEvent *Event = NULL;

    NvEventCreate (&Event, 0, 0);
    Task->pEvent = Event;
    NvQueuePut (capturevideosink_priv->Queue, (U8 *)Task, NV_TIMEOUT_INFINITE);
    NvEventWait (Event, NV_TIMEOUT_INFINITE);
    NvEventDestroy (Event);
}

static void CNvCaptureVideoSinkQueueTaskAsync (CNvCaptureVideoSink *capturevideosink, CNvCaptureVideoSinkTask *Task)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    Task->pEvent = NULL;
    NvQueuePut (capturevideosink_priv->Queue, (U8 *)Task, NV_TIMEOUT_INFINITE);
}

NvResult CNvCaptureVideoSinkQuit (CNvCaptureVideoSink *capturevideosink)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    Task.Cmd          = Quit;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return RESULT_OK;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkConnect (CNvCaptureVideoSink *capturevideosink, CaptureVideoSinkCallback *Callback)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = Connect;
    Task.pCallback     = Callback;
    Task.pnr           = &nr;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkDisconnect (CNvCaptureVideoSink *capturevideosink)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = Disconnect;
    Task.pnr           = &nr;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkStartCapture (CNvCaptureVideoSink *capturevideosink)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = StartCapture;
    Task.pnr           = &nr;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAsync (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkStopCapture (CNvCaptureVideoSink *capturevideosink)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = StopCapture;
    Task.pnr           = &nr;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAsync (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkQueryCaptureEnabled (CNvCaptureVideoSink *capturevideosink, int *Enabled)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr        = RESULT_OK;
    int enabled        = 0;
    Task.Cmd           = QueryCaptureEnabled;
    Task.pnr            = &nr;
    Task.pEnabled       = &enabled;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        *Enabled            = enabled;
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkQueryPictureControl (CNvCaptureVideoSink *capturevideosink, float *Brightness, float *Contrast, float *Saturation, float *Hue)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    float brightness  = 0;
    float contrast    = 0;
    float saturation  = 0;
    float hue         = 0;
    Task.Cmd          = QueryPictureControl;
    Task.pnr           = &nr;
    Task.pBrightness   = &brightness;
    Task.pContrast     = &contrast;
    Task.pSaturation   = &saturation;
    Task.pHue          = &hue;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        *Brightness        = brightness;
        *Contrast          = contrast;
        *Saturation        = saturation;
        *Hue               = hue;
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkPictureControl (CNvCaptureVideoSink *capturevideosink, float Brightness, float Contrast, float Saturation, float Hue)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr        = RESULT_OK;
    Task.Cmd           = PictureControl;
    Task.pnr            = &nr;
    Task.fBrightness   = Brightness;
    Task.fContrast     = Contrast;
    Task.fSaturation   = Saturation;
    Task.fHue          = Hue;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkVideoCropAndZoom (CNvCaptureVideoSink *capturevideosink, const NvRectangle *Src, const NvRectangle *Dst)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = VideoCropAndZoom;
    Task.pnr           = &nr;
    Task.Src          = *Src;
    Task.Dst          = *Dst;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkConfigureCapture (CNvCaptureVideoSink *capturevideosink, const NvCaptureConfig *Config)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = ConfigureCapture;
    Task.pnr           = &nr;
    Task.Config       = *Config;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

NvResult CNvCaptureVideoSinkCaptureEnable (CNvCaptureVideoSink *capturevideosink, int Enable)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    NvResult nr       = RESULT_OK;
    Task.Cmd          = CaptureEnable;
    Task.pnr           = &nr;
    Task.Enable       = Enable;
    if (capturevideosink_priv) {
        CNvCaptureVideoSinkQueueTaskAndWait (capturevideosink_priv, &Task);
        return nr;
    }
    return RESULT_FAIL;
}

void CNvCaptureVideoSinkThreadProc (CNvCaptureVideoSink *capturevideosink)
{
    CNvCaptureVideoSinkPriv *capturevideosink_priv = (CNvCaptureVideoSinkPriv *) capturevideosink;
    CNvCaptureVideoSinkTask Task;
    int bQuit = 0;
    while (!bQuit) {
        NvQueueGet (capturevideosink_priv->Queue, (U8 *)&Task, NV_TIMEOUT_INFINITE);
        bQuit = capturevideosink_priv->fnDispatchTable[Task.Cmd] (capturevideosink_priv, &Task);
        if (Task.pEvent) {
            NvEventSet (Task.pEvent);
        }
    }
}

