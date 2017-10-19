/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple GL stream consumer rendering sample app
//

#include "gl_consumer.h"
#include "nvmedia.h"
#include "ipp_yuv.h"
#include "log_utils.h"
#include "misc_utils.h"

#define MAX_EGLSTREAM 4
#define MAX_METASIZE (32*1024)

#define CHECK_GL_ERROR() {\
    GLenum error = glGetError();\
    if(error != GL_NO_ERROR) {\
        LOG_ERR("ogl error %s(%d): %d\n", __FILE__, __LINE__, error);\
        exit(-1);\
    }\
}
#if defined(EXTENSION_LIST)
EXTENSION_LIST(EXTLST_EXTERN)
#endif
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
    "    rotMat[0][0] = cos(rotation);\n"
    "    rotMat[1][0] = -sin(rotation);\n"
    "    rotMat[0][1] = sin(rotation);\n"
    "    rotMat[1][1] = cos(rotation);\n"

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
    "#version 300 es\n"
    "#extension GL_NV_EGL_stream_consumer_external: enable\n"
    "#extension GL_OES_EGL_image_external : enable\n"
    "#define Z_SIZE 64.0\n"
    "precision highp float;\n"

    // SEMIPLANAR YUV: Texture0 = the Y plane
    "uniform highp samplerExternalOES Texture0;\n"
    // SEMIPLANAR YUV: Texture1 = the UV plane
    "uniform highp samplerExternalOES Texture1;\n"
    "uniform highp sampler3D Lookup;\n"

    "in lowp vec2 TexCoord;\n"
    "in lowp float Opacity;\n"

    "void main (void)\n"
    "{\n"
    "    lowp vec3 color;\n"
    "    vec3 lookup_coord;\n"
    // SEMIPLANAR YUV: Convert to RGB Using Rec. 601 Colorspace constants
    "    lowp vec3 colorOff = vec3(0.0, -128.0/255.0, -128.0/255.0);\n"
    "    lowp mat3 colorMat;\n"
    "    colorMat[0][0] = 1.0;\n"
    "    colorMat[0][1] = 0.0;\n"
    "    colorMat[0][2] = 1.4020;\n"
    "    colorMat[1][0] = 1.0;\n"
    "    colorMat[1][1] = -0.3441;\n"
    "    colorMat[1][2] = -0.7141;\n"
    "    colorMat[2][0] = 1.0;\n"
    "    colorMat[2][1] = 1.772;\n"
    "    colorMat[2][2] = 0.0;\n"
    "    color.r = texture2D(Texture0, TexCoord).r;\n"
    // SEMIPLANAR YUV: Should probably offset TexCoord for correct UV sampling.
    "    color.gb = texture2D(Texture1, TexCoord).rg;\n"
    "    color.rgb += colorOff;\n"
    "    color.rgb *= colorMat;\n"
    "    gl_FragColor = vec4(color.rgb, Opacity);\n"
    "}\n"
};

/* ------  globals ---------*/
static float mvp[16];

static GLuint shaderID = 0;
static GLuint quadVboID = 0;
static GLuint yTexID[MAX_EGLSTREAM] = {0};
static GLuint uvTexID[MAX_EGLSTREAM] = {0};
static GLuint vertexLoc;
static GLuint transRotLoc;
static GLuint mvpLoc;
static GLuint texture0Loc;
static GLuint texture1Loc;

// Initialize a 4x4 matrix to identity
//   m <- I
void
EGLUtilMatrixIdentity(
    float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[4 * 0 + 0] = m[4 * 1 + 1] = m[4 * 2 + 2] = m[4 * 3 + 3] = 1.0;
}

// Multiply the second 4x4 matrix into the first
//   m0 <- m0 * m1
void
EGLUtilMatrixMultiply(
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
EGLUtilMatrixOrtho(
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

    EGLUtilMatrixMultiply(m, m1);
}
// Function to print logs when shader compilation fails
static void
EGLUtilShaderDebug(
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
        LOG_ERR("--- %s log ---\n", op);
        LOG_ERR("%s", str);
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
                    LOG_ERR("--- %s code ---\n", op);
                    LOG_ERR("%s", str);
                }
                free(str);
            }
        }
    } else { // LINK or VALIDATE
        glGetProgramiv(obj, status, &success);
    }

    if (!success)
    {
        LOG_ERR("--- %s failed ---\n", op);
        exit(-1);
    }
}

// Takes shader source strings, compiles them, and builds a shader program
unsigned int
EGLUtilLoadShaderSrcStrings(
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
        EGLUtilShaderDebug(vertShader, GL_COMPILE_STATUS, "Vert Compile");
    glShaderSource(fragShader, 1, (const char**)&fragSrc, &fragSrcSize);
    glCompileShader(fragShader);

    if (debugging)
        EGLUtilShaderDebug(fragShader, GL_COMPILE_STATUS, "Frag Compile");

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
            EGLUtilShaderDebug(prog, GL_LINK_STATUS, "Program Link");
        glValidateProgram(prog);
        if (debugging)
            EGLUtilShaderDebug(prog, GL_VALIDATE_STATUS, "Program Validate");
    }

    return prog;
}

static void initQuad(long inWidth, long inHeight, GLboolean yInvert)
{
    GLfloat v[64], *p, halfwidth, halfheight;
    GLuint  vertexID;

    // The vertex array is arranged like so:
    // x = Position x
    // y = Position y
    // z = Texture Coordinate s
    // w = Texture Coordinate t

#define MAKE_QUAD(X, Y, W, H)                   \
    p[0] = (X);                                 \
    p[1] = (Y);                                 \
    p[2] = 0.0f;                                \
    p[3] = yInvert ? 0.0f : 1.0f;               \
    p[4] = p[0] + (W);                          \
    p[5] = p[1];                                \
    p[6] = 1.0f;                                \
    p[7] = yInvert ? 0.0f : 1.0f;               \
    p[8] = p[0];                                \
    p[9] = p[1] + (H);                          \
    p[10] = 0.0f;                               \
    p[11] = yInvert ? 1.0f : 0.0f;              \
    p[12] = p[0] + (W);                         \
    p[13] = p[1] + (H);                         \
    p[14] = 1.0f;                               \
    p[15] = yInvert ? 1.0f : 0.0f;

    halfwidth  = inWidth * 0.5f;
    halfheight = inHeight * 0.5f;
    p = v;
    MAKE_QUAD(-halfwidth, -halfheight, halfwidth, halfheight);
    p = v + 16;
    MAKE_QUAD(0.0f, -halfheight, halfwidth, halfheight);
    p = v + 32;
    MAKE_QUAD(-halfwidth, 0.0f, halfwidth, halfheight);
    p = v + 48;
    MAKE_QUAD(0.0f, 0.0f, halfwidth, halfheight);

    // Upload to VBO

    glGenBuffers(1, &vertexID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();

    quadVboID = vertexID;

}

static NvMediaStatus checkEGLStreamReady(GLConsumerCtx *ctx) {
    NvU32 i;
    EGLint syncRet;
    NvU32 syncDone = 0;
    EglUtilState *state;

    state = ctx->eglUtil;
    while(!(*ctx->quit)) {
        for(i=0; i<ctx->ippNum; i++) {
            if (!(syncDone & (1 << i))) {
                syncRet = eglClientWaitSyncKHR(state->display,
                                               ctx->sync[i],
                                               0,
                                               15000000); // eglClientWaitSyncKHR accepts nanoseconds
                if(syncRet == EGL_CONDITION_SATISFIED_KHR) {
                    eglSignalSyncKHR(state->display,
                                     ctx->sync[i],
                                     EGL_UNSIGNALED_KHR);
                    syncDone |= (1 << i);
                } else if(syncRet != EGL_TIMEOUT_EXPIRED_KHR) {
                    LOG_ERR("EGL stream sync error = %d\n", syncRet);
                    return NVMEDIA_STATUS_ERROR;
                }
            }
        }
        if(syncDone == (NvU32)((1 << ctx->ippNum) - 1))
           break;
    }

    return NVMEDIA_STATUS_OK;
}

static void initShader(GLConsumerCtx *ctx)
{
    EglUtilState *eglUtil = NULL;
    GLuint simpleShaderID;
    NvU32 index;
    const EGLAttribKHR attribs[] = {
        EGL_YUV_NUMBER_OF_PLANES_EXT, 2,
        EGL_YUV_PLANE0_TEXTURE_UNIT_NV, 0,
        EGL_YUV_PLANE1_TEXTURE_UNIT_NV, 1,
        EGL_COLOR_BUFFER_TYPE, EGL_YUV_BUFFER_EXT,
        EGL_NONE,
    };

    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return;
    }

    eglUtil = ctx->eglUtil;

    simpleShaderID = EGLUtilLoadShaderSrcStrings(vshader, sizeof(vshader),
                                                  fshader, sizeof(fshader),
                                                  GL_TRUE, GL_FALSE);
    if(!simpleShaderID) {
        LOG_ERR("failed to create shader program\n");
    }

    CHECK_GL_ERROR();

    shaderID = simpleShaderID;
    vertexLoc = glGetAttribLocation(shaderID, "Vertex");
    texture0Loc = glGetUniformLocation(shaderID, "Texture0");
    texture1Loc = glGetUniformLocation(shaderID, "Texture1");
    transRotLoc = glGetUniformLocation(shaderID, "TransRot");
    mvpLoc = glGetUniformLocation(shaderID, "ModelViewProjectionMatrix");

    for(index = 0; index < ctx->ippNum; index++) {
        // Create a GL texture ID
        glGenTextures(1, &yTexID[index]);
        glGenTextures(1, &uvTexID[index]);

        // Bind the GL texture IDs to the GL_TEXTURE_EXTERNAL_OES target
        // SEMIPLANAR YUV: Bind multiple external textures by changing the
        // active texture unit.  Set up one texture unit for each plane you
        // wish to have associated with a GL texture.
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, yTexID[index]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, uvTexID[index]);

        // Attach ourselves to eglStream as a texture consumer, using the context
        // bound above and the texture units specified by the attrib list.
        eglStreamConsumerGLTextureExternalAttribsNV(eglUtil->display,
                                                    ctx->eglStrmCtx->eglStream[index],
                                                    attribs);
        // TODO: check result

        // Videos with non multiple of 2 dimensions will not show up if mipmaps are enabled!
        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        glActiveTexture(GL_TEXTURE1);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    }

    CHECK_GL_ERROR();
}

static NvMediaStatus drawQuad(GLConsumerCtx *ctx, int index)
{
    EglUtilState *eglUtil = NULL;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    EglStreamClient *eglStrmCtx = NULL;

    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    eglUtil = ctx->eglUtil;
    eglStrmCtx = ctx->eglStrmCtx;

    float px = eglUtil->width / 2.0f;
    float py = eglUtil->height / 2.0f;
    float pz = 0.0f;
    float rot = 0.0f;

    glEnable(GL_BLEND);
    glUseProgram(shaderID);

    // SEMIPLANAR YUV: Bind yTexID to GL_TEXTURE0, uvTexID to GL_TEXTURE1
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)yTexID[index]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)uvTexID[index]);

    // SEMIPLANAR YUV: Bind GL_TEXTURE0->Texture0, GL_TEXTURE1->Texture1
    glUniform1i(texture0Loc, 0);
    glUniform1i(texture1Loc, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)quadVboID);
    glVertexAttribPointer(vertexLoc, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(vertexLoc);
    CHECK_GL_ERROR();

    glUniform4f(transRotLoc, (float)px, (float)py, (float)pz, (float)rot);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (GLfloat*)&(mvp));

    // Pull in a fresh texture.
    // SEMIPLANAR YUV: One acquire call updates all textures bound to the stream
    if (eglStreamConsumerAcquireKHR(eglUtil->display, eglStrmCtx->eglStream[index])) {
        EGLuint64KHR consframe = -1ll;
        EGLuint64KHR prodframe = -1ll;
        if (eglQueryStreamu64KHR(eglUtil->display, eglStrmCtx->eglStream[index], EGL_CONSUMER_FRAME_KHR, &consframe)
                && eglQueryStreamu64KHR(eglUtil->display, eglStrmCtx->eglStream[index], EGL_PRODUCER_FRAME_KHR, &prodframe)) {
            LOG_DBG("frames: %llu %llu\n", prodframe, consframe);
        }
        if (ctx->fifoMode)
        {
            EGLTimeKHR eglCurrentTime, eglCustomerTime;
            if (eglQueryStreamTimeKHR(eglUtil->display, eglStrmCtx->eglStream[index], EGL_STREAM_TIME_NOW_KHR, &eglCurrentTime)) {
                if (eglQueryStreamTimeKHR(eglUtil->display, eglStrmCtx->eglStream[index], EGL_STREAM_TIME_CONSUMER_KHR, &eglCustomerTime)) {
                    if (eglCustomerTime > eglCurrentTime)
                    {
                        NvMediaTime deltaTime;
                        deltaTime.tv_sec = (eglCustomerTime-eglCurrentTime)/1000000000LL;
                        deltaTime.tv_nsec = (eglCustomerTime-eglCurrentTime)%1000000000LL;
                        nanosleep(&deltaTime, NULL);
                    }
                }
            }
        }

        glDrawArrays(GL_TRIANGLE_STRIP, index * 4, 4);

    } else {
        status = NVMEDIA_STATUS_ERROR;
    }

    CHECK_GL_ERROR();
    return status;
}

// YUV Pipeline
void *GlConsumerProc(void *data)
{
    GLConsumerCtx *ctx = NULL;
    NvU32 i = 0;
    EglUtilState *eglUtil = NULL;
    EglStreamClient *eglStrmCtx = NULL;

    if(!data) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NULL;
    }
    ctx = (GLConsumerCtx *)data;
    eglUtil = ctx->eglUtil;
    eglStrmCtx = ctx->eglStrmCtx;

    EGLUtilCreateContext(ctx->eglUtil);

    initShader(data);

    initQuad(eglUtil->width, eglUtil->height, 1);
    EGLUtilMatrixIdentity(mvp);
    EGLUtilMatrixOrtho(mvp, 0.0f, eglUtil->width, eglUtil->height, 0.0f, 0.0f, 1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    eglSwapInterval(eglUtil->display, 1);

    LOG_DBG("GL consumer thread is active\n");

    for(i = 0; i< ctx->ippNum; i++) {
        ctx->sync[i] = eglCreateStreamSyncNV(eglUtil->display,
                                             eglStrmCtx->eglStream[i],
                                             EGL_SYNC_NEW_FRAME_NV,
                                             0);

        if(ctx->sync[i] == EGL_NO_SYNC_KHR) {
            LOG_ERR("fail to create EGLSyncKHR object \n");
            goto done;
        }
    }

    *ctx->glConsumerInitDone = NVMEDIA_TRUE;

    // wait till the first frame avaliable
    if(NVMEDIA_STATUS_OK != checkEGLStreamReady(ctx)) {
        goto done;
    }


    while(!(*ctx->quit)) {
        // clear gl buffer
        glClear(GL_COLOR_BUFFER_BIT);
        // acqure from multiple eglstream
        for(i = 0; i < ctx->ippNum; i++) {
            if(drawQuad(data, i)) {
                *ctx->quit = NV_TRUE;
                goto done;
            }
            if(i==ctx->ippNum-1)
                eglSwapBuffers(eglUtil->display, eglUtil->surface);
        }
    }
done:
    for(i = 0; i < ctx->ippNum; i++) {
        eglStreamConsumerReleaseKHR(eglUtil->display, eglStrmCtx->eglStream[i]);
    }

    *ctx->quit = NV_TRUE;

    // Wait Producer to stop first
    while(!(*ctx->producerExited)) {
        LOG_DBG("%s: Waiting for threads to quit\n", __func__);
        usleep(100);
    }
    EGLUtilDestroyContext(ctx->eglUtil);

    *ctx->consumerExited = NVMEDIA_TRUE;

    return NULL;
}

GLConsumerCtx* GlConsumerInit(NvMediaBool *consumerDone,
                             EglStreamClient *streamClient,
                             EglUtilState *eglUtil,
                             InteropContext *interopCtx)
{
    GLConsumerCtx *ctx = NULL;

    LOG_DBG("%s - glConsumer_init\n", __func__);
    ctx = (GLConsumerCtx*)malloc(sizeof(GLConsumerCtx));
    if(!ctx) {
        LOG_ERR("%s: Could not allocate memory\n", __func__);
        return NULL;
    }
    memset (ctx, 0, sizeof(GLConsumerCtx));
    ctx->eglStrmCtx = streamClient;
    ctx->eglDisplay = streamClient->display;
    ctx->eglUtil= eglUtil;
    ctx->width = interopCtx->width;
    ctx->height = interopCtx->height;
    ctx->ippNum = interopCtx->ippNum;
    ctx->consumerExited= consumerDone;
    ctx->glConsumerInitDone = &interopCtx->consumerInitDone;
    ctx->producerExited = &interopCtx->producerExited;
    ctx->quit = interopCtx->quit;
    ctx->fifoMode = interopCtx->fifoMode;

    pthread_create(&ctx->glConsumerThread, NULL, GlConsumerProc, (void*)ctx);
    if (!ctx->glConsumerThread) {
        LOG_ERR("%s: GL consumer init: Unable to create process thread\n", __func__);
        interopCtx->consumerExited= NVMEDIA_TRUE;
        return NULL;
    }

    return ctx;
}

NvMediaStatus GlConsumerFini(GLConsumerCtx *ctx)
{
    NvU32 i = 0;
    NvMediaStatus status = NVMEDIA_STATUS_OK;

    glDeleteProgram(shaderID);
    for(i=0; i<MAX_EGLSTREAM; i++) {
        glDeleteTextures(1, &yTexID[i]);
        glDeleteTextures(1, &uvTexID[i]);
    }
    glDeleteBuffers(1, &quadVboID);

    for(i = 0; i < ctx->ippNum; i++) {
        // Finalize
        status = GlConsumerFlush(ctx->eglStrmCtx->eglStream[i], ctx->eglUtil);
    }

    free(ctx);
    return status;
}

void GlConsumerStop(GLConsumerCtx *ctx)
{
    *ctx->quit = GL_TRUE;
    while (!(*(ctx->consumerExited))) {
        LOG_DBG("wait for GL consumer thread exit\n");
        usleep(100);
    }
}

NvMediaStatus GlConsumerFlush(EGLStreamKHR eglStream, EglUtilState *eglUtil)
{
    if(!eglStream) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NVMEDIA_STATUS_ERROR;
    }

    eglStreamConsumerReleaseKHR(eglUtil->display, eglStream);
    while(eglStreamConsumerAcquireKHR(eglUtil->display, eglStream)) {
        eglStreamConsumerReleaseKHR(eglUtil->display, eglStream);
    }

    return NVMEDIA_STATUS_OK;
}
