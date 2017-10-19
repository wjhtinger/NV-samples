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
// Main bubble demo function and event callbacks
//

#include "nvgldemo.h"
#include "bubble.h"

// Bubble state info
static BubbleState bubbleState;

// Flag indicating it is time to shut down
static GLboolean   shutdown = GL_FALSE;

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
    BubbleState_reshapeViewport(&bubbleState, width, height);
}

// Callback to handle key presses
static void
keyCB(char key, int state)
{
    // Ignoring releases
    if (!state) return;

    BubbleState_callback(&bubbleState, key);
}

#ifndef USE_FAKE_MOUSE
// Callback to move mouse
static void
pointerCB(int x, int y)
{
    BubbleState_mouse(&bubbleState, x, y);
}

// Callback to handle button presses
static void
buttonCB(int button, int state)
{
    if (button != 1) return; // Only use left mouse
    BubbleState_leftMouse(&bubbleState, (bool)state);
}
#endif // USE_FAKE_MOUSE

// Main entry point
int main(int argc, char **argv)
{
    int         failure = 1;
    float       duration = 0.0f;
    int         autopoke = 0;
    int         fpsflag  = 0;
    int         startup  = 0;
    bool        fframeIncrement   = false;
    float       delta             = -1.0f;

    // Initialize window system and EGL
    if (!NvGlDemoInitialize(&argc, argv, "bubble", 2, 8, 0)) {
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

        // Auto-poking
        else if (NvGlDemoArgMatchInt(&argc, argv, 1, "-autopoke",
                                     "<frames>", 1, 65535,
                                     1, &autopoke)) {
            // No additional action needed
        }

        // FPS output
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-fps")) {
            fpsflag = 1;
        }

        // Fixed frame
        else if (NvGlDemoArgMatch(&argc, argv, 1, "-ff")) {
            fframeIncrement = true;
        }

        // Delta used in Fixed-frame
        else if (NvGlDemoArgMatchFlt(&argc, argv, 1, "-delta",
                                     "<value>", 0.0f, 31536000.0f,
                                     1, &delta)) {
            // No additional action needed
        }

        // Unknown or failure
        else {
            if (!NvGlDemoArgFailed())
                NvGlDemoLog("Unknown command line option (%s)\n", argv[1]);
            goto done;
        }
    }

    // Parsing succeeded
    startup = 1;

    // Set up callbacks
    NvGlDemoSetCloseCB(closeCB);
    NvGlDemoSetResizeCB(resizeCB);
    NvGlDemoSetKeyCB(keyCB);
#ifndef USE_FAKE_MOUSE
    NvGlDemoSetPointerCB(pointerCB);
    NvGlDemoSetButtonCB(buttonCB);
#endif

    // Initialize bubble state
    if (!BubbleState_init(&bubbleState,
                      autopoke, duration, fpsflag, fframeIncrement, delta,
                      demoState.width, demoState.height)) {
        goto done;
    }

    // Allocate the resource if renderahead is specified
    if (!NvGlDemoThrottleInit()) {
        goto done;
    }

    // Render until time limit is reached or application is terminated
    do {
        // Render the next frame
        BubbleState_tick(&bubbleState);

        // Add the fence object in queue and wait accordingly
        if (!NvGlDemoThrottleRendering()) {
            goto done;
        }

        // Swap the next frame
        eglSwapBuffers(demoState.display, demoState.surface);

        // Process any window system events
        NvGlDemoCheckEvents();

    } while (!bubbleState.drawFinal && !shutdown);

    // If time limit reached, indicate that's why we're exiting
    if ((duration != 0.0f) && bubbleState.drawFinal) {
        NvGlDemoLog("Exiting after %d seconds\n", (int)duration);

    }

    // Success
    failure = 0;

    done:

    // Clean up the bubble resources
    BubbleState_term(&bubbleState);

    // If basic startup failed, print usage message in case it was due
    //   to bad command line arguments.
    if (!startup) {
        NvGlDemoLog("Usage: bubble [options]"
                    "  Duration to run for (forever if not specified):\n"
                    "    [-sec <seconds>]\n"
                    "  Frequency with which to automatically poke bubble:\n"
                    "    [-autopoke <frames>]\n"
                    "  Turn on framerate logging:\n"
                    "    [-fps]\n"
                    "  To run in Fixed-Frame Increment mode"
                    " (Fixed increment of frames):\n"
                    "    [-ff]\n"
                    "  Delta used in Fixed-Frame Increment which defines"
                    " amount of change in scene:\n"
                    "    [-delta <value>]\n");
        NvGlDemoLog(NvGlDemoArgUsageString());
    }

    // Deallocate the resources used by renderahead
    NvGlDemoThrottleShutdown();

    // Clean up EGL and window system
    NvGlDemoShutdown();

    return failure;
}
