/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "nvcommon.h"
#include "egl_utils.h"
#include "eglstrm_setup.h"
#include "img_producer.h"
#include "cuda_consumer.h"

#include "cmdline.h"
#include "log_utils.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif
NvBool signal_stop = 0;

static void
sig_handler(int sig)
{
    signal_stop = NV_TRUE;
    LOG_DBG("Signal: %d\n", sig);
}

int main(int argc, char **argv)
{
    TestArgs args;
    EglUtilState *eglUtil = NULL;
    EglStreamClient *streamClient = NULL;
    volatile NvBool producerDone = 0;
    volatile NvBool consumerDone = 0;
    ImageProducerCtx imgProducer;
    CudaConsumerCtx  cudaConsumer;

    int res = 1;

    /* Hook up Ctrl-C handler */
    signal(SIGINT, sig_handler);

    memset(&args, 0, sizeof(TestArgs));
    if (MainParseArgs(argc, argv, &args)) {
        return res;
    }


    LOG_DBG("eglutil init\n");
    eglUtil = EGLUtilInit(NULL);
    if (!eglUtil) {
        LOG_ERR("failed to initialize egl \n");
        return 1;
    }

    if(EGLUtilInit_dGPU(eglUtil)){
        LOG_ERR("Failed in EGLUtilInit_dGPU \n");
        return 1;
    }

    /* Setting single proc/cross proc */
    if (args.isProdCrossProc) {
        streamClient = EGLStreamProducerProcInit(eglUtil->display,
                                                 args.fifoMode);
        if (!streamClient) {
            LOG_ERR("%s: failed to init EGLStream client in producer process\n", __func__);
            goto fail;
        }
    } else if (args.isConsCrossProc) {
        streamClient = EGLStreamConsumerProcInit(args.isConsumerondGPU ? eglUtil->display_dGPU : eglUtil->display,
                                                 args.fifoMode);
        if (!streamClient) {
            LOG_ERR("%s: failed to init EGLStream client in consumer process\n", __func__);
            goto fail;
        }

    } else {
        streamClient = EGLStreamSingleProcInit(eglUtil->display,
                                               args.fifoMode,
                                               eglUtil->display_dGPU,
                                               args.isConsumerondGPU);
        if (!streamClient) {
            LOG_ERR("%s: failed to init EGLStream client\n", __func__);
            goto fail;
        }
    }

    /* Init Consumer */
    if (args.isConsCrossProc || !args.isProdCrossProc) {
        switch(args.consumer) {
            case EGLSTREAM_CUDA:
                if(CudaConsumerInit(&consumerDone,
                                    &cudaConsumer,
                                    streamClient,
                                    &args)) {
                   LOG_ERR("%s: CudaConsumerInit failed\n", __func__);
                   goto fail;
                }
                break;
            default:
                break;
        }
    }

    /* Init Producer */
    if (args.isProdCrossProc || !args.isConsCrossProc) {
        EGLint streamState = 0;
        /*Wait till consumer is connected*/
        while(!signal_stop && streamState != EGL_STREAM_STATE_CONNECTING_KHR) {
            if(!eglQueryStreamKHR(streamClient->display,
                                  streamClient->eglStream,
                                  EGL_STREAM_STATE_KHR,
                                  &streamState)) {
                LOG_ERR("main: EGLStream failed to connect \n");
                goto fail;
            }
        }
        switch(args.producer) {
            case EGLSTREAM_NVMEDIA_IMAGE:
                if (ImageProducerInit(&producerDone,
                                      &imgProducer,
                                      streamClient,
                                      &args)) {
                    LOG_ERR("%s: ImageProducerInit failed\n", __func__);
                    goto fail;
                }
                break;
            default:
                break;
        }
    }

    /* wait for signal_stop or producer/consumer done */
    while(!signal_stop && !producerDone && !consumerDone) {
        usleep(1000);
    }

    /* Stop Producer */
    if (args.isProdCrossProc || !args.isConsCrossProc) {
        LOG_DBG("%s - stop producer thread \n", __func__);

        switch(args.producer) {
            case EGLSTREAM_NVMEDIA_IMAGE:
                ImageProducerStop(&imgProducer);
                break;
            default:
                break;
        }
    }

    /* Flush producer */
    if (args.isProdCrossProc || !args.isConsCrossProc) {
        LOG_DBG("%s - flush producer \n", __func__);

        switch(args.producer) {
            case EGLSTREAM_NVMEDIA_IMAGE:
                ImageProducerFlush(&imgProducer);
                break;
            default:
                break;
        }
    }

    LOG_DBG("%s - program end, clean up start\n", __func__);
    res = 0;

fail:
    /* Fini Producer */
    if (args.isProdCrossProc || !args.isConsCrossProc) {

        switch(args.producer) {
            case EGLSTREAM_NVMEDIA_IMAGE:
                ImageProducerFini(&imgProducer);
                break;
            default:
                break;
        }
    }

    /* Fini Consumer */
    if (args.isConsCrossProc || !args.isProdCrossProc){

        switch(args.consumer){
            case EGLSTREAM_CUDA:
            {
                CudaConsumerFini(&cudaConsumer);
                /* Test mode */
                if(cudaConsumer.testModeParams->isTestMode) {
                    if(cudaConsumer.testModeParams->isChkCrc) {
                        if(!cudaConsumer.testModeParams->isCrcMatched)
                            res = 1;
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    EGLStreamFini(streamClient, args.isConsumerondGPU);
    LOG_DBG("%s: EGLUtil shut down\n", __func__);
    EGLUtilDeinit(eglUtil);

    return res;
}
