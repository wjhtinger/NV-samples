/*
 * Copyright (c) 2012-2017, NVIDIA CORPORATION. All rights reserved.
 * All information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "cmdline.h"
#include "deinterlace_utils.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvcommon.h"
#include "nvmedia.h"
#include "surf_utils.h"
#include "testutil_board.h"
#include "testutil_capture_input.h"
#include "thread_utils.h"

#define BUFFER_SIZE     6
#define MAX_FRAMES      9
#define MAX_DISPLAYSURF 3

typedef struct _QueueElem {
    NvMediaVideoSurface    *surf;
    NvMediaBool             last;
} QueueElem;

typedef struct _CaptureContext {
    char                    *ctxName;
    NvSemaphore             *semStart;
    NvSemaphore             *semDone;
    NvMediaVideoOutput      *videoOutput;
    NvMediaVideoCapture     *capture;
    CaptureInputHandle       handle;
    NvMediaVideoMixer       *mixer;
    NvMediaDevice           *device;
    NvMediaBool              deinterlaceEnabled;
    unsigned int             deinterlaceType;
    unsigned int             deinterlaceAlgo;
    DeinterlaceContext      *deinterlaceCtx;
    NvMediaBool              inverceTelecine;
    NvMediaSurfaceType       surfaceType;
    unsigned int             inputWidth;
    unsigned int             inputHeight;
    unsigned int             extraLines;
    unsigned int             timeout;
    NvMediaBool              displayEnabled;
    unsigned int             visibleLineStart;
    unsigned int             visibleLineEnd;
    NvMediaBool              fileDumpEnabled;
    char                    *fname;
    NvMediaBool              timeNotCount;
    int                      last;
    NvMediaBool              checkCRC;
    unsigned int             crcChecksum;
    NvThread                *captureThread;
    NvMediaBool              externalBuffer;
    FrameBuffer              frameBuffPool[MAX_FRAMES];

    NvMediaVideoSurface     *displaySurf[MAX_DISPLAYSURF];
    NvU32                    displayIndex;
} CaptureContext;

static NvMediaBool sStop = NVMEDIA_FALSE;

static void ReleaseRef(CaptureContext *ctx, FrameBuffer *buffer)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    if (buffer->refCount > 0) {
        buffer->refCount--;

        if (!buffer->refCount) {
            LOG_DBG("ReleaseRef: Releasing surf %p\n", buffer->videoSurface);
            if (!ctx->externalBuffer) {
                status = NvMediaVideoCaptureReturnFrame(ctx->capture, buffer->videoSurface);
                if(status != NVMEDIA_STATUS_OK)
                    LOG_WARN("ReleaseRef: NvMediaVideoCaptureReturnFrame failed for surf %p\n", buffer->videoSurface);
            } else {
                NvMediaVideoSurfaceDestroy(buffer->videoSurface);
            }
        }
    }
}

static void AddRef(CaptureContext *ctx, FrameBuffer *buffer)
{
    buffer->refCount++;
}

static void GetFrame(CaptureContext *ctx, FrameBuffer **buffer)
{
    int i;

    if (!buffer) {
        LOG_ERR("GetFrame: Bad buffer parameter (NULL)\n");
        return;
    }

    for (i = 0; i < MAX_FRAMES; i++) {
        if (ctx->frameBuffPool[i].refCount == 0) {
            *buffer = &ctx->frameBuffPool[i];
            return;
        }
    }

    *buffer = NULL;
}

static void ReleaseFrame(CaptureContext *ctx, NvMediaVideoSurface *videoSurface)
{
    int i;
    FrameBuffer *buffer;

    for (i = 0; i < MAX_FRAMES; i++) {
        buffer = &ctx->frameBuffPool[i];
        if (videoSurface == buffer->videoSurface) {
            ReleaseRef(ctx, buffer);
            break;
        }
    }
}

static unsigned int
CaptureThreadFunc(void *params)
{
    int i = 0;
    unsigned int loop;
    NvMediaTime tStop = {0}, tStart = {0};
    NvS64 tDiff = 0;
    CaptureContext *ctx = (CaptureContext *)params;
    NvMediaVideoSurface *releaseFrames[4] = {NULL}, **releaseList = &releaseFrames[0];
    NvMediaVideoSurface *capSurf = NULL;
    NvMediaRect primarySrcRect;
    NvMediaPrimaryVideo primaryVideo;
    NvMediaBool crcMatchFlag;
    static int f_cnt = 0;
    char fName_t[50] = "";
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaVideoSurface *pOutputSurface[DEINT_LOOPS][DEINT_OUTPUT_SURFACES];
    NvU32 outputLoopCount;
    NvMediaPictureStructure picStructure[DEINT_LOOPS];
    FrameBuffer *frameBuff = NULL;
    FrameBuffer *pReleaseSurface = NULL;

    for (i = 0; i < MAX_FRAMES; i++) {
        ctx->frameBuffPool[i].refCount = 0;
    }

    primarySrcRect.x0 = 0;
    primarySrcRect.y0 = ctx->visibleLineStart;
    primarySrcRect.x1 = ctx->inputWidth;
    primarySrcRect.y1 = ctx->visibleLineEnd;

    memset(&primaryVideo, 0, sizeof(primaryVideo));
    primaryVideo.srcRect = &primarySrcRect;
    primaryVideo.dstRect = NULL;

    NvSemaphoreDecrement(ctx->semStart, NV_TIMEOUT_INFINITE);

    if(ctx->timeNotCount) {
        GetTimeUtil(&tStart);
        NvAddTime(&tStart, ctx->last * 1000000LL, &tStop);
    }

    NvSubTime(&tStop, &tStart, &tDiff);
    LOG_DBG("CaptureThread: Running capture %sThread...\n", ctx->ctxName);
    while((ctx->timeNotCount ? tDiff > 0 : ((ctx->last == -1) ? 1 : (i < ctx->last))) && !sStop) {
        capSurf = NvMediaVideoCaptureGetFrame(ctx->capture, ctx->timeout);
        if(!capSurf) { // TBD
            LOG_ERR("CaptureThread: NvMediaVideoCaptureGetFrame() failed in %sThread\n", ctx->ctxName);
            usleep(1000000); // To allow a clean status dump
            sStop = NVMEDIA_TRUE;
            break;
        }

        if(ctx->displayEnabled) {
            GetFrame(ctx, &frameBuff);
            if(!frameBuff) {
                LOG_ERR("CaptureThread: Buffers pool full.\n");
                sStop = NVMEDIA_TRUE;
                break;
            }

            frameBuff->topFieldFirstFlag = NVMEDIA_TRUE;
            frameBuff->progressiveFrameFlag = !ctx->deinterlaceEnabled;
            frameBuff->videoSurface = capSurf;
            capSurf->tag = frameBuff;

            AddRef(ctx, frameBuff);
            memset(pOutputSurface, 0, sizeof(NvMediaVideoSurface*) * DEINT_LOOPS * DEINT_OUTPUT_SURFACES);
            status = Deinterlace(ctx->deinterlaceCtx,
                                 frameBuff,
                                 &outputLoopCount,
                                 picStructure,
                                 pOutputSurface,
                                 &pReleaseSurface);
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("CaptureThreadFunc: Deinterlacing failed\n");
                continue;
            }

            for(loop = 0; loop < outputLoopCount; loop++) {
                releaseFrames[0] = NULL;
                releaseList = &releaseFrames[0];
                if(loop != 0)
                    usleep(15000); // To distance the bottom field flip from the top field flip (15 ms is a safe value for both 50 Hz and 60 Hz)
                primaryVideo.pictureStructure = picStructure[loop];
                primaryVideo.previous2 = pOutputSurface[loop][DEINT_OUTPUT_PREV2];
                primaryVideo.previous = pOutputSurface[loop][DEINT_OUTPUT_PREV];
                primaryVideo.current = pOutputSurface[loop][DEINT_OUTPUT_CURR];
                primaryVideo.next = pOutputSurface[loop][DEINT_OUTPUT_NEXT];

                if (primaryVideo.current == NULL) {
                    LOG_WARN ("DisplayFrame: Skipping the first frame for Advanced-2 deinterlacing");
                    continue;
                }

                frameBuff = (FrameBuffer*)((primaryVideo.current)->tag);
                AddRef(ctx, frameBuff);

                ctx->displayIndex = (ctx->displayIndex + 1) % MAX_DISPLAYSURF;
                status = NvMediaVideoMixerRenderSurface(ctx->mixer,                 // mixer
                                                        ctx->displaySurf[ctx->displayIndex], // output surface
                                                        NULL,                       // background
                                                        &primaryVideo,              // primaryVideo
                                                        NULL,                       // secondaryVideo
                                                        NULL,                       // graphics0
                                                        NULL);                      // graphics1
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("DisplayFrame: NvMediaVideoMixerRenderSurface failed\n");
                    sStop = NVMEDIA_TRUE;
                }

                NvMediaVideoOutputFlip(ctx->videoOutput,
                                       ctx->displaySurf[ctx->displayIndex],
                                       NULL,
                                       NULL,
                                       releaseList,
                                       NULL);

                ReleaseFrame(ctx, primaryVideo.current);
            }
            if (pReleaseSurface)
                ReleaseFrame(ctx, pReleaseSurface->videoSurface);
        }

        if(ctx->fileDumpEnabled) {
            if(strstr(ctx->fname, "%d"))
                sprintf(fName_t, ctx->fname, f_cnt++);
            else
                sprintf(fName_t, "%s_%d", ctx->fname, f_cnt++);

            status = WriteFrame(fName_t, capSurf, NVMEDIA_TRUE, NVMEDIA_FALSE);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("CaptureThread: DumpFrame() failed in %sThread\n", ctx->ctxName);
                sStop = NVMEDIA_TRUE;
            }
        }

        if(ctx->checkCRC) {
            status = CheckSurfaceCrc(capSurf,
                                     ctx->inputWidth,
                                     ctx->inputHeight,
                                     NVMEDIA_FALSE,     // Monochrome flag
                                     ctx->crcChecksum,
                                     &crcMatchFlag);
            if(!crcMatchFlag) {
                LOG_ERR("CaptureThread: CRC error occurred in frame %u in %sThread\n", i, ctx->ctxName);
                sStop = NVMEDIA_TRUE;
            }
        }

        if(!ctx->displayEnabled)
            NvMediaVideoCaptureReturnFrame(ctx->capture, capSurf);

        if(ctx->timeNotCount)
            GetTimeUtil(&tStart);

        i++;
        NvSubTime(&tStop, &tStart, &tDiff);
    }

    LOG_DBG("CaptureThread: Exiting capture %sThread...\n", ctx->ctxName);
    do {
        status = DeinterlaceFlush(ctx->deinterlaceCtx, &pReleaseSurface);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("CaptureThread: Deinterlace flush failed\n");
            break;
        }

        if (pReleaseSurface)
            ReleaseFrame(ctx, pReleaseSurface->videoSurface);
    } while(pReleaseSurface);

    // Release any left-over frames
    if(ctx->displayEnabled && capSurf) {
        releaseFrames[0] = NULL;
        releaseList = &releaseFrames[0];

        status = NvMediaVideoOutputFlip(ctx->videoOutput,       // mixer
                                        NULL,                   // video surf
                                        NULL,                   // srcRect
                                        NULL,                   // dstRect
                                        releaseList,            // releaseList
                                        NULL);                  // timeStamp
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("CaptureThread: NvMediaVideoMixerRender failed in %sThread\n", ctx->ctxName);
        }
    }

    NvMediaVideoCaptureDebugGetStatus(ctx->capture, NVMEDIA_FALSE);
    NvSemaphoreIncrement(ctx->semDone);

    return 0;
}

static unsigned int
CaptureThreadFuncWithBuffer(void *params)
{
    int i = 0;
    unsigned int loop;
    NvMediaTime tStop = {0}, tStart = {0};
    NvS64 tDiff = 0;
    CaptureContext *ctx = (CaptureContext *)params;
    NvMediaVideoSurface *releaseFrames[4] = {NULL}, **releaseList = &releaseFrames[0];
    NvMediaVideoSurface *capSurf = NULL;
    NvMediaRect primarySrcRect;
    NvMediaPrimaryVideo primaryVideo;
    NvMediaBool crcMatchFlag;
    static int f_cnt = 0;
    char fName_t[50] = "";
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaVideoSurface *pOutputSurface[DEINT_LOOPS][DEINT_OUTPUT_SURFACES];
    NvU32 outputLoopCount;
    NvMediaPictureStructure picStructure[DEINT_LOOPS];
    FrameBuffer *frameBuff = NULL;
    FrameBuffer *pReleaseSurface = NULL;
    NvMediaBool available = NVMEDIA_FALSE;
    NvU32 timeout = 0xfff;
    NvU16 extraLines = ctx->extraLines;
    NvMediaVideoSurfaceAttributes surfaceAttributes;
    NvMediaVideoSurfaceMap surfaceMap;

    for (i = 0; i < MAX_FRAMES; i++) {
        ctx->frameBuffPool[i].refCount = 0;
    }

    primarySrcRect.x0 = 0;
    primarySrcRect.y0 = ctx->visibleLineStart;
    primarySrcRect.x1 = ctx->inputWidth;
    primarySrcRect.y1 = ctx->visibleLineEnd;

    memset(&primaryVideo, 0, sizeof(primaryVideo));
    primaryVideo.srcRect = &primarySrcRect;
    primaryVideo.dstRect = NULL;

    switch(ctx->surfaceType) {
        case NvMediaSurfaceType_Video_420:
        case NvMediaSurfaceType_VideoCapture_422:
        case NvMediaSurfaceType_VideoCapture_YUYV_422:
            extraLines = (extraLines * 2);
            surfaceAttributes.interlaced = NVMEDIA_TRUE;
            break;
        default:
            surfaceAttributes.interlaced = NVMEDIA_FALSE;
            break;
    }

    NvSemaphoreDecrement(ctx->semStart, NV_TIMEOUT_INFINITE);

    if(ctx->timeNotCount) {
        GetTimeUtil(&tStart);
        NvAddTime(&tStart, ctx->last * 1000000LL, &tStop);
    }
    capSurf = NvMediaVideoSurfaceCreateEx(ctx->device,                              // device
                                         ctx->surfaceType,                          // surf type
                                         ctx->capture->width,                       // surf width
                                         ctx->capture->height + extraLines,         // surf height
                                         NVMEDIA_SURFACE_CREATE_ATTRIBUTE_CAPTURE); // flags
    if(!capSurf) {
        LOG_ERR("%s: Failed to create frame : %d %d\n", __func__, ctx->capture->width, ctx->capture->height);
        goto loop_end;
    }

    status = NvMediaVideoCaptureFeedFrame(ctx->capture, capSurf, ctx->timeout);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to feed frame: %d\n", __func__, status);
        goto loop_end;
    }
    capSurf = NULL;

    NvSubTime(&tStop, &tStart, &tDiff);
    LOG_DBG("%s: Running capture %sThread...\n", __func__, ctx->ctxName);
    while((ctx->timeNotCount ? tDiff > 0 : ((ctx->last == -1) ? 1 : (i < ctx->last))) && !sStop) {
        capSurf = NvMediaVideoSurfaceCreateEx(ctx->device,          // device
                                             ctx->surfaceType,     // surf type
                                             ctx->capture->width,           // surf width
                                             ctx->capture->height + extraLines,         // surf height
                                             NVMEDIA_SURFACE_CREATE_ATTRIBUTE_CAPTURE); // flags
        if(!capSurf) {
            LOG_ERR("%s: Failed to create frame : %d %d\n", __func__, ctx->capture->width, ctx->capture->height);
            break;
        }

        status = NvMediaVideoCaptureFeedFrame(ctx->capture, capSurf, ctx->timeout);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to feed frame: %d\n", __func__, status);
            break;
        }
        capSurf = NULL;

        capSurf = NvMediaVideoCaptureGetFrame(ctx->capture, ctx->timeout);
        if(!capSurf) { // TBD
            LOG_ERR("%s: NvMediaVideoCaptureGetFrame() failed in %sThread\n", __func__, ctx->ctxName);
            usleep(1000000); // To allow a clean status dump
            sStop = NVMEDIA_TRUE;
            break;
        }

        status = NvMediaVideoSurfaceLock(capSurf, &surfaceMap);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to lock video surface: %d\n", __func__, status);
            break;
        }
        status = NvMediaVideoSurfaceSetAttributes(
                    capSurf,
                    NVMEDIA_SURFACE_ATTRIBUTE_INTERLACED,
                    &surfaceAttributes);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("NvMediaVideoSurfaceSetAttributes failed: %d", status);
            break;
        }
        NvMediaVideoSurfaceUnlock(capSurf);

        if(ctx->displayEnabled) {
            GetFrame(ctx, &frameBuff);
            if(!frameBuff) {
                LOG_ERR("%s: Buffers pool full.\n", __func__);
                sStop = NVMEDIA_TRUE;
                break;
            }

            frameBuff->topFieldFirstFlag = NVMEDIA_TRUE;
            frameBuff->progressiveFrameFlag = !ctx->deinterlaceEnabled;
            frameBuff->videoSurface = capSurf;
            capSurf->tag = frameBuff;

            AddRef(ctx, frameBuff);
            memset(pOutputSurface, 0, sizeof(NvMediaVideoSurface*) * DEINT_LOOPS * DEINT_OUTPUT_SURFACES);
            status = Deinterlace(ctx->deinterlaceCtx,
                                 frameBuff,
                                 &outputLoopCount,
                                 picStructure,
                                 pOutputSurface,
                                 &pReleaseSurface);
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Deinterlacing failed\n", __func__);
                continue;
            }

            for(loop = 0; loop < outputLoopCount; loop++) {
                releaseFrames[0] = NULL;
                releaseList = &releaseFrames[0];
                if(loop != 0)
                    usleep(15000); // To distance the bottom field flip from the top field flip (15 ms is a safe value for both 50 Hz and 60 Hz)
                primaryVideo.pictureStructure = picStructure[loop];
                primaryVideo.previous2 = pOutputSurface[loop][DEINT_OUTPUT_PREV2];
                primaryVideo.previous = pOutputSurface[loop][DEINT_OUTPUT_PREV];
                primaryVideo.current = pOutputSurface[loop][DEINT_OUTPUT_CURR];
                primaryVideo.next = pOutputSurface[loop][DEINT_OUTPUT_NEXT];

                if (primaryVideo.current == NULL) {
                    LOG_WARN ("DisplayFrame: Skipping the first frame for Advanced-2 deinterlacing");
                    continue;
                }

                frameBuff = (FrameBuffer*)((primaryVideo.current)->tag);
                AddRef(ctx, frameBuff);
                ctx->displayIndex = (ctx->displayIndex + 1) % MAX_DISPLAYSURF;
                status = NvMediaVideoMixerRenderSurface(ctx->mixer,                          // mixer
                                                        ctx->displaySurf[ctx->displayIndex], // output surface
                                                        NULL,                                // background
                                                        &primaryVideo,                       // primaryVideo
                                                        NULL,                                // secondaryVideo
                                                        NULL,                                // graphics0
                                                        NULL);                               // graphics1
                if (status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("DisplayFrame: NvMediaVideoMixerRenderSurface failed\n");
                    sStop = NVMEDIA_TRUE;
                }

                NvMediaVideoOutputFlip(ctx->videoOutput,
                                       ctx->displaySurf[ctx->displayIndex],
                                       NULL,
                                       NULL,
                                       releaseList,
                                       NULL);
            }

            ReleaseFrame(ctx, primaryVideo.current);

            if (pReleaseSurface)
                ReleaseFrame(ctx, pReleaseSurface->videoSurface);
        }

        if(ctx->fileDumpEnabled) {
            if(strstr(ctx->fname, "%d"))
                sprintf(fName_t, ctx->fname, f_cnt++);
            else
                sprintf(fName_t, "%s_%d", ctx->fname, f_cnt++);

            status = WriteFrame(fName_t, capSurf, NVMEDIA_TRUE, NVMEDIA_FALSE);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: DumpFrame() failed in %sThread\n", __func__, ctx->ctxName);
                sStop = NVMEDIA_TRUE;
            }
        }

        if(ctx->checkCRC) {
            status = CheckSurfaceCrc(capSurf,
                                     ctx->inputWidth,
                                     ctx->inputHeight,
                                     NVMEDIA_FALSE,     // Monochrome flag
                                     ctx->crcChecksum,
                                     &crcMatchFlag);
            if(!crcMatchFlag) {
                LOG_ERR("%s: CRC error occurred in frame %u in %sThread\n", __func__, i, ctx->ctxName);
                sStop = NVMEDIA_TRUE;
            }
        }

        if(!ctx->displayEnabled)
            NvMediaVideoSurfaceDestroy(capSurf);

        if(ctx->timeNotCount)
            GetTimeUtil(&tStart);

        i++;
        NvSubTime(&tStop, &tStart, &tDiff);
    }

    LOG_DBG("%s: Exiting capture %sThread...\n", __func__, ctx->ctxName);
    do {
        status = DeinterlaceFlush(ctx->deinterlaceCtx, &pReleaseSurface);
        if (status != NVMEDIA_STATUS_OK) {
            LOG_ERR("CaptureThread: Deinterlace flush failed\n");
            break;
        }

        if (pReleaseSurface)
            ReleaseFrame(ctx, pReleaseSurface->videoSurface);
    } while(pReleaseSurface);

    // Release any left-over frames
    if(ctx->displayEnabled && capSurf) {
        releaseFrames[0] = NULL;
        releaseList = &releaseFrames[0];
        status = NvMediaVideoMixerRender(ctx->mixer,     // mixer
                                         NVMEDIA_OUTPUT_DEVICE_0, // outputDeviceMask
                                         NULL,           // background
                                         NULL,           // primaryVideo
                                         NULL,           // secondaryVideo
                                         NULL,           // graphics0
                                         NULL,           // graphics1
                                         releaseList,    // releaseList
                                         NULL);          // timeStamp
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: NvMediaVideoMixerRender failed in %sThread\n", __func__, ctx->ctxName);
        }
        while (*releaseList) {
            ReleaseFrame(ctx, *releaseList++);
        }
    }

loop_end:
    // Release the extra frames pushed to capture pool at the beginning
    while((!available) && timeout--) {
        NvMediaVideoCaptureCheckAvailableFrame(ctx->capture, &available);
        nanosleep((struct timespec[]){{0, 50000}}, NULL);
    }

    capSurf = NULL;
    capSurf = NvMediaVideoCaptureGetFrame(ctx->capture, 0);
    if(!capSurf) {
        NvMediaVideoSurfaceDestroy(capSurf);
    }

    NvMediaVideoCaptureDebugGetStatus(ctx->capture, NVMEDIA_FALSE);
    NvSemaphoreIncrement(ctx->semDone);

    return 0;
}

static void SignalHandler(int signal)
{
    sStop = NVMEDIA_TRUE;
    LOG_DBG("SignalHandler: %d signal received\n", signal);
}

static int
InitMixer(
    TestArgs *testArgs,
    CaptureContext *captureCtx)
{
    NvMediaVideoOutput *nullOutputList[1] = {NULL};
    unsigned int features = 0;
    NvMediaVideoMixerAttributes mixerAttributes;

    memset(&mixerAttributes, 0, sizeof(mixerAttributes));
    switch(testArgs->csiDeinterlaceType) {
        case 0: /* Deinterlace Off/Weave */
            break;
        case 1: /* Deinterlace BOB */
            features |= NVMEDIA_VIDEO_MIXER_FEATURE_PRIMARY_VIDEO_DEINTERLACING;
            break;
        case 2: /* Deinterlace Advanced, Frame Rate */
        case 3: /* Deinterlace Advanced, Field Rate */
            switch(testArgs->csiDeinterlaceAlgo) {
                case 1:
                default:
                    features |= NVMEDIA_VIDEO_MIXER_FEATURE_ADVANCED1_PRIMARY_DEINTERLACING;
                    mixerAttributes.primaryDeinterlaceType = NVMEDIA_DEINTERLACE_TYPE_ADVANCED1;
                    break;
                case 2:
                    features |= NVMEDIA_VIDEO_MIXER_FEATURE_ADVANCED2_PRIMARY_DEINTERLACING;
                    mixerAttributes.primaryDeinterlaceType = NVMEDIA_DEINTERLACE_TYPE_ADVANCED2;
                    break;
            }
            break;
        default:
            LOG_ERR("DisplayInit: Invalid deinterlace mode\n");
            return NVMEDIA_FALSE;
    }

    if(testArgs->csiInverceTelecine) {
        features |= NVMEDIA_VIDEO_MIXER_FEATURE_INVERSE_TELECINE;
        mixerAttributes.inverseTelecine = NVMEDIA_TRUE;
    }

    switch(testArgs->csiSurfaceFormat) {
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_UV_420_I:
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_420:
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_420:
            break;
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_YUYV_422_I:
            features |= NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_YUYV_I;
            break;
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_Y_V_U_422:
            features |= NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_YV16X2; // TBD***
            break;
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_Y_V_U_422:
            features |= NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_YV16;
            break;
        case NVMEDIA_VIDEO_CAPTURE_SURFACE_FORMAT_TYPE_R8G8B8A8:
            features |= NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_R8G8B8A8;
            break;
        default: // To eliminate warning-turrned-error - TBD
            LOG_ERR("SetCSIMixerFeatures: Unsupported CSI surface format was encountered (%d)\n", testArgs->csiSurfaceFormat);
            return -1;
    }
    if(testArgs->csiInputFormat != NVMEDIA_VIDEO_CAPTURE_INPUT_FORMAT_TYPE_RGB888 &&
        testArgs->outputType != NvMediaVideoOutputType_OverlayYUV) {
        features |= NVMEDIA_VIDEO_MIXER_FEATURE_DVD_MIXING_MODE;
    }

    captureCtx->mixer = NvMediaVideoMixerCreate(captureCtx->device,    // device
                                                testArgs->mixerWidth,  // mixerWidth
                                                testArgs->mixerHeight, // mixerHeight
                                                testArgs->aspectRatio, // sourceAspectRatio
                                                testArgs->inputWidth,  // primaryVideoWidth
                                                testArgs->inputHeight, // primaryVideoHeight
                                                0,                     // secondaryVideoWidth
                                                0,                     // secondaryVideoHeight
                                                0,                     // graphics0Width
                                                0,                     // graphics0Height
                                                0,                     // graphics1Width
                                                0,                     // graphics1Height
                                                features,              // features
                                                nullOutputList);       // outputList
    if(!captureCtx->mixer) {
        LOG_ERR("InitCSICaptureContext: NvMediaVideoMixerCreate() failed for csiMixer\n");
        return -1;
    }

    NvMediaVideoMixerSetAttributes(captureCtx->mixer,
                                   NVMEDIA_OUTPUT_DEVICE_0,    // outputMask
                                   NVMEDIA_VIDEO_MIXER_ATTRIBUTE_DEINTERLACE_TYPE_PRIMARY |
                                   NVMEDIA_VIDEO_MIXER_ATTRIBUTE_INVERSE_TELECINE,
                                   &mixerAttributes);

    return 0;
}

static int
InitCSICaptureContext(
        TestArgs *testArgs,
        CaptureContext *captureCtx)
{
    NvMediaVideoCaptureSettings settings;
    CaptureInputConfigParams inputParams;
    EDeinterlaceMode eDeinterlacingMode;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    int i = 0;

    captureCtx->device = NvMediaDeviceCreate();
    if(!captureCtx->device) {
        LOG_ERR("InitCSICaptureContext: NvMediaDeviceCreate() failed\n");
        return -1;
    }

    if(InitMixer(testArgs, captureCtx)) {
        LOG_ERR("InitCSICaptureContext: InitMixer() failed\n");
        return -1;
    }

    // Create stream start/done semaphores
    status = NvSemaphoreCreate(&captureCtx->semStart, 0, 1);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("InitCSICaptureContext: NvSemaphoreCreate() failed for start semaphore\n");
        return -1;
    }

    status = NvSemaphoreCreate(&captureCtx->semDone, 0, 1);
    if(status != NVMEDIA_STATUS_OK) {
        LOG_ERR("InitCSICaptureContext: NvSemaphoreCreate() failed for start semaphore\n");
        return -1;
    }

    // zero-initialize structures
    memset(&settings, 0, sizeof(NvMediaVideoCaptureSettings));
    memset(&inputParams, 0, sizeof(CaptureInputConfigParams));

    settings.interfaceType       = testArgs->csiPortInUse;
    settings.inputFormatType     = testArgs->csiInputFormat;
    settings.surfaceFormatType   = testArgs->csiSurfaceFormat;
    settings.width               = testArgs->inputWidth;
    settings.height              = testArgs->inputHeight;
    settings.startX              = 0;
    settings.startY              = 0;
    settings.extraLines          = testArgs->csiExtraLines;
    settings.interlace           = testArgs->csiCaptureInterlaced;
    settings.interfaceLanes      = testArgs->csiInterfaceLaneCount;
    settings.interlacedExtraLinesDelta = (testArgs->captureDeviceInUse == AnalogDevices_ADV7281 ||
                                          testArgs->captureDeviceInUse == AnalogDevices_ADV7282 ||
                                          testArgs->captureDeviceInUse == AnalogDevices_ADV7481C) &&
                                          testArgs->csiCaptureInterlaced ? 1 : 0;
    settings.externalBuffer = testArgs->externalBuffer;

    // Configure the board with CSI settings
    switch(testArgs->boardType) {
        case BOARD_TYPE_E1861:
            if(!testutil_board_detect(BOARD_TYPE_E1861, BOARD_VERSION_A02))
                testutil_board_module_workaround(BOARD_TYPE_E1861, BOARD_VERSION_A02, MODULE_TYPE_NONE);
            else if(!testutil_board_detect(BOARD_TYPE_E1861, BOARD_VERSION_NONE) &&
                (testArgs->captureDeviceInUse == AnalogDevices_ADV7281 || testArgs->captureDeviceInUse == AnalogDevices_ADV7282))
                testutil_board_module_workaround(BOARD_TYPE_E1861, BOARD_VERSION_A01, MODULE_TYPE_NONE);
            break;
        case BOARD_TYPE_E1611:
            if(!testutil_board_detect(BOARD_TYPE_E1611, BOARD_VERSION_A04))
                testutil_board_module_workaround(BOARD_TYPE_E1611, BOARD_VERSION_A04, MODULE_TYPE_NONE);
            break;
        case BOARD_TYPE_PM358:
            if(!testutil_board_detect(BOARD_TYPE_PM358, BOARD_VERSION_B00))
                testutil_board_module_workaround(BOARD_TYPE_PM358, BOARD_VERSION_B00, MODULE_TYPE_NONE);
        default:
            break;
    }

    inputParams.width = testArgs->inputWidth;
    inputParams.height = testArgs->inputHeight;

    switch(testArgs->captureDeviceInUse) {
        case AnalogDevices_ADV7281:
        case AnalogDevices_ADV7282:
        case AnalogDevices_ADV7481C:
            inputParams.cvbs2csi.std = testArgs->inputVideoStd;
            inputParams.input = CVBS;
            break;
        case Toshiba_TC358743:
        case Toshiba_TC358791:
            inputParams.hdmi2csi.lanes = testArgs->csiInterfaceLaneCount;
            inputParams.hdmi2csi.format = testArgs->csiInputFormat;
            inputParams.hdmi2csi.structure = testArgs->csiCaptureInterlaced;
            inputParams.input = HDMI;
            break;
        case Toshiba_TC358791_CVBS:
            inputParams.cvbs2csi.std = testArgs->inputVideoStd;
            inputParams.cvbs2csi.structure = testArgs->csiCaptureInterlaced;
            inputParams.input = CVBS;
            break;
        case NationalSemi_DS90UR910Q:
        case TI_DS90UH940:
            inputParams.hdmi2csi.lanes = testArgs->csiInterfaceLaneCount;
            inputParams.hdmi2csi.format = testArgs->csiInputFormat;
            inputParams.input = HDMI;
            break;
        case AnalogDevices_ADV7481H:
        case CapureInputDevice_NULL:
            inputParams.hdmi2csi.lanes = testArgs->csiInterfaceLaneCount;
            inputParams.hdmi2csi.format = testArgs->csiInputFormat;
            inputParams.input = HDMI;
            break;
        default:
            LOG_ERR("InitCSICaptureContext: Bad CSI device\n");
            return -1;
    }

    if(testutil_capture_input_open(testArgs->i2cDevice,
                                   testArgs->captureDeviceInUse,
                                   testArgs->isLiveMode,
                                   &captureCtx->handle) < 0) {
        LOG_ERR("InitCSICaptureContext: Failed to open CSI device\n");
        return -1;
    }

    captureCtx->capture = NvMediaVideoCaptureCreate(NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_CSI, // interfaceFormat
                                                    &settings,                                  // settings
                                                    BUFFER_SIZE);                               // numBuffers
    if(!captureCtx->capture) {
        LOG_ERR("InitCSICaptureContext: NvMediaVideoCaptureCreate() failed\n");
        return -1;
    }

    NvMediaVideoCaptureDebugGetStatus(captureCtx->capture, NVMEDIA_FALSE);

    if(testutil_capture_input_configure(captureCtx->handle, &inputParams) < 0) {
        LOG_ERR("InitCSICaptureContext: Failed to configure CSI device input\n");
        return -1;
    }

    if(testutil_capture_input_start(captureCtx->handle) < 0) {
        LOG_ERR("InitCSICaptureContext: Failed to start CSI device input\n");
        return -1;
    }

    // Create NvMedia mixer(s) and output(s) and bind them
    if(testArgs->displayEnabled) {
        captureCtx->displayIndex = 0;
        for(i=0; i<MAX_DISPLAYSURF; i++) {
            captureCtx->displaySurf[i] = NvMediaVideoSurfaceCreate(captureCtx->device,
                                                                NvMediaSurfaceType_R8G8B8A8,
                                                                captureCtx->capture->width,
                                                                captureCtx->capture->height);
        }

        captureCtx->videoOutput = NvMediaVideoOutputCreate((NvMediaVideoOutputType)0,                   // outputType
                                                           (NvMediaVideoOutputDevice)0,                 // outputDevice
                                                           NULL,                                        // outputPreference
                                                           testArgs->displaysList.isEnabled,            // alreadyCreated
                                                           testArgs->displaysList.displayId,            // displayId
                                                           testArgs->displaysList.windowId,             // windowId
                                                           NULL);                                       // displayHandle
        if(captureCtx->videoOutput) {
            if(testArgs->displaysList.isPositionSpecified) {
                status = NvMediaVideoOutputSetPosition(captureCtx->videoOutput, &testArgs->displaysList.position);
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("Unable to set display window position\n");
                    return -1;
                }
            }

            status = NvMediaVideoOutputSetDepth(captureCtx->videoOutput, testArgs->displaysList.depth);
            if (status != NVMEDIA_STATUS_OK) {
                LOG_ERR("Unable to set display window depth\n");
                return -1;
            }

        } else {
            LOG_ERR("main: NvMediaVideoOutputCreate() failed for videoOutput\n");
            return -1;
        }
    }

    captureCtx->ctxName                = "csi";
    captureCtx->deinterlaceEnabled  = testArgs->csiDeinterlaceEnabled;
    captureCtx->deinterlaceType     = testArgs->csiDeinterlaceType;
    captureCtx->deinterlaceAlgo     = testArgs->csiDeinterlaceAlgo;
    captureCtx->inverceTelecine     = testArgs->csiInverceTelecine;
    captureCtx->inputWidth          = testArgs->inputWidth;
    captureCtx->inputHeight         = testArgs->inputHeight;
    captureCtx->extraLines          = testArgs->csiExtraLines;
    captureCtx->surfaceType         = testArgs->outputSurfaceType;
    captureCtx->timeout             = testArgs->timeout;
    captureCtx->displayEnabled      = testArgs->displayEnabled;
    /* Scanning NTSC and PAL analog signal should contain invisible pixel data and
     * they'll be converted and embedded into the digital signal by CSI Tx device.
     * To show only the visible pixels, it needs to crop the input buffer to the mixer,
     * and visibleLineStart & visibleLineEnd give the start & end lines of
     * the visible window for that purpose.
     */
    captureCtx->visibleLineStart = (testArgs->captureDeviceInUse == AnalogDevices_ADV7281 ||
                                    testArgs->captureDeviceInUse == AnalogDevices_ADV7282 ||
                                    testArgs->captureDeviceInUse == AnalogDevices_ADV7481C) ?
                                       (testArgs->inputVideoStd == NVMEDIA_VIDEO_CAPTURE_INTERFACE_FORMAT_VIP_NTSC ? 24 : 2) : 0;
    captureCtx->visibleLineEnd = captureCtx->inputHeight -
                                 ((testArgs->captureDeviceInUse == AnalogDevices_ADV7281 ||
                                   testArgs->captureDeviceInUse == AnalogDevices_ADV7282 ||
                                   testArgs->captureDeviceInUse == AnalogDevices_ADV7481C) ? 2 : 0);
    captureCtx->fileDumpEnabled     = testArgs->fileDumpEnabled;
    captureCtx->fname               = testArgs->outputFileName;
    captureCtx->checkCRC            = testArgs->checkCRC;
    captureCtx->crcChecksum         = testArgs->crcChecksum;
    if(testArgs->captureTime) {
        captureCtx->timeNotCount    = NVMEDIA_TRUE;
        captureCtx->last            = testArgs->captureTime;
    } else {
        captureCtx->timeNotCount    = NVMEDIA_FALSE;
        captureCtx->last            = testArgs->captureCount;
    }

    switch(captureCtx->deinterlaceType) {
        case 0: /* Deinterlace Off/Weave */
            eDeinterlacingMode = DEINTERLACE_MODE_WEAVE;
            break;
        case 1: /* Deinterlace BOB */
            eDeinterlacingMode = DEINTERLACE_MODE_BOB;
            break;
        case 2: /* Deinterlace Advanced, Frame Rate */
            eDeinterlacingMode = DEINTERLACE_MODE_ADVANCED_FRAMERATE;
            break;
        case 3: /* Deinterlace Advanced, Field Rate */
            eDeinterlacingMode = DEINTERLACE_MODE_ADVANCED_FIELDRATE;
            break;
        default:
            LOG_ERR("Init: Invalid deinterlace mode\n");
            return -1;
    }

    DeinterlaceInit(&captureCtx->deinterlaceCtx, eDeinterlacingMode);

    return 0;
}

static void
FiniCaptureContext(
        CaptureContext *captureCtx)
{
    int i = 0;
    // Destroy thread and stream start/done semaphores
    if(captureCtx->captureThread)
        NvThreadDestroy(captureCtx->captureThread);

    if(captureCtx->semDone)
        NvSemaphoreDestroy(captureCtx->semDone);

    if(captureCtx->semStart)
        NvSemaphoreDestroy(captureCtx->semStart);

    // Unbind NvMedia mixer and output and destroy them
    if(captureCtx->videoOutput) {
        NvMediaVideoOutputDestroy(captureCtx->videoOutput);
    }

    for(i=0; i<MAX_DISPLAYSURF; i++) {
        if(captureCtx->displaySurf[i]) {
            NvMediaVideoSurfaceDestroy(captureCtx->displaySurf[i]);
            captureCtx->displaySurf[i] = NULL;
        }
    }

    if(captureCtx->mixer)
        NvMediaVideoMixerDestroy(captureCtx->mixer);

    // Destroy NvMedia device
    if(captureCtx->device)
        NvMediaDeviceDestroy(captureCtx->device);

    // Destroy NvMedia capture
    NvMediaVideoCaptureDestroy(captureCtx->capture);

    // Stop capture device
    testutil_capture_input_stop(captureCtx->handle);

    testutil_capture_input_close(captureCtx->handle);
}

int main(int argc, char *argv[])
{
    CaptureContext captureCtx;
    TestArgs testArgs;
    NvMediaStatus status;
    int result;

    signal(SIGINT, SignalHandler);

    memset(&captureCtx, 0, sizeof(CaptureContext));
    memset(&testArgs, 0, sizeof(TestArgs));
    result = ParseArgs(argc, argv, &testArgs);
    if(result) {
        if (result == -1)
            PrintUsage();
        return -1;
    }

    PrintOptions(&testArgs);

    // Success result value will be set at the end if no failure will occur
    result = -1;

    // Init video capture context
    if(testArgs.captureType == CAPTURE_CSI) {
        if(InitCSICaptureContext(&testArgs, &captureCtx)) {
            LOG_ERR("main: Failed creating CSI video capture context\n");
            goto done;
        }
    } else {
        LOG_ERR("main: Only CSI capture is supported by the capture test\n");
        goto done;
    }

    // Create Capture thread
    if(!testArgs.externalBuffer) {
        status = NvThreadCreate(&captureCtx.captureThread, CaptureThreadFunc, &captureCtx, NV_THREAD_PRIORITY_NORMAL);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: NvThreadCreate() failed\n");
            goto done;
        }
    } else {
        captureCtx.externalBuffer = testArgs.externalBuffer;
        status = NvThreadCreate(&captureCtx.captureThread, CaptureThreadFuncWithBuffer, &captureCtx, NV_THREAD_PRIORITY_NORMAL);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: NvThreadCreate() failed\n");
            goto done;
        }
    }

    // Kickoff
    NvMediaVideoCaptureStart(captureCtx.capture);
    NvSemaphoreIncrement(captureCtx.semStart);

    // Wait for completion
    NvSemaphoreDecrement(captureCtx.semDone, NV_TIMEOUT_INFINITE);
    result = 0;

done:
    FiniCaptureContext(&captureCtx);

    return result;
}
