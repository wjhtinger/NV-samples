/*
 * demo.cpp
 *
 * Copyright (c) 2015-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "utils.h"
#include "producer.h"
#include "consumer.h"
#include "nvgldemo.h"

EXTENSION_LIST(EXTLST_EXTERN)

static bool run()
{
    if (!strncmp(demoOptions.proctype, "producer", NVGLDEMO_MAX_NAME)) {
        return runProducerProcess(demoState.stream, demoState.display);
    } else if (!strncmp(demoOptions.proctype, "consumer", NVGLDEMO_MAX_NAME)) {
        return runConsumerProcess(demoState.stream, demoState.display);
    }

    return true;
}

static void printHelp()
{
    NvGlDemoLog("Usage: eglcrosspart [options] [command] [command options]\n"
                "FIFO mode / mailbox mode. 0 -> mailbox mode\n"
                "[-fifo <n <= 2>]\n"
                "Ip address\n"
                "[-ip <IP address>]\n"
                "Producer or Consumer\n"
                "[-proctype <producer or consumer>]\n");
}

// Usage: o Consumer: ./eglcrosspart -proctype consumer
//        o Producer:. /eglcrosspart -ip 12.0.0.11 -proctype producer
int main(int argc, char *argv[])
{

    if ((argc < 2) || !NvGlDemoArgParse(&argc, argv)) {
        printHelp();
        exit(0);
    }

    while (1) {
        if (!((!strncmp(demoOptions.proctype, "consumer", NVGLDEMO_MAX_NAME)) && (demoOptions.eglQnxScreenTest == 1))) {
            if (!NvGlDemoInitializeParsed(&argc, argv, "eglcrosspart", 2, 8, 0)) {
                NvGlDemoLog("Error during initialization. Please check help\n");
                exit(0);
           }
        } else {
            EGLBoolean eglStatus;
            const char* extensions = NULL;

            if (!NvGlDemoInitConsumerProcess()) {
                NvGlDemoLog("EGL failed to create consumer socket.\n");
                exit(0);
            }

            demoState.display = NVGLDEMO_EGL_GET_DISPLAY(demoState.nativeDisplay);
            if (demoState.display == EGL_NO_DISPLAY) {
                NvGlDemoLog("EGL failed to obtain display.\n");
                exit(0);
            }

            // Initialize EGL
            eglStatus = NVGLDEMO_EGL_INITIALIZE(demoState.display, 0, 0);
            if (!eglStatus) {
                NvGlDemoLog("EGL failed to initialize.\n");
                exit(0);
            }

            // Get extension string
            extensions = NVGLDEMO_EGL_QUERY_STRING(demoState.display, EGL_EXTENSIONS);
            if (!extensions) {
                NvGlDemoLog("eglQueryString fail.\n");
                exit(0);
            }

            if (!strstr(extensions, "EGL_EXT_stream_consumer_qnxscreen_window")) {
                NvGlDemoLog("EGL does not support EGL_EXT_stream_consumer_qnxscreen_window extension.\n");
                exit(0);
            }


            if(!NvGlDemoCreateCrossPartitionEGLStream()) {
                NvGlDemoLog("EGL failed to create CrossPartitionEGLStream.\n");
                exit(0);
            }
        }

        // Setup the EGL extensions.
        setupExtensions();

        run();

        demoState.stream = EGL_NO_STREAM_KHR;

        if (!((!strncmp(demoOptions.proctype, "consumer", NVGLDEMO_MAX_NAME)) && (demoOptions.eglQnxScreenTest == 1))) {
            NvGlDemoShutdown();
        } else {
            NvGlDemoEglTerminate();
        }
    }
    return 0;
}
