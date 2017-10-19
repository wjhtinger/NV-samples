/*
 * main.c
 *
 * Copyright (c) 2003-2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// Main loop and event handling
//

#include "nvgldemo.h"
#include "screen.h"

// Flag indicating it is time to shut down
static GLboolean shutdown = GL_FALSE;

// Callback to close window
static void
closeCB(void)
{
    shutdown = GL_TRUE;
}

// Callback to resize window
static void
resizeCB(
    int width,
    int height)
{
    Screen_resize(width, height);
}

// Callback to handle key presses
static void
keyCB(
    char key,
    int  state)
{
    // Ignoring releases
    if (!state) return;

    Screen_callback(key, 0, 0);
}

// Entry point of this demo program.
int main(int argc, char **argv)
{
    int         failure = 1;
    GLboolean   fpsFlag  = GL_FALSE;
    GLboolean   demoMode = GL_FALSE;
    float       duration = 0.0f;
    GLboolean   startup  = GL_FALSE;

    // Initialize window system and EGL
    if (!NvGlDemoInitialize(&argc, argv, "ctree", 2, 8, 0)) {
        goto done;
    }

    // Parse non-generic command line options
    while (argc > 1) {

        // Duration
        if (NvGlDemoArgMatchFlt(&argc, argv, 1, "-sec",
                                "<seconds>", 0.0f, 31536000.0f,
                                1, &duration)) {
            // No additional action needed
        }

        // Demo mode
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-demo")) {
            demoMode = GL_TRUE;
        }

        // Use small textures
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-smalltex")) {
            Screen_setSmallTex();
        }

        // Disable menu
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-nomenu")) {
            Screen_setNoMenu();
        }

        // Disable sky rendering
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-nosky")) {
            Screen_setNoSky();
        }

        // FPS output
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-fps")) {
            fpsFlag = GL_TRUE;
        }

        // Unknown or failure
        else {
            if (!NvGlDemoArgFailed())
                NvGlDemoLog("Unknown command line option (%s)\n", argv[1]);
            goto done;
        }
    }

    // Parsing succeeded
    startup = GL_TRUE;

    // Set up callbacks
    NvGlDemoSetCloseCB(closeCB);
    NvGlDemoSetResizeCB(resizeCB);
    NvGlDemoSetKeyCB(keyCB);

    // Initialize the rendering
    if (!Screen_initialize(duration, demoState.width, demoState.height, fpsFlag)) {
        Screen_deinitialize();
        goto done;
    }

    if (demoMode) { Screen_setDemoParams(); }

    // Allocate the resource if renderahead is specified
    if (!NvGlDemoThrottleInit()) {
        goto done;
    }

    // Render until time limit is reached or application is terminated
    do {
        // Render the next frame
        Screen_draw();

        // Add the fence object in queue and wait accordingly
        if (!NvGlDemoThrottleRendering()) {
            goto done;
        }

        // Swap the next frame
        eglSwapBuffers(demoState.display, demoState.surface);

        // Process any window system events
        NvGlDemoCheckEvents();
    } while (!Screen_isFinished() && !shutdown);

    // Success
    failure = 0;

    done:

    // Clean up the rendering resources
    Screen_deinitialize();

    // If basic startup failed, print usage message in case it was due
    //   to bad command line arguments.
    if (!startup) {
        NvGlDemoLog("Usage: ctree [options]\n"
                    "  Put into demo mode:\n"
                    "    [-demo]\n"
                    "  Duration to run for (forever if not specified):\n"
                    "    [-sec <seconds>]\n"
                    "  Use low resolution textures:\n"
                    "    [-smalltex]\n"
                    "  Disable the menu interface:\n"
                    "    [-nomenu]\n"
                    "  Disable rendering of the sky:\n"
                    "    [-nosky]\n"
                    "  Turn on framerate logging:\n"
                    "    [-fps]\n");
        NvGlDemoLog(NvGlDemoArgUsageString());
    }

    // Deallocate the resources used by renderahead
    NvGlDemoThrottleShutdown();

    // Clean up EGL and window system
    NvGlDemoShutdown();

    return failure;
}
