/*
 * grutil_x11.c
 *
 * Copyright (c) 2013-2015, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// This file illustrates how to access the display and create a window for
//   a GL application using X11.
//

#include "grutil.h"
#include "grutil_x11.h"
#include <X11/Xos.h>

//
// Window system startup/shutdown
//

// Initialize access to the display system
int
GrUtilDisplayInit(void)
{
    // Allocate a structure for the platform-specific state
    grUtilState.platform =
        (GrUtilPlatformState*)malloc(sizeof(GrUtilPlatformState));
    if (!grUtilState.platform) {
        GrUtilLog("Could not allocate platform specific storage memory.\n");
        goto fail;
    }
    grUtilState.platform->XDisplay = NULL;
    grUtilState.platform->XScreen  = 0;
    grUtilState.platform->XWindow  = (Window)0;

    // Open X display
    grUtilState.platform->XDisplay = XOpenDisplay(NULL);
    if (grUtilState.platform->XDisplay == NULL) {
        GrUtilLog("X failed to open display.\n");
        goto fail;
    }
    grUtilState.platform->XScreen =
        DefaultScreen(grUtilState.platform->XDisplay);
    grUtilState.nativeDisplay =
        (EGLNativeDisplayType)grUtilState.platform->XDisplay;

    // If display option is specified, but isn't supported, then exit.

    if (grUtilOptions.displayName[0]) {
        GrUtilLog("Setting display output is not supported. Exiting.\n");
        goto fail;
    }

    if (grUtilOptions.displayRate) {
        GrUtilLog("Setting display refresh is not supported. Exiting.\n");
        goto fail;
    }

    if ((grUtilOptions.displayBlend >= GrUtilDisplayBlend_None) ||
        (grUtilOptions.displayAlpha >= 0.0) ||
        (grUtilOptions.displayColorKey[0] >= 0.0) ||
        (grUtilOptions.displayLayer > 0)) {
        GrUtilLog("Display layers are not supported. Exiting.\n");
        goto fail;
    }

    grUtilState.display = eglGetDisplay(grUtilState.nativeDisplay);
    return 1;

    fail:
    free(grUtilState.platform);
    grUtilState.platform = NULL;

    return 0;
}

// Terminate access to the display system
void
GrUtilDisplayTerm(void)
{
    if (grUtilState.platform) {
        if (grUtilState.platform->XDisplay) {
            XCloseDisplay(grUtilState.platform->XDisplay);
            grUtilState.platform->XDisplay = NULL;
        }

        free(grUtilState.platform);
        grUtilState.platform = NULL;
    }
}

// Create the window
int GrUtilWindowInit(int *argc, char **argv, const char *appName)
{
    XSizeHints size_hints;
    Atom       wm_destroy_window;

// If not specified, window size defaults to display resolution
    if (!grUtilOptions.windowSize[0])
        grUtilOptions.windowSize[0] = DisplayWidth(grUtilState.platform->XDisplay,
                                                   grUtilState.platform->XScreen);
    if (!grUtilOptions.windowSize[1])
        grUtilOptions.windowSize[1] = DisplayHeight(grUtilState.platform->XDisplay,
                                                    grUtilState.platform->XScreen);

    // Create a native window
    grUtilState.platform->XWindow =
        XCreateSimpleWindow(grUtilState.platform->XDisplay,
                            RootWindow(grUtilState.platform->XDisplay,
                                       grUtilState.platform->XScreen),
                            grUtilOptions.windowOffset[0],
                            grUtilOptions.windowOffset[1],
                            grUtilOptions.windowSize[0],
                            grUtilOptions.windowSize[1],
                            0,
                            BlackPixel(grUtilState.platform->XDisplay,
                                       grUtilState.platform->XScreen),
                            WhitePixel(grUtilState.platform->XDisplay,
                                       grUtilState.platform->XScreen));
    if (!grUtilState.platform->XWindow) {
        GrUtilLog("Error creating native window.\n");
        goto fail;
    }
    grUtilState.nativeWindow = (NativeWindowType)grUtilState.platform->XWindow;

    // Set up window properties
    size_hints.flags = PPosition | PSize | PMinSize;
    size_hints.x = grUtilOptions.windowOffset[0];
    size_hints.y = grUtilOptions.windowOffset[1];
    size_hints.min_width  = (grUtilOptions.windowSize[0] < 320)
                          ?  grUtilOptions.windowSize[0] : 320;
    size_hints.min_height = (grUtilOptions.windowSize[1] < 240)
                          ?  grUtilOptions.windowSize[1] : 240;
    size_hints.width  = grUtilOptions.windowSize[0];
    size_hints.height = grUtilOptions.windowSize[1];

    XSetStandardProperties(grUtilState.platform->XDisplay,
                           grUtilState.platform->XWindow,
                           appName, appName, None,
                           argv, *argc, &size_hints);

    XSetWindowBackgroundPixmap(grUtilState.platform->XDisplay,
                               grUtilState.platform->XWindow,
                               None);

    wm_destroy_window = XInternAtom(grUtilState.platform->XDisplay,
                                    "WM_DELETE_WINDOW", False);
    XSetWMProtocols(grUtilState.platform->XDisplay,
                    grUtilState.platform->XWindow,
                    &wm_destroy_window,
                    True);

    // Make sure the MapNotify event goes into the queue
    XSelectInput(grUtilState.platform->XDisplay,
                 grUtilState.platform->XWindow,
                 StructureNotifyMask);

    XMapWindow(grUtilState.platform->XDisplay,
               grUtilState.platform->XWindow);

    return 1;

    fail:
    GrUtilWindowTerm();
    return 0;
}

// Close the window
void
GrUtilWindowTerm(void)
{
    // Close the native window
    if (grUtilState.platform && grUtilState.platform->XWindow) {
        if (grUtilState.platform->XDisplay) {
        XDestroyWindow(grUtilState.platform->XDisplay,
                       grUtilState.platform->XWindow);
        }

        grUtilState.platform->XWindow = (Window)0;
        grUtilState.nativeWindow = (NativeWindowType)0;
    }
}

int GrUtilCreatOutputSurface(void)
{
    int depthbits = 8, stencilbits=0;
    int glversion = 2;
    EGLint cfgAttrs[2*MAX_ATTRIB+1], cfgAttrIndex=0;
    EGLint srfAttrs[2*MAX_ATTRIB+1], srfAttrIndex=0;
    const char* extensions;
    EGLConfig* configList = NULL;
    EGLint     configCount;
    EGLBoolean eglStatus;

    // Query EGL extensions
    extensions = eglQueryString(grUtilState.display, EGL_EXTENSIONS);

    // Bind GL API
    eglBindAPI(EGL_OPENGL_ES_API);

    // Request GL version
    cfgAttrs[cfgAttrIndex++] = EGL_RENDERABLE_TYPE;
    cfgAttrs[cfgAttrIndex++] = (glversion == 2) ? EGL_OPENGL_ES2_BIT
                                                    : EGL_OPENGL_ES_BIT;

    // Request a minimum of 1 bit each for red, green, and blue
    // Setting these to anything other than DONT_CARE causes the returned
    //   configs to be sorted with the largest bit counts first.
    cfgAttrs[cfgAttrIndex++] = EGL_RED_SIZE;
    cfgAttrs[cfgAttrIndex++] = 1;
    cfgAttrs[cfgAttrIndex++] = EGL_GREEN_SIZE;
    cfgAttrs[cfgAttrIndex++] = 1;
    cfgAttrs[cfgAttrIndex++] = EGL_BLUE_SIZE;
    cfgAttrs[cfgAttrIndex++] = 1;

    // If application requires depth or stencil, request them
    if (depthbits) {
        cfgAttrs[cfgAttrIndex++] = EGL_DEPTH_SIZE;
        cfgAttrs[cfgAttrIndex++] = depthbits;
    }
    if (stencilbits) {
        cfgAttrs[cfgAttrIndex++] = EGL_STENCIL_SIZE;
        cfgAttrs[cfgAttrIndex++] = stencilbits;
    }
    // Request antialiasing
    cfgAttrs[cfgAttrIndex++] = EGL_SAMPLES;
    cfgAttrs[cfgAttrIndex++] = grUtilOptions.msaa;
#ifdef EGL_NV_coverage_sample
    if (strstr(extensions, "EGL_NV_coverage_sample")) {
        cfgAttrs[cfgAttrIndex++] = EGL_COVERAGE_SAMPLES_NV;
        cfgAttrs[cfgAttrIndex++] = grUtilOptions.csaa;
        cfgAttrs[cfgAttrIndex++] = EGL_COVERAGE_BUFFERS_NV;
        cfgAttrs[cfgAttrIndex++] = grUtilOptions.csaa ? 1 : 0;
    } else
#endif // EGL_NV_coverage_sample
    if (grUtilOptions.csaa) {
        GrUtilLog("Coverage sampling not supported.\n");
        goto fail;
    }

    // Request buffering
    if (grUtilOptions.buffering) {

        srfAttrs[srfAttrIndex++] = EGL_RENDER_BUFFER;

        switch (grUtilOptions.buffering) {
            case 1:
                srfAttrs[srfAttrIndex++] = EGL_SINGLE_BUFFER;
                break;

            case 2:
                srfAttrs[srfAttrIndex++] = EGL_BACK_BUFFER;
                break;

            #ifdef EGL_TRIPLE_BUFFER_NV
            case 3:
                if(!strstr(extensions, "EGL_NV_triple_buffer")) {
                    GrUtilLog("TRIPLE_BUFFER not supported.\n");
                    goto fail;
                }
                srfAttrs[srfAttrIndex++] = EGL_TRIPLE_BUFFER_NV;
                break;
            #endif

            #ifdef EGL_QUADRUPLE_BUFFER_NV
            case 4:
                if(!strstr(extensions, "EGL_NV_quadruple_buffer")) {
                    GrUtilLog("QUADRUPLE_BUFFER not supported.\n");
                    goto fail;
                }
                srfAttrs[srfAttrIndex++] = EGL_QUADRUPLE_BUFFER_NV;
                break;
            #endif

            default:
                GrUtilLog("Buffering (%d) not supported.\n",
                            grUtilOptions.buffering);
                goto fail;
        }
    }


    // Terminate attribute lists
    cfgAttrs[cfgAttrIndex++] = EGL_NONE;
    srfAttrs[srfAttrIndex++] = EGL_NONE;

    // Find out how many configurations suit our needs
    eglStatus = eglChooseConfig(grUtilState.display, cfgAttrs,
                                NULL, 0, &configCount);
    if (!eglStatus || !configCount) {
        GrUtilLog("EGL failed to return any matching configurations.\n");
        goto fail;
    }

    // Allocate room for the list of matching configurations
    configList = (EGLConfig*)malloc(configCount * sizeof(EGLConfig));
    if (!configList) {
        GrUtilLog("Allocation failure obtaining configuration list.\n");
        goto fail;
    }

    // Obtain the configuration list from EGL
    eglStatus = eglChooseConfig(grUtilState.display, cfgAttrs,
                                configList, configCount, &configCount);
    if (!eglStatus || !configCount) {
        GrUtilLog("EGL failed to populate configuration list.\n");
        goto fail;
    }

    // Select an EGL configuration that matches the native window
    // Currently we just choose the first one, but we could search
    //   the list based on other criteria.
    grUtilState.config = configList[0];
    free(configList);
    configList = 0;

    // Create EGL surface
    grUtilState.surface =
            eglCreateWindowSurface(grUtilState.display,
                               grUtilState.config,
                               grUtilState.nativeWindow,
                               srfAttrs);
    if (grUtilState.surface == EGL_NO_SURFACE) {
        GrUtilLog("EGL couldn't create window surface.\n");
        goto fail;
    }

    return 1;
fail:
    if (configList) free(configList);
    return 0;
}

//
// Callback handling
//
static GrUtilCloseCB   closeCB   = NULL;
static GrUtilResizeCB  resizeCB  = NULL;
static GrUtilKeyCB     keyCB     = NULL;
static GrUtilPointerCB pointerCB = NULL;
static GrUtilButtonCB  buttonCB  = NULL;
static long              eventMask = 0;

static void GrUtilUpdateEventMask(void)
{
    eventMask = StructureNotifyMask;
    if (keyCB) eventMask |= (KeyPressMask | KeyReleaseMask);
    if (pointerCB) eventMask |= (PointerMotionMask | PointerMotionHintMask);
    if (buttonCB) eventMask |= (ButtonPressMask | ButtonReleaseMask);
    XSelectInput(grUtilState.platform->XDisplay,
                 grUtilState.platform->XWindow,
                 eventMask);
}

void GrUtilSetCloseCB(GrUtilCloseCB cb)
{
    closeCB  = cb;
    GrUtilUpdateEventMask();
}

void GrUtilSetResizeCB(GrUtilResizeCB cb)
{
    resizeCB = cb;
    GrUtilUpdateEventMask();
}

void GrUtilSetKeyCB(GrUtilKeyCB cb)
{
    keyCB = cb;
    GrUtilUpdateEventMask();
}

void GrUtilSetPointerCB(GrUtilPointerCB cb)
{
    pointerCB = cb;
    GrUtilUpdateEventMask();
}

void GrUtilSetButtonCB(GrUtilButtonCB cb)
{
    buttonCB = cb;
    GrUtilUpdateEventMask();
}

void GrUtilCheckEvents(void)
{
    XEvent event;
    KeySym key;
    char   str[2] = {0,0};
    Window root, child;
    int    rootX, rootY;
    int    winX, winY;
    unsigned int mask;

    while (XPending(grUtilState.platform->XDisplay)) {
        XNextEvent(grUtilState.platform->XDisplay, &event);
        switch (event.type) {

            case ConfigureNotify:
                if (!resizeCB) break;
                if (  (event.xconfigure.width  != grUtilState.width)
                   || (event.xconfigure.height != grUtilState.height) ) {
                    resizeCB(event.xconfigure.width, event.xconfigure.height);
                    grUtilState.width = event.xconfigure.width;
                    grUtilState.height = event.xconfigure.height;
                }
                break;

            case ClientMessage:
                if (!closeCB) break;
                if (event.xclient.message_type ==
                        XInternAtom(grUtilState.platform->XDisplay,
                                    "WM_PROTOCOLS", True) &&
                    (Atom)event.xclient.data.l[0] ==
                        XInternAtom(grUtilState.platform->XDisplay,
                                    "WM_DELETE_WINDOW", True))
                    closeCB();
                break;

            case KeyPress:
            case KeyRelease:
                if (!keyCB) break;
                XLookupString(&event.xkey, str, 2, &key, NULL);
                if (!str[0] || str[1]) break; // Only care about basic keys
                keyCB(str[0], (event.type == KeyPress) ? 1 : 0);
                break;

            case MotionNotify:
                if (!pointerCB) break;
                XQueryPointer(grUtilState.platform->XDisplay,
                              event.xmotion.window,
                              &root, &child,
                              &rootX, &rootY, &winX, &winY, &mask);
                pointerCB(winX, winY);
                break;

            case ButtonPress:
            case ButtonRelease:
                if (!buttonCB) break;
                buttonCB(event.xbutton.button,
                         (event.type == ButtonPress) ? 1 : 0);
                break;

            case MapNotify:
                // Once the window is mapped, can set the focus
                XSetInputFocus(grUtilState.platform->XDisplay,
                               event.xmap.window,
                               RevertToNone,
                               CurrentTime);
                break;

           default:
               break;
        }
    }
}
