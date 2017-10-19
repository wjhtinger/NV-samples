/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "cuda_consumer.h"
#include<sys/time.h>
#include "nvmedia_image.h"
extern NvBool signal_stop;

#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif
/*TODO:cleanup the below*/
extern     NvMediaBool                 Producerstopped;
//#define PROFILE_ENABLE
#define DUMP_ANDPROFILE_INTERVAL (1)
#define CALC_TIMEDIFF(tv1, tv2, diff) \
    do{  \
diff = tv2.tv_usec - tv1.tv_usec; \
diff += (tv2.tv_sec - tv1.tv_sec) * 1000000; \
}while(0)


#define SET_X_OFFSET(xoffsets, red, clear1, clear2, blue) \
            xoffsets[red] = 0;\
            xoffsets[clear1] = 1;\
            xoffsets[clear2] = 0;\
            xoffsets[blue] = 1

#define SET_Y_OFFSET(yoffsets, red, clear1, clear2, blue) \
            yoffsets[red] = 0;\
            yoffsets[clear1] = 0;\
            yoffsets[clear2] = 1;\
            yoffsets[blue] = 1

#define CALCULATE_PIXEL(buf, pitch, x, y, xoffset, yoffset) \
            (buf[(pitch * (y + yoffset)) + (2 * (x + xoffset)) + 1] << 2) | \
            (buf[(pitch * (y + yoffset)) + (2 * (x + xoffset))] >> 6)

enum PixelColor {
    RED,
    CLEAR1,
    CLEAR2,
    BLUE,
    NUM_PIXEL_COLORS
};


static void convert_Bayer_RGBA(NvU8 *bayer, NvU8 *rgba, NvU32 width, NvU32 height, NvU32 pitch)
{
    NvU32 i, j, index=0;
    NvMediaRawPixelOrder pixelOrder;
    NvU32 xOffsets[NUM_PIXEL_COLORS] = {0}, yOffsets[NUM_PIXEL_COLORS] = {0};
    NvU8 r, c1, c2, b, alpha = 0xFF;

    pixelOrder = NVMEDIA_RAW_PIXEL_ORDER_GRBG;
    switch (pixelOrder) {
    case NVMEDIA_RAW_PIXEL_ORDER_RGGB:
        SET_X_OFFSET(xOffsets, RED, CLEAR1, CLEAR2, BLUE);
        SET_Y_OFFSET(yOffsets, RED, CLEAR1, CLEAR2, BLUE);
        break;

    case NVMEDIA_RAW_PIXEL_ORDER_BGGR:
        SET_X_OFFSET(xOffsets, BLUE, CLEAR1, CLEAR2, RED);
        SET_Y_OFFSET(yOffsets, BLUE, CLEAR1, CLEAR2, RED);
        break;

    case NVMEDIA_RAW_PIXEL_ORDER_GBRG:
        SET_X_OFFSET(xOffsets, CLEAR1, BLUE, RED, CLEAR2);
        SET_Y_OFFSET(yOffsets, CLEAR1, BLUE, RED, CLEAR2);
        break;

    case NVMEDIA_RAW_PIXEL_ORDER_GRBG:
    default:
        SET_X_OFFSET(xOffsets, CLEAR1, RED, BLUE, CLEAR2);
        SET_Y_OFFSET(yOffsets, CLEAR1, RED, BLUE, CLEAR2);
        break;
    }

    // Convert image from RCCB to RGBA and write to CPU buffer
    for (i = 0; i < height; i += 2) {
        for (j = 0; j < width; j += 2) {
            // RED
            r = CALCULATE_PIXEL(bayer, pitch, j, i, xOffsets[RED], yOffsets[RED]);
            // CLEAR1
            c1 = CALCULATE_PIXEL(bayer, pitch, j, i, xOffsets[CLEAR1], yOffsets[CLEAR1]);
            // CLEAR2
            c2 = CALCULATE_PIXEL(bayer, pitch, j, i, xOffsets[CLEAR2], yOffsets[CLEAR2]);
            // BLUE
            b = CALCULATE_PIXEL(bayer, pitch, j, i, xOffsets[BLUE], yOffsets[BLUE]);

            // RED
            rgba[index++] = r;
            // GREEN (average of CLEAR1 and CLEAR2)
            rgba[index++] = (c1 + c2) / 2;
            // BLUE
            rgba[index++] = b;
            // ALPHA
            rgba[index++] = alpha;
        }
    }
}
//Cuda consumer init
static int _initialized = 0;
static int *CudaDeviceCreate(CudaConsumerCtx *cudaConsumer, NvU32 ippNum)
{
    CUdevice device;
    CUcontext oldContext;
    NvU32 i;

    if(!_initialized) {
        if(cuInit(0)) {
            LOG_DBG("failed to initialize Cuda\n");
            return NULL;
        }
        _initialized = NV_TRUE;
    }

    for(i = 0; i < ippNum; i++){
        if(cuDeviceGet(&device, 0)) {
            LOG_DBG("failed to get Cuda device\n");
            return NULL;
        }
        if(cuCtxCreate(&cudaConsumer->context[i], 0, device)) {
            LOG_DBG("failed to create Cuda context\n");
            return NULL;
        }
        cuCtxPopCurrent(&oldContext);
    }
    return NULL;
}

static NvMediaStatus
cudaprocThreadFunc (
    void *data)
{
    CudaConsumerCtx *ctx = (CudaConsumerCtx *)(((cudaThreadData *)data)->ctx);
    CUcontext oldContext;
    CUresult cuStatus;
    CUarray cudaArr = NULL;
    CUeglFrame cudaEgl;
    CUgraphicsResource cudaResource;
    FILE *fp = NULL;
    unsigned int i, cam_idx = ((cudaThreadData *)data)->threadId;
# ifdef EGL_NV_stream_metadata
    NvU32 buf;
    NvU32 crc_local = 0;
    NvU32 mismatch_count = 0;
    NvMediaSurfaceType imageType;
    NvU32 rawBytesPerPixel = 1;
    NvU32 total_frames = 0;
#endif //EGL_NV_stream_metadata

#ifdef PROFILE_ENABLE
    struct timeval tv1, tv2, tv3;
    long int diff1, diff2;
#endif
    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_BAD_PARAMETER;
    }
    LOG_DBG("Cuda consumer thread is active\n");

    if(ctx->savetofile && ctx->outFileName[cam_idx]) {
        fp = fopen(ctx->outFileName[cam_idx], "wb");
        if(!fp) {
            LOG_ERR("WriteFrame: file open failed: %s\n", ctx->outFileName[cam_idx]);
            return NVMEDIA_STATUS_ERROR;
        }
    }

    while(!Producerstopped) {
        EGLint streamState = 0;

        if(!eglQueryStreamKHR(ctx->display,
                              ctx->eglStream[cam_idx],
                              EGL_STREAM_STATE_KHR,
                              &streamState)) {
            LOG_ERR("Cuda consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            LOG_DBG("CUDA Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
            *ctx->quit = NV_TRUE;
            goto done;
        }
        if(streamState != EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
            usleep(5000);
            LOG_DBG("CUDA Consumer: eglstream state not new available for cam %d..sleeping\n", cam_idx);
            if(*ctx->quit){
                break;
            }
            continue;
        }

        cuCtxPushCurrent(ctx->context[cam_idx]);
#ifdef PROFILE_ENABLE
        gettimeofday(&tv1, NULL);
#endif
        cuStatus = cuEGLStreamConsumerAcquireFrame(&(ctx->cudaConn[cam_idx]),
                                                   &cudaResource,
                                                   NULL,
                                                   16000);
# ifdef EGL_NV_stream_metadata
        if(ctx->checkcrc) {
        //Query metaData from the stream
            if(cuStatus == CUDA_SUCCESS ) {
                if(!eglQueryStreamMetadataNV(
                    ctx->display,                // display
                    ctx->eglStream[cam_idx],     // stream
                    EGL_CONSUMER_METADATA_NV,    // name
                    0,                           // block id
                    0,                           // offset
                    4,                           // size
                    (void*)(&buf))) {            // data
                    LOG_ERR("eglQueryStreamMetadataNV failed\n");
                }
                total_frames = total_frames + 1;
            }
        }
#endif //EGL_NV_stream_metadata

#ifdef PROFILE_ENABLE
        gettimeofday(&tv2, NULL);
#endif
        if(cuStatus != CUDA_SUCCESS) {
            LOG_ERR("CUDA Consumer: cuEGLStreamConsumerAcquireFrame failed with status %d\n", cuStatus);
        }
        cuStatus = cuCtxSynchronize();
        if(cuStatus != CUDA_SUCCESS) {
            LOG_ERR("cuda ctx synchronize  failed for index %d with status %d\n", cam_idx, cuStatus);
        }
#ifdef PROFILE_ENABLE
        gettimeofday(&tv3, NULL);

        if((ctx->frameCount % DUMP_ANDPROFILE_INTERVAL) == 0) {
            CALC_TIMEDIFF(tv1, tv2, diff1);
            CALC_TIMEDIFF(tv2, tv3, diff2);
            LOG_ERR("CUDACONS:frm %d cam %d, diff1 %ld diff2 %ld\n", ctx->frameCount, cam_idx, diff1, diff2);
        }
#endif
        cuCtxPopCurrent(&oldContext);

        if (cuStatus == CUDA_SUCCESS) {
            if (ctx->savetofile) {
                CUdeviceptr pDevPtr = NULL;
                int bufferSize;
                unsigned char *pCudaCopyMem = NULL;
                unsigned int copyWidthInBytes=0, copyHeight=0;

                LOG_DBG("Cuda consumer Acquired Frame for cuda copy\n");
                //CUDA memcpy
                cuStatus = cuGraphicsResourceGetMappedEglFrame(&cudaEgl, cudaResource,0,0);

                for (i=0; i<cudaEgl.planeCount; i++) {
                    cuCtxPushCurrent(ctx->context[cam_idx]);
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
                        cuCtxPushCurrent(ctx->context[cam_idx]);
                        cuStatus = cuMemcpyDtoH(pCudaCopyMem, pDevPtr, bufferSize);
                        if(cuStatus != CUDA_SUCCESS) {
                            LOG_DBG("cuda_consumer: pitch linear Memcpy failed, bufferSize =%d\n", bufferSize);
                            free(pCudaCopyMem);
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
                        cuCtxPushCurrent(ctx->context[cam_idx]);
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
                        if((ctx->frameCount % DUMP_ANDPROFILE_INTERVAL) == 0) {
                            if(fp) {
                                if (cudaEgl.frameType == CU_EGL_FRAME_TYPE_PITCH) {

                                    LOG_DBG("CUDA output Pitch linear: w %d h %d pitch %d\n", cudaEgl.width, cudaEgl.height, cudaEgl.pitch);

                                    NvU8 *rgbabuf = malloc((cudaEgl.width / 2) *
                                                           (cudaEgl.height / 2) * 4);
                                    if(!rgbabuf) {
                                        LOG_ERR("Failed to allocate rgbabuffer \n", ctx->outFileName[cam_idx]);
                                    }

                                    convert_Bayer_RGBA(pCudaCopyMem, rgbabuf, cudaEgl.width,
                                                       cudaEgl.height, cudaEgl.pitch);
                                    if (fwrite(rgbabuf, (cudaEgl.width / 2) *
                                               (cudaEgl.height / 2) * 4, 1, fp) != 1) {
                                        LOG_ERR("Cuda consumer: output file write failed\n");
                                        *ctx->quit = NV_TRUE;
                                        free(pCudaCopyMem);
                                        free(rgbabuf);
                                        goto done;
                                    }
                                    free(rgbabuf);
                                }
                                else {
                                    LOG_ERR("Writing Block linear format not supported \n");
                                }
                            }
                        }
                    }
                }
# ifdef EGL_NV_stream_metadata
                if(ctx->checkcrc) {
                    /* calculate the crc and match with crc acquired from the metadata */
                    crc_local = 0;
                    /* These if checks are hack because of the cuda Bug 200247081,there is no color format
                       specifed for RAW image type. Once this cuda bug is fixed, these if checks must be
                       modified with the correct logic */
                    if(cudaEgl.planeCount >= 2) {
                        imageType = NvMediaSurfaceType_Image_YUV_420;
                    } else if(cudaEgl.cuFormat == CU_AD_FORMAT_UNSIGNED_INT16) {
                        imageType = NvMediaSurfaceType_Image_RAW;
                        rawBytesPerPixel = 2;
                    } else {
                        imageType = NvMediaSurfaceType_Image_RGBA;
                    }

                    GetFrameCrc(pCudaCopyMem,cudaEgl.width,cudaEgl.height,cudaEgl.pitch,imageType,&crc_local,rawBytesPerPixel);
                    if (crc_local != buf) {
                        mismatch_count++;
                    }
                }
#endif //EGL_NV_stream_metadata
                LOG_DBG("Cuda consumer cudacopy finish frame %d\n", ctx->frameCount);
                free(pCudaCopyMem);
            }
            cuCtxPushCurrent(ctx->context[cam_idx]);
            cuStatus = cuEGLStreamConsumerReleaseFrame(&ctx->cudaConn[cam_idx],
                                                       cudaResource,
                                                       NULL);
            cuCtxPopCurrent(&oldContext);
            if (cuStatus != CUDA_SUCCESS) {
                *ctx->quit = NV_TRUE;
                goto done;
            }
        } else {
            LOG_ERR("cuda acquire failed cuStatus=%d\n", cuStatus);
        }
        ctx->frameCount++;
    }
done:
    if(fp){
        fclose(fp);
    }

    ctx->procThreadExited[cam_idx] = NV_TRUE;
    LOG_DBG("camidx %d, procexited %d\n", cam_idx, ctx->procThreadExited[cam_idx]);
# ifdef EGL_NV_stream_metadata
    if(ctx->checkcrc) {
        if(mismatch_count == 0) {
           LOG_MSG("Camidx = %d,Crc matching for all the frames\n",cam_idx);
        }
        else {
           LOG_MSG("Camidx %d, Total Frames  = %d,No of  Crc mismatches = %d \n", cam_idx, total_frames, mismatch_count);
        }
    }
#endif //EGL_NV_stream_metadata
    return NVMEDIA_STATUS_OK;
}

CudaConsumerCtx * CudaConsumerInit(NvMediaBool *consumerDone, EglStreamClient *streamClient, TestArgs *args, InteropContext *interopCtx)
{
    CudaConsumerCtx *cudaConsumer;
    CUresult curesult;
    CUcontext oldContext;
    NvU32 i;
    char strbuf[8];

    LOG_DBG("%s: \n", __FUNCTION__);

    cudaConsumer = calloc(1, sizeof(CudaConsumerCtx));
    if(!cudaConsumer) {
        LOG_ERR("Failed to alloc memory for cudaConsumerCtx \n");
        return NULL;
    }

    cudaConsumer->display        = streamClient->display_dGPU;
    cudaConsumer->consumerDone   = consumerDone;
    cudaConsumer->ippNum = interopCtx->ippNum;
    cudaConsumer->quit = interopCtx->quit;
    cudaConsumer->savetofile = interopCtx->savetofile;
    cudaConsumer->checkcrc = args->checkcrc;
    if(cudaConsumer->savetofile) {
        for(i = 0; i < cudaConsumer->ippNum; i++) {
            memset(cudaConsumer->outFileName[i], 0, MAX_STRING_SIZE);
            memset(strbuf, 0, sizeof(strbuf));
            strncpy(cudaConsumer->outFileName[i], interopCtx->filename, MAX_STRING_SIZE - 16);
            strcat(cudaConsumer->outFileName[i], "_cam");
            sprintf(strbuf, "%d", i);
            strcat(cudaConsumer->outFileName[i], strbuf);
            strcat(cudaConsumer->outFileName[i], ".rgba");
            LOG_MSG("Writing Output to %s\n", cudaConsumer->outFileName[i]);
        }
    }


    for(i = 0; i < cudaConsumer->ippNum; i++) {
        cudaConsumer->eglStream[i]  = streamClient->eglStream_dGPU[i];
    }

    CudaDeviceCreate(cudaConsumer, cudaConsumer->ippNum);

    for(i = 0; i < cudaConsumer->ippNum; i++) {
        cuCtxPushCurrent(cudaConsumer->context[i]);
        LOG_DBG("Connect CUDA consumer\n");
        if (CUDA_SUCCESS !=
         (curesult = cuEGLStreamConsumerConnect(&cudaConsumer->cudaConn[i], cudaConsumer->eglStream[i]))) {
            LOG_ERR("Connect CUDA consumer ERROR %d\n", curesult);
            return NULL;
       }
       cuCtxPopCurrent(&oldContext);
    }
    interopCtx->consumerInitDone = NVMEDIA_TRUE;

    for(i = 0; i < cudaConsumer->ippNum; i++) {
        cudaConsumer->threadinfo[i].ctx = cudaConsumer;
        cudaConsumer->threadinfo[i].threadId = i;
        cudaConsumer->procThreadExited[i] = NV_FALSE;
        if(IsFailed(NvThreadCreate(&cudaConsumer->procThread[i],
                    &cudaprocThreadFunc,
                    (void *)&cudaConsumer->threadinfo[i],
                                   NV_THREAD_PRIORITY_NORMAL))) {
            LOG_ERR("Cuda consumer init: Unable to create process thread\n");
            cudaConsumer->procThreadExited[i] = NV_TRUE;
            return NULL;
        }
    }

    return cudaConsumer;
}

void CudaConsumerStop(CudaConsumerCtx *cudaConsumer)
{
    NvU32 i;
    LOG_DBG("%s: \n", __func__);

    if(cudaConsumer) {
        *cudaConsumer->quit = NV_TRUE;

        for(i = 0; i < cudaConsumer->ippNum; i++) {
            if (cudaConsumer->procThread[i]) {
                LOG_DBG("%s Waiting for cuda threads to exit procexited %d, camidx %d\n",
                        __func__, cudaConsumer->procThreadExited[i], i);
                NvThreadDestroy(cudaConsumer->procThread[i]);
            }
        }
        *cudaConsumer->consumerDone = NV_TRUE;
    }
    else {
        LOG_ERR("%s: CudaconsumerCtx is NULL\n");
    }
}
void CudaConsumerFini(CudaConsumerCtx *cudaConsumer)
{
    CUresult curesult;
    NvU32 i;
    LOG_DBG("%s :\n", __func__);

    if(cudaConsumer) {
        for(i = 0; i < cudaConsumer->ippNum; i++) {
            if (CUDA_SUCCESS !=
                (curesult = cuEGLStreamConsumerDisconnect(&cudaConsumer->cudaConn[i]))) {
                LOG_ERR("Disconnect CUDA consumer ERROR %d\n", curesult);
            }
        }
        free(cudaConsumer);
    }
    else {
        LOG_ERR("%s: CudaconsumerCtx is NULL\n");
    }
}

