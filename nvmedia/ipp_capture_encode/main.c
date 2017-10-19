/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "staging.h"
#include "cmdline.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia.h"
#include "stream_top.h"
#include "check_version.h"
#include "err_handler.h"
#include "display.h"
#include "op_stream_handler.h"
#include "image_encoder_top.h"
#include "writer.h"

#define IDLE_TIME                   10000
#define MAX_NUM_OUTPUTS_PER_PIPE    2

/* Quit flag. Out of context structure for sig handling */
static volatile NvMediaBool *quit_flag;

NvMainContext   mainCtx;

static int
ExecuteNextCommand(NvMainContext *ctx) {
    char input[256] = { 0 };

    if (!fgets(input, 256, stdin)) {
        LOG_ERR("%s: Failed to read command\n", __func__);
        return -1;
    }

    /* Remove new line character */
    if (input[strlen(input) - 1] == '\n')
        input[strlen(input) - 1] = '\0';


    if (!strcasecmp(input, "q") || !strcasecmp(input, "quit")) {
        *quit_flag = NVMEDIA_TRUE;
        return 0;
    } else if (!strcasecmp(input, "h") || !strcasecmp(input, "help")) {
        PrintUsage();
        return 0;
    } else if (!strcasecmp(input, "")) {
        return 0;
    }

    return 0;
}

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

static NvMediaStatus
GetOutput (
        NvMediaIPPComponentOutput *pOutput,
        NvU32                     pipeNum,
        NvU32                     outputNum)
{
    NvMediaStatus   status;

    // call to get the output
    status = StagingGetOutput(&mainCtx,
                              pOutput,
                              pipeNum,
                              outputNum);
    return status;
}

static NvMediaStatus
PutOutput (
        NvMediaIPPComponentOutput *pOutput,
        NvU32                     pipeNum,
        NvU32                     outputNum)
{
    NvMediaStatus   status;

    // call to put the output
    status = StagingPutOutput(&mainCtx,
                              pOutput,
                              pipeNum,
                              outputNum);
    return status;
}

int main (int  argc, char *argv[])
{
    TestArgs                    allArgs;
    void                        *pImageEncoderTop = NULL;
    StreamCtx                   *pStreamCtx;
    void                        *pWriter = NULL;
    NvU32                       count;
    OpStreamHandlerParams       opStreamHandlerParams;
    NvQueue                     *pEncoderOutputQueueRef = NULL;
    NvQueue                     *pOutputStreamHandlerFeedQueueRef[MAX_NUM_OUTPUTS_PER_PIPE];
    NvU32                       numOutputs = 0;

    memset(&allArgs, 0, sizeof(TestArgs));
    memset(&mainCtx, 0, sizeof(NvMainContext));

    if (CheckModulesVersion() != NVMEDIA_STATUS_OK) {
        return -1;
    }

    if (IsFailed(ParseArgs(argc, argv, &allArgs))) {
        return -1;
    }

    quit_flag = &mainCtx.quit;
    SigSetup();

    /* Initialize context */
    mainCtx.testArgs = &allArgs;

    if(StagingInit(&mainCtx) != NVMEDIA_STATUS_OK){
        LOG_ERR("%s: Error in IPPInit", __func__);
        goto failed;
    }

    if (ErrHandlerInit(&mainCtx) != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: Failed to Initialize ErrHandler\n", __func__);
        goto failed;
    }

    pStreamCtx = (StreamCtx *) mainCtx.ctxs[STREAMING_ELEMENT];
    allArgs.outputWidth = pStreamCtx->captureSS.captureParams.width;
    allArgs.outputHeight = pStreamCtx->captureSS.captureParams.height;

    if (NVMEDIA_TRUE == allArgs.enableEncode) {
        numOutputs = 1;
    }

    opStreamHandlerParams.pGetOutput = &GetOutput;
    opStreamHandlerParams.pPutOutput = &PutOutput;
    opStreamHandlerParams.pQuit = quit_flag;
    opStreamHandlerParams.skipInitialFramesCount = allArgs.skipInitialFramesCount;

    // create allArgs.imagesNum contexts for the output stream handler
    for (count = 0; count < allArgs.imagesNum; count++) {
        // only the first pipe output is rendered
        if ((0 == count) && (NVMEDIA_TRUE == allArgs.displayEnabled)) {
            opStreamHandlerParams.numOutputs = numOutputs + 1;
        }
        else {
            opStreamHandlerParams.numOutputs = numOutputs;
        }

        opStreamHandlerParams.pipeNum = count;
        pOutputStreamHandlerFeedQueueRef[0] = NULL;
        pOutputStreamHandlerFeedQueueRef[1] = NULL;

        mainCtx.pOutputStreamHandlerContext[count] = OpStreamHandlerInit(&opStreamHandlerParams,
                                                                         pOutputStreamHandlerFeedQueueRef);

        if (NULL == mainCtx.pOutputStreamHandlerContext[count]) {
            LOG_ERR("%s: Failed to Initialize output stream handler\n", __func__);
            goto failed;
        }

        if (NVMEDIA_TRUE == allArgs.enableEncode) {
            pImageEncoderTop = (void *)ImageEncoderTopInit(&allArgs,
                                                           pOutputStreamHandlerFeedQueueRef[0],
                                                           &OpStreamHandlerPutBuffer,
                                                           quit_flag,
                                                           &pEncoderOutputQueueRef);

            mainCtx.pImageEncoderContext[count] = (void *)pImageEncoderTop;

            if ((NULL == pImageEncoderTop) ||
                (NULL == pEncoderOutputQueueRef)) {
                LOG_ERR("%s: Failed to create image encoder\n", __func__);
                goto failed;
            }

            pWriter = WriterInit(&allArgs,
                                 pEncoderOutputQueueRef,
                                 count,
                                 quit_flag);

            if (NULL == pWriter) {
                LOG_ERR("%s: Failed to create writer\n", __func__);
                goto failed;
            }

            mainCtx.pWriterContext[count] = pWriter;
        }

        if ((0 == count) && (NVMEDIA_TRUE == allArgs.displayEnabled)) {
            if (DisplayInit(&mainCtx, pOutputStreamHandlerFeedQueueRef[1]) != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to Initialize Display\n", __func__);
                goto failed;
            }
        }
    }

    if (DisplayStart(&mainCtx) != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: DisplayStart Failed\n", __func__);
        goto failed;
    }

    if (ErrHandlerStart(&mainCtx) != NVMEDIA_STATUS_OK) {
        LOG_ERR("%s: ErHandlerProc Failed\n", __func__);
        goto failed;
    }

    for (count = 0; count < allArgs.imagesNum; count++) {
        if (OpStreamHandlerStart(mainCtx.pOutputStreamHandlerContext[count]) != NVMEDIA_STATUS_OK) {
            LOG_ERR("%s: Failed to start output stream handler\n", __func__);
            goto failed;
        }

        if (NVMEDIA_TRUE == allArgs.enableEncode) {
            if (ImageEncoderTopStart(mainCtx.pImageEncoderContext[count]) != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to start image encoder\n", __func__);
                goto failed;
            }

            if (WriterStart(mainCtx.pWriterContext[count]) != NVMEDIA_STATUS_OK) {
                LOG_ERR("%s: Failed to start writer\n", __func__);
                goto failed;
            }
        }
    }

    if(StagingStart(&mainCtx) != NVMEDIA_STATUS_OK){
        LOG_ERR("%s: Failed to start streaming pipe\n", __func__);
       goto failed;
    }

    if(!allArgs.disableInteractiveMode){
        LOG_MSG("Type \"h\" for a list of options\n");
    }

    // Waiting for new commands from user
    while(!mainCtx.quit) {
        if(allArgs.disableInteractiveMode) {
            if(allArgs.timedRun) {
                sleep(allArgs.runningTime);
                mainCtx.quit = NVMEDIA_TRUE;
                continue;
            }

            usleep(IDLE_TIME);
            continue;
        }

        LOG_MSG("-");
        ExecuteNextCommand(&mainCtx);
    }

    StagingStop(&mainCtx);

failed:

    ErrHandlerStop(&mainCtx);

    // first stop all the execution agents
    for (count = 0; count < allArgs.imagesNum; count++) {
        OpStreamHandlerStop(mainCtx.pOutputStreamHandlerContext[count]);
    }

    if (NVMEDIA_TRUE == allArgs.enableEncode) {
         for (count = 0; count < allArgs.imagesNum; count++) {
             ImageEncoderTopStop(mainCtx.pImageEncoderContext[count]);
             WriterStop(mainCtx.pWriterContext[count]);
         }
    }

    DisplayStop(&mainCtx);

    // destroy the blocks in reverse order, i.e. from downstream to upstream
    // e.g. destroy writer first followed by encoder
    // similarly destroy display before destroying output stream handler
    // this order guarantees that the downstream blocks may not be using
    // any of the shared data structures of the upstream blocks when they
    // the upstream blocks are destroyed
    DisplayFini(&mainCtx);

    if (NVMEDIA_TRUE == allArgs.enableEncode) {
        for (count = 0; count < allArgs.imagesNum; count++) {
            WriterFini(mainCtx.pWriterContext[count]);
            ImageEncoderTopFini(mainCtx.pImageEncoderContext[count]);
        }
    }

    for (count = 0; count < allArgs.imagesNum; count++) {
        OpStreamHandlerFini(mainCtx.pOutputStreamHandlerContext[count]);
    }

    StagingFini(&mainCtx);

    ErrHandlerFini(&mainCtx);

    return 0;
}
