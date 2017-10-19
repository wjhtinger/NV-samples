/*
 * Copyright (c) 2012-2017, NVIDIA CORPORATION.  All rights reserved. All
 * information contained herein is proprietary and confidential to NVIDIA
 * Corporation.  Any use, reproduction, or disclosure without the written
 * permission of NVIDIA Corporation is prohibited.
 */

#include "nvmvid_encode.h"
#include "config_parser.h"
#include "log_utils.h"

int EncodeOneFrame(InputParameters *pParams, NvMediaVideoSurface *pVideoSurface,
                   NvMediaRect *sourceRect)
{
    InputParameters *pInputParams = pParams;
    NvMediaVideoEncoder *h264Encoder = pInputParams->h264Encoder;
    FILE *outputFile = pInputParams->outputFile;
    NvMediaEncodePicParamsH264 *encodePicParams = &pInputParams->encodePicParams;
    NvU32 uNumBytes, uNumBytesAvailable = 0;
    NvU8 *pBuffer;

    //set one frame params, default = 0
    memset(encodePicParams, 0, sizeof(NvMediaEncodePicParamsH264));
    //IPP mode
    encodePicParams->pictureType = NVMEDIA_ENCODE_PIC_TYPE_AUTOSELECT;

    NvMediaStatus status = NvMediaVideoEncoderFeedFrame(
                                h264Encoder,    // *encoder
                                pVideoSurface,  // *frame
                                sourceRect,     // *sourceRect
                                encodePicParams
        );
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("\nNvMediaVideoEncoderFeedFrame failed: %x\n",
                                status);
            goto fail;
        }
        NvMediaBool bEncodeFrameDone = NVMEDIA_FALSE;
        while(!bEncodeFrameDone) {
            uNumBytesAvailable = 0;
            uNumBytes = 0;
            status = NvMediaVideoEncoderBitsAvailable(h264Encoder,
                                &uNumBytesAvailable,
                                NVMEDIA_ENCODE_BLOCKING_TYPE_IF_PENDING,
                                NVMEDIA_VIDEO_ENCODER_TIMEOUT_INFINITE);
            switch(status) {
                case NVMEDIA_STATUS_OK:
                //Add extra header space
                    pBuffer = malloc(uNumBytesAvailable+256);
                    if(!pBuffer) {
                        LOG_ERR("Error allocating %d bytes\n",
                                        uNumBytesAvailable);
                        goto fail;
                    }
                    memset(pBuffer, 0xE5, uNumBytesAvailable);
                    status = NvMediaVideoEncoderGetBits(h264Encoder,
                                                        &uNumBytes,
                                                        pBuffer);

                    if(status != NVMEDIA_STATUS_OK &&
                        status != NVMEDIA_STATUS_NONE_PENDING) {
                        LOG_ERR("Error getting encoded bits\n");
                        goto fail;
                    }

                    if(uNumBytes != uNumBytesAvailable) {
                        LOG_ERR("Error-byte counts do not match %d vs. %d\n", uNumBytesAvailable, uNumBytes);
                        goto fail;
                    }

                    if(fwrite(pBuffer,
                                uNumBytesAvailable,
                                1,
                                outputFile) != 1) {
                        LOG_ERR("Error writing %d bytes\n",
                                        uNumBytesAvailable);
                        goto fail;
                    }
                    if (pBuffer) {
                        free(pBuffer);
                        pBuffer = NULL;
                    }

                    bEncodeFrameDone = 1;
                    break;
                case NVMEDIA_STATUS_PENDING:
                    LOG_DBG("Status - pending\n");
                    break;
                case NVMEDIA_STATUS_NONE_PENDING:
                    LOG_ERR("Error - no encoded data is pending\n");
                    goto fail;
                default:
                    LOG_ERR("Error occured\n");
                    goto fail;
            }
        }
    return 0;
fail:
    return 1;
}

int InitEncoder(InputParameters *pParams, int width, int height, NvMediaSurfaceType surfaceType)
{
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    InputParameters *pInputParams = pParams;
    NvMediaVideoEncoder **ph264Encoder = &pInputParams->h264Encoder;
    NvMediaVideoEncoder *h264Encoder = NULL;

    NvMediaDevice  *device;
    NvMediaEncodeInitializeParamsH264 encoderInitParams;
    pInputParams->encodeConfigH264Params.h264VUIParameters =
                  calloc(1, sizeof(NvMediaEncodeConfigH264VUIParams));

    device = NvMediaDeviceCreate();
    if(!device) {
        LOG_ERR("NvMediaDeviceCreate failed\n");
        goto fail;
    }

    NvU32 uFrameCounter = 0;
    LOG_DBG("Encode start from frame %d\n", uFrameCounter);

    LOG_DBG("Setting encoder initialization params\n");
    memset(&encoderInitParams, 0, sizeof(encoderInitParams));

    encoderInitParams.encodeHeight = height;
    encoderInitParams.encodeWidth = width;
    encoderInitParams.frameRateDen = 1;
    encoderInitParams.frameRateNum = 30;
    encoderInitParams.maxNumRefFrames = 2;
    encoderInitParams.enableExternalMEHints = NVMEDIA_FALSE;

    h264Encoder = NvMediaVideoEncoderCreate(
                            device,
                            NVMEDIA_VIDEO_CODEC_H264,     // codec
                            &encoderInitParams,           // init params
                            (surfaceType == NvMediaSurfaceType_R8G8B8A8_BottomOrigin ||
                             surfaceType == NvMediaSurfaceType_R8G8B8A8)?
                            NvMediaSurfaceType_R8G8B8A8:
                            NvMediaSurfaceType_Video_420, // surfaceType
                            0,                            // maxInputBuffering
                            0,                            // maxOutputBuffering
                            device);

    if(!h264Encoder) {
        LOG_ERR("NvMediaVideoEncoderCreate failed\n");
        goto fail;
    }
    *ph264Encoder = h264Encoder;

    LOG_DBG("NvMediaVideoEncoderCreate, %p\n", h264Encoder);

    //set RC param
    pInputParams->encodeConfigH264Params.rcParams.rateControlMode = 1;    //Const QP
    pInputParams->encodeConfigH264Params.rcParams.numBFrames      = 0;
    //Const QP mode
    pInputParams->encodeConfigH264Params.rcParams.params.const_qp.constQP.qpIntra = 27;
    pInputParams->encodeConfigH264Params.rcParams.params.const_qp.constQP.qpInterP = 29;

    status = NvMediaVideoEncoderSetConfiguration(h264Encoder,
                                        &pInputParams->encodeConfigH264Params);

    if(status != NVMEDIA_STATUS_OK) {
       LOG_ERR("Main SetConfiguration failed\n");
       goto fail;
    }
    LOG_DBG("NvMediaVideoEncoderSetConfiguration done\n");
    return 0;
fail:
    return 1;
}
