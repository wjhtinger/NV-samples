/*
 * grutil.c
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
// This file illustrates how to set up the main rendering context and
// surface for a GL application.
//

#include "grutil.h"

#include <sys/stat.h>

// Global state
GrUtilState grUtilState = {
    (NativeDisplayType)0,  // nativeDisplay
    (NativeWindowType)0,   // nativeWindow
    EGL_NO_DISPLAY,        // display
    EGL_NO_SURFACE,        // surface
    (EGLConfig)0,          // config
    EGL_NO_CONTEXT,        // context
    0,                     // width
    0,                     // height
    NULL                   // platform
};

// Global parsed options structure
GrUtilOptions grUtilOptions;

// Start up, parsing GrUtil options and initializing window system and EGL
int
GrUtilInitialize(
    int* argc, char** argv, const char *appName,
    int glversion, int depthbits, int stencilbits)
{
    // Initialize options
    memset(&grUtilOptions, 0, sizeof(grUtilOptions));
    grUtilOptions.displayBlend = -1;
    grUtilOptions.displayAlpha = -1.0;
    grUtilOptions.displayColorKey[0] = -1.0;

    // Do the startup using the parsed options
    return GrUtilInitializeParsed(argc, argv, appName,
                                  glversion, depthbits, stencilbits);
}

int
GrUtilCreateEGLContext()
{
    int glversion = 2;
    EGLint ctxAttrs[2*MAX_ATTRIB+1], ctxAttrIndex=0;
    EGLBoolean eglStatus;

    ctxAttrs[ctxAttrIndex++] = EGL_CONTEXT_CLIENT_VERSION;
    ctxAttrs[ctxAttrIndex++] = glversion;
    ctxAttrs[ctxAttrIndex++] = EGL_NONE;
    // Create an EGL context
    grUtilState.context =
        eglCreateContext(grUtilState.display,
                         grUtilState.config,
                         NULL,
                         ctxAttrs);
    if (!grUtilState.context) {
        GrUtilLog("EGL couldn't create context.\n");
        return 0;
    }

    // Make the context and surface current for rendering
    eglStatus = eglMakeCurrent(grUtilState.display,
                               grUtilState.surface, grUtilState.surface,
                               grUtilState.context);
    if (!eglStatus) {
        GrUtilLog("EGL couldn't make context/surface current.\n");
        return 0;
    }
    return 1;
}

// Start up, initializing native window system and EGL after GrUtil
//   options have been parsed. (Still need argc/argv for window system
//   options.)
int
GrUtilInitializeParsed(
    int* argc, char** argv, const char *appName,
    int glversion, int depthbits, int stencilbits)
{
    EGLBoolean eglStatus;

    // Initialize display access
    if (!GrUtilDisplayInit()) return 0;

    // Obtain the EGL display
    if (grUtilState.display == EGL_NO_DISPLAY) {
        GrUtilLog("EGL failed to obtain display.\n");
        goto fail;
    }

    // Initialize EGL
    eglStatus = eglInitialize(grUtilState.display, 0, 0);
    if (!eglStatus) {
        GrUtilLog("EGL failed to initialize.\n");
        goto fail;
    }

    // Create the window
    if (!GrUtilWindowInit(argc, argv, appName)) goto fail;

    // Create output surface
    if (!GrUtilCreatOutputSurface()) goto fail;

    // Create EGL Context
    if (!GrUtilCreateEGLContext()) goto fail;

    // Query the EGL surface width and height
    eglStatus =  eglQuerySurface(grUtilState.display, grUtilState.surface,
                                     EGL_WIDTH,  &grUtilState.width)
                  && eglQuerySurface(grUtilState.display, grUtilState.surface,
                                     EGL_HEIGHT, &grUtilState.height);
    if (!eglStatus) {
        GrUtilLog("EGL couldn't get window width/height.\n");
        goto fail;
    }
    return 1;

    // On failure, clean up partial initialization
fail:
    GrUtilShutdown();
    return 0;
}

// Shut down, freeing all EGL and native window system resources.
void
GrUtilShutdown(void)
{
    EGLBoolean eglStatus;

    // Clear rendering context
    // Note that we need to bind the API to unbind... yick
    if (grUtilState.display != EGL_NO_DISPLAY) {
        eglBindAPI(EGL_OPENGL_ES_API);
        eglStatus = eglMakeCurrent(grUtilState.display,
                                   EGL_NO_SURFACE, EGL_NO_SURFACE,
                                   EGL_NO_CONTEXT);
        if (!eglStatus)
            GrUtilLog("Error clearing current surfaces/context.\n");
    }

    // Destroy the EGL context
    if (grUtilState.context != EGL_NO_CONTEXT) {
        eglStatus = eglDestroyContext(grUtilState.display, grUtilState.context);
        if (!eglStatus)
            GrUtilLog("Error destroying EGL context.\n");
        grUtilState.context = EGL_NO_CONTEXT;
    }

    // Destroy the EGL surface
    if (grUtilState.surface != EGL_NO_SURFACE) {
        eglStatus = eglDestroySurface(grUtilState.display, grUtilState.surface);
        if (!eglStatus)
            GrUtilLog("Error destroying EGL surface.\n");
        grUtilState.surface = EGL_NO_SURFACE;
    }

    // Close the window
    GrUtilWindowTerm();

    // Terminate EGL
    if (grUtilState.display != EGL_NO_DISPLAY) {
        eglStatus = eglTerminate(grUtilState.display);
        if (!eglStatus)
            GrUtilLog("Error terminating EGL.\n");
        grUtilState.display = EGL_NO_DISPLAY;
    }

    // Release EGL thread
    eglStatus = eglReleaseThread();
    if (!eglStatus)
        GrUtilLog("Error releasing EGL thread.\n");

    // Terminate display access
    GrUtilDisplayTerm();
}

#ifdef EGL_NV_system_time
// Gets the system time in nanoseconds
long long
GrUtilSysTime(void)
{
    static PFNEGLGETSYSTEMTIMENVPROC eglGetSystemTimeNV = NULL;
    static int inited = 0;
    static long long nano = 1;
    if(!inited)
    {
        PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC eglGetSystemTimeFrequencyNV =
            (PFNEGLGETSYSTEMTIMEFREQUENCYNVPROC)eglGetProcAddress("eglGetSystemTimeFrequencyNV");
        eglGetSystemTimeNV = (PFNEGLGETSYSTEMTIMENVPROC)eglGetProcAddress("eglGetSystemTimeNV");

        assert(eglGetSystemTimeFrequencyNV && eglGetSystemTimeNV);

        // Compute factor for converting eglGetSystemTimeNV() to nanoseconds
        nano = 1000000000/eglGetSystemTimeFrequencyNV();
        inited = 1;
    }

    return nano*eglGetSystemTimeNV();
}
#endif // EGL_NV_system_time

// Initialize a 4x4 matrix to identity
//   m <- I
void
GrUtilMatrixIdentity(
    float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[4 * 0 + 0] = m[4 * 1 + 1] = m[4 * 2 + 2] = m[4 * 3 + 3] = 1.0;
}

// Multiply the second 4x4 matrix into the first
//   m0 <- m0 * m1
void
GrUtilMatrixMultiply(
    float m0[16], float m1[16])
{
    int r, c, i;
    for (r = 0; r < 4; r++) {
        float m[4] = {0.0, 0.0, 0.0, 0.0};
        for (c = 0; c < 4; c++) {
            for (i = 0; i < 4; i++) {
                m[c] += m0[4 * i + r] * m1[4 * c + i];
            }
        }
        for (c = 0; c < 4; c++) {
            m0[4 * c + r] = m[c];
        }
    }
}

// Apply orthographic projection to a 4x4 matrix
//   m <- m * ortho(l,r,b,t,n,f)
void
GrUtilMatrixOrtho(
    float m[16],
    float l, float r, float b, float t, float n, float f)
{
    float m1[16];
    float rightMinusLeftInv, topMinusBottomInv, farMinusNearInv;

    rightMinusLeftInv = 1.0f / (r - l);
    topMinusBottomInv = 1.0f / (t - b);
    farMinusNearInv = 1.0f / (f - n);

    m1[ 0] = 2.0f * rightMinusLeftInv;
    m1[ 1] = 0.0f;
    m1[ 2] = 0.0f;
    m1[ 3] = 0.0f;

    m1[ 4] = 0.0f;
    m1[ 5] = 2.0f * topMinusBottomInv;
    m1[ 6] = 0.0f;
    m1[ 7] = 0.0f;

    m1[ 8] = 0.0f;
    m1[ 9] = 0.0f;
    m1[10] = -2.0f * farMinusNearInv;
    m1[11] = 0.0f;

    m1[12] = -(r + l) * rightMinusLeftInv;
    m1[13] = -(t + b) * topMinusBottomInv;
    m1[14] = -(f + n) * farMinusNearInv;
    m1[15] = 1.0f;

    GrUtilMatrixMultiply(m, m1);
}

// Function to print logs when shader compilation fails
static void
GrUtilShaderDebug(
    GLuint obj, GLenum status, const char* op)
{
    int success;
    int len;
    char *str = NULL;
    if (status == GL_COMPILE_STATUS) {
        glGetShaderiv(obj, GL_INFO_LOG_LENGTH, &len);
        if (len > 0) {
            str = (char*)malloc(len * sizeof(char));
            glGetShaderInfoLog(obj, len, NULL, str);
        }
    } else { // LINK or VALIDATE
        glGetProgramiv(obj, GL_INFO_LOG_LENGTH, &len);
        if (len > 0) {
            str = (char*)malloc(len * sizeof(char));
            glGetProgramInfoLog(obj, len, NULL, str);
        }
    }
    if (str != NULL && *str != '\0') {
        GrUtilLog("--- %s log ---\n", op);
        GrUtilLog("%s", str);
    }
    if (str) { free(str); }

    // check the compile / link status.
    if (status == GL_COMPILE_STATUS) {
        glGetShaderiv(obj, status, &success);
        if (!success) {
            glGetShaderiv(obj, GL_SHADER_SOURCE_LENGTH, &len);
            if (len > 0) {
                str = (char*)malloc(len * sizeof(char));
                glGetShaderSource(obj, len, NULL, str);
                if (str != NULL && *str != '\0') {
                    GrUtilLog("--- %s code ---\n", op);
                    GrUtilLog("%s", str);
                }
                free(str);
            }
        }
    } else { // LINK or VALIDATE
        glGetProgramiv(obj, status, &success);
    }

    if (!success)
    {
        GrUtilLog("--- %s failed ---\n", op);
        exit(-1);
    }
}


// Takes shader source strings, compiles them, and builds a shader program
unsigned int
GrUtilLoadShaderSrcStrings(
    const char* vertSrc, int vertSrcSize,
    const char* fragSrc, int fragSrcSize,
    unsigned char link,
    unsigned char debugging)
{
    GLuint prog = 0;
    GLuint vertShader;
    GLuint fragShader;

    // Create the program
    prog = glCreateProgram();

    // Create the GL shader objects
    vertShader = glCreateShader(GL_VERTEX_SHADER);
    fragShader = glCreateShader(GL_FRAGMENT_SHADER);

    // Load shader sources into GL and compile
    glShaderSource(vertShader, 1, (const char**)&vertSrc, &vertSrcSize);
    glCompileShader(vertShader);
    if (debugging)
        GrUtilShaderDebug(vertShader, GL_COMPILE_STATUS, "Vert Compile");
    glShaderSource(fragShader, 1, (const char**)&fragSrc, &fragSrcSize);
    glCompileShader(fragShader);
    if (debugging)
        GrUtilShaderDebug(fragShader, GL_COMPILE_STATUS, "Frag Compile");

    // Attach the shaders to the program
    glAttachShader(prog, vertShader);
    glAttachShader(prog, fragShader);

    // Delete the shaders
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    // Link (if requested) and validate the shader program
    if (link) {
        glLinkProgram(prog);
        if (debugging)
            GrUtilShaderDebug(prog, GL_LINK_STATUS, "Program Link");
        glValidateProgram(prog);
        if (debugging)
            GrUtilShaderDebug(prog, GL_VALIDATE_STATUS, "Program Validate");
    }

    return prog;
}
