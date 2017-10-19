/*
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "main.h"
#include "interop.h"
#include <gl_consumer.h>
#include <nvmedia.h>
#include <log_utils.h>
#include "eglstrm_setup.h"
#include "egl_utils.h"

#define CHECK_GL_ERROR() {\
    GLenum error = glGetError();\
    if(error != GL_NO_ERROR) {\
        LOG_ERR("ogl error %s(%d): %d\n", __FILE__, __LINE__, error);\
        exit(-1);\
    }\
}

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
static const char fshader_rgba[] ={
    "#extension GL_NV_EGL_stream_consumer_external: enable\n"
    "#extension GL_OES_EGL_image_external : enable\n"

    "uniform samplerExternalOES Texture0;\n"

    "varying lowp vec2 TexCoord;\n"
    "varying lowp float Opacity;\n"

    "void main (void)\n"
    "{\n"
    "    gl_FragColor = texture2D(Texture0, TexCoord);\n"
    //"    gl_FragColor.a *= Opacity;\n"
    "}\n"
};
static const char fshader_yuvsp[] ={
    "#extension GL_NV_EGL_stream_consumer_external: enable\n"
    "#extension GL_OES_EGL_image_external : enable\n"

    /* SEMIPLANAR YUV: Texture0 = the Y plane */
    "uniform samplerExternalOES Texture0;\n"
    /* SEMIPLANAR YUV: Texture1 = the UV plane */
    "uniform samplerExternalOES Texture1;\n"

    "varying lowp vec2 TexCoord;\n"
    "varying lowp float Opacity;\n"

    "void main (void)\n"
    "{\n"
    "    lowp vec3 color;\n"
    /* SEMIPLANAR YUV: Convert to RGB Using Rec. 601 Colorspace constants */
    "    lowp vec3 colorOff = vec3(-16.0/255.0, -128.0/255.0, -128.0/255.0);\n"
    "    lowp mat3 colorMat;\n"
    "    colorMat[0][0] = 1.1644;\n"
    "    colorMat[0][1] = 0.0;\n"
    "    colorMat[0][2] = 1.5960;\n"
    "    colorMat[1][0] = 1.1644;\n"
    "    colorMat[1][1] = -0.3918;\n"
    "    colorMat[1][2] = -0.8130;\n"
    "    colorMat[2][0] = 1.1644;\n"
    "    colorMat[2][1] = 2.0172;\n"
    "    colorMat[2][2] = 0.0;\n"
    "    color.r = texture2D(Texture0, TexCoord).r;\n"
    /* SEMIPLANAR YUV: Should probably offset TexCoord for correct UV sampling. */
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
static GLuint yTexID;
static GLuint uvTexID;
static GLuint rgbaTexID;
static GLuint vertexLoc = 0;
static GLuint transRotLoc = 0;
static GLuint mvpLoc = 0;
static GLuint texture0Loc = 0;
static GLuint texture1Loc = 0;

EXTENSION_LIST(EXTLST_EXTERN)

// Initialize a 4x4 matrix to identity
//   m <- I
static void
matrixIdentity(
    float m[16])
{
    memset(m, 0, sizeof(float) * 16);
    m[4 * 0 + 0] = m[4 * 1 + 1] = m[4 * 2 + 2] = m[4 * 3 + 3] = 1.0;
}

// Multiply the second 4x4 matrix into the first
//   m0 <- m0 * m1
static void
matrixMultiply(
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
static void
matrixOrtho(
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

    matrixMultiply(m, m1);
}

// Function to print logs when shader compilation fails
static void
shaderDebug(
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
static unsigned int
loadShaderSrcStrings(
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
        shaderDebug(vertShader, GL_COMPILE_STATUS, "Vert Compile");
    glShaderSource(fragShader, 1, (const char**)&fragSrc, &fragSrcSize);
    glCompileShader(fragShader);

    if (debugging)
        shaderDebug(fragShader, GL_COMPILE_STATUS, "Frag Compile");

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
            shaderDebug(prog, GL_LINK_STATUS, "Program Link");
        glValidateProgram(prog);
        if (debugging)
            shaderDebug(prog, GL_VALIDATE_STATUS, "Program Validate");
    }

    return prog;
}

static void initQuad(long inWidth, long inHeight, GLboolean yInvert)
{
    GLfloat        v[16];
    GLuint        vertexID;

    /* The vertex array is arranged like so:
     x = Position x
     y = Position y
     z = Texture Coordinate s
     w = Texture Coordinate t    */

    v[0] = (float)-inWidth/2.0f;
    v[1] = (float)-inHeight/2.0f;
    v[2] = 0.0f;
    v[3] = yInvert ? 0.0f : 1.0f;
    v[4] = v[0] + (float)inWidth;
    v[5] = v[1] + 0.0f;
    v[6] = 1.0f;
    v[7] = yInvert ? 0.0f : 1.0f;
    v[8] = v[0] + 0.0f;
    v[9] = v[1] + (float)inHeight;
    v[10] = 0.0f;
    v[11] = yInvert ? 1.0f : 0.0f;
    v[12] = v[0] + (float)inWidth;
    v[13] = v[1] + (float)inHeight;
    v[14] = 1.0f;
    v[15] = yInvert ? 1.0f : 0.0f;

    /* upload to VBO */

    glGenBuffers(1, &vertexID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexID);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    CHECK_GL_ERROR();
    quadVboID = vertexID;
}

static void initShader(GLConsumerTestArgs *ctx)
{
    GLuint simpleShaderID;
    if (ctx->surfTypeIsRGBA)
    {
        simpleShaderID = loadShaderSrcStrings(vshader, sizeof(vshader),
                fshader_rgba, sizeof(fshader_rgba),
                GL_TRUE, GL_FALSE);
        if(!simpleShaderID) {
            LOG_ERR("failed to create shader program\n");
        }
    }
    else /* YUV */
    {
        simpleShaderID = loadShaderSrcStrings(vshader, sizeof(vshader),
                fshader_yuvsp, sizeof(fshader_yuvsp),
                GL_TRUE, GL_FALSE);
        if(!simpleShaderID) {
            LOG_ERR("failed to create shader program\n");
        }

    }

    CHECK_GL_ERROR();

    shaderID = simpleShaderID;
    vertexLoc = glGetAttribLocation(shaderID, "Vertex");
    transRotLoc = glGetUniformLocation(shaderID, "TransRot");
    mvpLoc = glGetUniformLocation(shaderID, "ModelViewProjectionMatrix");

    if (ctx->surfTypeIsRGBA)
    {

        texture0Loc = glGetUniformLocation(shaderID, "Texture0");

        /* Create a GL texture ID */
        glActiveTexture(GL_TEXTURE0);
        glGenTextures(1, &rgbaTexID);

        /* Bind the GL texture ID to the GL_TEXTURE_EXTERNAL_OES target */
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, rgbaTexID);

        /* Attach ourselves to eglStream as a texture consumer, using the context
           bound to the texture unit above. */
        eglStreamConsumerGLTextureExternalKHR(ctx->eglDisplay, ctx->eglStream);

        /* Videos with non multiple of 2 dimensions will not show up if mipmaps are enabled! */
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        CHECK_GL_ERROR();
    }
    else /* YUV */
    {
        const EGLAttribKHR attribs[] = {
            EGL_YUV_NUMBER_OF_PLANES_EXT, 2,
            EGL_YUV_PLANE0_TEXTURE_UNIT_NV, 0,
            EGL_YUV_PLANE1_TEXTURE_UNIT_NV, 1,
            EGL_COLOR_BUFFER_TYPE, EGL_YUV_BUFFER_EXT,
            EGL_NONE,
        };
        texture0Loc = glGetUniformLocation(shaderID, "Texture0");
        texture1Loc = glGetUniformLocation(shaderID, "Texture1");

        /* Create a GL texture ID */
        glGenTextures(1, &yTexID);
        glGenTextures(1, &uvTexID);

        /* Bind the GL texture IDs to the GL_TEXTURE_EXTERNAL_OES target */
        /* SEMIPLANAR YUV: Bind multiple external textures by changing the
           active texture unit. Set up one texture unit for each plane you
           wish to have associated with a GL texture. */
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, yTexID);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, uvTexID);

        /* Attach ourselves to eglStream as a texture consumer, using the context
           bound above and the texture units specified by the attrib list. */
        eglStreamConsumerGLTextureExternalAttribsNV(ctx->eglDisplay,
                ctx->eglStream,
                attribs);

        /* Videos with non multiple of 2 dimensions will not show up if mipmaps are enabled! */
        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        glActiveTexture(GL_TEXTURE1);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

        glActiveTexture(GL_TEXTURE0);

    }
    CHECK_GL_ERROR();
}

static NvMediaStatus drawQuad(GLConsumerTestArgs *ctx)
{
    EglUtilState *state = ctx->state;
    NvMediaStatus status = NVMEDIA_STATUS_OK;
    float px = ctx->width / 2.0f;
    float py = ctx->height / 2.0f;
    float pz = 0.0f;
    float rot = 0.0f;
    CHECK_GL_ERROR();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_BLEND);
    glUseProgram(shaderID);

    if (ctx->surfTypeIsRGBA)
    {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)rgbaTexID);
        glUniform1i(texture0Loc, 0);
    }
    else /* YUV */
    {
        /* SEMIPLANAR YUV: Bind yTexID to GL_TEXTURE0, uvTexID to GL_TEXTURE1 */
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)yTexID);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)uvTexID);

        /* SEMIPLANAR YUV: Bind GL_TEXTURE0->Texture0, GL_TEXTURE1->Texture1 */
        glUniform1i(texture0Loc, 0);
        glUniform1i(texture1Loc, 1);
        glActiveTexture(GL_TEXTURE0);
    }

    glBindBuffer(GL_ARRAY_BUFFER, (GLuint)quadVboID);
    glVertexAttribPointer(vertexLoc, 4, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glEnableVertexAttribArray(vertexLoc);
    CHECK_GL_ERROR();

    glUniform4f(transRotLoc, (float)px, (float)py, (float)pz, (float)rot);
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, (GLfloat*)&(mvp));
    glViewport(0,0,ctx->width,ctx->height);

    /* Pull in a fresh texture */
    if (eglStreamConsumerAcquireKHR(state->display, ctx->eglStream)) {
        EGLuint64KHR consframe = -1ll;
        EGLuint64KHR prodframe = -1ll;
        if (eglQueryStreamu64KHR(state->display, ctx->eglStream, EGL_CONSUMER_FRAME_KHR, &consframe)
                && eglQueryStreamu64KHR(state->display, ctx->eglStream, EGL_PRODUCER_FRAME_KHR, &prodframe)) {
            LOG_DBG("GL Consumer: Display image %llu\n", consframe);
        }

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        status = NVMEDIA_STATUS_ERROR;
    }

    CHECK_GL_ERROR();
    return status;
}

static void *
glConsumerProc (
        void *data)
{
    GLConsumerTestArgs *ctx = (GLConsumerTestArgs *)data;
    EglUtilState* state = ctx->state;
    GLboolean     yInvert;
    GLubyte * pixels=NULL;

    if(!ctx) {
        LOG_ERR("%s: Bad parameter\n", __func__);
        return NULL;
    }
    LOG_DBG("GL consumer thread is active\n");

    if(!EGLUtilCreateContext(ctx->state)) {
        LOG_ERR("%s: Context  not created\n",__func__);
        return NULL;
    }
    initShader(ctx);

    if (ctx->surfTypeIsRGBA) {
        yInvert = GL_TRUE;
    } else {
        yInvert = GL_TRUE;
    }

    initQuad(ctx->width, ctx->height, yInvert);
    matrixIdentity(mvp);
    matrixOrtho(mvp, 0.0f, ctx->width, ctx->height, 0.0f, 0.0f, 1.0f);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    eglSwapInterval(state->display, 1);

    while(!(*ctx->quit)) {
        // clear gl buffer
        glClear(GL_COLOR_BUFFER_BIT);
        EGLint streamState = 0;
        if(!eglQueryStreamKHR(
                state->display,
                ctx->eglStream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("GL consumer: eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }
        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            LOG_DBG("GL Consumer: - EGL_STREAM_STATE_DISCONNECTED_KHR received\n");
            *ctx->quit = NV_TRUE;
            goto done;
        } else if(streamState != EGL_STREAM_STATE_NEW_FRAME_AVAILABLE_KHR) {
            continue;
        }

        if(drawQuad(ctx)) {
            *ctx->quit = NV_TRUE;
            goto done;
        }

        ctx->frameCount++;
        if(ctx->outputFrames) {
                pixels = (GLubyte*) malloc(ctx->width*ctx->height*4*sizeof(GLubyte));
                glReadPixels(0,0,ctx->width,ctx->height,GL_RGBA,GL_UNSIGNED_BYTE,pixels);

                LOG_DBG("GL Consumer: Saving RGBA file %d ...\n", ctx->frameCount);
                {

                    FILE* fp = fopen(ctx->outputFrames,"ab");
                    fwrite(pixels,ctx->width*ctx->height*4,1,fp);
                    fclose(fp);
                }
                free(pixels);
        }
        eglSwapBuffers(state->display, state->surface);
    }
done:

    eglStreamConsumerReleaseKHR(state->display, ctx->eglStream);
    EGLUtilDestroyContext(ctx->state);
    ctx->procThreadExited = GL_TRUE;
    *ctx->consumerDone = NV_TRUE;
    return NULL;
}

NvMediaStatus glConsumerInit(volatile NvBool *consumerDone, GLConsumerTestArgs *ctx, EglStreamClient *streamClient,EglUtilState *state, InteropArgs *args)
{
    memset (ctx, 0, sizeof(GLConsumerTestArgs));

    LOG_DBG("Main - glConsumer_init\n");
    ctx->surfTypeIsRGBA = args->surfTypeIsRGBA;
    ctx->eglDisplay = streamClient->display;
    ctx->eglStream = streamClient->eglStream;
    ctx->state= state;
    ctx->width = args->width;
    ctx->height = args->height;


    ctx->consumerDone = consumerDone;
    ctx->outputFrames = args->outfile;
    ctx->quit = args->quit;

    pthread_create(&ctx->procThread, NULL, glConsumerProc, (void *)ctx);
        if (!ctx->procThread) {
            LOG_ERR("GL consumer init: Unable to create process thread\n");
            ctx->procThreadExited = GL_TRUE;
            return NVMEDIA_STATUS_ERROR;
        }

    return NVMEDIA_STATUS_OK;
}


void glConsumerFini(GLConsumerTestArgs *ctx)
{
    glDeleteProgram(shaderID);

    if (ctx->surfTypeIsRGBA)
    {
        glDeleteTextures(1, &rgbaTexID);
    }
    else /*YUV*/
    {
        glDeleteTextures(1, &yTexID);
        glDeleteTextures(1, &uvTexID);
    }
    glDeleteBuffers(1, &quadVboID);

}

void glConsumerStop(GLConsumerTestArgs *ctx)
{
    *ctx->quit = GL_TRUE;
    while (!ctx->procThreadExited) {
        LOG_DBG("wait for GL consumer thread exit\n");
    }
}

void glConsumerFlush(GLConsumerTestArgs *ctx)
{
    eglStreamConsumerReleaseKHR(ctx->eglDisplay, ctx->eglStream);
    while(eglStreamConsumerAcquireKHR(ctx->eglDisplay, ctx->eglStream)) {
        eglStreamConsumerReleaseKHR(ctx->eglDisplay, ctx->eglStream);
    }
}
