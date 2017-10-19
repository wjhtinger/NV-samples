/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "cuda_consumer.h"

extern NvBool signal_stop;

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif

//Cuda consumer init
int _initialized = 0;
static int *CudaDeviceCreate(CudaConsumerCtx *cudaConsumer)
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

static void *
procThreadFunc (
    void *data)
{
    CudaConsumerCtx *ctx = (CudaConsumerCtx *)data;
    CUcontext oldContext;
    CUresult cuStatus;
    CUarray cudaArr = NULL;
    CUeglFrame cudaEgl;
    CUgraphicsResource cudaResource;
    NvU8* buffptr = NULL;
    unsigned int i;
    FILE *fp = NULL;
    NvU32 BytesPerPixel;

    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NULL;
    }

    NvU32 chkcrc_prev=100;
    LOG_DBG("Cuda consumer thread is active\n");

    if(ctx->outFileName){
        fp = fopen(ctx->outFileName, "wb");
        if(!fp) {
            LOG_ERR("WriteFrame: file open failed: %s\n", ctx->outFileName);
            return NV_FALSE;
        }
    }
    while(!ctx->quit) {
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(ctx->display,
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
                                                   &cudaResource,
                                                   NULL,
                                                   16000);
        if(cuStatus != CUDA_SUCCESS){
            LOG_ERR("CUDA Consumer: cuEGLStreamConsumerAcquireFrame failed with status %d\n", cuStatus);
        }
        cuCtxPopCurrent(&oldContext);
        if (cuStatus == CUDA_SUCCESS) {
            CUdeviceptr pDevPtr = NULL;
            int bufferSize;
            unsigned char *pCudaCopyMem = NULL;
            unsigned int copyWidthInBytes=0, copyHeight=0;

            LOG_DBG("Cuda consumer Acquired Frame for cuda copy\n");
            //CUDA memcpy
            cuStatus = cuGraphicsResourceGetMappedEglFrame(&cudaEgl, cudaResource,0,0);

            //memcpy for test
            NvU32 pitch = (cudaEgl.pitch)? cudaEgl.pitch:cudaEgl.width;
            NvU8* buff = malloc(pitch * cudaEgl.height*sizeof(unsigned int));
            if(buff == NULL) {
                LOG_ERR("\n Unable to allocate memory \n");
                return 0;
            }
            buffptr = buff;

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
                        bufferSize = cudaEgl.width * cudaEgl.height *4;
                        copyWidthInBytes = cudaEgl.width * 4;
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
                    if((i ==1) && (cudaEgl.planeCount == 2)) {
                        /*YUV frames are received in NV12 format*/
                        //Convert 420SP to 420P
                        NvU8* ptrU, *ptrV , *ptrUV , *ptrTemp;
                        int idx =0;
                        LOG_DBG(" convert nv12 to 420p bufsize %d\n", bufferSize);
                        ptrTemp = malloc(bufferSize);
                        ptrU = ptrTemp;
                        ptrV = ptrU + bufferSize/2;
                        ptrUV = pCudaCopyMem;

                        for(idx = 0; idx < bufferSize/2;idx++){
                            ptrU[0] = ptrUV[0];
                            ptrV[0] = ptrUV[1];
                            ptrUV+=2;
                            ptrU++;
                            ptrV++;
                        }
                        memcpy(pCudaCopyMem,ptrTemp,bufferSize);
                        free(ptrTemp);
                    }

                    if(fp) {
                        if (cudaEgl.frameType == CU_EGL_FRAME_TYPE_PITCH) {
                            unsigned int line;
                            unsigned char *bufptr = pCudaCopyMem;
                            LOG_DBG("WriteFrame Pitch linear: w %d h %d pitch %d\n", cudaEgl.width, cudaEgl.height, cudaEgl.pitch);
                            LOG_DBG("WriteFrame: bufsize: %d\n", bufferSize);

                            if((i ==1) && (cudaEgl.planeCount == 2)){
                                for(line = 0; line < cudaEgl.height ; line++){
                                    if (fwrite(bufptr, cudaEgl.width / 2, 1, fp) != 1) {
                                        LOG_ERR("Cuda consumer: output file write failed\n");
                                        ctx->quit = NV_TRUE;
                                        free(pCudaCopyMem);
                                        goto done;
                                    }
                                    bufptr += (cudaEgl.pitch / 2);
                                }
                            }
                            else{
                                if (cudaEgl.cuFormat == CU_AD_FORMAT_UNSIGNED_INT16){
                                    BytesPerPixel = 2;
                                }
                                else{
                                    BytesPerPixel = 1;
                                }
                                for(line = 0; line < cudaEgl.height ; line++){

                                    if (fwrite(bufptr, cudaEgl.width * BytesPerPixel, 1, fp) != 1) {
                                        LOG_ERR("Cuda consumer: output file write failed\n");
                                        ctx->quit = NV_TRUE;
                                        free(pCudaCopyMem);
                                        goto done;
                                    }
                                    bufptr += cudaEgl.pitch;
                                }
                            }
                        }
                        else{
                            LOG_DBG("WriteFrame Pitch linear: w %d h %d pitch %d\n", cudaEgl.width, cudaEgl.height, cudaEgl.pitch);
                            LOG_DBG("WriteFrame: bufsize: %d\n", bufferSize);

                            if (fwrite(pCudaCopyMem, bufferSize, 1, fp) != 1) {
                                LOG_ERR("Cuda consumer: output file write failed\n");
                                ctx->quit = NV_TRUE;
                                free(pCudaCopyMem);
                                goto done;
                            }
                        }
                    }

                    if(ctx->testModeParams->isTestMode) {
                        if(ctx->testModeParams->isChkCrc) {
                            memcpy(buffptr,pCudaCopyMem,bufferSize);
                            buffptr += bufferSize;
                        }
                    }
                }
            }

            /*Calculate the CRC and check with the reference CRC*/
            if(ctx->testModeParams->isTestMode) {
                if(ctx->testModeParams->isChkCrc) {

                    NvU32 chkcrc = 1;
                    NvMediaSurfaceType imageType;
                    NvU32 refcrc=0;
                    if(cudaEgl.planeCount >= 2) {
                        imageType = NvMediaSurfaceType_Image_YUV_420;
                    }else {
                        imageType = NvMediaSurfaceType_Image_RGBA;
                    }

                    GetFrameCrc(buff,
                                cudaEgl.width,
                                cudaEgl.height,
                                cudaEgl.pitch,
                                imageType,
                                &chkcrc,
                                1);  /*Passing 1 as default value in rawBytesPerPixel */
                    //If the consumer reads the same frame again, dont save it again
                    if(chkcrc == chkcrc_prev) {
                        free(pCudaCopyMem);
                        free(buff);
                        continue;
                    }
                    fprintf(ctx->testModeParams->chkCrcFile,"%8x\n",chkcrc);
                    chkcrc_prev = chkcrc;

                    fscanf(ctx->testModeParams->refCrcFile,"%8x\n",&refcrc);
                    if(chkcrc != refcrc) {
                        LOG_ERR("\n CRC Mismatch : Frame no. - %d RefCRC = %x ChkCRC = %x\n",ctx->frameCount + 1, refcrc,chkcrc);
                        ctx->testModeParams->isCrcMatched = NV_FALSE;
                        signal_stop = NV_TRUE;
                        ctx->quit = NV_TRUE;
                    } else {
                        //LOG_DBG("\n CRC Pass : Frame no. - %d RefCRC = %x ChkCRC = %x\n",ctx->frameCount + 1, refcrc,chkcrc);
                        ctx->testModeParams->isCrcMatched = NV_TRUE;
                    }
                }
            }

            ctx->frameCount++;
            LOG_DBG("Cuda consumer cudacopy finish frame %d\n", ctx->frameCount);
            cuCtxPushCurrent(ctx->context);
            cuStatus = cuEGLStreamConsumerReleaseFrame(&ctx->cudaConn,
                                                       cudaResource,
                                                       NULL);
            cuCtxPopCurrent(&oldContext);
            free(pCudaCopyMem);
            free(buff);

            if (cuStatus != CUDA_SUCCESS) {
                ctx->quit = NV_TRUE;
                goto done;
            }
        } else {
            LOG_ERR("cuda acquire failed cuStatus=%d\n", cuStatus);
        }
    }

done:
    if(fp){
        fclose(fp);
    }
    ctx->procThreadExited = NV_TRUE;
    *ctx->consumerDone = NV_TRUE;
    return NULL;
}

int CudaConsumerInit(volatile NvBool *consumerDone, CudaConsumerCtx *cudaConsumer,EglStreamClient *streamClient, TestArgs *args)
{
    CUresult curesult;
    CUcontext oldContext;

    memset(cudaConsumer, 0, sizeof(CudaConsumerCtx));
    LOG_DBG("cudaConsumerInit: \n");

    if(args->isConsumerondGPU && (!(args->isProdCrossProc || args->isConsCrossProc))){
        cudaConsumer->display        = streamClient->display_dGPU;
        cudaConsumer->eglStream      = streamClient->eglStream_dGPU;
    }
    else {
        cudaConsumer->display        = streamClient->display;
        cudaConsumer->eglStream      = streamClient->eglStream;
    }
    cudaConsumer->outFileName    = args->outFileName;
    cudaConsumer->consumerDone   = consumerDone;
    cudaConsumer->testModeParams = &args->testModeParams;
    cudaConsumer->testModeParams->isCrcMatched = NV_FALSE;

    CudaDeviceCreate(cudaConsumer);

    cuCtxPushCurrent(cudaConsumer->context);
    LOG_DBG("Connect CUDA consumer\n");
    if (CUDA_SUCCESS !=
        (curesult = cuEGLStreamConsumerConnect(&cudaConsumer->cudaConn, cudaConsumer->eglStream))) {
        LOG_ERR("Connect CUDA consumer ERROR %d\n", curesult);
        return 1;
    }
    cuCtxPopCurrent(&oldContext);

    /* Test mode */
    if(cudaConsumer->testModeParams->isTestMode) {
        if(cudaConsumer->testModeParams->isChkCrc) {
            char str[1024];
            /*Open the Reference CRC file and the current CRC of the input stream */
            cudaConsumer->testModeParams->refCrcFile = fopen(cudaConsumer->testModeParams->refCrcFileName, "rb");
            if(!cudaConsumer->testModeParams->refCrcFile) {
                LOG_ERR("ChkCrc: Error opening ref crc file: %s\n", cudaConsumer->testModeParams->refCrcFileName);
                return 1;
            }
            strcpy(str,cudaConsumer->testModeParams->refCrcFileName);
            strcat(str,"_out.txt");
            cudaConsumer->testModeParams->chkCrcFile = fopen(str, "wb");
            if(!cudaConsumer->testModeParams->chkCrcFile) {
                LOG_ERR("ChkCrc: Error opening chk crc file: %s\n", str);
                return 1;
            }
        }
    }

    pthread_create(&cudaConsumer->procThread,
                   NULL,
                   procThreadFunc,
                   (void *)cudaConsumer);

    if (!cudaConsumer->procThread) {
        LOG_ERR("Cuda consumer init: Unable to create process thread\n");
        cudaConsumer->procThreadExited = NV_TRUE;
        return 1;
    }
    return 0;
}

void CudaConsumerFini(CudaConsumerCtx *cudaConsumer)
{
    CUresult curesult;

    LOG_DBG("cuda_consumer_Deinit: \n");

    cudaConsumer->quit = NV_TRUE;

    while (!cudaConsumer->procThreadExited) {
        LOG_DBG("wait for CUDA consumer thread exit\n");
    }

    if (CUDA_SUCCESS !=
        (curesult = cuEGLStreamConsumerDisconnect(&cudaConsumer->cudaConn))) {
        LOG_ERR("Disconnect CUDA consumer ERROR %d\n", curesult);
    }

    /* Test mode */
    if(cudaConsumer->testModeParams->isTestMode) {
        if(cudaConsumer->testModeParams->isChkCrc) {
            fclose(cudaConsumer->testModeParams->refCrcFile);
            fclose(cudaConsumer->testModeParams->chkCrcFile);

            if(cudaConsumer->testModeParams->isCrcMatched) {
                printf(" ----  CRC test passed (Total no of frames = %d) ----\n",
                        cudaConsumer->frameCount-1);
            } else {
                printf(" ----  CRC test failed @ frame %d ----\n",
                        cudaConsumer->frameCount);
            }
        }
    }
}

