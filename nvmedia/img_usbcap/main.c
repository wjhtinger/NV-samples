/* Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <limits.h>
#include <math.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "thread_utils.h"

#include "main.h"
#include "capture.h"
#include "process2d.h"
#include "interop.h"


/* Quit flag. Out of context structure for sig handling */
volatile NvBool *quit_flag;

static void
SigHandler (int signum)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    *quit_flag = NVMEDIA_TRUE;

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

static void
SigSetup (void)
{
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = SigHandler;
    sigaction(SIGINT, &action, NULL);
}

int main (int  argc, char *argv[])
{
    NvMainContext mainCtx;
    TestArgs  testArgs;
    memset(&testArgs, 0, sizeof(TestArgs));
    memset(&mainCtx, 0, sizeof(NvMainContext));

    if(ParseArgs(argc, argv, &testArgs))
        return -1;

    quit_flag = &mainCtx.quit;
    SigSetup();

    /*Initialize context*/
    mainCtx.devPath = testArgs.devPath;
    mainCtx.width = testArgs.width;
    mainCtx.height = testArgs.height;
    mainCtx.saveOutFileFlag = testArgs.saveOutFile;
    mainCtx.windowOffset[0] = testArgs.windowOffset[0];
    mainCtx.windowOffset[1] = testArgs.windowOffset[1];
    mainCtx.displayId = testArgs.displayId;
    mainCtx.outFileName = testArgs.outFileName;
    strcpy(mainCtx.surfFmt,testArgs.surfFmt);
    strcpy(mainCtx.inpFmt,testArgs.inpFmt);

    /*Call Init for each block*/
    if (IsFailed(CaptureInit(&mainCtx))) {
        LOG_DBG("\n Capture Thread Init failed\n");
        return -1;
    }
    if (IsFailed(Process2DInit(&mainCtx))) {
        LOG_DBG("\n Process2D Thread Init failed\n");
        return -1;
    }
    if (IsFailed(InteropInit(&mainCtx))) {
        LOG_DBG("\n Interop Thread Init failed\n");
        return -1;
    }

    /*Create threads for each block*/
    mainCtx.threadsExited[USB_CAPTURE] = NV_FALSE;
    if (IsFailed(NvThreadCreate(&mainCtx.threads[USB_CAPTURE],
                             (void *)&CaptureProc,
                             (void *)&mainCtx,
                             NV_THREAD_PRIORITY_NORMAL))) {
        mainCtx.threadsExited[USB_CAPTURE] = NV_TRUE;
        LOG_DBG("\n Capture Thread Creation failed\n");
        return -1;
    }

    mainCtx.threadsExited[PROCESS_2D] = NV_FALSE;
    if (IsFailed(NvThreadCreate(&mainCtx.threads[PROCESS_2D],
                             (void *)&Process2DProc,
                             (void *)&mainCtx,
                             NV_THREAD_PRIORITY_NORMAL))) {
        mainCtx.threadsExited[PROCESS_2D] = NV_TRUE;
        LOG_DBG("\n Process2D Thread Creation failed\n");
        return -1;
    }

    mainCtx.threadsExited[INTEROP] = NV_FALSE;
    if (IsFailed(NvThreadCreate(&mainCtx.threads[INTEROP],
                             (void *)&InteropProc,
                             (void *)&mainCtx,
                             NV_THREAD_PRIORITY_NORMAL))) {
        mainCtx.threadsExited[INTEROP] = NV_TRUE;
        LOG_DBG("\n Interop Thread Creation failed\n");
        return -1;
    }

    /* Waiting for user to quit */
    while (!mainCtx.quit) {
        usleep(1000);
    }

    /* Waiting for all threads to exit*/
    while (!(mainCtx.threadsExited[USB_CAPTURE] &&
             mainCtx.threadsExited[PROCESS_2D] &&
             mainCtx.threadsExited[INTEROP])) {
        usleep(1000);
    }

    InteropFini(&mainCtx);

    Process2DFini(&mainCtx);

    CaptureFini(&mainCtx);

    return 0;
}
