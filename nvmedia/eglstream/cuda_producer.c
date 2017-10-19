/*
 * cuda_producer.c
 *
 * Copyright (c) 2014-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple cuda EGL stream producer app
//

#include "eglstrm_setup.h"
#include "cudaEGL.h"
#include <cuda_producer.h>
#include "nvmimg_producer.h"
#include <nvmedia.h>
#include <nvmedia_eglstream.h>
#include "misc_utils.h"

extern Image2DTestArgs g_testArgs;  //Need for cuda consumer, it doesn't extract size out of the cuda array

extern NvBool signal_stop;
test_cuda_producer_s g_cudaProducer;
#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

static NvMediaStatus
cudaProduerReadYUVFrame(
    FILE *file,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvU8 *pBuff,
    NvMediaBool bOrderUV)
{
    NvU8 *pYBuff, *pUBuff, *pVBuff, *pChroma;
    NvU32 frameSize = (width * height *3)/2;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;
    unsigned int i;

    if(!pBuff || !file)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    pYBuff = pBuff;

    //YVU order in the buffer
    pVBuff = pYBuff + width * height;
    pUBuff = pVBuff + width * height / 4;

    if(fseek(file, frameNum * frameSize, SEEK_SET)) {
        LOG_ERR("ReadYUVFrame: Error seeking file: %p\n", file);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
    //read Y U V separately
    for(i = 0; i < height; i++) {
        if(fread(pYBuff, width, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %p\n", file);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pYBuff += width;
    }

    pChroma = bOrderUV ? pUBuff : pVBuff;
    for(i = 0; i < height / 2; i++) {
        if(fread(pChroma, width / 2, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %p\n", file);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pChroma += width / 2;
    }

    pChroma = bOrderUV ? pVBuff : pUBuff;
    for(i = 0; i < height / 2; i++) {
        if(fread(pChroma, width / 2, 1, file) != 1) {
            LOG_ERR("ReadYUVFrame: Error reading file: %p\n", file);
            ret = NVMEDIA_STATUS_ERROR;
            goto done;
        }
        pChroma += width / 2;
    }

done:
    return ret;
}

static NvMediaStatus
cudaProduerReadRGBAFrame(
    FILE *file,
    NvU32 frameNum,
    NvU32 width,
    NvU32 height,
    NvU8 *pBuff)
{
    NvU32 frameSize = width * height * 4;
    NvMediaStatus ret = NVMEDIA_STATUS_OK;

    if(!pBuff || !file)
        return NVMEDIA_STATUS_BAD_PARAMETER;

    if(fseek(file, frameNum * frameSize, SEEK_SET)) {
        LOG_ERR("ReadYUVFrame: Error seeking file: %p\n", file);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }

    //read rgba data
    if(fread(pBuff, frameSize, 1, file) != 1) {
        if (feof(file))
            LOG_DBG("ReadRGBAFrame: file read to the end\n");
        else
            LOG_ERR("ReadRGBAFrame: Error reading file: %p\n", file);
        ret = NVMEDIA_STATUS_ERROR;
        goto done;
    }
done:
    return ret;
}

static NvU32 CudaProducerThread(void *parserArg)
{
    test_cuda_producer_s *cudaProducer = (test_cuda_producer_s *)parserArg;
    int framenum = 0;
    FILE *file=NULL;
    NvU8 *pBuff=NULL;
    CUarray cudaArr[2][3] = {{0,0,0}, {0,0,0}};
    CUeglFrame cudaEgl;
    CUDA_ARRAY3D_DESCRIPTOR desc = {0};
    CUdeviceptr cudaPtr[2][3] = {{0,0,0}, {0,0,0}};
    int cudaIdx =0;
    NvU32 bufferSize;
    CUresult cuStatus;
    CUcontext oldContext;
    NvU32 i, surfNum, uvOffset[3]={0};
    NvU32 copyWidthInBytes[3]={0}, copyHeight[3]={0};
    CUeglColorFormat eglColorFormat;

    LOG_DBG("CudaProducerThread: Init\n");

    file = fopen(cudaProducer->fileName, "rb");
    if(!file) {
        LOG_ERR("CudaProducerThread: Error opening file: %s\n", cudaProducer->fileName);
        goto done;
    }

    pBuff = malloc((cudaProducer->width*cudaProducer->height*4));
    if(!pBuff) {
        LOG_ERR("CudaProducerThread: Failed to allocate image buffer\n");
        goto done;
    }

    for(cudaIdx=0; cudaIdx<2; cudaIdx++) {
        if (cudaProducer->pitchLinearOutput) {
            if (cudaProducer->isRgbA) {
                cuCtxPushCurrent(cudaProducer->context);
                cuStatus = cuMemAlloc(&cudaPtr[cudaIdx][0], (cudaProducer->width*cudaProducer->height*4));
                if(cuStatus != CUDA_SUCCESS) {
                    LOG_DBG("Create CUDA pointer failed, cuStatus=%d\n", cuStatus);
                    goto done;
                }
                LOG_DBG("Create CUDA pointer GOOD, cuStatus=%d\n", cuStatus);
                cuCtxPopCurrent(&oldContext);
            } else { //YUV case
                for (i=0; i<3; i++) {
                    cuCtxPushCurrent(cudaProducer->context);
                    if (i==0)
                        bufferSize = cudaProducer->width*cudaProducer->height;
                    else
                        bufferSize = cudaProducer->width*cudaProducer->height/4;

                    cuStatus = cuMemAlloc(&cudaPtr[cudaIdx][i], bufferSize);
                    if(cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("Create CUDA pointer %d failed, cuStatus=%d\n", i, cuStatus);
                        goto done;
                    }
                    cuCtxPopCurrent(&oldContext);
                }
            }
        } else {
            desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            desc.Depth = 1;
            desc.Flags = CUDA_ARRAY3D_SURFACE_LDST;
            if (cudaProducer->isRgbA) {
                desc.NumChannels = 4;
                desc.Width = cudaProducer->width;
                desc.Height = cudaProducer->height;
                cuCtxPushCurrent(cudaProducer->context);
                cuStatus = cuArray3DCreate( &cudaArr[cudaIdx][0], &desc );
                if(cuStatus != CUDA_SUCCESS) {
                    LOG_DBG("Create CUDA array failed, cuStatus=%d\n", cuStatus);
                    goto done;
                }
                cuCtxPopCurrent(&oldContext);
            } else { //YUV case
                for (i=0; i<3; i++) {
                    if (i==0) {
                        desc.NumChannels = 1;
                        desc.Width = cudaProducer->width;
                        desc.Height = cudaProducer->height;
                    } else { // U/V surface as planar
                        desc.NumChannels = 1;
                        desc.Width = cudaProducer->width/2;
                        desc.Height = cudaProducer->height/2;
                    }
                    cuCtxPushCurrent(cudaProducer->context);
                    cuStatus = cuArray3DCreate( &cudaArr[cudaIdx][i], &desc );
                    if(cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("Create CUDA array failed, cuStatus=%d\n", cuStatus);
                        goto done;
                    }
                    cuCtxPopCurrent(&oldContext);
                }
            }
        }
    }
    LOG_DBG("CudaProducerThread: read data and produce\n");

    cudaIdx = 0;
    while(!signal_stop) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                cudaProducer->eglDisplay,
                cudaProducer->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("main: NvMediaPostSurface: eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }
        if(streamState == EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
            LOG_DBG("cuda producer - consumer not consume the last frame yet\n");
            sleep(1);
            continue;
        }

        if (!cudaProducer->frameCount || framenum<cudaProducer->frameCount) {
            uvOffset[0] = 0;
            if (cudaProducer->isRgbA) {
                if (NVMEDIA_STATUS_OK==cudaProduerReadRGBAFrame(file, framenum, cudaProducer->width, cudaProducer->height, pBuff)) {
                    LOG_DBG("cuda producer, reading RGBA frame %d\n", framenum);
                 } else {
                    LOG_DBG("cuda producer, read frame %d done\n", framenum);
                    goto done;
                 }
                 copyWidthInBytes[0] = cudaProducer->width * 4;
                 copyHeight[0] = cudaProducer->height;
                 surfNum = 1;
                 eglColorFormat = CU_EGL_COLOR_FORMAT_ARGB;
            } else {
                if (NVMEDIA_STATUS_OK==cudaProduerReadYUVFrame(file, framenum, cudaProducer->width, cudaProducer->height, pBuff, cudaProducer->inputUVOrderFlag)) {
                    LOG_DBG("cuda producer, reading YUV frame %d\n", framenum);
                 } else
                    goto done;
                 surfNum = 3;
                 eglColorFormat = CU_EGL_COLOR_FORMAT_YUV420_PLANAR;
                 copyWidthInBytes[0] = cudaProducer->width;
                 copyHeight[0] = cudaProducer->height;
                 copyWidthInBytes[1] = cudaProducer->width/2;
                 copyHeight[1] = cudaProducer->height/2;
                 copyWidthInBytes[2] = cudaProducer->width/2;
                 copyHeight[2] = cudaProducer->height/2;
                 uvOffset[1] = cudaProducer->width *cudaProducer->height;
                 uvOffset[2] = uvOffset[1] + cudaProducer->width/2 *cudaProducer->height/2;
            }

            if (cudaProducer->pitchLinearOutput) {
                //copy pBuff to cudaPointer
                for (i=0; i<surfNum; i++) {
                    cuCtxPushCurrent(cudaProducer->context);
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG ("cuCtxSynchronize failed \n");
                    }
                    cuStatus = cuMemcpy(cudaPtr[cudaIdx][i], (CUdeviceptr)(pBuff + uvOffset[i]), copyWidthInBytes[i]*copyHeight[i]);
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("Cuda producer: cuMemCpy pitchlinear failed, cuStatus =%d\n",cuStatus);
                    }
                    LOG_DBG("Cuda producer: cuMemCpy pitchlinear %i size=%d done\n", i, copyWidthInBytes[i]*copyHeight[i]);
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("cuCtxSynchronize failed after memcpy, cuStatus=%d \n", cuStatus);
                    }
                    cuCtxPopCurrent(&oldContext);
                }
            } else {
                //copy pBuff to cudaArray
                LOG_DBG("Cuda producer copy Frame to cudaArray...\n");

                CUDA_MEMCPY3D cpdesc;

                for (i=0; i<surfNum; i++) {
                    cuCtxPushCurrent(cudaProducer->context);
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG ("cuCtxSynchronize failed \n");
                    }
                    memset(&cpdesc, 0, sizeof(cpdesc));
                    cpdesc.srcXInBytes = cpdesc.srcY = cpdesc.srcZ = cpdesc.srcLOD = 0;
                    cpdesc.srcMemoryType = CU_MEMORYTYPE_HOST;
                    cpdesc.srcHost = (void *)(pBuff + uvOffset[i]);
                    cpdesc.dstXInBytes = cpdesc.dstY = cpdesc.dstZ = cpdesc.dstLOD = 0;
                    cpdesc.dstMemoryType = CU_MEMORYTYPE_ARRAY;
                    cpdesc.dstArray = cudaArr[cudaIdx][i];

                    cpdesc.WidthInBytes = copyWidthInBytes[i];
                    cpdesc.Height = copyHeight[i];
                    cpdesc.Depth = 1;

                    cuStatus = cuMemcpy3D(&cpdesc);
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("Cuda producer: cuMemCpy failed, cuStatus =%d\n",cuStatus);
                    }
                    cuStatus = cuCtxSynchronize();
                    if (cuStatus != CUDA_SUCCESS) {
                        LOG_DBG("cuCtxSynchronize failed after memcpy, cuStatus=%d \n", cuStatus);
                    }
                    cuCtxPopCurrent(&oldContext);
                    LOG_DBG("Cuda producer copy Frame to cudaArray %d, done\n", i);
                }
            }

            cuCtxPushCurrent(cudaProducer->context);

            for (i=0; i<surfNum; i++) {
               if (cudaProducer->pitchLinearOutput)
                   cudaEgl.frame.pPitch[i] = (void *)cudaPtr[cudaIdx][i];
               else
                   cudaEgl.frame.pArray[i] = cudaArr[cudaIdx][i];
            }
            cudaEgl.width = cudaProducer->width;
            cudaEgl.depth = 1;
            cudaEgl.height = cudaProducer->height;
            cudaEgl.pitch = cudaProducer->pitchLinearOutput ? copyWidthInBytes[0] : 0;
            cudaEgl.frameType = cudaProducer->pitchLinearOutput ?
                        CU_EGL_FRAME_TYPE_PITCH : CU_EGL_FRAME_TYPE_ARRAY;
            cudaEgl.planeCount = surfNum;
            cudaEgl.numChannels = (eglColorFormat == CU_EGL_COLOR_FORMAT_ARGB) ? 4: 1;
            cudaEgl.eglColorFormat = eglColorFormat;
            cudaEgl.cuFormat = CU_AD_FORMAT_UNSIGNED_INT8;

            cuStatus = cuEGLStreamProducerPresentFrame(&cudaProducer->cudaConn, cudaEgl, NULL);

            framenum++;
            cudaIdx++;
            if(cudaIdx>=2) cudaIdx = 0;
            LOG_DBG("cuda Producer present frame custatus= %d, framenum=%d\n", cuStatus, framenum);
            cuCtxPopCurrent(&oldContext);
            if (cuStatus != CUDA_SUCCESS) {
                LOG_ERR("Cuda producer: presentFrame failed, cuStatus = %d\n", cuStatus);
            }
        } else
            goto done;

    }

done:
    if (file) {
        fclose(file);
        file = NULL;
    }
    if (pBuff) {
        free(pBuff);
        pBuff = NULL;
    }

    // Signal end of producer
    *cudaProducer->producerFinished = NV_TRUE;

    return 0;
}

extern int _initialized;
static int *cudaDeviceCreate(test_cuda_producer_s *cudaProducer)
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

   if(cuCtxCreate(&cudaProducer->context, 0, device)) {
      LOG_DBG("failed to create Cuda context\n");
      return NULL;
   }

   cuCtxPopCurrent(&oldContext);

   return NULL;
}

int CudaProducerInit(volatile NvBool *producerFinished, EGLDisplay eglDisplay, EGLStreamKHR eglStream, TestArgs *args)
{
    test_cuda_producer_s *cudaProducer = &g_cudaProducer;
    CUresult curesult;
    CUcontext oldContext;

    memset(cudaProducer, 0, sizeof(test_cuda_producer_s));

    cudaProducer->fileName = args->infile;
    if(!args->prodFrameCount){
        FILE *fp = NULL;
        long filelength;
        fp = fopen(args->infile, "rb");
        if(fp == NULL){
            LOG_ERR("Failed to open file %s \n", args->infile);
            return NV_FALSE;
        }

        fseek(fp, 0, SEEK_END);
        filelength = ftell(fp);
        fclose(fp);
        if(args->prodIsRGBA){
            args->prodFrameCount = filelength / (args->inputWidth * args->inputHeight * 4);
        }
        else{
            args->prodFrameCount = (filelength * 2)/ (args->inputWidth * args->inputHeight * 3);
        }
    }
    cudaProducer->frameCount = args->prodFrameCount;
    cudaProducer->width = args->inputWidth;
    cudaProducer->height = args->inputHeight;
    g_testArgs.outputWidth = cudaProducer->width;  //Hack here
    g_testArgs.outputHeight = cudaProducer->height;
    cudaProducer->isRgbA = args->prodIsRGBA;
    cudaProducer->pitchLinearOutput = args->pitchLinearOutput;
    // Set cudaProducer default parameters
    cudaProducer->eglDisplay = eglDisplay;
    cudaProducer->eglStream = eglStream;
    cudaProducer->producerFinished = producerFinished;

    cudaDeviceCreate(cudaProducer);

    cuCtxPushCurrent(cudaProducer->context);

    LOG_DBG("Connect CUDA producer\n");
    if (CUDA_SUCCESS == (curesult = cuEGLStreamProducerConnect(&cudaProducer->cudaConn, cudaProducer->eglStream, cudaProducer->width, cudaProducer->height))) {
        LOG_DBG("Connect CUDA producer Done, CudaProducer %p\n", cudaProducer->cudaConn);
    } else {
        LOG_ERR("Connect CUDA producer ERROR %d\n", curesult);
        return NV_FALSE;
    }

    cuCtxPopCurrent(&oldContext);

    cudaProducer->outFile = fopen("cuda.yuv", "wb");
    if(!cudaProducer->outFile) {
        LOG_ERR("WriteFrame: file open failed: %s\n", "cuda.yuv");
        perror(NULL);
        return NV_FALSE;
    }

    // Create cuda producer thread
    if(IsFailed(NvThreadCreate(&cudaProducer->thread, &CudaProducerThread, (void *)cudaProducer, NV_THREAD_PRIORITY_NORMAL))) {
        LOG_ERR("CudaProducerInit: Unable to create producer thread\n");
        return 0;
    }

    return 1;
}

void CudaProducerDeinit()
{
    test_cuda_producer_s *cudaProducer = &g_cudaProducer;
    CUresult curesult;

    LOG_DBG("main: CudaProducerDeinit\n");

    if(cudaProducer->thread) {
        LOG_DBG("wait for cuda producer thread exit\n");
        NvThreadDestroy(cudaProducer->thread);
    }

    if (CUDA_SUCCESS == (curesult = cuEGLStreamProducerDisconnect(&cudaProducer->cudaConn))) {
         LOG_DBG("Disconnect CUDA producer Done, CudaProducer %p\n", cudaProducer->cudaConn);
    } else {
        LOG_ERR("DisConnect CUDA producer ERROR %d\n", curesult);
    }
}

