/*
 * gears.c
 *
 * Copyright (c) 2003-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// This file illustrates a simple rendering program. It uses nvgldemo to
//   initialize the window and graphics context, and gearslib to perform
//   rendering. This is wrapped with a loop that handles window system
//   callbacks and updates the gear rotation angle every frame.
//

#include "nvgldemo.h"
#include "gearslib.h"

// The default number of seconds after which the test will end.
#define TIME_LIMIT 5.0

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
resizeCB(int width, int height)
{
    glViewport(0, 0, width, height);
    gearsResize(width, height);
}

// Callback to handle key presses
static void
keyCB(char key, int state)
{
    // Ignoring releases
    if (!state) return;

    if ((key == 'q') || (key == 'Q')) shutdown = GL_TRUE;
}

// Entry point of this demo program.
int main(int argc, char **argv)
{
    int         failure = 1;
    long long   startTime, currTime, endTime;
    double      runTime    = TIME_LIMIT;
    int         runforever = 0;
    int         frames     = 0;
    float       angle      = 0.0;
    float       color[4];
    int         i;

    // Initialize window system and EGL
    if (!NvGlDemoInitialize(&argc, argv, "gears", 2, 8, 0)) {
        goto done;
    }

    // Initialize the gears rendering
    if (!gearsInit(demoState.width, demoState.height)) {
        goto done;
    }

    // Set up callbacks
    NvGlDemoSetCloseCB(closeCB);
    NvGlDemoSetResizeCB(resizeCB);
    NvGlDemoSetKeyCB(keyCB);

    // Querying -clearColor arguments
    if (argc > 2) {
        for (i = 1; i < argc; i++) {
            if (NvGlDemoArgMatchFlt(&argc, argv, i, "-clearColor",
                        "<R G B A> (float)", 0.0f, 1.0f, 4, color)) {
                glClearColor(color[0], color[1], color[2], color[3]);
                break;
            }
        }
    }

    // Determine how long to run for
    if (argc == 2) {
        runTime = STRTOF(argv[1], NULL);
        if (!runTime) goto done;
    } else if (argc > 2) {
        goto done;
    }

    // Print runtime
    if (runTime < 0.0) {
        runforever = 1;
        NvGlDemoLog(" running forever...\n");
    } else {
        NvGlDemoLog(" running for %f seconds...\n", runTime);
    }

    // Allocate the resource if renderahead is specified
    if (!NvGlDemoThrottleInit()) {
        goto done;
    }

    // Get start time and compute end time
    startTime = endTime = currTime = SYSTIME();
    endTime += (long long)(1000000000.0 * runTime);

    // Main loop.
    do {
        // Draw a frame
        gearsRender(angle);

        // Add the fence object in queue and wait accordingly
        if (!NvGlDemoThrottleRendering()) {
            goto done;
        }

        // Swap a frame
        eglSwapBuffers(demoState.display, demoState.surface);

        // Process any window system events
        NvGlDemoCheckEvents();

        // Increment frame count, get time and update angle
        ++frames;
        currTime = SYSTIME();
        angle = (float)((120ull * (currTime-startTime) / 1000000000ull) % 360);

        // Check whether time limit has been exceeded
        if (!runforever && !shutdown) shutdown = (endTime <= currTime);
    } while (!shutdown);

    // Success
    failure = 0;

    done:

    // If any frames were generated, print the framerate
    if (frames) {
        NvGlDemoLog("Total FPS: %f\n",
                    (float)frames / ((currTime - startTime) / 1000000000ull));
    }

    // Otherwise something went wrong. Print usage message in case it
    //   was due to bad command line arguments.
    else {
        NvGlDemoLog("Usage: gears [options] [runTime]\n"
                    "  (negative runTime means \"forever\")\n" );
        NvGlDemoLog("\n  Clear color option:\n"
                "    [-clearColor <r> <g> <b> <a>] (background color)\n\n");
        NvGlDemoLog(NvGlDemoArgUsageString());
    }

    // Clean up gears rendering resources
    gearsTerm();

    // Deallocate the resources used by renderahead
    NvGlDemoThrottleShutdown();

    // Clean up EGL and window system
    NvGlDemoShutdown();

    return failure;
}
