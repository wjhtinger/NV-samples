/*
 * Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdlib.h>
#include <string.h>
#include <cmdline.h>
#include <misc_utils.h>
#include <log_utils.h>

void PrintUsage(void)
{
    LOG_MSG("nvm_egldgpu:\n");
    LOG_MSG("-h                         \tPrint this usage\n");
    LOG_MSG("-v        [level]          \tVerbose, diagnostics prints\n");
    LOG_MSG("-f        [file name]      \tInput File Name(should be in YUV420p or RAW)\n");
    LOG_MSG("-fr       [WxH]            \tInput file resolution\n");
    LOG_MSG("-st       [type]           \tType: 420p/raw (Default type: 420p)\n");
    LOG_MSG("-bl                        \tProducer uses blocklinear surface. Default is pitch linear\n");
    LOG_MSG("                           \tBlocklinear is supported only for iGPU\n");
    LOG_MSG("                           \tdGPU works with pitch linear only\n");
    LOG_MSG("-n        [frame count]    \tNo. of frames to produce(Default: all frames in file)\n");
    LOG_MSG("--fifo                      \tSet FIFO mode for EGL stream. Default is Mailbox mode\n");
    LOG_MSG("--crossproc                 \tSet cross process for producer & consumer: prod/con\n");
    LOG_MSG("-s        [file name]      \tSave to file for consumer \n");
    LOG_MSG("--dgpu    [1/0]            \t 1- Consumer is running on dGPU.\n");
    LOG_MSG("                           \t 0 - Consumer is on Tegra.(Default is 1)\n");
    LOG_MSG("--useblit                  \t Use blit before posting the surfaces to eglstream\n");
    LOG_MSG("Examples: \n");
    LOG_MSG(" 1.To test image producer and cuda consumer from a single process\n");
    LOG_MSG("      ./nvm_egldgpu -f test.yuv -s cuda.yuv --fifo  -fr 1920x1080\n");
    LOG_MSG(" 2.To test image producer and cuda consumer from 2 different processes\n");
    LOG_MSG(" First run consumer \n");
    LOG_MSG("      ./nvm_egldgpu --crossproc con -s cuda.yuv --fifo \n");
    LOG_MSG(" Then run producer \n");
    LOG_MSG("      ./nvm_egldgpu --crossproc prod -f test1.yuv -fr 1920x1080  --fifo \n");

}

static int FilePathChanger(char * dest, char * src)
{
    int l;
    char * p;
    char * p2;
    l = strlen(dest);
    p = &dest[l-1];

    if(p == NULL) {
        return 1;
    }
    while(*(p-1) != '/') {
        p--;
    }

    l = strlen(src);
    p2 = &src[l];

    if(p2 == NULL) {
        return 1;
    }

    while(*(p2-1) != '/') {
        p2--;
    }

    l = strlen(p2);
    memset(p2, 0, l);

    strcpy(p2, p);
    strcpy(dest, src);
    return 0;
}

static void CheckPath(char * p)
{
    int l = strlen(p);
    p = &p[l-1];

    if(*p != '/') {
        p++;
        *p = '/';
        p++;
        p = '\0';
    }
}

int MainParseArgs(int argc, char **argv, TestArgs *args)
{
    int bLastArg = 0;
    int bDataAvailable = 0;
    int i;
    //default params
    args->imagetype = IMAGE_TYPE_YUV420;
    args->isConsumerondGPU = NV_TRUE;
    args->producer = EGLSTREAM_NVMEDIA_IMAGE;
    args->consumer = EGLSTREAM_CUDA;
    args->pitchLinearOutput = NV_TRUE;

    for (i = 1; i < argc; i++) {
        // check if this is the last argument
        bLastArg = ((argc - i) == 1);

        // check if there is data available to be parsed following the option
        bDataAvailable = (!bLastArg) && !(argv[i+1][0] == '-');

        if (argv[i][0] == '-') {
            if (strcmp(&argv[i][1], "h") == 0) {
                PrintUsage();
                return 1;
            } else if (strcmp(&argv[i][1], "-fifo") == 0) {
                args->fifoMode = NV_TRUE;
            } else if (strcmp(&argv[i][1], "-useblit") == 0) {
                args->useblitpath = NV_TRUE;
            } else if (strcmp(&argv[i][1], "v") == 0) {
                int logLevel = LEVEL_DBG;
                if(bDataAvailable) {
                    logLevel = atoi(argv[++i]);
                    if(logLevel < LEVEL_ERR || logLevel > LEVEL_DBG) {
                        LOG_INFO("MainParseArgs: Invalid logging level chosen (%d). ", logLevel);
                        LOG_INFO("           Setting logging level to LEVEL_ERR (0)\n");
                        logLevel = LEVEL_ERR;
                    }
                }
                SetLogLevel(logLevel);
                args->logLevel = logLevel;
            } else if (strcmp(&argv[i][1], "-crossproc") == 0 ) {
                if(bDataAvailable) {
                    ++i;
                    if(!strcasecmp(argv[i], "prod")) {
                        args->isProdCrossProc = NV_TRUE;
                    } else if(!strcasecmp(argv[i], "con")) {
                        args->isConsCrossProc = NV_TRUE;
                    } else {
                        LOG_ERR("ERR: ParseArgs: --crossproc must be followed by prod or con\n");
                        return 1;
                    }
                } else {
                    args->isCrossProc = NV_TRUE;
                }
            } else if (strcmp(&argv[i][1], "-dgpu") == 0 ) {
                if(bDataAvailable) {
                    ++i;
                    if(!strcasecmp(argv[i], "1")) {
                        args->isConsumerondGPU = NV_TRUE;
                    } else if(!strcasecmp(argv[i], "0")) {
                        args->isConsumerondGPU = NV_FALSE;
                    } else {
                        LOG_ERR("ERR: ParseArgs: --dgpu must be followed by 1 or 0\n");
                        return 1;
                    }
                } else {
                    args->isConsumerondGPU = NV_TRUE;
                }
            } else if(strcmp(&argv[i][1], "f") == 0) {
                // Input file name
                if(bDataAvailable) {
                    args->inpFileName = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -f must be followed by input file name\n");
                    return 1;
                }
            } else if(strcmp(&argv[i][1], "fr") == 0) {
                if(bDataAvailable) {
                    if((sscanf(argv[++i], "%ux%u", &args->width, &args->height) != 2)) {
                        LOG_ERR("ParseArgs: Bad output resolution: %s\n", argv[i]);
                        return 1;
                    }
                } else {
                    LOG_ERR("ParseArgs: -fr must be followed by resolution\n");
                    return 1;
                }
            } else if(strcmp(&argv[i][1], "bl") == 0) {
                args->pitchLinearOutput = NV_FALSE;
            } else if (strcmp(&argv[i][1], "st") == 0 ) {
                if(bDataAvailable) {
                    ++i;
                    if(!strcasecmp(argv[i], "420p")) {
                        args->imagetype = IMAGE_TYPE_YUV420;
                    } else if(!strcasecmp(argv[i], "rgba")) {
                        args->imagetype = IMAGE_TYPE_RGBA;
                    } else if(!strcasecmp(argv[i], "raw")) {
                        args->imagetype = IMAGE_TYPE_RAW;
                    } else {
                        LOG_ERR("ParseArgs: -st must be 420p or rgba or raw. Setting to 420p\n");
                        args->imagetype = IMAGE_TYPE_YUV420;
                    }
                } else {
                    LOG_ERR("ERR: ParseArgs: -st must be followed by surface type\n");
                    return 1;
                }
            } else if(strcmp(&argv[i][1],"-gencrc")  == 0) {
                if(args->testModeParams.isChkCrc == NV_TRUE) {
                    LOG_ERR("ParseArgs: --gencrc and --chkcrc cannot be done together\n");
                    return 1;
                }
                args->testModeParams.isTestMode = NV_TRUE;
                args->testModeParams.isGenCrc   = NV_TRUE;
                args->fifoMode = NV_TRUE;
            }   else if(strcmp(&argv[i][1], "-chkcrc") == 0) {
                if(bDataAvailable) {
                    args->testModeParams.refCrcFileName = argv[++i];
                    if(args->testModeParams.isGenCrc == NV_TRUE) {
                        LOG_ERR("ParseArgs: --chkcrc and --gencrc cannot be done together\n");
                        return 1;
                    }
                    args->testModeParams.isTestMode = NV_TRUE;
                    args->testModeParams.isChkCrc   = NV_TRUE;
                    args->fifoMode = NV_TRUE;
                } else {
                    LOG_ERR("ParseArgs: --chkcrc must be followed by output crc file name\n");
                    return 1;
                }
            }
            else if (strcmp(&argv[i][1], "-inputpath") == 0) {
                if(bDataAvailable) {
                    strcpy(args->inputPathName, argv[++i]);
                    CheckPath(args->inputPathName);
                    /*Change input filename*/
                    if(FilePathChanger(args->inpFileName, args->inputPathName) == 1) {
                        LOG_ERR("Invalid Input Path\n");
                        return 1;
                    }
                } else {
                    LOG_ERR("Error: --inputpath must be followed by directory path where input files are located.\n");
                    return 1;
                }
            }
            else if (strcmp(&argv[i][1], "-crcpath") == 0) {
                if(bDataAvailable) {
                    strcpy(args->crcPathName, argv[++i]);
                    CheckPath(args->crcPathName);
                    /*Change crc filename*/
                    if(FilePathChanger(args->testModeParams.refCrcFileName, args->crcPathName) == 1) {
                        LOG_ERR("Invalid Input Path\n");
                        return 1;
                    }
                } else {
                    LOG_ERR("Error: --crcpath must be followed by crc by directory path where crc files are located.\n");
                    return 1;
                }
            }  else if(strcmp(&argv[i][1], "s") == 0) {
                // Output file name
                if(bDataAvailable) {
                    args->outFileName = argv[++i];
                } else {
                    LOG_ERR("ParseArgs: -s must be followed by output file name\n");
                    return 1;
                }
            } else if (strcmp(&argv[i][1], "n") == 0) {
                if (bDataAvailable) {
                    int frameCount;
                    if (sscanf(argv[++i], "%d", &frameCount)) {
                        args->frameCount = frameCount;
                    } else {
                        LOG_DBG("ERR: -n must be followed by frame count.\n");
                    }
                } else {
                    LOG_DBG("ERR: -n must be followed by frame count.\n");
                    return 1;
                }
            }
            else {
                LOG_ERR("Invalid command line option --  %s\n", &argv[i][1]);
                return 1;
            }
        }
    }

    //check validity of the Args
    if (!args->isConsCrossProc) {
        if (!args->inpFileName || !args->width || !args->height) {
            goto fail;
        }
    }

    if((args->isCrossProc)) {
        /*Cross-process creation of producer */
        char argsProducer[1024];
        char str[256];

        /*Append flag -crossproc and prod used to internally indicate crossproc producer*/
        strcpy(argsProducer,"./nvm_egldgpu --crossproc prod ");

        if(args->inpFileName) {
            sprintf(str,"-f %s ",args->inpFileName);
            strcat(argsProducer,str);
        }
        if((args->width) & (args->height)) {
            sprintf(str,"-fr %ux%u ",args->width,args->height);
            strcat(argsProducer,str);
        }

        if(args->imagetype == IMAGE_TYPE_RGBA) {
            strcat(argsProducer,"-st rgba ");
        }else if(args->imagetype == IMAGE_TYPE_RAW) {
            strcat(argsProducer,"-st raw ");
        }else {
            strcat(argsProducer,"-st 420p ");
        }

        if(args->fifoMode) {
            strcat(argsProducer,"--fifo ");
        }

        if(args->testModeParams.isGenCrc) {
            strcat(argsProducer,"--gencrc ");
        }

        if(args->frameCount) {
            sprintf(str,"-n %d ",args->frameCount);
            strcat(argsProducer,str);
        }

        if(args->logLevel == LEVEL_DBG) {
            strcat(argsProducer,"-v ");
        }

        /*Make the process run in bg*/
        strcat (argsProducer,"& ");

        LOG_DBG("\n Crossproc Producer command: %s \n",argsProducer);
        /*Create crossproc Producer*/
        system(argsProducer);

        /*Enable crossproc Consumer in the same process */
        args->isConsCrossProc = NV_TRUE;
    }

    return 0;

fail:
    LOG_ERR("Invalid command\n");
    if(!args->testModeParams.isTestMode)
        PrintUsage();
    return 1;
}

