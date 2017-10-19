/* Copyright (c) 2016-2017, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ipp.h"
#include "cmdline.h"
#include "log_utils.h"
#include "misc_utils.h"

#define IDLE_TIME 10000

/* Quit flag. Out of context structure for sig handling */
static NvMediaBool *quit_flag;

static void
PrintCommandUsage(void)
{
    LOG_MSG("Usage: Run time command\n");
    LOG_MSG("\nAvailable commands:\n");
    LOG_MSG("-h      Print command usage\n");
    LOG_MSG("-q(uit) Quit the applicatin\n");
}

static int
ExecuteNextCommand (
    TestArgs *testArgs,
    IPPTest *ctx,
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
    } else {
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
    signal(SIGPIPE, SIG_IGN);

    *quit_flag = NVMEDIA_TRUE;
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
    sigaction(SIGPIPE, &action, NULL);
}

int main (int  argc, char *argv[])
{
    IPPTest *ctx = NULL;
    TestArgs testArgs;

    memset(&testArgs, 0, sizeof(TestArgs));

    if(ParseArgs(argc, argv, &testArgs))
        return -1;

    if(IsFailed(IPP_Init(&ctx, &testArgs)))
        return -1;

    SigSetup();

    if(IsFailed(IPP_Start(ctx, &testArgs)))
        return -1;

    quit_flag = &ctx->quit;

    if(!testArgs.disableInteractiveMode)
        LOG_MSG("Type \"h\" for a list of options\n");

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

    if(IsFailed(IPP_Stop(ctx)))
        return -1;

    IPP_Fini(ctx);

    return 0;
}
