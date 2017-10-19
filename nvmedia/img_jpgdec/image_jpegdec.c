/*
 * Copyright (c) 2014-2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include "cmdline.h"
#include "config_parser.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvcommon.h"
#include "nvmedia_image.h"
#include "nvmedia_ijpd.h"
#include "surf_utils.h"

int decodeStop = 0;

#define MAX_BITSTREAM_SIZE (10 * 1024 * 1024)
#define ALIGN_16(_x) ((_x + 15) & (~15))

/* Signal Handler for SIGINT */
static void sigintHandler(int sig_num)
{
    LOG_MSG("\n Exiting decode process \n");
    decodeStop = 1;
}

static NvMediaStatus
CheckVersion(void)
{
    NvMediaVersion version;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    memset(&version, 0, sizeof(NvMediaVersion));

    NVMEDIA_SET_VERSION(version, NVMEDIA_VERSION_MAJOR,
                                 NVMEDIA_VERSION_MINOR);
    status = NvMediaCheckVersion(&version);
    if(status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_IMAGE_VERSION_MAJOR,
                                 NVMEDIA_IMAGE_VERSION_MINOR);
    status = NvMediaImageCheckVersion(&version);
    if(status != NVMEDIA_STATUS_OK)
        return status;

    NVMEDIA_SET_VERSION(version, NVMEDIA_IJPD_VERSION_MAJOR,
                                 NVMEDIA_IJPD_VERSION_MINOR);
    status = NvMediaIJPDCheckVersion(&version);

    return status;
}

int main(int argc, char *argv[])
{
    TestArgs args;
    FILE *crcFile = NULL, *streamFile = NULL;
    char inFileName[FILE_NAME_SIZE];
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaDevice *device = NULL;
    NvMediaImage *imageFrame = NULL;
    NvMediaIJPD *testDecoder = NULL;
    NVMEDIAJPEGDecInfo jpegInfo;
    NvMediaRect dstRect;
    NvMediaBool nextFrameFlag = NVMEDIA_TRUE;
    long fileLength;
    NvMediaBitstreamBuffer bsBuffer;
    NvU32 frameCounter = 0;//, calcCrc = 0;
    NvMediaImageSurfaceMap surfaceMap;
    NvU64 startTime, endTime;
    double elapse = 0;

    signal(SIGINT, sigintHandler);

    memset(&args, 0, sizeof(TestArgs));
    memset(&bsBuffer, 0, sizeof(NvMediaBitstreamBuffer));

    LOG_DBG("main: Parsing jpeg decode command\n");
    if(!ParseArgs(argc, argv, &args)) {
        LOG_ERR("main: Parsing arguments failed\n");
        return -1;
    }

    if(CheckVersion() != NVMEDIA_STATUS_OK) {
        return -1;
    }

    if(args.crcoption.crcGenMode && args.crcoption.crcCheckMode) {
        LOG_ERR("main: crcGenMode and crcCheckMode cannot be enabled at the same time\n");
        return -1;
    }

    // Read JPEG stream, get stream info
    sprintf(inFileName, args.infile, frameCounter);
    streamFile = fopen(inFileName, "rb");
    if(!streamFile) {
       LOG_ERR("main: Error opening '%s' for reading, decode done!\n", inFileName);
       nextFrameFlag = NVMEDIA_FALSE;
       return -1;
    }
    fseek(streamFile, 0, SEEK_END);
    fileLength = ftell(streamFile);
    if(!fileLength) {
       LOG_ERR("main: Zero file length for file %s, len=%d\n", args.infile, (int)fileLength);
       fclose(streamFile);
       return -1;
    }

    bsBuffer.bitstream = malloc(fileLength);
    if(!bsBuffer.bitstream) {
       LOG_ERR("main: Error allocating %d bytes\n", fileLength);
       return -1;
    }
    bsBuffer.bitstreamBytes = fileLength;
    fseek(streamFile, 0, SEEK_SET);
    if(fread(bsBuffer.bitstream, fileLength, 1, streamFile) != 1) {
       LOG_ERR("main: Error read JPEG file %s for %d bytes\n", inFileName, fileLength);
       goto fail;
    }
    fclose(streamFile);

    status = NvMediaIJPDGetInfo(&jpegInfo, 1, &bsBuffer);
    free(bsBuffer.bitstream);
    bsBuffer.bitstream = NULL;
    if(status != NVMEDIA_STATUS_OK) {
       LOG_ERR("main: Can't get JPEG stream info.\n");
       return -1;
    }

    LOG_DBG("main: NvMediaIJPDGetInfo: width=%d, height=%d, partialAccel=%d\n",
            jpegInfo.width, jpegInfo.height, jpegInfo.partialAccel);

    args.supportPartialAccel = jpegInfo.partialAccel;
    if(!args.outputWidth || !args.outputHeight) {
        args.outputWidth = jpegInfo.width;
        args.outputHeight = jpegInfo.height;
    }

    // align width and height to multiple of 16
    args.outputWidth = ALIGN_16(args.outputWidth);
    args.outputHeight = ALIGN_16(args.outputHeight);

    if(args.outputSurfType == NvMediaSurfaceType_Image_RGBA) {
        dstRect.x0 = 0;
        dstRect.y0 = 0;
        dstRect.x1 = args.outputWidth;
        dstRect.y1 = args.outputHeight;
    }

    LOG_DBG("main: NvMediaDeviceCreate\n");
    device = NvMediaDeviceCreate();
    if(!device) {
        LOG_ERR("main: NvMediaDeviceCreate failed\n");
        return -1;
    }

    imageFrame =  NvMediaImageCreate(device,                            // device
                                     args.outputSurfType,               // surface type
                                     NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE,  // image class
                                     1,                                 // images count
                                     args.outputWidth,                  // surf width
                                     args.outputHeight,                 // surf height
                                     args.outputSurfAttributes,         // attributes
                                     &args.outputSurfAdvConfig);        // config

    if(!imageFrame) {
        LOG_ERR("main: NvMediaImageCreate failed\n");
        return -1;
    }

    testDecoder = NvMediaIJPDCreate(device,
                                   args.maxWidth,
                                   args.maxHeight,
                                   args.maxBitstreamBytes,
                                   args.supportPartialAccel);
    if(!testDecoder) {
        LOG_ERR("main: NvMediaIJPDCreate failed\n");
        goto fail;
    }

    LOG_DBG("main: NvMediaIJPDCreate, %p\n", testDecoder);

    if(args.crcoption.crcGenMode){
        crcFile = fopen(args.crcoption.crcFilename, "wt");
        if(!crcFile){
            LOG_ERR("main: Cannot open crc gen file for writing\n");
            goto fail;
        }
    } else if(args.crcoption.crcCheckMode){
        crcFile = fopen(args.crcoption.crcFilename, "rb");
        if(!crcFile){
            LOG_ERR("main: Cannot open crc gen file for reading\n");
            goto fail;
        }
    }

    while(nextFrameFlag && !decodeStop) {
        // Read JPEG stream
        sprintf(inFileName, args.infile, frameCounter);
        streamFile = fopen(inFileName, "rb");
        if(!streamFile) {
            LOG_ERR("main: Error opening '%s' for reading, decode done!\n", inFileName);
            nextFrameFlag = NVMEDIA_FALSE;
            goto done;
        }
        fseek(streamFile, 0, SEEK_END);
        fileLength = ftell(streamFile);
        if(!fileLength) {
           LOG_ERR("main: Zero file length for file %s, len=%d\n", args.infile, (int)fileLength);
           fclose(streamFile);
           goto fail;
        }

        bsBuffer.bitstream = malloc(fileLength);
        if(!bsBuffer.bitstream) {
           LOG_ERR("main: Error allocating %d bytes\n", fileLength);
           goto fail;
        }
        bsBuffer.bitstreamBytes = fileLength;
        fseek(streamFile, 0, SEEK_SET);
        if(fread(bsBuffer.bitstream, fileLength, 1, streamFile) != 1) {
           LOG_ERR("main: Error read JPEG file %s for %d bytes\n", inFileName, fileLength);
           goto fail;
        }
        fclose(streamFile);
        LOG_DBG("main: Read JPEG stream %d done\n", frameCounter);

        GetTimeMicroSec(&startTime);
        LOG_DBG("main: Decoding frame #%d\n", frameCounter);

        if(args.outputSurfType == NvMediaSurfaceType_Image_RGBA) {
           status = NvMediaIJPDRender(testDecoder,
                                      imageFrame,
                                      NULL,
                                      &dstRect,
                                      args.downscaleLog2,
                                      1,
                                      &bsBuffer,
                                      0);
        } else {
           status = NvMediaIJPDRenderYUV(testDecoder,
                                         imageFrame,
                                         args.downscaleLog2,
                                         1,
                                         &bsBuffer,
                                         0);
        }
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: NvMediaIJPDRender(YUV) failed: %x\n", status);
            goto fail;
        }
        if(NvMediaImageLock(imageFrame, NVMEDIA_IMAGE_ACCESS_WRITE, &surfaceMap) != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: NvMediaImageLock failed\n");
            return NVMEDIA_STATUS_ERROR;
        }

        NvMediaImageUnlock(imageFrame);
        GetTimeMicroSec(&endTime);
        elapse += (double)(endTime - startTime) / 1000.0;

        status = WriteImage(args.outfile,
                           imageFrame,
                           NVMEDIA_TRUE,
                           frameCounter ? NVMEDIA_TRUE : NVMEDIA_FALSE,
                           0);

        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: WriteImage failed: %x\n", status);
            goto fail;
        }

        free(bsBuffer.bitstream);
        bsBuffer.bitstream = NULL;

        if(args.crcoption.crcGenMode){
            NvU32 calcCrc;
            calcCrc = 0;
            status = GetImageCrc(imageFrame,
                                 args.outputWidth,
                                 args.outputHeight,
                                 &calcCrc,
                                 1);
            if(status != NVMEDIA_STATUS_OK) {
                LOG_ERR("main: GetImageCrc failed: %x\n", status);
                goto fail;
            }

            if(!fprintf(crcFile, "%08x\n",calcCrc)) {
                LOG_ERR("main: Failed writing calculated CRC to file %s\n", crcFile);
                goto fail;
            }
        } else if(args.crcoption.crcCheckMode){
            NvU32 refCrc;
            NvMediaBool isMatching;
            if(fscanf(crcFile, "%8x\n", &refCrc) == 1) {
                status = CheckImageCrc(imageFrame,
                                       args.outputWidth,
                                       args.outputHeight,
                                       refCrc,
                                       &isMatching,
                                       1); /* Passing 1 as default value in rawBytesPerPixel */
                if(status != NVMEDIA_STATUS_OK) {
                    LOG_ERR("main: CheckImageCrc failed: %x\n", status);
                    goto fail;
                }

                if(isMatching != NVMEDIA_TRUE){
                    LOG_ERR("main: Frame %d crc does not match with ref crc 0x%x\n",
                            frameCounter, refCrc);
                    goto fail;
                }
            } else {
                LOG_ERR("main: Failed checking CRC. Failed reading file %s\n", crcFile);
                goto fail;
            }

        }
        // Next frame
        frameCounter++;

        if(frameCounter == args.frameNum) {
            nextFrameFlag = NVMEDIA_FALSE;
        }
    }

done:
    //get decoding time info
    LOG_MSG("\nTotal Decoding time for %d frames: %.3f ms\n", frameCounter, elapse);
    LOG_MSG("Decoding time per frame %.4f ms \n", elapse / frameCounter);
    LOG_MSG("\nTotal decoded frames = %d\n", frameCounter);
    if (args.crcoption.crcGenMode){
        LOG_MSG("\n***crc gold file %s has been generated***\n", args.crcoption.crcFilename);
    } else if (args.crcoption.crcCheckMode){
        LOG_MSG("\n***crc checking with file %s is successful\n", args.crcoption.crcFilename);
    }
    LOG_MSG("\n***DECODING PROCESS ENDED SUCCESSFULY***\n");

fail:
    if(imageFrame) {
       NvMediaImageDestroy(imageFrame);
    }

    if (testDecoder)
        NvMediaIJPDDestroy(testDecoder);

    if(device) {
        LOG_DBG("main: Destroying device\n");
        NvMediaDeviceDestroy(device);
    }

    if(crcFile) {
        fclose(crcFile);
    }

    if (bsBuffer.bitstream)
        free(bsBuffer.bitstream);

    return 0;
}
