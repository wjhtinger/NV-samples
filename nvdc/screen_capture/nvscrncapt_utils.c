/* Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
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
#include <unistd.h>
#include <sys/time.h>

#include "nvscrncapt_utils.h"

void
PrintUsage ()
{
    LOG_MSG("Usage: nvdisp_scrncapt [options]\n");
    LOG_MSG("Sample test-application to validate Tegra screen capture API.\n");
    LOG_MSG("\n Available command line options:\n");
    LOG_MSG("-h                      Print usage\n");
    LOG_MSG("-v [level]              Logging Level. Default = 0\n");
    LOG_MSG("                        0: Errors, 1: Warnings, 2: Info, 3: Debug\n");
    LOG_MSG("                        Default: 1\n");
    LOG_MSG("-t                      Timestamp the RGB capture file.\n");
    LOG_MSG("-m [head-mask]          Mask of heads for screen capture in decimal\n");
    LOG_MSG("-alloc                  Test app will pre-allocate memory\n");
}

int
ParseArgs (
    int argc,
    char *argv[],
    TestArgs *args)
{
    int i = 0;
    unsigned char bLastArg = 0;
    unsigned char bDataAvailable = 0;
    struct timeval currentTime;

    // Initialize default args
    args->headMask = ~0;
    args->logLevel = LEVEL_WARN;
    snprintf(args->filePrefix, MAX_STRING_SIZE, "scrncap");

    // First look for help request
    if (argc >= 2) {
        for (i = 1; i < argc; i++) {
            // check if this is the last argument
            bLastArg = ((argc - i) == 1);

            // check if there is data available to be parsed
            bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

            if (!strcasecmp(argv[i], "-h")) {
                PrintUsage();
                return 1;
            }
        }
    }

    // The rest
    if (argc >= 2) {
        for (i = 1; i < argc; i++) {
            // check if this is the last argument
            bLastArg = ((argc - i) == 1);

            // check if there is data available to be parsed
            bDataAvailable = (!bLastArg) && (argv[i+1][0] != '-');

            if (strcasecmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                return 1;
            } else if (!strcasecmp(argv[i], "-v")) {
                args->logLevel = LEVEL_DBG;
                if (bDataAvailable) {
                    args->logLevel = atoi(argv[++i]);
                    if (args->logLevel < LEVEL_ERR ||
                       args->logLevel > LEVEL_DBG) {
                        printf("Invalid logging level chosen (%d)\n",
                               args->logLevel);
                        printf("Setting logging level to LEVEL_ERR (0)\n");
                        args->logLevel = LEVEL_ERR;
                    }
                }
            } else if(!strcasecmp(argv[i], "-t")) {
                gettimeofday(&currentTime, NULL);
                snprintf(args->filePrefix, MAX_STRING_SIZE,
                        "scrncap-%d", (int) currentTime.tv_sec);
            } else if(!strcasecmp(argv[i], "-m")) {
                if(bDataAvailable) {
                    args->headMask = (unsigned int) atoi(argv[++i]);
                }
            } else if(!strcasecmp(argv[i], "-alloc")) {
                args->preAllocateMemory = 1;
            } else {
                LOG_ERR("Unsupported option encountered: %s\n", argv[i]);
                return -1;
            }
        }
    }
    return 0;
}
