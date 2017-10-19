/*
 * common.c
 *
 * Copyright (c) 2013-2016, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

// Description: creation/initialization/usage of EGL stream & display

#include <common.h>
#include "fdshare.h"

#define CHECK_GL_ERROR() {\
    GLenum error = glGetError();\
    if(error != GL_NO_ERROR) {\
        printf("ogl error %s(%d): %d\n", __FILE__, __LINE__, error);\
        exit(-1);\
    } }

static const char vshader[] = {
    "attribute vec4 Vertex;\n"

    "uniform vec4 TransRot;\n"
    "uniform mat4 ModelViewProjectionMatrix;\n"

    "varying vec2 TexCoord;\n"
    "varying float Opacity;\n"

    "void main (void)\n"
    "{\n"
    "    vec4 transformedVert = Vertex;\n"
    "    float rotation = radians(TransRot.w);\n"
    "    mat4 rotMat = mat4(1.0);\n"
    "    mat4 transMat = mat4(1.0);\n"

    "    // Construct Rotation Matrix\n"
    "    rotMat[1][1] = cos(rotation);\n"
    "    rotMat[1][2] = -sin(rotation);\n"
    "    rotMat[2][1] = sin(rotation);\n"
    "    rotMat[2][2] = cos(rotation);\n"

    "    // Construct Translation Matrix\n"
    "    transMat[3][0] = TransRot.x;\n"
    "    transMat[3][1] = TransRot.y * 1.0;\n"

    "    //All our quads are screen aligned and layered (z = TransRot.z)\n"
    "    transformedVert.z = TransRot.z;\n"
    "    transformedVert.w = 1.0;\n"

    "    //Get our texture coordinates\n"
    "    TexCoord.s = Vertex.z;\n"
    "    TexCoord.t = Vertex.w;\n"

    "    //Pass Through the Opacity\n"
    "    Opacity = 100.0;\n"

    "    gl_Position = ModelViewProjectionMatrix * transMat * rotMat * transformedVert;\n"
    "}\n"
};
static const char fshader[] ={
    "#extension GL_NV_EGL_stream_consumer_external: enable\n"
    "#extension GL_OES_EGL_image_external : enable\n"

    "uniform samplerExternalOES Texture0;\n"

    "varying lowp vec2 TexCoord;\n"
    "varying lowp float Opacity;\n"

    "void main (void)\n"
    "{\n"
    "    gl_FragColor = texture2D(Texture0, TexCoord);\n"
    "    gl_FragColor.a *= Opacity;\n"
    "}\n"
};

/* -----  Extension pointers  ---------*/
#if defined(EGL_KHR_stream) && defined(EGL_KHR_stream_consumer_gltexture)
#define EXTENSION_LIST(T) \
    T( PFNEGLCREATESTREAMKHRPROC,          eglCreateStreamKHR ) \
    T( PFNEGLDESTROYSTREAMKHRPROC,         eglDestroyStreamKHR ) \
    T( PFNEGLQUERYSTREAMKHRPROC,           eglQueryStreamKHR ) \
    T( PFNEGLQUERYSTREAMU64KHRPROC,        eglQueryStreamu64KHR ) \
    T( PFNEGLSTREAMATTRIBKHRPROC,          eglStreamAttribKHR ) \
    T( PFNEGLSTREAMCONSUMERACQUIREKHRPROC, eglStreamConsumerAcquireKHR ) \
    T( PFNEGLSTREAMCONSUMERRELEASEKHRPROC, eglStreamConsumerReleaseKHR ) \
    T( PFNEGLCREATESTREAMFROMFILEDESCRIPTORKHRPROC, eglCreateStreamFromFileDescriptorKHR) \
    T( PFNEGLSTREAMCONSUMERGLTEXTUREEXTERNALKHRPROC, \
                        eglStreamConsumerGLTextureExternalKHR )

#define EXTLST_DECL(tx, x) static tx x = NULL;
#define EXTLST_ENTRY(tx, x) { (extlst_fnptr_t *)&x, #x },

EXTENSION_LIST(EXTLST_DECL)
typedef void (*extlst_fnptr_t)(void);
static struct {
    extlst_fnptr_t *fnptr;
    char const *name;
} extensionList[] = { EXTENSION_LIST(EXTLST_ENTRY) };
#endif

/* ------  globals ---------*/
#if defined(EGL_KHR_stream)
EGLStreamKHR eglStream = EGL_NO_STREAM_KHR;
#endif

static GLuint shaderID = 0;
static GLuint quadVboID = 0;
static GLuint videoTexID;
static GLuint vertexLoc = 0;
static GLuint transRotLoc = 0;
static GLuint mvpLoc = 0;
static GLuint textureLoc = 0;
static GLint acquireTimeout = 16000;
static float mvp[16];
static double prevNow = 0.0;
static GLboolean quit = GL_FALSE;
static GLboolean fifo_mode = GL_FALSE;
static GLboolean showCounters = GL_FALSE;
static GLboolean forceRelease = GL_FALSE;

#if defined(EXTENSION_LIST)
static int setupExtensions(void)
{
    GLuint i;

    for (i = 0; i < (sizeof(extensionList) / sizeof(*extensionList)); i++) {
        *extensionList[i].fnptr = eglGetProcAddress(extensionList[i].name);
        if (*extensionList[i].fnptr == NULL) {
            printf("Couldn't get address of %s()\n", extensionList[i].name);
            return 0;
        }
    }

    return 1;
}

static void initQuad(long inWidth, long inHeight)
{
    GLfloat        v[16];
    GLuint        vertexID;

    // The vertex array is arranged like so:
    // x = Position x
    // y = Position y
    // z = Texture Coordinate s
    // w = Texture Coordinate t

    v[0] = (float)-inWidth/2.0f;
    v[1] = (float)-inHeight/2.0f;
    v[2] = 0.0f;
    v[3] = 1.0f;
    v[4] = v[0] + (float)inWidth;
    v[5] = v[1] + 0.0f;
    v[6] = 1.0f;
    v[7] = 1.0f;
    v[8] = v[0] + 0.0f;
    v[9] = v[1] + (float)inHeight;
    v[10] = 0.0f;
    v[11] = 0.0f;
    v[12] = v[0] + (float)inWidth;
    v[13] = v[1] + (float)inHeight;
    v[14] = 1.0f;
    v[15] = 0.0f;

    // Upload to VBO

    glGenBuffers(1, &vertexID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();

    quadVboID = vertexID;
}

static void initShader(void)
{
    GLuint simpleShaderID;
    simpleShaderID = GrUtilLoadShaderSrcStrings(vshader, sizeof(vshader),
                                                  fshader, sizeof(fshader),
                                                  GL_TRUE, GL_FALSE);
    if(!simpleShaderID) {
        printf("failed to create shader program\n");
    }

    CHECK_GL_ERROR();

    shaderID = simpleShaderID;
    vertexLoc = glGetAttribLocation(shaderID, "Vertex");
    textureLoc = glGetUniformLocation(shaderID, "Texture0");
    transRotLoc = glGetUniformLocation(shaderID, "TransRot");
    mvpLoc = glGetUniformLocation(shaderID, "ModelViewProjectionMatrix");

    // Create a GL texture ID
    glGenTextures(1, &videoTexID);

    // Bind the GL texture ID to the GL_TEXTURE_EXTERNAL_OES target
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, videoTexID);

    // Attach ourselves to eglStream as a texture consumer, using the context
    // bound to the texture unit above.
    eglStreamConsumerGLTextureExternalKHR(grUtilState.display, eglStream);
    // TODO: check result

    // Videos with non multiple of 2 dimensions will not show up if mipmaps are enabled!
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    CHECK_GL_ERROR();
}

int init_CP (void)
{
    EGLint fifo_length = 0, latency = 0, timeout = 0;

    if(!setupExtensions())
        return 0;
        EGLNativeFileDescriptorKHR file_descriptor;

        // Get the file descriptor of the stream from the consumer process
        // and re-create the EGL stream from it.
        file_descriptor = receive_fd(SOCK_PATH);
        if(file_descriptor == -1) {
            printf("EGLStreamInit: Cannot receive EGL file descriptor to socket: %s\n", SOCK_PATH);
            return 0;
        }
        printf("Producer file descriptor: %d\n", file_descriptor);
        eglStream = eglCreateStreamFromFileDescriptorKHR(
                        grUtilState.display, file_descriptor);
        close(file_descriptor);

        if (eglStream == EGL_NO_STREAM_KHR) {
            printf("EGLStreamInit: Couldn't create EGL Stream from fd.\n");
            return 0;
        }
    // Set stream attribute
    if(!eglStreamAttribKHR(grUtilState.display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
        printf("Consumer: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglStreamAttribKHR(grUtilState.display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, acquireTimeout)) {
        printf("Consumer: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    // Get stream attributes
    if(!eglQueryStreamKHR(grUtilState.display, eglStream, EGL_STREAM_FIFO_LENGTH_KHR, &fifo_length)) {
        printf("Consumer: eglQueryStreamKHR EGL_STREAM_FIFO_LENGTH_KHR failed\n");
    }
    if(!eglQueryStreamKHR(grUtilState.display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, &latency)) {
        printf("Consumer: eglQueryStreamKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglQueryStreamKHR(grUtilState.display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, &timeout)) {
        printf("Consumer: eglQueryStreamKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    if(fifo_mode != (fifo_length > 0)) {
        printf("EGL Stream consumer - Unable to set FIFO mode\n");
        fifo_mode = GL_FALSE;
    }
    if(fifo_mode)
        printf("EGL Stream consumer - Mode: FIFO Length: %d\n",  fifo_length);
    else
        printf("EGL Stream consumer - Mode: Mailbox\n");
    printf("EGL Stream consumer - Latency: %d usec\n", latency);
    printf("EGL Stream consumer - Timeout: %d usec\n", timeout);
    return 1;
}

int init(void)
{
    static const EGLint streamAttrMailboxMode[] = { EGL_NONE };
    static const EGLint streamAttrFIFOMode[] = { EGL_STREAM_FIFO_LENGTH_KHR, 4, EGL_NONE };
    EGLint fifo_length = 0, latency = 0, timeout = 0;

    if(!setupExtensions())
        return 0;

    eglStream = eglCreateStreamKHR(grUtilState.display,
        fifo_mode ? streamAttrFIFOMode : streamAttrMailboxMode);
    if (eglStream == EGL_NO_STREAM_KHR) {
        printf("Couldn't create eglStream.\n");
        return 0;
    }

    // Set stream attribute
    if(!eglStreamAttribKHR(grUtilState.display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, 16000)) {
        printf("Consumer: eglStreamAttribKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglStreamAttribKHR(grUtilState.display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, acquireTimeout)) {
        printf("Consumer: eglStreamAttribKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    // Get stream attributes
    if(!eglQueryStreamKHR(grUtilState.display, eglStream, EGL_STREAM_FIFO_LENGTH_KHR, &fifo_length)) {
        printf("Consumer: eglQueryStreamKHR EGL_STREAM_FIFO_LENGTH_KHR failed\n");
    }
    if(!eglQueryStreamKHR(grUtilState.display, eglStream, EGL_CONSUMER_LATENCY_USEC_KHR, &latency)) {
        printf("Consumer: eglQueryStreamKHR EGL_CONSUMER_LATENCY_USEC_KHR failed\n");
    }
    if(!eglQueryStreamKHR(grUtilState.display, eglStream, EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR, &timeout)) {
        printf("Consumer: eglQueryStreamKHR EGL_CONSUMER_ACQUIRE_TIMEOUT_USEC_KHR failed\n");
    }

    if(fifo_mode != (fifo_length > 0)) {
        printf("EGL Stream consumer - Unable to set FIFO mode\n");
        fifo_mode = GL_FALSE;
    }
    if(fifo_mode)
        printf("EGL Stream consumer - Mode: FIFO Length: %d\n",  fifo_length);
    else
        printf("EGL Stream consumer - Mode: Mailbox\n");
    printf("EGL Stream consumer - Latency: %d usec\n", latency);
    printf("EGL Stream consumer - Timeout: %d usec\n", timeout);

    initShader();
    initQuad(DEFAULT_RENDER_WIDTH, DEFAULT_RENDER_HEIGHT);
    GrUtilMatrixIdentity(mvp);
    GrUtilMatrixOrtho(mvp, 0.0f, DEFAULT_RENDER_WIDTH, DEFAULT_RENDER_HEIGHT, 0.0f, -1.0f, 1.0f);
    glClearColor(0.4f, 0.0f, 0.5f, 1.0f);
    eglSwapInterval(grUtilState.display, 1);
    return 1;
}
#else
int init(void)
{
    printf("EGL Stream extensions not available.\n");
    return 0;
}
#endif

void fini(void)
{
#if defined(EXTENSION_LIST)
    eglDestroyStreamKHR(grUtilState.display, eglStream);
#endif
}

void drawQuad(void)
{
    float px = DEFAULT_RENDER_WIDTH / 2.0f;
    float py = DEFAULT_RENDER_HEIGHT / 2.0f;
    float pz = 0.0f;
    float rot = 180.0f;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glUseProgram(shaderID);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)videoTexID);
    glUniform1i(textureLoc, 0);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)quadVboID);
    glVertexAttribPointer(vertexLoc, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(vertexLoc);
    CHECK_GL_ERROR();

    glUniform4f(transRotLoc, (float)px, (float)py, (float)pz, (float)rot);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (GLfloat*)&(mvp));

#if defined(EXTENSION_LIST)
    // Pull in a fresh texture.
    eglStreamConsumerAcquireKHR(grUtilState.display, eglStream);

    {
        EGLuint64KHR consframe = -1ll;
        EGLuint64KHR prodframe = -1ll;
        if (eglQueryStreamu64KHR(grUtilState.display, eglStream, EGL_CONSUMER_FRAME_KHR, &consframe)
                && eglQueryStreamu64KHR(grUtilState.display, eglStream, EGL_PRODUCER_FRAME_KHR, &prodframe)) {
            if (showCounters)
                printf("frames: %llu %llu\n", (long long int)prodframe, (long long int)consframe);
        }
    }
#endif

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

#if defined(EXTENSION_LIST)
    if (forceRelease)
        eglStreamConsumerReleaseKHR(grUtilState.display, eglStream);
#endif

    CHECK_GL_ERROR();
}

void cleanup(void)
{
    glDeleteProgram(shaderID);
    glDeleteTextures(1, &videoTexID);
    glDeleteBuffers(1, &quadVboID);
}

void printFPS(void)
{
    double now = SYSTIME();
    if(now - prevNow > 0) {
        // printf("fps = %.2f\r", 1.f / (float)(now - prevNow));
    }
    prevNow = now;
}


// Callback to close window
void
closeCB(void)
{
    quit = GL_TRUE;
}

// Callback to handle key presses
void
keyCB(char key, int state)
{
    // Ignoring releases
    if (!state) return;

    if ((key == 'q') || (key == 'Q'))
        quit = GL_TRUE;
}
