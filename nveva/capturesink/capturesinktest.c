/*
 * Copyright (c) 2014-2015, NVIDIA Corporation.  All Rights Reserved.
 *
 * BY INSTALLING THE SOFTWARE THE USER AGREES TO THE TERMS BELOW.
 *
 * User agrees to use the software under carefully controlled conditions
 * and to inform all employees and contractors who have access to the software
 * that the source code of the software is confidential and proprietary
 * information of NVIDIA and is licensed to user as such.  User acknowledges
 * and agrees that protection of the source code is essential and user shall
 * retain the source code in strict confidence.  User shall restrict access to
 * the source code of the software to those employees and contractors of user
 * who have agreed to be bound by a confidentiality obligation which
 * incorporates the protections and restrictions substantially set forth
 * herein, and who have a need to access the source code in order to carry out
 * the business purpose between NVIDIA and user.  The software provided
 * herewith to user may only be used so long as the software is used solely
 * with NVIDIA products and no other third party products (hardware or
 * software).   The software must carry the NVIDIA copyright notice shown
 * above.  User must not disclose, copy, duplicate, reproduce, modify,
 * publicly display, create derivative works of the software other than as
 * expressly authorized herein.  User must not under any circumstances,
 * distribute or in any way disseminate the information contained in the
 * source code and/or the source code itself to third parties except as
 * expressly agreed to by NVIDIA.  In the event that user discovers any bugs
 * in the software, such bugs must be reported to NVIDIA and any fixes may be
 * inserted into the source code of the software by NVIDIA only.  User shall
 * not modify the source code of the software in any way.  User shall be fully
 * responsible for the conduct of all of its employees, contractors and
 * representatives who may in any way violate these restrictions.
 *
 * NO WARRANTY
 * THE ACCOMPANYING SOFTWARE (INCLUDING OBJECT AND SOURCE CODE) PROVIDED BY
 * NVIDIA TO USER IS PROVIDED "AS IS."  NVIDIA DISCLAIMS ALL WARRANTIES,
 * EXPRESS, IMPLIED OR STATUTORY, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF TITLE, MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.

 * LIMITATION OF LIABILITY
 * NVIDIA SHALL NOT BE LIABLE TO USER, USERS CUSTOMERS, OR ANY OTHER PERSON
 * OR ENTITY CLAIMING THROUGH OR UNDER USER FOR ANY LOSS OF PROFITS, INCOME,
 * SAVINGS, OR ANY OTHER CONSEQUENTIAL, INCIDENTAL, SPECIAL, PUNITIVE, DIRECT
 * OR INDIRECT DAMAGES (WHETHER IN AN ACTION IN CONTRACT, TORT OR BASED ON A
 * WARRANTY), EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGES.  THESE LIMITATIONS SHALL APPLY NOTWITHSTANDING ANY FAILURE OF THE
 * ESSENTIAL PURPOSE OF ANY LIMITED REMEDY.  IN NO EVENT SHALL NVIDIAS
 * AGGREGATE LIABILITY TO USER OR ANY OTHER PERSON OR ENTITY CLAIMING THROUGH
 * OR UNDER USER EXCEED THE AMOUNT OF MONEY ACTUALLY PAID BY USER TO NVIDIA
 * FOR THE SOFTWARE PROVIDED HEREWITH.
 */

//------------------------------------------------------------------------------
//! \file capturesinktest.c
//! \brief Test application for Capture Sink
//------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "nvcapturesink.h"

volatile char bQuit = 0;
volatile char g_bDrop = 0;
volatile char g_bFreeze = 0;
NvCaptureSink *pCaptureSink = NULL;
struct timespec oStartTime, oFirstFrameTime;

void OnCaptureError(void *pClient);
void OnRenderFirstFrame(void *pClient);
void PrintOptions(void);
int tokenizeline(char *input, char **voString);
ENvCaptureErrorType OnCaptureFrame(void *pClient, NvCaptureSurfaceMap oNvCaptureSurfaceMap);

#define CONFIG_SET(width, height, format, surfaceFormat ) \
    {                                                     \
                oParam.oCSIParam.uWidth = width;          \
                oParam.oCSIParam.uHeight = height;        \
                oParam.oCSIParam.eCSICaptureFormat = format; \
                oParam.oCSIParam.eCSICaptureSurfaceFormat = surfaceFormat; \
    }

void OnCaptureError(void *pClient)
{
    printf("Capture Error\n");
}

void OnRenderFirstFrame(void *pClient)
{
    unsigned long long uTimeStart, uTimeEnd;
    uTimeStart = (oStartTime.tv_sec * 1000000000LL) + oStartTime.tv_nsec;
    clock_gettime(CLOCK_MONOTONIC, &oFirstFrameTime);
    uTimeEnd = (oFirstFrameTime.tv_sec * 1000000000LL) + oFirstFrameTime.tv_nsec;
    uTimeEnd = (uTimeEnd-uTimeStart)/1000000;
    printf("First Frame Display took %llu milliseconds \n",uTimeEnd);
}

ENvCaptureErrorType OnCaptureFrame(void *pClient, NvCaptureSurfaceMap oNvCaptureSurfaceMap)
{
    U8 *Y, *U, *V;
    U8 YData, UData, VData;
    U8 red = 95, green = 200, blue = 20;
    Y = oNvCaptureSurfaceMap.pSurfacePtrY;
    U = oNvCaptureSurfaceMap.pSurfacePtrU;
    V = oNvCaptureSurfaceMap.pSurfacePtrV;

    if (g_bDrop == 1) {
        g_bDrop = 0;
        return eDisplayPictureDrop;
    }
    if (g_bFreeze == 1) {
        if (oNvCaptureSurfaceMap.eFormat != eNvVideoSurface_RGBA) {
            YData = (U8) (0.257 * red + 0.504 * green + 0.098 * blue + 16);
            UData = (U8) (0);
            VData = (U8) (0);

            memset(Y, YData, oNvCaptureSurfaceMap.uPitchLuma * oNvCaptureSurfaceMap.uHeight);
            memset(U, UData, oNvCaptureSurfaceMap.uPitchChroma * (oNvCaptureSurfaceMap.uHeight >> 1));
            memset(V, VData, oNvCaptureSurfaceMap.uPitchChroma * (oNvCaptureSurfaceMap.uHeight >> 1));
        }
        g_bFreeze = 0;
        return eDisplayPictureFreeze;
    }

    return eDisplayPictureSuccess;
}

typedef struct _tagConfigTable {
    char *type;
    U16  uWidth;
    U16  uHeight;
    U8   bInterlaced;
    U8   bYUV;
} sConfigTable;

sConfigTable supportedConfigs[] =
{
    { "480p.yuv422", 720, 480, 0, 1 },
    { "480i.yuv422", 720, 480, 1, 1 },
    { "480p.rgb888", 720, 480, 0, 0 },
    { "480i.rgb888", 720, 480, 1, 0 },
    { "576p.yuv422", 720, 576, 0, 1 },
    { "576i.yuv422", 720, 576, 1, 1 },
    { "576p.rgb888", 720, 576, 0, 0 },
    { "576i.rgb888", 720, 576, 1, 0 },
    { "720p.yuv422", 1280, 720, 0, 1 },
    { "720i.yuv422", 1280, 720, 1, 1 },
    { "720p.rgb888", 1280, 720, 0, 0 },
    { "720i.rgb888", 1280, 720, 1, 0 },
    { "1080p.yuv422", 1920, 1080, 0, 1 },
    { "1080i.yuv422", 1920, 1080, 1, 1 },
    { "1080p.rgb888", 1920, 1080, 0, 0 },
    { "1080i.rgb888", 1920, 1080, 1, 0 },
    { "vga.yuv422", 320, 244, 0, 1 },
    { "vga.rgb888", 320, 244, 0, 0 },
    { NULL, 0, 0, 0, 0 }
};

void PrintOptions(void)
{
    printf("Usage, set config and diplay head and then start capture \n\n");
    printf("config <configs>       (available capture configs below)\n");
    printf("(480p.yuv422/480p.rgb888/576p.yuv422/576p.rgb888/vga.yuv422/vga.rgb888 \n");
    printf("(720p.yuv422/720p.rgb888/1080p.yuv422/1080p.rgb888\n\n");
    printf("(480i.yuv422/480i.rgb888/576i.yuv422/576i.rgb888 \n");
    printf("(720i.yuv422/720i.rgb888/1080i.yuv422/1080i.rgb888\n\n");
    printf("dh  <hdmi/dp>          (display head) \n");
    printf("depth  (0-255)         (0-topmost, 255-lowest) \n");
    printf("hdmi/cvbs              (default hdmi capture) \n");
    printf("sc                     (Start Capture) \n");
    printf("kc                     (Kill Capture) \n");
    printf("ba  <value>            (adjusts the brightness with value ranges from -1.0 to 1.0) \n");
    printf("ca  <value>            (adjusts the contrast with value ranges from 0.0 to 2.0)  \n");
    printf("sa  <value>            (adjusts the saturation with value ranges from 0.0 to 2.0) \n");
    printf("ha  <value>            (adjusts the hue with value ranges from -1.0 to 1.0) \n");
    printf("vz [x0 y0 x1 y1] [x0 y0 x1 y1] \n");
    printf("dc                     (Capture Disable) \n");
    printf("ec                     (Capture Enable) \n");
    printf("st                     (Sets the capture timeout in milliseconds\n");
    printf("drop                   (Simulate callback to drop frame)\n");
    printf("freeze                 (Simulate callback to freeze frame\n");
    printf("wt <time in seconds>   (waits/sleeps for n seconds \n");
    printf("q                      (Quits the Program) \n");
    printf("-s (script)            (Takes all the arguments in a script file for non-interactive option)\n");
    printf("h                      (prints the above information) \n");
}

int tokenizeline(char *input, char **voString)
{
    int len = strlen(input);
    int i=0,k;
    char strtemp[255];
    int numstrings = 0;
    for(i=0;i<len;i++)
    {
        while(i<len && isspace(input[i]))
            i++;
        char bQuoted = 0;
        if(i < len && input[i] == '"') {
            bQuoted = 1;
            i++;
        }
        k=0;
        if(bQuoted) {
            while(i < len && input[i] != '"')
                strtemp[k++] = input[i++];
            if(i < len && input[i] == '"')
                i++;
            strtemp[k]='\0';
        } else {
            while(i < len && !isspace(input[i]))
                strtemp[k++] = input[i++] ;
            strtemp[k]='\0';
        }
        if(k)
        {
            voString[numstrings] = (char*)malloc(k+1);
            strcpy(voString[numstrings++],strtemp);
        }
    }
    return numstrings;
}

static void sighandler(int s)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    bQuit = 1;
    if (pCaptureSink) {
        NvCaptureSinkDestroy(pCaptureSink);
    }

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

static void signalsetup (void)
{
    signal (SIGINT,  sighandler);
    signal (SIGTERM, sighandler);
    signal (SIGQUIT, sighandler);
}

int main(int argc, char **argv)
{
    NvResult nr = RESULT_OK;
    float fBrightness = 0, fContrast = 1, fSaturation = 1, fHue = 0;
    int uDepth, uDisplayId;
    char bvalidcommand = 0;
    char *voString[256] = {0};
    char input[256] = {0};
    char command[256] = {0};
    U32 i, numstrings = 0;
    S32 value, timeout = 0;
    NvCaptureSinkParam oParam;
    NvCaptureSinkCallback oCallback;
    NvRectangle rSrc, rDst;
    FILE *fp = NULL;

    signalsetup();

    memset(&oParam, 0 ,sizeof(NvCaptureSinkParam));
    // Default values
    oParam.oRenderParam.eMode = eNvDeinterlaceWEAVE;
    // Set DisplayId = 1, which is the HDMI port on Drive-CX
    oParam.oRenderParam.uDisplayId = 1;
    oParam.oRenderParam.uDepth = 0;
    oParam.oCSIParam.eCSIPort = eNvCSIPortAB;
    oParam.oCSIParam.eCSICaptureFormat = eNvVideoFormatYUV422;
    oParam.oCSIParam.eCSICaptureSurfaceFormat = eNvVideoSurface_YV16;
    oParam.oCSIParam.uWidth = 720;
    oParam.oCSIParam.uHeight = 480;
    oParam.oCSIParam.uNumLanes = 4;

    if (argc > 1) {
        if (!strcmp(argv[1],"-s")) {
            fp = fopen (argv[2],"r");
            if (fp == NULL) {
                printf("Invalid script, not present : %s \n",argv[2]);
            }
        } else {
            printf("Usage :  %s -s <script> \n",argv[0]);
            return 0;
        }
    }

    while (!bQuit) {
        if (!fp) {
            printf("- ");
            fgets(input, 256, stdin);
        } else {
            fgets(input, 256, fp);
            if (feof(fp)) {
                strcpy(input,"q");
            }
        }
        if(input[0] == 0 || !(numstrings = tokenizeline(input,voString))) {
            continue;
        }
        else if (!strcmp(voString[0],"h")) {
            PrintOptions();
            bvalidcommand = 1;
            goto freetokens;
        }
        else if (!strcmp(voString[0],"sc")) {
            bvalidcommand = 1;
            if (pCaptureSink) {
                printf("Capture is already opened\n");
                goto freetokens;
            }
            oParam.uCaptureTimeout = timeout;
            oCallback.OnCaptureError = OnCaptureError;
            oCallback.OnRenderFirstFrame = OnRenderFirstFrame;
            oCallback.OnCaptureFrame = OnCaptureFrame;
            oParam.pCallback = &oCallback;
            pCaptureSink = NvCaptureSinkCreate(&oParam);
            clock_gettime(CLOCK_MONOTONIC, &oStartTime);
            nr = NvCaptureSinkStart(pCaptureSink);
            if (IsFailed(nr)) {
                printf("Unable to Start the Capture\n");
            }
            goto freetokens;
        }
        else if (!strcmp(voString[0],"cvbs")) {
            bvalidcommand = 1;
            oParam.oCSIParam.uNumLanes = 1;
            goto freetokens;
        }
        else if (!strcmp(voString[0],"hdmi")) {
            bvalidcommand = 1;
            oParam.oCSIParam.uNumLanes = 4;
            goto freetokens;
        }
        else if (!strcmp(voString[0],"config") && voString[1]) {
            int i = 0;
            bvalidcommand = 0;
            while(supportedConfigs[i].type != NULL)
            {
                if(!strcmp(voString[1], supportedConfigs[i].type)) {
                    if(supportedConfigs[i].bYUV && !supportedConfigs[i].bInterlaced ) {
                        CONFIG_SET(supportedConfigs[i].uWidth,
                                   supportedConfigs[i].uHeight,
                                   eNvVideoFormatYUV422,
                                   eNvVideoSurface_YV16);
                    }
                    else if(supportedConfigs[i].bYUV) {
                        CONFIG_SET(supportedConfigs[i].uWidth,
                                   supportedConfigs[i].uHeight,
                                   eNvVideoFormatYUV422,
                                   eNvVideoSurface_NV24);
                    }
                    else {
                        CONFIG_SET(supportedConfigs[i].uWidth,
                                   supportedConfigs[i].uHeight,
                                   eNvVideoFormatRGB888,
                                   eNvVideoSurface_RGBA);
                    }
                    bvalidcommand = 1;
                    if (supportedConfigs[i].bInterlaced)
                        oParam.oCSIParam.bInterlace = 1;
                    goto freetokens;
               }
               i++;
           }
        }
        else if (!strcmp(voString[0],"deint")) {
            if (!strcmp(voString[1], "bob")) {
                oParam.oRenderParam.eMode = eNvDeinterlaceBOB;
            }
            else if (!strcmp(voString[1], "weave")) {
                oParam.oRenderParam.eMode = eNvDeinterlaceWEAVE;
            }
            else if (!strcmp(voString[1], "tempfield")) {
                oParam.oRenderParam.eMode = eNvDeinterlaceADVANCEDField;
            }
            else if (!strcmp(voString[1], "tempframe")) {
                oParam.oRenderParam.eMode = eNvDeinterlaceADVANCEDFrame;
            }
            else {
                printf("not a valid deinterlacing option for sc command \n");
                bvalidcommand = 0;
                break;
            }
            bvalidcommand = 1;
            goto freetokens;
        }
        else if (!strcmp(voString[0],"d")) {
            sscanf(input,"%s %u \n", command, &uDisplayId);
            if (uDisplayId < 0 || uDisplayId > 2) {
                printf("uDisplayId should be between 0 and 2\n");
                goto freetokens;
            }
            oParam.oRenderParam.uDisplayId = uDisplayId;
            bvalidcommand = 1;
            goto freetokens;
        }
        else if (!strcmp(voString[0],"depth")) {
            sscanf(input,"%s %u \n", command, &uDepth);
            if (uDepth > 255) {
                printf("Depth should be between 0 and 255\n");
                goto freetokens;
            }
            oParam.oRenderParam.uDepth = uDepth;
            bvalidcommand = 1;
            goto freetokens;
        }
        else if (!strcmp(voString[0],"ba")) {
            sscanf(input,"%s %f \n", command, &fBrightness);
            if (fBrightness < -1 || fBrightness > 1) {
                printf("Brightness value should be in between -1 & 1\n");
                goto freetokens;
            }
            NvCaptureSinkColorControl(pCaptureSink, fBrightness, fContrast, fSaturation, fHue);
            goto freetokens;
        }
        else if (!strcmp(voString[0],"ca")) {
            sscanf(input,"%s %f \n", command, &fContrast);
            if(fContrast < 0 || fContrast > 2) {
                printf("Contrast value should be in between 0 & 2\n");
                goto freetokens;
            }
            NvCaptureSinkColorControl(pCaptureSink, fBrightness, fContrast, fSaturation, fHue);
            goto freetokens;
        }
        else if (!strcmp(voString[0],"sa")) {
            sscanf(input,"%s %f \n", command, &fSaturation);
            if (fSaturation < 0 || fSaturation > 2) {
                printf("Saturation value should be in between 0 & 2\n");
                goto freetokens;
            }
            NvCaptureSinkColorControl(pCaptureSink, fBrightness, fContrast, fSaturation, fHue);
            goto freetokens;
        }
        else if (!strcmp(voString[0],"ha")) {
            sscanf(input,"%s %f \n", command, &fHue);
            if (fHue < -1 || fHue > 1) {
                printf("Hue value should be in between -1 & 1\n");
                goto freetokens;
            }
            NvCaptureSinkColorControl(pCaptureSink, fBrightness, fContrast, fSaturation, fHue);
            goto freetokens;
        }
        else if (!strcmp(voString[0],"vz")) {
            if (numstrings > 9 || (numstrings - 1) % 4 != 0) {
                printf("not a valid command for vz (video crop and zoom)\n");
                goto freetokens;
            }
            i  = 1;
            while (voString[i] && voString[i + 1] && voString[i + 2] && voString[i + 3]) {
                if (i < 5) {
                    sscanf(voString[i],"%d",&value);
                    rSrc.sLeft = value;
                    sscanf(voString[i+1],"%d",&value);
                    rSrc.sTop = value;
                    sscanf(voString[i+2],"%d",&value);
                    rSrc.sRight = value;
                    sscanf(voString[i+3],"%d",&value);
                    rSrc.sBot = value;
                    i += 4;
                } else {
                    sscanf(voString[i],"%d",&value);
                    rDst.sLeft = value;
                    sscanf(voString[i+1],"%d",&value);
                    rDst.sTop = value;
                    sscanf(voString[i+2],"%d",&value);
                    rDst.sRight = value;
                    sscanf(voString[i+3],"%d",&value);
                    rDst.sBot = value;
                    i += 4;
                    break;
                }
            }
            if (i == 1) {
                rSrc.sLeft = rSrc.sTop = rSrc.sRight = rSrc.sBot = -1;
                rDst.sLeft = rDst.sTop = rDst.sRight = rDst.sBot = -1;
            }
            else if (i == 5) {
                rDst.sLeft = rDst.sTop = rDst.sRight = rDst.sBot = -1;
            }
            NvCaptureSinkVideoCropAndZoom(pCaptureSink, rSrc, rDst);
            goto freetokens;
        }
        else if(!strcmp(voString[0],"kc")) {
            if (pCaptureSink) {
                nr = NvCaptureSinkDestroy(pCaptureSink);
                pCaptureSink = NULL;
            }
            goto freetokens;
        }
        else if(!strcmp(voString[0],"drop")) {
             g_bDrop = 1;
             goto freetokens;
        }
        else if(!strcmp(voString[0],"freeze")) {
             g_bFreeze = 1;
             goto freetokens;
        }
        else if(!strcmp(voString[0],"q") || !strcmp(voString[0],"quit")) {
            if (pCaptureSink) {
                nr = NvCaptureSinkDestroy(pCaptureSink);
                pCaptureSink = NULL;
            }
            bQuit = 1;
        }
        else if (!strcmp(voString[0],"dc")) {
            nr = NvCaptureSinkEnable(pCaptureSink, 0);
        }
        else if (!strcmp(voString[0],"ec")) {
            clock_gettime(CLOCK_MONOTONIC, &oStartTime);
            nr = NvCaptureSinkEnable(pCaptureSink, 1);
        }
        else if (!strcmp(voString[0],"wt")) {
            sleep(atoi(voString[1]));
            goto freetokens;
        }
        else if (!strcmp(voString[0],"st")) {
            sscanf(input,"%s %d \n", command, &timeout);
            if (timeout < 0) {
                printf("Timeout can not be a negative value\n");
                timeout = 0;
            }
            goto freetokens;
        }
        else {
            printf("Invalid command \n");
     }

freetokens:
        if(!bvalidcommand)
            printf("Invalid command, type h for help \n");
        for(i=0;i<numstrings;i++) {
            free(voString[i]);
            voString[i] = 0;
        }
    }
    return 0;
}
