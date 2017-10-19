/*
 * nvgldemo_parse.c
 *
 * Copyright (c) 2007-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// This file contains utility functions to parse the command line for
//   settings shared by multiple demos.
//

#include "nvgldemo.h"

// Global parsed options structure
NvGlDemoOptions demoOptions;

// Static variable to detect failure during parsing
static int parseFailed   = 0;
static int parseComplete = 0;

// For some systems, the command line argument list is constant.
//   Therefore we provide a utility to duplicate as a non-const list.
char**
NvGlDemoArgDuplicate(
    int argc, const char *const *argv)
{
    char** newargv;
    int i;

    newargv = (char**)MALLOC((argc + 1) * sizeof(char*));
    for (i=0; i<argc; ++i) {
        int len = 1 + STRLEN(argv[i]);
        newargv[i] = (char*)MALLOC(len);
        STRNCPY(newargv[i], argv[i], len);
    }
    newargv[argc] = NULL;

    return newargv;
}

// Extract and return an argument from the list
const char*
NvGlDemoArgExtract(
    int* argc, char** argv, int index)
{
    char* val = argv[index];
    int   j;
    for (j=index+1; j<*argc; ++j) {
        argv[j-1] = argv[j];
    }
    (*argc)--;
    return val;
}

// Advance past first part of argument, extracting if nothing is left.
void
NvGlDemoArgAdvance(
    int* argc, char** argv, int index, int count)
{
    argv[index] += count;
    if (argv[index][0] == 0)
        (void)NvGlDemoArgExtract(argc, argv, index);
}

// Check whether argument matches specified string. If so, advance.
int
NvGlDemoArgMatch(
    int* argc, char** argv, int index, const char* test)
{
    int len;

    if (parseFailed) return 0;

    len = STRLEN(test);
    if (!STRNCMP(argv[index], test, len)) {
        NvGlDemoArgAdvance(argc, argv, index, len);
        return 1;
    }

    return 0;
}

// Check whether argument matches. If so, obtain string value.
int
NvGlDemoArgMatchStr(
    int* argc, char** argv, int index, const char* test,
    const char* usage,
    unsigned int maxlen, char* value)
{
    const char* v;

    if (!NvGlDemoArgMatch(argc, argv, index, test)) return 0;

    if (index > *argc - 1) {
        NvGlDemoLog("Missing parameters (%s) for %s.\n", usage, test);
        parseFailed = 1;
        return 0;
    }

    v = NvGlDemoArgExtract(argc, argv, index);
    if (STRLEN(v) >= maxlen) {
        NvGlDemoLog("Value for %s for %s exceeds allowed length (%d)\n",
                    v, test, maxlen);
        parseFailed = 1;
        return 0;
    }

    STRNCPY(value, v, maxlen);
    return 1;
}

// Check whether argument matches. If so, obtain integer value(s).
int
NvGlDemoArgMatchInt(
    int* argc, char** argv, int index, const char* test,
    const char* usage, int minval, int maxval,
    unsigned int count, int* value)
{
    const char* v;
    unsigned int i;

    if (!NvGlDemoArgMatch(argc, argv, index, test)) return 0;

    if (index > *argc - (int)count) {
        NvGlDemoLog("Missing parameter (%s) for %s.\n", usage, test);
        parseFailed = 1;
        return 0;
    }

    for (i=0; i<count; ++i) {
        v = NvGlDemoArgExtract(argc, argv, index);
        value[i] = STRTOL(v, NULL, 10);
        if ((value[i] < minval) || (maxval < value[i])) {
            NvGlDemoLog("Invalid parameter (%s) for %s.\n", v, test);
            parseFailed = 1;
            return 0;
        }
    }
    return 1;
}

// Check whether argument matches. If so, obtain float value(s).
int
NvGlDemoArgMatchFlt(
    int* argc, char** argv, int index, const char* test,
    const char* usage, float minval, float maxval,
    unsigned int count, float* value)
{
    const char* v;
    unsigned int i;

    if (!NvGlDemoArgMatch(argc, argv, index, test)) return 0;

    if (index > *argc - (int)count) {
        NvGlDemoLog("Missing parameter (%s) for %s.\n", usage, test);
        parseFailed = 1;
        return 0;
    }

    for (i=0; i<count; ++i) {
        v = NvGlDemoArgExtract(argc, argv, index);
        value[i] = STRTOF(v, NULL);
        if ((value[i] < minval) || (maxval < value[i])) {
            NvGlDemoLog("Invalid parameter (%s) for %s.\n", v, test);
            parseFailed = 1;
            return 0;
        }
    }

    return 1;
}

// Usage string for common command line arguments
const char*
NvGlDemoArgUsageString(void)
{
    return
        "  Window system options (not all available on all platforms):\n"
        "    [-windowsize   <xres> <yres>] (window size)\n"
        "    [-windowoffset <xpos> <ypos>] (window position)\n"
#ifdef NVGLDEMO_HAS_DEVICE
        "    [-dispno <connector index>]\n"
        "                                  (display output number selection)\n"
        "    [-displaysize <xres> <yres>] (sets display size)\n"
        "    [-refresh <rate>] (sets display refresh rate)\n"
        "    [-currentmode (uses currently available display mode)]\n"
        "\n"
#endif
        "\n"
        "  Rendering options (availability varies by platform):\n"
        "    [-buffering <n>]                       (n-buffered swaps)\n"
        "    [-msaa <samples>] or [-fsaa <samples>] (multi/super sampling)\n"
#ifdef EGL_NV_coverage_sample
        "    [-csaa <samples>] or [-vcaa <samples>] (coverage sampling)\n"
#endif
        "    [-useprogbin <boolean>]            (0 false, 1 true)\n"
#ifdef EGL_EXT_stream_consumer_qnxscreen_window
        "    [-eglqnxscreentest <disable|enable>]        \n"
        "                            (eglqnxscreentest selection)\n"
        "    [-fifo <length>]               (0 min, 5 max)\n"
        "    [-renderahead <length>]        (-1 min, 10 max)\n"
        "    [-latency <usec>]              (0 min, 2147483647 max)\n"
        "    [-timeout <usec>]              (0 min, 2147483647 max)\n"
        "    [-postflags <int>]         (0 immediate, 1 wait_idle)\n"
        "    [-surfacetype <int>]       (0 Normal, 1 Bottom_Origin)\n"
#endif //EGL_EXT_stream_consumer_qnxscreen_window
        "    [-ip <IP address>]        (Ip address)\n"
        "    [-proctype <producer or consumer>] (Producer or Consumer)\n"
        "    [-surfaceid <n>]              (0 min, 9999 max)\n"
        "\n"
        "  Note:\n"
        "    Use of parameters which modify the display configuration\n"
        "    is primarily intended for use when running the application\n"
        "    in a standalone fashion.  When there is a separate display\n"
        "    server running, their use is discouraged, as they will blindly\n"
        "    change the state without regard to any other applications\n"
        "    sharing the display.\n"
        ;
}

// Parse the common arguments
int
NvGlDemoArgParse(
    int* argc, char** argv)
{
    char tmp[32];
    int i;

    // If parsing already done, skip.
    if (parseComplete) return 1;

    // Initialize options
    MEMSET(&demoOptions, 0, sizeof(demoOptions));
    demoOptions.displayBlend = -1;
    demoOptions.displayAlpha = -1.0;
    demoOptions.displayColorKey[0] = -1.0;
    demoOptions.renderahead = -1;
    demoOptions.timeout         = 16000;
    demoOptions.flags  = 0;
#ifdef EGL_EXT_stream_consumer_qnxscreen_window
    //Default value of eglqnx screen window consumer
    demoOptions.postWindowFlags = 1;
    // By Default egldevice surface rendering type is
    // Bottom_Origin. Need to configure Flip display in DC
    demoOptions.surfaceType = 1;
#endif //EGL_EXT_stream_consumer_qnxscreen_window

    //Default value of port
    demoOptions.port = 8888;

    // Parse all recognized arguments. Skip unrecognized.
    for (i=1; i<*argc && !parseFailed; /*nop*/) {

        // Window and default desktop/screen size
        if (NvGlDemoArgMatchInt(argc, argv, i, "-res",
                                "<xres> <yres>", 1, 9999,
                                2, demoOptions.windowSize)) {

            if (!demoOptions.desktopSize[0]) {
                demoOptions.desktopSize[0] = demoOptions.windowSize[0];
                demoOptions.desktopSize[1] = demoOptions.windowSize[1];
            }

            if (!demoOptions.displaySize[0]) {
                demoOptions.displaySize[0] = demoOptions.windowSize[0];
                demoOptions.displaySize[1] = demoOptions.windowSize[1];
            }
        }

        // Window size
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-windowsize",
                                     "<xres> <yres>", 1, 9999,
                                     2, demoOptions.windowSize)) {
            // No additional action needed
        }

        // Window offset
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-windowoffset",
                                     "<xpos> <ypos>", -9999, +9999,
                                     2, demoOptions.windowOffset)) {
            demoOptions.windowOffsetValid = 1;
        }

        // Desktop size
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-desktopsize",
                                     "<xres> <yres>", 1, 9999,
                                     2, demoOptions.desktopSize)) {
            // No additional action needed
        }

        // Display size
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-displaysize",
                                     "<xres> <yres>", 1, 9999,
                                     2, demoOptions.displaySize) ||
                 NvGlDemoArgMatchInt(argc, argv, i, "-screensize",
                                     "<xres> <yres>", 1, 9999,
                                     2, demoOptions.displaySize)) {
            // No additional action needed
        }

        // Use Current Display Mode
        else if(NvGlDemoArgMatch(argc, argv, 1, "-currentmode")) {
             demoOptions.useCurrentMode = 1;
        }

        // Display rate
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-refresh",
                                     "<rate>", 1, 240,
                                     1, &demoOptions.displayRate)) {
            // No additional action needed
        }

        // Display output number selection
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-dispno",
                                 "<connector index>", 0, 99,
                                     1, &demoOptions.displayNumber)) {
            // No additional action needed
        }

        // Display selection
        else if (NvGlDemoArgMatchStr(argc, argv, i, "-disp",
                                 "{vga|dvi|lvds|tftlcd|clilcd|dsi|hdmi|tv}[#]",
                                 NVGLDEMO_MAX_NAME,
                                 demoOptions.displayName)) {
            // No additional action needed
        }

        // Display layer
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-layer",
                                     "<depth>", 0, 99,
                                     1, &demoOptions.displayLayer)) {
            // No additional action needed
        }

        // Display layer blending mode
        else if (NvGlDemoArgMatchStr(argc, argv, i, "-blendmode",
                                 "{none|constant|perpixel}",
                                 sizeof(tmp), tmp)) {
            if (!STRCMP(tmp, "none")) {
                demoOptions.displayBlend = NvGlDemoDisplayBlend_None;
            } else if (!STRCMP(tmp, "constant")) {
                demoOptions.displayBlend = NvGlDemoDisplayBlend_ConstAlpha;
            } else if (!STRCMP(tmp, "perpixel")) {
                demoOptions.displayBlend = NvGlDemoDisplayBlend_PixelAlpha;
            } else if (!STRCMP(tmp, "colorkey")) {
                demoOptions.displayBlend = NvGlDemoDisplayBlend_ColorKey;
            } else {
                NvGlDemoLog("Unrecognized value (%s) for -blendmode.\n"
                            "  Use none, constant, perpixel, or colorkey.\n",
                            tmp);
                parseFailed = 1;
            }
        }

        // Display layer constant blending factor
        else if (NvGlDemoArgMatchFlt(argc, argv, i, "-alpha",
                                     "<alpha>", 0.0f, 1.0f,
                                     1, &demoOptions.displayAlpha)) {
            // No additional action needed
        }

        // Display layer colorkey range
        else if (NvGlDemoArgMatchFlt(argc, argv, i, "-colorkey",
                                     "<r0> <g0> <b0> <a0> <r1> <g1> <b1> <a1>",
                                     0.0f, 1.0f,
                                     8, &demoOptions.displayColorKey[0])) {
            // No additional action needed
        }

        // Multi/super sampling
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-msaa",
                                     "<samples>", 1, 64,
                                     1, &demoOptions.msaa)) {
            // No additional action needed
        }
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-fsaa",
                                     "<samples>", 1, 64,
                                     1, &demoOptions.msaa)) {
            // No additional action needed
        }

        // Coverage sampling
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-csaa",
                                     "<samples>", 1, 64,
                                     1, &demoOptions.csaa)) {
            // No additional action needed
        }
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-vcaa",
                                     "<samples>", 1, 64,
                                     1, &demoOptions.csaa)) {
            // No additional action needed
        }

        // n-buffered swapping
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-buffering",
                                     "<n>", 1, 4,
                                     1, &demoOptions.buffering)) {
            // No additional action needed
        }

        // Socket to get EGLStream from
        else if (NvGlDemoArgMatchStr(argc, argv, i, "-eglstreamsocket",
                                     "<socket name>",
                                     sizeof(demoOptions.eglstreamsock),
                                     demoOptions.eglstreamsock)) {
            // No additional action needed
        }

        // Program binary loading
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-useprogbin",
                                     "<boolean>", 0, 1,
                                     1, &demoOptions.useProgramBin)) {
            // No additional action needed
        }

#ifdef EGL_EXT_stream_consumer_qnxscreen_window
        // To enable the eglQnxScreenTest
        else if (NvGlDemoArgMatchStr(argc, argv, i, "-eglqnxscreentest",
                                     "<disable|enable>",
                                     sizeof(tmp), tmp)) {
            if (!STRCMP(tmp, "disable")) {
                demoOptions.eglQnxScreenTest = 0;
            } else if (!STRCMP(tmp, "enable")) {
                demoOptions.eglQnxScreenTest = 1;
            } else {
                NvGlDemoLog("Unrecognized value (%s) for -eglqnxscreentest.\n"
                            "  Use disable, or enable.\n",
                            tmp);
                parseFailed = 1;
            }
        }

        // QNX CAR2 Screen post window flags
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-postflags",
                                     "<int>", 0, 1,
                                     1, &demoOptions.postWindowFlags)) {
            // No additional action needed
        }

        // Rendering surface type
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-surfacetype",
                                     "<int>", 0, 1,
                                     1, &demoOptions.surfaceType)) {
            // No additional action needed
        }
#endif //EGL_EXT_stream_consumer_qnxscreen_window

        // FIFO mode for EGLStreams
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-fifo",
                                     "<length>", 0, 5,
                                     1, &demoOptions.nFifo)) {
            // No additional action needed
        }

        // Egl Stream Consumer latency
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-latency",
                                     "<usec>", 0, 2147483647,
                                     1, &demoOptions.latency)) {
            // No additional action needed
        }

        // Egl Stream Consumer acquire timeout
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-timeout",
                                     "<usec>", 0, 2147483647,
                                     1, &demoOptions.timeout)) {
            demoOptions.flags = demoOptions.flags | NVGL_DEMO_OPTION_TIMEOUT;
            // No additional action needed
        }

        // To set producer/consumer
        else if (NvGlDemoArgMatchStr(argc, argv, i, "-proctype",
                                 "{producer|consumer}[#]",
                                 NVGLDEMO_MAX_NAME,
                                 demoOptions.proctype)) {
            // No additional action needed
        }

        // Set IP. This is needed only on producer part.
        else if (NvGlDemoArgMatchStr(argc, argv, i, "-ip",
                                 "[#]",
                                 NVGLDEMO_MAX_NAME,
                                 demoOptions.ipAddr)) {
            // No additional action needed
        }

        //Set port number. This is only needed to run
        //multiple instances of producer and consumer
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-port",
                                     "<int>", 0, 65535,
                                     1, &demoOptions.port)) {
            // No additional action needed
        }

        // Max number of frames to run
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-frames",
                                     "<int>", 0, 99999,
                                     1, &demoOptions.frames)) {
            // No additional action needed
        }

        // To Set Mailbox mode renderahead
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-renderahead",
                                     "<length>", -1, 10,
                                     1, &demoOptions.renderahead)) {
            // No additional action needed
        }

        // Surface ID for weston ivi-shell
        else if (NvGlDemoArgMatchInt(argc, argv, i, "-surfaceid",
                                    "<n>", 0, 9999,
                                    1, &demoOptions.surface_id)) {
           // No additional action needed
        }

        // Argument not recognized, so skip it
        // @todo: why skip? isn't this bad?
        else {
            i++;
            //parseFailed = 1;
        }
    }

    if (demoOptions.nFifo != 0 && demoOptions.renderahead != -1) {
        NvGlDemoLog("  set -fifo 0 for -renderahead\n");
        parseFailed = 1;
    }
    parseComplete = 1;
    return !parseFailed;
}

// Query whether a failure occurred during parsing
int
NvGlDemoArgFailed(void)
{
    return parseFailed;
}
