/*
 * Copyright (c) 2014-2016, NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <nvss.h>

#define PLATFORM_ID           0
#define INPUT_Q_LEN           8
#define RTP_FIELD_WIDTH       0
#define BYTES_PER_READ        4096
#define FRAME_DECODE_TIMEOUT  1000 // 1sec timeout

typedef struct {
    FILE                    *pFile;
    char                    *szName;

    unsigned char           uDisplayId;
    unsigned char           uWindowId;
    unsigned char           uOverlayDepth;
    unsigned int            uWidth;
    unsigned int            uHeight;
    float                   fMaxFrameRate;
    float                   fAspectRatio;
} NvSSTest;

static int
GetBuffer(NvSSTest *pTest, NvSSBitStreamBuffer *pStream)
{
    unsigned int Num;

    if (!pTest || !pStream) {
        printf("GetBuffer: Error Invalid arguments\n");
    }

    if (!pTest->pFile) {
        pTest->pFile = fopen(pTest->szName, "rb");
        if (!pTest->pFile) {
            printf("Error: Could not open %s\n", pTest->szName);
            return -1;
        }
    }

    Num = fread(pStream->pBuffer, 1, pStream->uNumBytes, pTest->pFile);
    if (Num !=pStream->uNumBytes) {
        if (Num) {
            pStream->uNumBytes = Num;
            pStream->ullTimeStampMicroSecs = NVSS_VIDEO_INVALID_TIMESTAMP;
            return 0;
        }
        printf("File read complete\n");
        fclose(pTest->pFile);
        return -1;
    }
    pStream->ullTimeStampMicroSecs = NVSS_VIDEO_INVALID_TIMESTAMP;
    return 0;
}

static int
CalculateRectForAspectRatio(NvSSVideoAttributes *pAttrib, NvSSTest *pTest)
{
    unsigned int uTargetWidth;
    unsigned int uTargetHeight;

    if (!pTest || !pAttrib) {
        printf("GetBuffer: Error Invalid arguments\n");
        return -1;
    }

    if (!pAttrib->uDisplayWidth || !pAttrib->uDisplayHeight) {
        printf("Display dimensions are not set correctly\n");
        return -1;
    }
    if (!pTest->uWidth || !pTest->uHeight) {
        printf("Stream dimensions are not set correctly\n");
        return -1;
    }

    if (pTest->fAspectRatio) {
        uTargetWidth = pTest->uWidth;
        uTargetHeight = uTargetWidth/pTest->fAspectRatio;
    }
    else {
        uTargetWidth = pAttrib->uDisplayWidth;
        uTargetHeight = pAttrib->uDisplayHeight;
    }

    if ((pAttrib->uDisplayWidth * uTargetHeight) >
          (pAttrib->uDisplayHeight * uTargetWidth)) {
        uTargetWidth = (pAttrib->uDisplayHeight * uTargetWidth)/uTargetHeight;
        uTargetHeight = pAttrib->uDisplayHeight;
    }
    else {
        uTargetHeight = (pAttrib->uDisplayWidth * uTargetHeight)/uTargetWidth;
        uTargetWidth = pAttrib->uDisplayWidth;
    }

    pAttrib->uXDst = (pAttrib->uDisplayWidth - uTargetWidth) >> 1;
    pAttrib->uYDst = (pAttrib->uDisplayHeight - uTargetHeight) >> 1;
    pAttrib->uWDst = uTargetWidth;
    pAttrib->uHDst = uTargetHeight;

    pAttrib->uXSrc = 0;
    pAttrib->uYSrc = 0;
    pAttrib->uWSrc = pTest->uWidth;
    pAttrib->uHSrc = pTest->uHeight;

    return 0;
}

static void
PrintUsage(void)
{
    printf("Usage: nvss_video_test [options]\n");
    printf("Options:\n");
    printf("-h                  Prints usage\n");
    printf("-f  [input file]**  Input file name\n");
    printf("-s  [Width]X[Ht]**  Width and height of the input stream\n");
    printf("-a  [value]         Aspect Ratio. Default: Display FullScreen \n");
    printf("-w  [id]            Window ID. Default: 1\n");
    printf("-z  [value]         Overlay Depth. Default: 2 range: 0 to 255\n");
    printf("-d  [id](0/1/2)     Display ID. Default: 0\n");
    printf(" ** marked items are mandatory\n");
}


static int
ParseArgs(int argc, char* argv[], NvSSTest* pTest)
{
    unsigned char bLastArg          = 0;
    unsigned char bDataAvailable    = 0;
    unsigned char bHasFileName      = 0;
    unsigned char bValidSize        = 0;

    int i = 0;

    for (i = 1; i < argc; i++) {
        // check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (argv[i][0] == '-') {
            if (strcmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                return 1;
            }
            else if (strcmp(&argv[i][1], "f") == 0) {
                if (bDataAvailable) {
                    pTest->szName = argv[++i];
                    pTest->pFile = fopen(pTest->szName, "rb");
                    if (!pTest->pFile) {
                        printf("ParseArgs: failed to open stream %s\n", pTest->szName);
                        return -1;
                    }
                    fclose(pTest->pFile);
                    pTest->pFile = 0;
                    bHasFileName = 1;
                } else {
                    printf("ParseArgs: -f must be followed by file name\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "s") == 0) {
                if(bDataAvailable) {
                    if(sscanf(argv[++i], "%dX%d", &pTest->uWidth, &pTest->uHeight) != 2) {
                        printf("ParseArgs: Bad  width or height");
                        return -1;
                    }
                    bValidSize = 1;
                    printf("ParseArgs: Using Width and Height : %dX%d.\n", pTest->uWidth, pTest->uHeight);
                }
                else {
                    printf("ParseArgs: -s must be followed by [width]X[height]");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "w") == 0) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    pTest->uWindowId = atoi(arg);
                    printf("ParseArgs: Using Window Id: %d.\n", pTest->uWindowId);
                } else {
                    printf("ParseArgs: -w must be followed by window id\n");
                    return -1;
                }
                if (pTest->uWindowId < 0 || pTest->uWindowId > 2) {
                    printf("ParseArgs: Bad window ID: %d. Valid values are [0-2]. ", pTest->uWindowId);
                    printf("           Using default window ID 1\n");
                    pTest->uWindowId = 1;
                }
            }
            else if (strcmp(&argv[i][1], "z") == 0) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    pTest->uOverlayDepth = atoi(arg);
                    printf("ParseArgs: Using overlay depth: %d.\n", pTest->uOverlayDepth);
                } else {
                    printf("ParseArgs: -z must be followed by overlay depth\n");
                    return -1;
                }
                if (pTest->uOverlayDepth < 0 || pTest->uOverlayDepth > 255) {
                    printf("ParseArgs: Bad overlay depth: %d. Valid values are [0-255].\n", pTest->uOverlayDepth);
                    printf("           Using default overlay depth 2\n");
                    pTest->uOverlayDepth = 2;
                }
            }
            else if (strcmp(&argv[i][1], "d") == 0) {
                if (bDataAvailable) {
                    char *arg = argv[++i];
                    pTest->uDisplayId = atoi(arg);
                    printf("ParseArgs: Using display id : %d\n", pTest->uDisplayId);
                    if (pTest->uDisplayId < 0 || pTest->uDisplayId > 2) {
                        pTest->uDisplayId = 0;
                        printf("ParseArgs: Invalid displayID setting to default 0");
                    }
                } else {
                    printf("ParseArgs: -d must be followed by display id\n");
                    return -1;
                }
            }
            else if (strcmp(&argv[i][1], "a") == 0) {
                if (bDataAvailable) {
                    float aspectRatio;
                    if (sscanf(argv[++i], "%f", &aspectRatio) && aspectRatio > 0.0) {
                        pTest->fAspectRatio = aspectRatio;
                        printf("ParseArgs: Using Aspect Ratio : %f.\n", pTest->fAspectRatio);
                    } else {
                        printf("ParseArgs: Invalid aspect ratio encountered (%s)\n", argv[i]);
                        printf("Displaying full screen\n");
                        pTest->fAspectRatio = 0.0;
                    }
                } else {
                    printf("ParseArgs: -a must be followed by aspect ratio.\n");
                    return -1;
                }
            }
            else {
                printf("ParseArgs: Unsupported option %c\n", argv[i][1]);
                return -1;
            }
        }
    }

    if (bHasFileName && bValidSize) {
        return 0;
    }

    return -1;
}

static void
Run(NvSSTest *pTest)
{
    NvSS_Status eRet;
    int sRet;
    unsigned char bTimedOut = 0;

    NvSSVideoConfig oVideoConfig;
    NvSSVideoHandle oHandle;
    NvSSVideoAttributes oAttribs;
    NvSSVideoStreamProperties oProps;
    NvSSBitStreamBuffer oStream;

    memset(&oVideoConfig, 0, sizeof(oVideoConfig));
    oVideoConfig.uDisplayId         = pTest->uDisplayId;
    oVideoConfig.uWindowId             = pTest->uWindowId;
    oVideoConfig.uOverlayDepth         = pTest->uOverlayDepth;
    oVideoConfig.uPlatformSpecificId   = PLATFORM_ID;
    oVideoConfig.uInputQLen            = INPUT_Q_LEN;
    oVideoConfig.eStreamType           = NVSS_VIDEO_STREAM_TYPE_H264;
    oVideoConfig.uRTPFieldWidth        = RTP_FIELD_WIDTH;
    #ifdef EGL_STREAM_ENABLE
    oVideoConfig.bEGLStream = 0;
    #endif

    memset(&oStream, 0, sizeof(oStream));
    oStream.uNumBytes  = BYTES_PER_READ;
    oStream.uCompleteFrame = 0;
    oStream.pBuffer    = (unsigned char *)malloc(oStream.uNumBytes);
    if(!oStream.pBuffer) {
        printf("Run: Error allocating memory to buffer\n");
        return;
    }

    oProps.uWidth          = pTest->uWidth;
    oProps.uHeight         = pTest->uHeight;
    oProps.fMaxFrameRate   = pTest->fMaxFrameRate;

    eRet = NvSSVideoOpen(&oHandle, &oVideoConfig);
    if (eRet != NVSS_STATUS_OK) {
        printf("Error with NvSS open!!\n");
        return;
    }

    eRet = NvSSVideoStreamConfigure(oHandle, &oProps );
    if (eRet != NVSS_STATUS_OK) {
        printf("Error with NvSS stream configure!!\n");
        return;
    }

    eRet = NvSSVideoGetAttribs(oHandle, &oAttribs);
    if (eRet != NVSS_STATUS_OK) {
        printf("Error with NvSS GetAttribs!!\n");
        return;
    }

    oAttribs.bUpdateColors          = 0;
    oAttribs.bUpdateRenderState     = 0;
    oAttribs.bUpdateRects           = 0;

    if (pTest->fAspectRatio) {
        sRet = CalculateRectForAspectRatio(&oAttribs, pTest);
        if(!sRet) {
            printf("calculate aspect ratio \n");
            oAttribs.bUpdateRects       = 1;
        }
    }

    eRet = NvSSVideoSetAttribs(oHandle, &oAttribs);
    if (eRet != NVSS_STATUS_OK) {
        printf("Error with NvSS set attribs!!\n");
        return;
    }

    while (1) {
        if (!bTimedOut && GetBuffer(pTest, &oStream)) {
            break;
        }
        eRet = NvSSVideoDecode(oHandle, &oStream, FRAME_DECODE_TIMEOUT);
        if (eRet == NVSS_STATUS_TIMED_OUT) {
           bTimedOut = 1;
           printf("NvSSVideoDecode timed out \n");
           continue;
        }
        else if (eRet != NVSS_STATUS_OK) {
            printf("Error decode failed , bailing out \n");
            break;
        }
        bTimedOut = 0;
    }
    free(oStream.pBuffer);

    /*printf("Waiting 1sec for playback to finish\n");*/
    sleep(FRAME_DECODE_TIMEOUT/1000); // 1sec

    eRet = NvSSVideoClose(oHandle);
    if (eRet != NVSS_STATUS_OK) {
        printf("Error with NvSS Video Close , bailing out \n");
        return;
    }
    return;
}


int
main (int argc, char* argv[])
{
    NvSSTest *pTest;
    int i;
    pTest = (NvSSTest *)malloc(sizeof(*pTest));
    if (!pTest) {
        printf("Main: Error allocating memory!\n");
        return 0;
    }

    memset(pTest, 0, sizeof(*pTest));
    // Set defaults
    pTest->uWindowId        = 1;
    pTest->uOverlayDepth    = 2;
    pTest->fMaxFrameRate    = 30;
    pTest->uDisplayId       = 0;

    i = ParseArgs(argc, argv, pTest);
    if(i) {
        if (i == -1) {
            printf("Main: ParseArgs failed\n");
            PrintUsage();
        }
        return 0;
    }

    Run(pTest);
    free(pTest);
    return 0;
}
