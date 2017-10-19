/*
 * Copyright (c) 2014, NVIDIA Corporation.  All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "nvevaapp.h"
#include "nvevacontrol.h"

#define MAX_WDEL 2048

NvSemaphore *m_pSem;
volatile int bNonInteractive = 0;
volatile int bQuit = 0;
volatile int bCleanupDone;
char errorLog[MAX_WDEL];
const char *EVAStatusStrings[] =  {
    "EVA_IDLE",
    "EVA_DISPLAY_CAMERA_RVC",
    "EVA_DISPLAY_CAMERA_SVC",
    "EVA_DISPLAY_CAMERA_TVC",
    "EVA_CAPTURE_FAILURE_CAMERA_RVC",
    "EVA_CAPTURE_FAILURE_CAMERA_SVC",
    "EVA_CAPTURE_FAILURE_CAMERA_TVC",
    "EVA_DISPLAY_SPLASH_SCREEN",
    "EVA_DISPLAY_WELCOME_ANIMATION",
    "EVA_SHUTDOWN_OK",
    "EVA_SHUTDOWN_FAILURES",
    "EVA_STATE_ERROR"
};

static void cleanUp (void)
{
    if (bCleanupDone == 0) {
        bCleanupDone = 1;
        {
            EarlyVideoApp * pEarlyVideo;
            pEarlyVideo = GetEarlyVideoApp ();
            if (pEarlyVideo) {
                if (EVA_LogGet (pEarlyVideo, errorLog, MAX_WDEL) == RESULT_OK)
                    printf ("Error Log\n%s", errorLog);
                else
                    printf ("Failed to get Error Log\n");

                if (EVA_StateSet (pEarlyVideo, EVA_SHUTDOWN) == RESULT_OK)
                    printf ("EVA shutdown successful\n");
                else
                    printf ("EVA shutdown failure\n");

            }
        }
        EarlyVideoAppFinish ();
    }
}

static void PrintOptions (void)
{
    printf ("NvEarlyVideo [-StartupEmblem=X] [-StartupType=Y] [-NonInteractive]\n");
    printf ("where X is BLACK or SPLASH\n");
    printf("where Y is BLACK, SPLASH, RVC, SVC, TVC or WA (Welcome Animation)\n");
    printf("-NonInteractive option only for noninteractive mode\n");


    printf ("Usage \n\n");
    printf ("gs    (Get Status)\n");
    printf ("nc    (Select no camera for view)\n");
    printf ("rvc   (Select rear view camera for display)\n");
    printf ("svc   (Select side view camera for display)\n");
    printf ("tvc   (Select top view camera for display)\n");
    printf ("wap   (Play welcome animation, only valid if no camera view selected\n");

    printf ("b+    (Increase brightness for current video source)\n");
    printf ("b-    (Decrease brightness for current video source)\n");
    printf ("c+    (Increase contrast for current video source)\n");
    printf ("c-    (Decrease contrast for current video source)\n");
    printf ("s+    (Increase saturation for current video source)\n");
    printf ("s-    (Decrease saturation current video source)\n");
    printf ("h+    (Increase hue for current video source)\n");
    printf ("h-    (Decrease hue for current video source)\n");
    printf ("q (Quits the Program) \n");
    printf ("? (prints the above information) \n");
}

static int tokenizeline (char *input, char **voString)
{
    int len = strlen(input);
    int i=0, k;
    char strtemp[255];
    int numstrings = 0;
    for (i = 0; i < len; i++)
    {
        while (i < len && isspace (input[i]))
            i++;
        int bQuoted = 0;
        if (i < len && input[i] == '"') {
            bQuoted = 1;
            i++;
        }
        k = 0;
        if (bQuoted) {
            while (i < len && input[i] != '"')
                strtemp[k++] = input[i++];
            if (i < len && input[i] == '"')
                i++;
            strtemp[k] = '\0';
        } else {
            while (i < len && !isspace (input[i]))
                strtemp[k++] = input[i++] ;
            strtemp[k] = '\0';
        }
        if (k)
        {
            voString[numstrings] = (char*) malloc(k+1);
            strcpy (voString[numstrings++], strtemp);
        }
    }
    return numstrings;
}

typedef struct tagPictureControl {
    float Brightness;
    float Contrast;
    float Saturation;
    float Hue;
} tPictureControl;

tPictureControl PictureControlMax =
{
    1.0f,
    2.0f,
    2.0f,
    1.0f
};
tPictureControl PictureControlMin =
{
    -1.0f,
    0.0f,
    0.0f,
    -1.0f
};
tPictureControl PictureControlIncrement =
{
    .1f,
    .1f,
    .1f,
    .1f
};

static int TestEarlyVideo (void)
{
    tPictureControl pictureSettings [ECID_MAX_ID+1];
    char *voString [256] = {0};
    char input [256] = {0};
    ECamera_ID CameraSelected = ECID_NONE_SELECTED;
    U32 i, numstrings = 0;

    for (i = 0; i < ECID_MAX_ID; i++) {
        pictureSettings[i].Brightness = 0.0f;
        pictureSettings[i].Contrast = 1.0f;
        pictureSettings[i].Saturation = 1.0f;
        pictureSettings[i].Hue = 0.0f;
    }


    while (!bQuit) {
        printf ("-");
        fgets (input, 256, stdin);

        if (input[0] == 0 || !(numstrings = tokenizeline (input, voString))) {
            continue;
        }
        else if ((!strcmp (voString[0], "?")) || (!strcmp (voString[0], "h"))) {
            PrintOptions ();
            goto freetokens;
        }
        else if (!strcmp (voString[0], "gs")) {
            EarlyVideoApp * pEarlyVideo;
            pEarlyVideo = GetEarlyVideoApp ();
            if (pEarlyVideo) {
                printf ("EVA Status = %s\n", EVAStatusStrings [EVA_StatusGet (pEarlyVideo)]);
            } else {
                printf ("Get Status failed, early video not available\n");
            }
        }
        else if (!strcmp (voString[0], "rvc")) {
            if (CameraSelected == ECID_REAR_VIEW)
                printf ("Selecting rear view camera when it is already selected\n");
            if (EvaControlCameraSelect (ECID_REAR_VIEW) == RESULT_OK) {
                printf ("Rear view camera selected\n");
                CameraSelected = ECID_REAR_VIEW;
            }
            else
                printf ("Rear view camera selection failed\n");
            goto freetokens;
        }
        else if (!strcmp (voString[0],"svc")) {
            if (CameraSelected == ECID_SIDE_VIEW)
                printf ("Selecting side view camera when it is already selected\n");
            if (EvaControlCameraSelect (ECID_SIDE_VIEW) == RESULT_OK) {
                printf ("Side view camera selected\n");
                CameraSelected = ECID_SIDE_VIEW;
            }
            else
                printf ("Side view camera selection failed\n");
            goto freetokens;
        }
        else if (!strcmp (voString[0],"tvc")) {
            if (CameraSelected == ECID_TOP_VIEW)
                printf ("Selecting top view camera when it is already selected\n");
            if (EvaControlCameraSelect (ECID_TOP_VIEW) == RESULT_OK) {
                printf ("Top view camera selected\n");
                CameraSelected = ECID_TOP_VIEW;
            }
            else
                printf ("Top view camera selection failed\n");
            goto freetokens;
        }
        else if (!strcmp (voString[0],"nc")) {
            if (CameraSelected == ECID_NONE_SELECTED)
                printf ("Selecting to remove camera when it should already be removed\n");
            if (EvaControlCameraSelect (ECID_NONE_SELECTED) == RESULT_OK) {
                printf ("Camera view removed\n");
                CameraSelected = ECID_NONE_SELECTED;
            }
            else
                printf ("camera view removal failed\n");
            goto freetokens;
        }
        else if (!strcmp (voString[0],"b+")) {
            pictureSettings[CameraSelected].Brightness += PictureControlIncrement.Brightness;
            if (pictureSettings[CameraSelected].Brightness > PictureControlMax.Brightness)
                pictureSettings[CameraSelected].Brightness = PictureControlMax.Brightness;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Brightness set to %f\n", pictureSettings[CameraSelected].Brightness);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"b-")) {
            pictureSettings[CameraSelected].Brightness -= PictureControlIncrement.Brightness;
            if (pictureSettings[CameraSelected].Brightness < PictureControlMin.Brightness)
                pictureSettings[CameraSelected].Brightness = PictureControlMin.Brightness;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Brightness set to %f\n", pictureSettings[CameraSelected].Brightness);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"c+")) {
            pictureSettings[CameraSelected].Contrast += PictureControlIncrement.Contrast;
            if (pictureSettings[CameraSelected].Contrast > PictureControlMax.Contrast)
                pictureSettings[CameraSelected].Contrast = PictureControlMax.Contrast;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Contrast set to %f\n", pictureSettings[CameraSelected].Contrast);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"c-")) {
            pictureSettings[CameraSelected].Contrast -= PictureControlIncrement.Contrast;
            if (pictureSettings[CameraSelected].Contrast < PictureControlMin.Contrast)
                pictureSettings[CameraSelected].Contrast = PictureControlMin.Contrast;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Contrast set to %f\n", pictureSettings[CameraSelected].Contrast);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"s+")) {
            pictureSettings[CameraSelected].Saturation += PictureControlIncrement.Saturation;
            if (pictureSettings[CameraSelected].Saturation > PictureControlMax.Saturation)
                pictureSettings[CameraSelected].Saturation = PictureControlMax.Saturation;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Saturation set to %f\n", pictureSettings[CameraSelected].Saturation);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"s-")) {
            pictureSettings[CameraSelected].Saturation -= PictureControlIncrement.Saturation;
            if (pictureSettings[CameraSelected].Saturation < PictureControlMin.Saturation)
                pictureSettings[CameraSelected].Saturation = PictureControlMin.Saturation;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Saturation set to %f\n", pictureSettings[CameraSelected].Saturation);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"h+")) {
            pictureSettings[CameraSelected].Hue += PictureControlIncrement.Hue;
            if (pictureSettings[CameraSelected].Hue > PictureControlMax.Hue)
                pictureSettings[CameraSelected].Hue = PictureControlMax.Hue;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Hue set to %f\n", pictureSettings[CameraSelected].Hue);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"h-")) {
            pictureSettings[CameraSelected].Hue -= PictureControlIncrement.Hue;
            if (pictureSettings[CameraSelected].Hue < PictureControlMin.Hue)
                pictureSettings[CameraSelected].Hue = PictureControlMin.Hue;
            EvaControlPictureControl (pictureSettings[CameraSelected].Brightness,
                pictureSettings[CameraSelected].Contrast,
                pictureSettings[CameraSelected].Saturation,
                pictureSettings[CameraSelected].Hue);
            printf ("Hue set to %f\n", pictureSettings[CameraSelected].Hue);
            goto freetokens;
        }
        else if (!strcmp (voString[0],"wap")) {
            EvaControlWelcomeAnimationPlay ();
        }
        else if(!strcmp (voString[0],"q") || !strcmp (voString[0],"quit")) {
            bQuit = 1;
        }

        freetokens:
        for(i = 0; i < numstrings; i++) {
            free (voString[i]);
            voString[i] = 0;
        }
    }
    cleanUp ();
    return 0;

}

static void sighandler (int s)
{
    signal (SIGINT, SIG_IGN);
    signal (SIGTERM, SIG_IGN);
    signal (SIGQUIT, SIG_IGN);

    bQuit = 1;
    if (bNonInteractive)
      NvSemaphoreIncrement(m_pSem);
    cleanUp ();

    signal (SIGINT, SIG_DFL);
    signal (SIGTERM, SIG_DFL);
    signal (SIGQUIT, SIG_DFL);
    exit (0);
}

static void signalsetup (void)
{
  signal (SIGINT,  sighandler);
  signal (SIGTERM, sighandler);
  signal (SIGQUIT, sighandler);
}

// For noninteractive mode, wait for the semaphore indefinitely.
static void noninteractivemode (void)
{
    bNonInteractive = 1;
    NvSemaphoreCreate(&m_pSem, 0, 1);
    NvSemaphoreDecrement(m_pSem, NV_TIMEOUT_INFINITE);
    NvSemaphoreDestroy(m_pSem);
}

int main (int argc, char* argv[])
{
    // Do any arguement processing needed
    EarlyVideoApp *pEarlyVideoApp;
    EarlyVideoAppCreate ();
    pEarlyVideoApp = GetEarlyVideoApp ();
    signalsetup ();
    if (pEarlyVideoApp)
      EVA_main (pEarlyVideoApp, argc, argv);
    if ((argc > 2) && (!strcmp(argv[2], "-NonInteractive")))
      noninteractivemode();
    else
      TestEarlyVideo ();
    return 0;
}
