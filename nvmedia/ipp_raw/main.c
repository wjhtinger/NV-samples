/* Copyright (c) 2015-2017, NVIDIA CORPORATION.  All rights reserved.
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

#include "ipp_raw.h"
#include "cmdline.h"
#include "log_utils.h"
#include "misc_utils.h"
#include "nvmedia.h"
#include "nvmedia_2d.h"
#include "interop.h"

#define IDLE_TIME 10000

/* Quit flag. Out of context structure for sig handling */
static NvMediaBool *quit_flag;

static void
PrintCommandUsage(void)
{
    LOG_MSG("Usage: Run time command\n");
    LOG_MSG("\nAvailable commands:\n");
    LOG_MSG("-h             Print command usage\n");
    LOG_MSG("-q(uit)        Quit the applicatin\n");
}

static int
ExecuteNextCommand (
    TestArgs *testArgs,
    IPPCtx *ctx,
    FILE *file)
{
    char input_line[MAX_STRING_SIZE] = {0};
    char command[MAX_STRING_SIZE] = {0};
    char *input_tokens[MAX_STRING_SIZE] = {0};
    int i, tokens_num = 0;

    ParseNextCommand(file, input_line, input_tokens, &tokens_num);
    if(input_line[0] == 0 || !tokens_num)
        return 0;

    strcpy(command, input_tokens[0]);

    if(command[0] == '[') {
        goto freetokens;
    } else if(!strcasecmp(command, "h")) {
        PrintCommandUsage();
    } else if(!strcasecmp(command, "q") || !strcasecmp(command, "quit")) {
        *quit_flag = NVMEDIA_TRUE;
    }else {
        LOG_ERR("Invalid Command, type \"h\" for help on usage \n");
    }

freetokens:
    for(i = 0; i < tokens_num; i++) {
        free(input_tokens[i]);
    }

    return 0;
}

static void
SigHandler (int signum)
{
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGKILL, SIG_IGN);
    signal(SIGSTOP, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    *quit_flag = NVMEDIA_TRUE;

    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGKILL, SIG_DFL);
    signal(SIGSTOP, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
}

static void
SigSetup (void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SigHandler;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGKILL, &action, NULL);
    sigaction(SIGSTOP, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
}

int main (int  argc, char *argv[])
{
    IPPCtx *ctx = NULL;
    TestArgs testArgs;
    InteropContext *interopCtx = NULL;
    // Create and initialize ipp context
    ctx = calloc(1, sizeof(IPPCtx));
    if (!ctx) {
        LOG_ERR("%s: Failed to allocate memory", __func__);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    quit_flag = &ctx->quit;
    SigSetup();

    memset(&testArgs, 0, sizeof(TestArgs));

    if(IsFailed(ParseArgs(argc, argv, &testArgs))){
        free(ctx);
        return NVMEDIA_STATUS_ERROR;
    }

    if(IsFailed(IPPInit(ctx, &testArgs))){
        LOG_ERR("%s: Error in IPPInit", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    interopCtx = calloc(1, sizeof(InteropContext));
    if (!interopCtx) {
        LOG_ERR("%s: Failed to allocate memory", __func__);
        IPPFini(ctx);
        return NVMEDIA_STATUS_OUT_OF_MEMORY;
    }

    if (IsFailed(InteropInit(interopCtx, ctx, &testArgs))){
        LOG_ERR("%s: Error in InteropInit", __func__);
        goto failed;
    }

    if(IsFailed(InteropProc(interopCtx))){
       goto failed;
    }

    if(IsFailed(IPPStart(ctx))){
       goto failed;
    }

    if(!testArgs.disableInteractiveMode){
        LOG_MSG("Type \"h\" for a list of options\n");
    }

    // Waiting for new commands from user
    while(!ctx->quit) {

        if(testArgs.disableInteractiveMode) {
            if(testArgs.timedRun) {
                sleep(testArgs.runningTime);
                ctx->quit = NVMEDIA_TRUE;
                continue;
            }

            usleep(IDLE_TIME);
            continue;
        }

        LOG_MSG("-");
        ExecuteNextCommand(&testArgs, ctx, NULL);
    }

    if(IsFailed(IPPStop(ctx))){
        goto failed;
    }


failed:
    InteropFini(interopCtx);
    IPPFini(ctx);

    return 0;
}
