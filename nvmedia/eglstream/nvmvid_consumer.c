/*
 * nvmvideo_consumer.c
 *
 * Copyright (c) 2013-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple video consumer rendering sample app
//

#include "nvm_consumer.h"
#include "log_utils.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

static NvU32
procThreadFunc (
    void *data)
{
    test_nvmedia_consumer_display_s *display = (test_nvmedia_consumer_display_s *)data;
    NvMediaTime timeStamp;
    NvMediaVideoSurface *surface = NULL;
    NvMediaStatus status;

    if(!display) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return 0;
    }

    LOG_DBG("NVMedia video consumer thread is active\n");
    while(!display->quit) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                display->eglDisplay,
                display->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Nvmedia video consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            LOG_DBG("Nvmedia video Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
            display->quit = NV_TRUE;
            goto done;
        }

        //! [docs_eglstream:consumer_acquires_frame]
        if(NVMEDIA_STATUS_ERROR ==
            NvMediaEglStreamConsumerAcquireSurface(display->consumer, &surface, 16, &timeStamp)) {
            LOG_DBG("Nvmedia video Consumer: - surface acquire failed\n");
            display->quit = NV_TRUE;
            goto done;
        }
        //! [docs_eglstream:consumer_acquires_frame]

        if (surface)
        {
           NvMediaPrimaryVideo displayVideo;
           NvMediaRect displayVideoSourceRect = { 0, 0, surface->width, surface->height };
           NvMediaVideoSurface *releaseOutputFrames[3] = { NULL, NULL, NULL }, **outputReleaseList = &releaseOutputFrames[0];
           if (!display->outputMixer) {
              LOG_DBG("Video Consumer: Output Mixer create\n");
              display->outputMixer = NvMediaVideoMixerCreate(
                        display->device,                                 // device,
                        surface->width,                                  // mixerWidth
                        surface->height,                                 // mixerHeight
                        (float)surface->width / (float)surface->height,  // sourceAspectRatio
                        surface->width,                                  // primaryVideoWidth
                        surface->height,                                 // primaryVideoHeight
                        0,                                               // secondaryVideoWidth
                        0,                                               // secondaryVideoHeight
                        0,                                               // graphics0Width
                        0,                                               // graphics0Height
                        0,                                               // graphics1Width
                        0,                                               // graphics1Height
                        (display->surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin ||
                        display->surfaceType == NvMediaSurfaceType_Image_RGBA)?
                        NVMEDIA_VIDEO_MIXER_FEATURE_VIDEO_SURFACE_TYPE_R8G8B8A8 : 0,
                        display->outputList);
              if(!display->outputMixer) {
                 LOG_ERR("video_display_init: Unable to create output mixer\n");
                 goto done;
              }
           }

           if (display->encodeEnable) {
              if (!display->inputParams.h264Encoder) {
                 LOG_DBG("Nvmedia video consumer - InitEncoder\n");
                 if(InitEncoder(&display->inputParams, surface->width, surface->height, display->surfaceType)) {
                    LOG_ERR("Nvmedia video consumer: InitEncoder failed \n");
                    goto done;;
                 }
              }
           }
           memset(&displayVideo, 0, sizeof(NvMediaPrimaryVideo));
           displayVideo.pictureStructure = NVMEDIA_PICTURE_STRUCTURE_FRAME;
           displayVideo.current = surface;
           displayVideo.srcRect = &displayVideoSourceRect;
           // Display acquired surface
           status = NvMediaVideoMixerRender(
                    display->outputMixer,    // mixer
                    NVMEDIA_OUTPUT_DEVICE_0, // outputDeviceMask
                    NULL,                    // background
                    &displayVideo,           // primaryVideo
                    NULL,                    // secondaryVideo
                    NULL,                    // graphics0
                    NULL,                    // graphics1
                    outputReleaseList,       // release list
                    &timeStamp);             // timeStamp
           if(status != NVMEDIA_STATUS_OK) {
               LOG_ERR("main: NvMediaVideoMixerRender failed\n");
           }

           if (display->encodeEnable) {
               NvMediaRect sourceRect;
               if(surface->type == NvMediaSurfaceType_R8G8B8A8_BottomOrigin) {
                  sourceRect.x0 = 0;
                  sourceRect.y0 = surface->height;
                  sourceRect.x1 = surface->width;
                  sourceRect.y1 = 0;
               } else {
                    sourceRect.x0 = 0;
                    sourceRect.y0 = 0;
                    sourceRect.x1 = surface->width;
                    sourceRect.y1 = surface->height;
               }

               if(EncodeOneFrame(&display->inputParams, surface, &sourceRect)){
                   LOG_ERR("main: Encode frame %d fails \n", display->inputParams.uFrameCounter);
                   goto done;
               }
               display->inputParams.uFrameCounter++;
           }

           NvMediaEglStreamConsumerReleaseSurface(display->consumer, surface);
       }
    }

done:
    display->procThreadExited = NV_TRUE;
    *display->consumerDone = NV_TRUE;
    return 0;
}

int video_display_init(volatile NvBool *consumerDone,
                       test_nvmedia_consumer_display_s *display,
                       EGLDisplay eglDisplay, EGLStreamKHR eglStream,
                       TestArgs *args)
{
    NvMediaBool deviceEnabled = NVMEDIA_FALSE;
    NvMediaStatus rt;

    display->consumerDone = consumerDone;
    display->eglDisplay = eglDisplay;
    display->eglStream = eglStream;
    LOG_DBG("video_display_init: NvMediaDeviceCreate\n");
    display->device = NvMediaDeviceCreate();
    if (!display->device) {
        LOG_DBG("video_display_init: Unable to create device\n");
        return NV_FALSE;
    }

    display->surfaceType = args->consSurfaceType;

    // Check that the device is enabled (initialized)
    rt = CheckDisplayDeviceID(args->displayId, &deviceEnabled);
    if (rt != NVMEDIA_STATUS_OK) {
        LOG_ERR("Err: Chosen display (%d) not available\n", args->displayId);
        return 0;
    }

    LOG_DBG("video_display_init: Output create 0\n");
    display->outputList[0] = NvMediaVideoOutputCreate(
        (NvMediaVideoOutputType)0,            // outputType
        (NvMediaVideoOutputDevice)0,          // outputDevice
        NULL,                                 // outputPreference
        deviceEnabled,                        // alreadyCreated
        args->displayId,                      // displayId
        args->windowId,                       // windowId
        NULL);                                // displayHandle
    if(!display->outputList[0]) {
        LOG_ERR("Unable to create output\n");
        return NV_FALSE;
    }
    display->outputList[1] = NULL;

    LOG_DBG("video_display_init: Output create done: %p\n", display->outputList[0]);

    display->consumer = NvMediaEglStreamConsumerCreate(
        display->device,
        display->eglDisplay,
        display->eglStream,
        args->consSurfaceType);
    if(!display->consumer) {
        LOG_DBG("video_display_init: Unable to create consumer\n");
        return NV_FALSE;
    }

    display->encodeEnable = args->nvmediaEncoder;

    if (display->encodeEnable) {
       memset(&display->inputParams, 0, sizeof(InputParameters));
       display->inputParams.outputFile = fopen(args->outfile, "w+");
       if(!(display->inputParams.outputFile)) {
            LOG_ERR("Error opening '%s' for encoder writing\n", args->outfile);
            return NV_FALSE;
       }
    }
    if(IsFailed(NvThreadCreate(&display->procThread, &procThreadFunc, (void *)display, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("Nvmedia video consumer init: Unable to create process thread\n");
        display->procThreadExited = NV_TRUE;
        return NV_FALSE;
    }

    return NV_TRUE;
}

void video_display_Deinit(test_nvmedia_consumer_display_s *display)
{
    NvMediaVideoOutput **outputList = &display->outputList[0];
    NvMediaVideoSurface *releaseFrames[9], **releaseList = &releaseFrames[0];

    display->quit = NV_TRUE;

    if (display->outputMixer)
        NvMediaVideoMixerRender(display->outputMixer,    // mixer
                                NVMEDIA_OUTPUT_DEVICE_0, // outputMask
                                NULL,                    // background
                                NULL,                    // primaryVideo
                                NULL,                    // secondaryVideo
                                NULL,                    // graphics0
                                NULL,                    // graphics1
                                releaseList,
                                NULL);                   // timeStamp

    if(display->inputParams.outputFile)
        fclose(display->inputParams.outputFile);
    if(display->inputParams.h264Encoder)
        NvMediaVideoEncoderDestroy(display->inputParams.h264Encoder);
    if(display->inputParams.encodeConfigH264Params.h264VUIParameters) {
        free(display->inputParams.encodeConfigH264Params.h264VUIParameters);
        display->inputParams.encodeConfigH264Params.h264VUIParameters = NULL;
    }

    LOG_DBG("video_display_Deinit: Output Mixer destroy\n");
    if(display && display->outputMixer) {
        NvMediaVideoMixerDestroy(display->outputMixer);
        display->outputMixer = NULL;
    }

    LOG_DBG("video_display_Deinit: Output list destroy\n");
    while(*outputList) {
        NvMediaVideoOutputDestroy(*outputList++);
    }

    if(display->consumer) {
        NvMediaEglStreamConsumerDestroy(display->consumer);
        display->consumer = NULL;
    }
    LOG_DBG("video_display_Deinit: consumer destroy\n");

    if(display->device) {
        NvMediaDeviceDestroy(display->device);
        display->device = NULL;
    }
}

void video_display_Stop(test_nvmedia_consumer_display_s *display) {
    display->quit = 1;
    if(display->procThread) {
        LOG_DBG("wait for nvmedia video consumer thread exit\n");
        NvThreadDestroy(display->procThread);
    }
}

void video_display_Flush(test_nvmedia_consumer_display_s *display) {
    NvMediaTime timeStamp;
    NvMediaVideoSurface *surface = NULL;
    EGLint streamState = 0;

    do {
        if(!eglQueryStreamKHR(
                display->eglDisplay,
                display->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Nvmedia video consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            break;
        }

        NvMediaEglStreamConsumerAcquireSurface(display->consumer, &surface, 16, &timeStamp);
        if(surface) {
            LOG_DBG("%s: EGL Consumer: Release surface %p (display->consumer)\n", __func__, surface);
            NvMediaEglStreamConsumerReleaseSurface(display->consumer, surface);
        }
    } while(surface);
}
