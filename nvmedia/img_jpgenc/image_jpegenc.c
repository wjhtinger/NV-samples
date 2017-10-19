/*
 * Copyright (c) 2014-2017, NVIDIA CORPORATION.  All rights reserved.
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
#include "nvmedia_ijpe.h"
#include "surf_utils.h"

int encodeStop = 0;

#define MAX_BITSTREAM_SIZE (10 * 1024 * 1024)

/* Signal Handler for SIGINT */
static void sigintHandler(int sig_num)
{
    LOG_MSG("\n Exiting encode process \n");
    encodeStop = 1;
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

    NVMEDIA_SET_VERSION(version, NVMEDIA_IJPE_VERSION_MAJOR,
                                 NVMEDIA_IJPE_VERSION_MINOR);
    status = NvMediaIJPECheckVersion(&version);

    return status;
}

int main(int argc, char *argv[])
{
    TestArgs args;
    FILE *crcFile = NULL, *outputFile = NULL, *streamFile = NULL;
    char outFileName[FILE_NAME_SIZE];
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    NvMediaDevice *device;
    NvMediaImage *imageFrame = NULL;
    NvMediaIJPE *testEncoder = NULL;
    NvMediaBool nextFrameFlag = NVMEDIA_TRUE, encodeDoneFlag;
    long long totalBytes = 0;
    long fileLength;
    NvU8 *buffer = NULL;
    NvU32 framesNum = 0, frameCounter = 0, bytes, bytesAvailable = 0, calcCrc = 0;
    NvU32 imageSize = 0;
    NvU64 startTime, endTime1, endTime2;
    double encodeTime = 0;
    double getbitsTime = 0;

    signal(SIGINT, sigintHandler);

    memset(&args, 0, sizeof(TestArgs));

    LOG_DBG("main: Parsing jpeg encode command\n");
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

    imageSize = (args.inputWidth * args.inputHeight * 3) / 2;

    LOG_DBG("main: NvMediaDeviceCreate\n");
    device = NvMediaDeviceCreate();
    if(!device) {
        LOG_ERR("main: NvMediaDeviceCreate failed\n");
        return -1;
    }

    LOG_DBG("main: Encode start from frame %d, imageSize=%d\n", frameCounter, imageSize);

    streamFile = fopen(args.infile, "rb");
    if(!streamFile) {
        LOG_ERR("main: Error opening '%s' for reading\n", args.infile);
        goto fail;
    }
    fseek(streamFile, 0, SEEK_END);
    fileLength = ftell(streamFile);
    fclose(streamFile);
    if(!fileLength) {
       LOG_ERR("main: Zero file length for file %s, len=%d\n", args.infile, (int)fileLength);
       goto fail;
    }
    framesNum = fileLength / imageSize;

    imageFrame =  NvMediaImageCreate(device,                            // device
                                     args.inputSurfType,                // surface type
                                     NVMEDIA_IMAGE_CLASS_SINGLE_IMAGE,  // image class
                                     1,                                 // images count
                                     args.inputWidth,                   // surf width
                                     args.inputHeight,                  // surf height
                                     args.inputSurfAttributes,          // attributes
                                     &args.inputSurfAdvConfig);         // config


    if(!imageFrame) {
        LOG_ERR("main: NvMediaImageCreate failed\n");
        goto fail;
    }

    testEncoder = NvMediaIJPECreate(device,
                                   args.inputSurfType,               // inputFormat
                                   args.maxOutputBuffering,          // maxOutputBuffering
                                   MAX_BITSTREAM_SIZE);              // maxBitstreamBytes
    if(!testEncoder) {
        LOG_ERR("main: NvMediaIJPECreate failed\n");
        goto fail;
    }

    LOG_DBG("main: NvMediaIJPECreate, %p\n", testEncoder);

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

    while(nextFrameFlag && !encodeStop) {
        // Read Frame
        LOG_DBG("main: Reading YUV frame %d from file %s to image surface location: %p. (W:%d, H:%d)\n",
                 frameCounter, args.infile, imageFrame, args.inputWidth, args.inputHeight);
        status = ReadImage(args.infile,
                           frameCounter,
                           args.inputWidth,
                           args.inputHeight,
                           imageFrame,
                           NVMEDIA_TRUE,            //inputUVOrderFlag
                           1);                      //rawBytesPerPixel
        if(status != NVMEDIA_STATUS_OK) {
           LOG_ERR("readYUVFile failed\n");
           goto fail;
        }
        LOG_DBG("main: ReadYUVFrame %d/%d done\n", frameCounter, framesNum-1);

        GetTimeMicroSec(&startTime);
        LOG_DBG("main: Encoding frame #%d\n", frameCounter);
        status = NvMediaIJPEFeedFrame(testEncoder,
                                     imageFrame,
                                     args.quality);
        if(status != NVMEDIA_STATUS_OK) {
            LOG_ERR("main: NvMediaIJPEFeedFrameQuality failed: %x\n", status);
            goto fail;
        }

        encodeDoneFlag = NVMEDIA_FALSE;
        while(!encodeDoneFlag) {
            bytesAvailable = 0;
            bytes = 0;
            status = NvMediaIJPEBitsAvailable(testEncoder,
                                             &bytesAvailable,
                                             NVMEDIA_ENCODE_BLOCKING_TYPE_IF_PENDING,
                                             NVMEDIA_VIDEO_ENCODER_TIMEOUT_INFINITE);
            switch(status) {
                case NVMEDIA_STATUS_OK:
                    // Encode Time
                    GetTimeMicroSec(&endTime1);
                    encodeTime += (double)(endTime1 - startTime) / 1000.0;

                    buffer = malloc(bytesAvailable);
                    if(!buffer) {
                        LOG_ERR("main: Error allocating %d bytes\n", bytesAvailable);
                        goto fail;
                    }
                    status = NvMediaIJPEGetBits(testEncoder, &bytes, buffer, 0);
                    if(status != NVMEDIA_STATUS_OK && status != NVMEDIA_STATUS_NONE_PENDING) {
                        LOG_ERR("main: Error getting encoded bits\n");
                        goto fail;
                    }

                    if(bytes != bytesAvailable) {
                        LOG_ERR("main: byte counts do not match %d vs. %d\n", bytesAvailable, bytes);
                        goto fail;
                    }

                    GetTimeMicroSec(&endTime2);
                    getbitsTime += (double)(endTime2 - endTime1) / 1000.0;

                    LOG_DBG("main: Opening output file\n");
                    sprintf(outFileName, args.outfile, frameCounter);
                    outputFile = fopen(outFileName, "w+");
                    if(!outputFile) {
                        LOG_ERR("main: Failed opening '%s' file for writing\n", args.outfile);
                        goto fail;
                    }

                    if(fwrite(buffer, bytesAvailable, 1, outputFile) != 1) {
                       LOG_ERR("main: Error writing %d bytes\n", bytesAvailable);
                       fclose(outputFile);
                       goto fail;
                    }
                    fclose(outputFile);

                    if(args.crcoption.crcGenMode){
                        //calculate CRC from buffer 'buffer'
                        calcCrc = 0;
                        calcCrc = CalculateBufferCRC(bytesAvailable, calcCrc, buffer);
                        if(!fprintf(crcFile, "%08x\n",calcCrc)) {
                            LOG_ERR("main: Failed writing calculated CRC to file %s\n", crcFile);
                            goto fail;
                        }
                    } else if(args.crcoption.crcCheckMode){
                        //calculate CRC from buffer 'buffer'
                        NvU32 refCrc;
                        calcCrc = 0;
                        calcCrc = CalculateBufferCRC(bytesAvailable, calcCrc, buffer);
                        if(fscanf(crcFile, "%8x\n", &refCrc) == 1) {
                            if(refCrc != calcCrc){
                                LOG_ERR("main: Frame %d crc 0x%x does not match with ref crc 0x%x\n",
                                        frameCounter, calcCrc, refCrc);
                                goto fail;
                            }
                        } else {
                            LOG_ERR("main: Failed checking CRC. Failed reading file %s\n", crcFile);
                            goto fail;
                        }
                    }

                    free(buffer);
                    buffer = NULL;

                    //Tracking the bitrate
                    totalBytes += bytesAvailable;

                    encodeDoneFlag = 1;
                    break;
                case NVMEDIA_STATUS_PENDING:
                    LOG_DBG("main: Status - pending\n");
                    break;
                case NVMEDIA_STATUS_NONE_PENDING:
                    LOG_ERR("main: No encoded data is pending\n");
                    goto fail;
                default:
                    LOG_ERR("main: Error occured\n");
                    goto fail;
            }
        }

        // Next frame
        frameCounter++;

        if(frameCounter == framesNum) {
            nextFrameFlag = NVMEDIA_FALSE;
        }
    }

    //get encoding time info
    LOG_MSG("\nTotal Encoding time for %d frames: %.3f ms\n", frameCounter, encodeTime + getbitsTime);
    LOG_MSG("Encoding time per frame %.4f ms \n", encodeTime / frameCounter);
    LOG_MSG("Get bits time per frame %.4f ms \n", getbitsTime / frameCounter);
    //Get the bitrate info
    LOG_MSG("\nTotal encoded frames = %d, avg. bitrate=%d\n",
            frameCounter, (int)(totalBytes*8*30/frameCounter));
    if (args.crcoption.crcGenMode){
        LOG_MSG("\n***crc gold file %s has been generated***\n", args.crcoption.crcFilename);
    } else if (args.crcoption.crcCheckMode){
        LOG_MSG("\n***crc checking with file %s is successful\n", args.crcoption.crcFilename);
    }
    LOG_MSG("\n***ENCODING PROCESS ENDED SUCCESSFULY***\n");

fail:
    if(imageFrame) {
       NvMediaImageDestroy(imageFrame);
    }

    if(testEncoder) {
        NvMediaIJPEDestroy(testEncoder);
    }

    if(device) {
        LOG_DBG("main: Destroying device\n");
        NvMediaDeviceDestroy(device);
    }

    if(crcFile) {
        fclose(crcFile);
    }

    if (buffer)
        free(buffer);

    return 0;
}
