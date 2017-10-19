/*
 * cuda_consumer.c
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
// DESCRIPTION:   Simple CUDA consumer rendering sample app
//

#include "eglstrm_setup.h"
#include "cuda_consumer.h"
#include "log_utils.h"
#include "misc_utils.h"

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

//Cuda consumer init
int _initialized = 0;
static int *cudaDeviceCreate(test_cuda_consumer_s *cudaConsumer)
{
   CUdevice device;
   CUcontext oldContext;

   if(!_initialized) {
      if(cuInit(0)) {
         LOG_DBG("failed to initialize Cuda\n");
         return NULL;
      }
      _initialized = NV_TRUE;
   }

   if(cuDeviceGet(&device, 0)) {
      LOG_DBG("failed to get Cuda device\n");
      return NULL;
   }

   if(cuCtxCreate(&cudaConsumer->context, 0, device)) {
      LOG_DBG("failed to create Cuda context\n");
      return NULL;
   }

   cuCtxPopCurrent(&oldContext);

   return NULL;
}

static NvU32
procThreadFunc (
    void *data)
{
    test_cuda_consumer_s *ctx = (test_cuda_consumer_s *)data;
    CUcontext oldContext;
    CUresult cuStatus;
    CUarray cudaArr = NULL;
    CUeglFrame cudaEgl;
    CUgraphicsResource cudaResource;
    unsigned int i;

    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return 0;
    }

    LOG_DBG("Cuda consumer thread is active\n");
    while(!ctx->quit) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                ctx->display,
                ctx->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Cuda consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            LOG_DBG("CUDA Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
            ctx->quit = NV_TRUE;
            goto done;
        }
        if(streamState != EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
            continue;
        }

        cuCtxPushCurrent(ctx->context);
        cuStatus = cuEGLStreamConsumerAcquireFrame(&(ctx->cudaConn),
                                                   &cudaResource, NULL, 16000);
        cuCtxPopCurrent(&oldContext);
        if (cuStatus == CUDA_SUCCESS) {
            CUdeviceptr pDevPtr = 0;
            int bufferSize;
            unsigned char *pCudaCopyMem = NULL;
            unsigned int copyWidthInBytes=0, copyHeight=0;

            LOG_DBG("Cuda consumer Acquired Frame for cuda copy\n");
            //CUDA memcpy
            cuStatus = cuGraphicsResourceGetMappedEglFrame(&cudaEgl, cudaResource,0,0);

            for (i=0; i<cudaEgl.planeCount; i++) {
                cuCtxPushCurrent(ctx->context);
                if (cudaEgl.frameType == CU_EGL_FRAME_TYPE_PITCH) {
                    pDevPtr =(CUdeviceptr) cudaEgl.frame.pPitch[i];

                    if (cudaEgl.planeCount==1) {
                        bufferSize = cudaEgl.pitch * cudaEgl.height;
                        copyWidthInBytes = cudaEgl.pitch;
                        copyHeight = cudaEgl.height;
                    } else if (i==1 && cudaEgl.planeCount==2) { //YUV 420 semi-planar
                        bufferSize = cudaEgl.pitch * cudaEgl.height / 2;
                        copyWidthInBytes = cudaEgl.pitch;
                        copyHeight = cudaEgl.height/2;
                    } else {
                        bufferSize = cudaEgl.pitch * cudaEgl.height;
                        copyWidthInBytes = cudaEgl.pitch;
                        copyHeight = cudaEgl.height;
                        if (i>0) {
                            bufferSize >>= 2;
                            copyWidthInBytes >>= 1;
                            copyHeight >>= 1;
                        }
                    }
                } else {
                    cudaArr = cudaEgl.frame.pArray[i];
                    if (cudaEgl.planeCount==1) {
                        if(cudaEgl.eglColorFormat == CU_EGL_COLOR_FORMAT_L)
                            copyWidthInBytes = cudaEgl.width * 2;
                        else //rgba
                            copyWidthInBytes = cudaEgl.width * 4;
                        bufferSize = copyWidthInBytes * cudaEgl.height;
                        copyHeight = cudaEgl.height;
                    } else if (i==1 && cudaEgl.planeCount==2) { //YUV 420 semi-planar
                        bufferSize = cudaEgl.width * cudaEgl.height / 2;
                        copyWidthInBytes = cudaEgl.width;
                        copyHeight = cudaEgl.height/2;
                    } else {
                        bufferSize = cudaEgl.width * cudaEgl.height;
                        copyWidthInBytes = cudaEgl.width;
                        copyHeight = cudaEgl.height;
                        if (i>0) {
                            bufferSize >>= 2;
                            copyWidthInBytes >>= 1;
                            copyHeight >>= 1;
                        }
                    }
                }
                cuCtxPopCurrent(&oldContext);
                if (cuStatus != CUDA_SUCCESS) {
                    LOG_DBG("Cuda get resource failed with %d\n", cuStatus);
                    goto done;
                }
                if (i==0) {
                    pCudaCopyMem = (unsigned char *)malloc(bufferSize);
                    if (pCudaCopyMem == NULL) {
                        LOG_DBG("pCudaCopyMem malloc failed\n");
                        goto done;
                    }
                }
                memset(pCudaCopyMem, 0, bufferSize);

                if (cudaEgl.frameType == CU_EGL_FRAME_TYPE_PITCH) {
                    cuCtxPushCurrent(ctx->context);
                    cuStatus = cuMemcpyDtoH(pCudaCopyMem, pDevPtr, bufferSize);
                    if(cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("cuda_consumer: pitch linear Memcpy failed, bufferSize =%d\n", bufferSize);
                        goto done;
                    }
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG ("cuda_consumer: cuCtxSynchronize failed after memcpy \n");
                    }
                    LOG_DBG("cuda_consumer: pitch linear Memcpy PASSED. \n");

                    cuCtxPopCurrent(&oldContext);
                } else {
                    CUDA_MEMCPY3D cpdesc;
                    cuCtxPushCurrent(ctx->context);
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG ("cuCtxSynchronize failed \n");
                    }

                    memset(&cpdesc, 0, sizeof(cpdesc));
                    cpdesc.srcXInBytes = cpdesc.srcY = cpdesc.srcZ = cpdesc.srcLOD = 0;
                    cpdesc.srcMemoryType = CU_MEMORYTYPE_ARRAY;
                    cpdesc.srcArray = cudaArr;
                    cpdesc.dstXInBytes = cpdesc.dstY = cpdesc.dstZ = cpdesc.dstLOD = 0;
                    cpdesc.dstMemoryType = CU_MEMORYTYPE_HOST;
                    cpdesc.dstHost = (void *)pCudaCopyMem;

                    cpdesc.WidthInBytes = copyWidthInBytes;
                    cpdesc.Height = copyHeight;
                    cpdesc.Depth = 1;

                    cuStatus = cuMemcpy3D(&cpdesc);
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("Cuda consumer: cuMemCpy3D failed,  copyWidthInBytes=%d, copyHight=%d\n", copyWidthInBytes, copyHeight);
                    }
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG ("cuCtxSynchronize failed after memcpy \n");
                    }
                    cuCtxPopCurrent(&oldContext);
                }
                if (cuStatus == CUDA_SUCCESS) {
                    //write the data into output file
                    if (fwrite(pCudaCopyMem, bufferSize, 1, ctx->outFile) != 1) {
                         LOG_ERR("Cuda consumer: output file write failed\n");
                         cuEGLStreamConsumerReleaseFrame(&ctx->cudaConn,
                                            cudaResource, NULL);
                         ctx->quit = NV_TRUE;
                         if (pCudaCopyMem) {
                             free(pCudaCopyMem);
                             pCudaCopyMem = NULL;
                         }
                         goto done;
                    }
                }
            }
            ctx->frameCount++;
            LOG_DBG("Cuda consumer cudacopy finish frame %d\n", ctx->frameCount);
            cuCtxPushCurrent(ctx->context);
            cuStatus = cuEGLStreamConsumerReleaseFrame(&ctx->cudaConn,
                                            cudaResource, NULL);
            cuCtxPopCurrent(&oldContext);
            if (pCudaCopyMem) {
                free(pCudaCopyMem);
                pCudaCopyMem = NULL;
            }

            if (cuStatus != CUDA_SUCCESS) {
                ctx->quit = NV_TRUE;
                goto done;
            }
        } else {
            printf("cuda acquire failed cuStatus=%d\n", cuStatus);
        }
    }

done:
    *ctx->consumerDone = NV_TRUE;
    do {
        cuCtxPushCurrent(ctx->context);
        cuStatus = cuEGLStreamConsumerAcquireFrame(&(ctx->cudaConn),
                                                   &cudaResource, NULL, 16000);
        if (cuStatus == CUDA_SUCCESS) {
            cuEGLStreamConsumerReleaseFrame(&ctx->cudaConn,
                                            cudaResource, NULL);
        }
        cuCtxPopCurrent(&oldContext);
    } while(cuStatus == CUDA_SUCCESS);

    ctx->procThreadExited = NV_TRUE;

    return 0;
}

int cuda_consumer_init(volatile NvBool *consumerDone, test_cuda_consumer_s *cudaConsumer, EGLDisplay display, EGLStreamKHR eglStream, TestArgs *args)
{
    CUresult curesult;
    CUcontext oldContext;

    LOG_DBG("cuda_consumer_init: \n");
    memset(cudaConsumer, 0, sizeof(test_cuda_consumer_s));

    cudaConsumer->display        = display;
    cudaConsumer->eglStream      = eglStream;

    cudaDeviceCreate(cudaConsumer);

    cudaConsumer->consumerDone = consumerDone;

    cuCtxPushCurrent(cudaConsumer->context);

    LOG_DBG("Connect CUDA consumer\n");
    if (CUDA_SUCCESS !=
        (curesult = cuEGLStreamConsumerConnect(&cudaConsumer->cudaConn, cudaConsumer->eglStream))) {
        LOG_ERR("Connect CUDA consumer ERROR %d\n", curesult);
        return NV_FALSE;
    }

    cuCtxPopCurrent(&oldContext);

    cudaConsumer->outFile = fopen("cuda.yuv", "wb");
    if(!cudaConsumer->outFile) {
        LOG_ERR("WriteFrame: file open failed: %s\n", "cuda.yuv");
        perror(NULL);
        return NV_FALSE;
    }

    //! [docs_eglstream:start_consumer_thread]
    if(IsFailed(NvThreadCreate(&cudaConsumer->procThread, &procThreadFunc, (void *)cudaConsumer, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("Cuda consumer init: Unable to create process thread\n");
        cudaConsumer->procThreadExited = NV_TRUE;
        return NV_FALSE;
    }
    //! [docs_eglstream:start_consumer_thread]
    return NV_TRUE;
}

void cuda_consumer_Deinit(test_cuda_consumer_s *cudaConsumer)
{
    CUresult curesult;

    LOG_DBG("cuda_consumer_Deinit: \n");

    cudaConsumer->quit = NV_TRUE;

    if (cudaConsumer->procThread) {
        LOG_DBG("wait for CUDA consumer thread exit\n");
        NvThreadDestroy(cudaConsumer->procThread);
    }

    if (CUDA_SUCCESS !=
        (curesult = cuEGLStreamConsumerDisconnect(&cudaConsumer->cudaConn))) {
        LOG_ERR("Disconnect CUDA consumer ERROR %d\n", curesult);
    }

    if(cudaConsumer->outFile) {
        fclose(cudaConsumer->outFile);
    }
}

