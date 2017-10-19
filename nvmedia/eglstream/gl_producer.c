/*
 * gl_producer.c
 *
 * Copyright (c) 2015-2017, NVIDIA CORPORATION. All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

//
// DESCRIPTION:   Simple gearslib EGL stream producer app
//
#include "gl_producer.h"
#include "log_utils.h"

extern NvBool signal_stop;
GLTestArgs gl_testArgs;

// Gear structures and matrices
static Gear* gear1 = NULL;
static Gear* gear2 = NULL;
static Gear* gear3 = NULL;
static GLfloat gear1_mat[16];
static GLfloat gear2_mat[16];
static GLfloat gear3_mat[16];

// Shader program to use for gears and indices of attributes
static GLuint gearShaderProgram = 0;
static GLuint mview_mat_index;
static GLuint material_index;
static GLuint pos_index;
static GLuint nrm_index;

static const char vshader[] = {
"// Constant uniforms\n"
"uniform vec3 light_dir;  // Light 0 direction\n"
"uniform mat4 proj_mat;   // Projection matrix.\n"

"// Per-frame/per-object uniforms\n"
"uniform mat4 mview_mat;  // Model-view matrix\n"
"uniform vec3 material;   // Ambient and diffuse material\n"

"// Per-vertex attributes\n"
"attribute vec3 pos_attr;\n"
"attribute vec3 nrm_attr;\n"

"// Output vertex color\n"
"varying vec3 col_var;\n"

"void main()\n"
"{\n"
   " // Transformed position is projection * modelview * pos\n"
   " gl_Position = proj_mat * mview_mat * vec4(pos_attr, 1.0);\n"

    "// We know modelview matrix has no scaling, so don't need a separate\n"
    "//   inverse-transpose for transforming normals\n"
    "vec3 normal = vec3(normalize(mview_mat * vec4(nrm_attr, 0.0)));\n"

    "// Compute dot product of light and normal vectors\n"
    "float ldotn = max(dot(normal, light_dir), 0.0);\n"

    "// Compute affect of lights and clamp\n"
    "//   Global ambient is default GL value of 0.2\n"
    "//   Light0 ambient is default GL value of 0.0\n"
    "//   Light0 diffuse is default GL value of 1.0\n"
    "col_var = min((ldotn+0.2) * material, 1.0);\n"
"}\n"
};

static const char fshader[] ={

"varying lowp vec3 col_var;\n"

"void main()\n"
"{\n"
  "  gl_FragColor = vec4(col_var, 1.0);\n"
"}\n"
};

#if defined(EXTENSION_LIST)
//  Make a gear wheel.
//
//  Input:  inner_radius - radius of hole at center
//          outer_radius - radius at center of teeth
//          width - width of gear
//          teeth - number of teeth
//          tooth_depth - depth of tooth
static Gear* makegear(
    GLfloat  inner_radius,
    GLfloat  outer_radius,
    GLfloat  width,
    int      teeth,
    GLfloat  tooth_depth)
{
    Gear     *gear;
    GLfloat  r0, r1, r2, da, hw;
    GLfloat  *vert, *norm;
    GLushort *index, *indexF, *indexB;
    int      i;

    // Create gear structure and arrays of vertex/index data
    gear = (Gear*)malloc(sizeof(Gear));
    gear->teeth = teeth;
    gear->vertices   = (GLfloat*) malloc(20*teeth*3*sizeof(GLfloat));
    gear->normals    = (GLfloat*) malloc(20*teeth*3*sizeof(GLfloat));
    gear->frontbody  = (GLushort*)malloc((4*teeth+2)*sizeof(GLushort));
    gear->frontteeth = (GLushort*)malloc(4*teeth*sizeof(GLushort));
    gear->backbody   = (GLushort*)malloc((4*teeth+2)*sizeof(GLushort));
    gear->backteeth  = (GLushort*)malloc(4*teeth*sizeof(GLushort));
    gear->outer      = (GLushort*)malloc((16*teeth+2)*sizeof(GLushort));
    gear->inner      = (GLushort*)malloc((4*teeth+2)*sizeof(GLushort));

    // Set up vertices
    r0 = inner_radius;
    r1 = outer_radius - 0.5f * tooth_depth;
    r2 = outer_radius + 0.5f * tooth_depth;
    hw = 0.5f * width;
    da = (GLfloat)(0.5f * PI / teeth);
    vert = gear->vertices;
    norm = gear->normals;
    for (i=0; i<teeth; ++i) {
        GLfloat angA, angB, angC, angD;
        GLfloat cosA, cosB, cosC, cosD;
        GLfloat sinA, sinB, sinC, sinD;
        GLfloat posA0[2], posA1[2], posB2[2], posD0[2], posD1[2], posC2[2];
        GLfloat nrmA[2],  nrmAB[2], nrmBC[2], nrmCD[2], nrmD[2];

        // Compute angles used by this tooth
        angA = (GLfloat)(i * 2.0f * (float)M_PI / teeth);
        cosA = COS(angA);
        sinA = SIN(angA);
        angB = angA + da;
        cosB = COS(angB);
        sinB = SIN(angB);
        angC = angB + da;
        cosC = COS(angC);
        sinC = SIN(angC);
        angD = angC + da;
        cosD = COS(angD);
        sinD = SIN(angD);

        // Compute x/y positions of vertices for this tooth
        posA0[0] = r0 * cosA;
        posA0[1] = r0 * sinA;
        posA1[0] = r1 * cosA;
        posA1[1] = r1 * sinA;
        posB2[0] = r2 * cosB;
        posB2[1] = r2 * sinB;
        posC2[0] = r2 * cosC;
        posC2[1] = r2 * sinC;
        posD1[0] = r1 * cosD;
        posD1[1] = r1 * sinD;
        posD0[0] = r0 * cosD;
        posD0[1] = r0 * sinD;

        // Compute normals used by this tooth
        nrmA[0]  = COS(angA - 0.5f*da);
        nrmA[1]  = SIN(angA - 0.5f*da);
        nrmAB[0] = +(posB2[1] - posA1[1]);
        nrmAB[1] = -(posB2[0] - posA1[0]);
        nrmBC[0] = +(posC2[1] - posB2[1]);
        nrmBC[1] = -(posC2[0] - posB2[0]);
        nrmCD[0] = +(posD1[1] - posC2[1]);
        nrmCD[1] = -(posD1[0] - posC2[0]);
        nrmD[0]  = COS(angD + 0.5f*da);
        nrmD[1]  = SIN(angD + 0.5f*da);

        // Produce the vertices
        //   Outer face gets doubled to handle flat shading, inner doesn't.
        vert[0] = vert[3] = posA0[0];
        vert[1] = vert[4] = posA0[1];
        vert[2] = +hw;
        vert[5] = -hw;
        norm[0] = norm[3] = -posA0[0];
        norm[1] = norm[4] = -posA0[1];
        norm[2] = norm[5] = 0.0f;
        vert += 6;
        norm += 6;

        vert[0] = vert[3]  = vert[6] = vert[9]  = posA1[0];
        vert[1] = vert[4]  = vert[7] = vert[10] = posA1[1];
        vert[2] = vert[8]  = +hw;
        vert[5] = vert[11] = -hw;
        norm[0] = norm[3]  = nrmA[0];
        norm[1] = norm[4]  = nrmA[1];
        norm[6] = norm[9]  = nrmAB[0];
        norm[7] = norm[10] = nrmAB[1];
        norm[2] = norm[5]  = norm[8] = norm[11] = 0.0f;
        vert += 12;
        norm += 12;

        vert[0] = vert[3]  = vert[6] = vert[9]  = posB2[0];
        vert[1] = vert[4]  = vert[7] = vert[10] = posB2[1];
        vert[2] = vert[8]  = +hw;
        vert[5] = vert[11] = -hw;
        norm[0] = norm[3]  = nrmAB[0];
        norm[1] = norm[4]  = nrmAB[1];
        norm[6] = norm[9]  = nrmBC[0];
        norm[7] = norm[10] = nrmBC[1];
        norm[2] = norm[5]  = norm[8] = norm[11] = 0.0f;
        vert += 12;
        norm += 12;

        vert[0] = vert[3]  = vert[6] = vert[9]  = posC2[0];
        vert[1] = vert[4]  = vert[7] = vert[10] = posC2[1];
        vert[2] = vert[8]  = +hw;
        vert[5] = vert[11] = -hw;
        norm[0] = norm[3]  = nrmBC[0];
        norm[1] = norm[4]  = nrmBC[1];
        norm[6] = norm[9]  = nrmCD[0];
        norm[7] = norm[10] = nrmCD[1];
        norm[2] = norm[5]  = norm[8] = norm[11] = 0.0f;
        vert += 12;
        norm += 12;

        vert[0] = vert[3]  = vert[6] = vert[9]  = posD1[0];
        vert[1] = vert[4]  = vert[7] = vert[10] = posD1[1];
        vert[2] = vert[8]  = +hw;
        vert[5] = vert[11] = -hw;
        norm[0] = norm[3]  = nrmCD[0];
        norm[1] = norm[4]  = nrmCD[1];
        norm[6] = norm[9]  = nrmD[0];
        norm[7] = norm[10] = nrmD[1];
        norm[2] = norm[5]  = norm[8] = norm[11] = 0.0f;
        vert += 12;
        norm += 12;

        vert[0] = vert[3] = posD0[0];
        vert[1] = vert[4] = posD0[1];
        vert[2] = +hw;
        vert[5] = -hw;
        norm[0] = norm[3] = -posD0[0];
        norm[1] = norm[4] = -posD0[1];
        norm[2] = norm[5] = 0.0f;
        vert += 6;
        norm += 6;
    }

    // Build index lists for circular parts of front and back faces
    indexF = gear->frontbody;
    indexB = gear->backbody;
    for (i=0; i<teeth; i++) {
        indexF[0] = 20 * i;
        indexF[1] = 20 * i + 2;
        indexF[2] = 20 * i + 18;
        indexF[3] = 20 * i + 16;
        indexF += 4;
        indexB[0] = 20 * i + 1;
        indexB[1] = 20 * i + 3;
        indexB[2] = 20 * i + 19;
        indexB[3] = 20 * i + 17;
        indexB += 4;
    }
    indexF[0] = 0;
    indexF[1] = 2;
    indexB[0] = 1;
    indexB[1] = 3;

    // Build index lists for front and back sides of teeth
    indexF = gear->frontteeth;
    indexB = gear->backteeth;
    for (i=0; i<teeth; ++i) {
        indexF[0] = 20 * i + 2;
        indexF[1] = 20 * i + 6;
        indexF[2] = 20 * i + 10;
        indexF[3] = 20 * i + 14;
        indexF += 4;
        indexB[0] = 20 * i + 3;
        indexB[1] = 20 * i + 7;
        indexB[2] = 20 * i + 11;
        indexB[3] = 20 * i + 15;
        indexB += 4;
    }

    // Build index list for inner core
    index = gear->inner;
    for (i=0; i<teeth; i++) {
        index[0] = 20 * i;
        index[1] = 20 * i + 1;
        index[2] = 20 * i + 18;
        index[3] = 20 * i + 19;
        index += 4;
    }
    index[0] = 0;
    index[1] = 1;

    // Build index list for outsides of teeth
    index = gear->outer;
    for (i=0; i<teeth; i++) {
        index[0]  = 20 * i + 2;
        index[1]  = 20 * i + 3;
        index[2]  = 20 * i + 4;
        index[3]  = 20 * i + 5;
        index[4]  = 20 * i + 6;
        index[5]  = 20 * i + 7;
        index[6]  = 20 * i + 8;
        index[7]  = 20 * i + 9;
        index[8]  = 20 * i + 10;
        index[9]  = 20 * i + 11;
        index[10] = 20 * i + 12;
        index[11] = 20 * i + 13;
        index[12] = 20 * i + 14;
        index[13] = 20 * i + 15;
        index[14] = 20 * i + 16;
        index[15] = 20 * i + 17;
        index += 16;
    }
    index[0] = 2;
    index[1] = 3;

    return gear;
}

// Initialize a 4x4 matrix to identity
//   m <- I
static void NvGlDemoMatrixIdentity(float m[16])
{
    MEMSET(m, 0, sizeof(float) * 16);
    m[4 * 0 + 0] = m[4 * 1 + 1] = m[4 * 2 + 2] = m[4 * 3 + 3] = 1.0;
}

// Multiply the second 4x4 matrix into the first
//   m0 <- m0 * m1
static void NvGlDemoMatrixMultiply(
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

// Multiply the 3x3 matrix into the 4x4
//   m0 <- m0 * m1
static void NvGlDemoMatrixMultiply_4x4_3x3(
    float m0[16], float m1[9])
{
    int r, c, i;
    for (r = 0; r < 4; r++) {
        float m[3] = {0.0, 0.0, 0.0};
        for (c = 0; c < 3; c++) {
            for (i = 0; i < 3; i++) {
                m[c] += m0[4 * i + r] * m1[3 * c + i];
            }
        }
        for (c = 0; c < 3; c++) {
            m0[4 * c + r] = m[c];
        }
    }
}

// Apply perspective projection to a 4x4 matrix
//   m <- m * perspective(l,r,b,t,n,f)
static void NvGlDemoMatrixFrustum(
    float m[16],
    float l, float r, float b, float t, float n, float f)
{
    float m1[16];
    float rightMinusLeftInv, topMinusBottomInv, farMinusNearInv, twoNear;

    rightMinusLeftInv = 1.0f / (r - l);
    topMinusBottomInv = 1.0f / (t - b);
    farMinusNearInv = 1.0f / (f - n);
    twoNear = 2.0f * n;

    m1[ 0] = twoNear * rightMinusLeftInv;
    m1[ 1] = 0.0f;
    m1[ 2] = 0.0f;
    m1[ 3] = 0.0f;

    m1[ 4] = 0.0f;
    m1[ 5] = twoNear * topMinusBottomInv;
    m1[ 6] = 0.0f;
    m1[ 7] = 0.0f;

    m1[ 8] = (r + l) * rightMinusLeftInv;
    m1[ 9] = (t + b) * topMinusBottomInv;
    m1[10] = -(f + n) * farMinusNearInv;
    m1[11] = -1.0f;

    m1[12] = 0.0f;
    m1[13] = 0.0f;
    m1[14] = -(twoNear * f) * farMinusNearInv;
    m1[15] = 0.0f;

    NvGlDemoMatrixMultiply(m, m1);
}

// Initialize a 3x3 rotation matrix
//   m <- rotate(th,x,y,z)
static void NvGlDemoMatrixRotate_create3x3(
    float m[9],
    float theta, float x, float y, float z)
{
    float len = SQRT(x * x + y * y + z * z);
    float u0 = x / len;
    float u1 = y / len;
    float u2 = z / len;
    float rad = (float)(theta / 180 * PI);
    float c = COS(rad);
    float s = SIN(rad);
    m[3 * 0 + 0] = u0 * u0 + c * (1 - u0 * u0) + s * 0;
    m[3 * 0 + 1] = u0 * u1 + c * (0 - u0 * u1) + s * u2;
    m[3 * 0 + 2] = u0 * u2 + c * (0 - u0 * u2) - s * u1;

    m[3 * 1 + 0] = u1 * u0 + c * (0 - u1 * u0) - s * u2;
    m[3 * 1 + 1] = u1 * u1 + c * (1 - u1 * u1) + s * 0;
    m[3 * 1 + 2] = u1 * u2 + c * (0 - u1 * u2) + s * u0;

    m[3 * 2 + 0] = u2 * u0 + c * (0 - u2 * u0) + s * u1;
    m[3 * 2 + 1] = u2 * u1 + c * (0 - u2 * u1) - s * u0;
    m[3 * 2 + 2] = u2 * u2 + c * (1 - u2 * u2) + s * 0;
}

// Apply a rotation to a 4x4 matrix
//   m <- m * rotate(th,x,y,z)
static void NvGlDemoMatrixRotate(
    float m[16], float theta, float x, float y, float z)
{
    float r[9];
    NvGlDemoMatrixRotate_create3x3(r, theta, x, y, z);
    NvGlDemoMatrixMultiply_4x4_3x3(m, r);
}

// Apply translation to a 4x4 matrix
//   m <- m + translate(x,y,z)
static void NvGlDemoMatrixTranslate(
    float m[16], float x, float y, float z)
{
    float m1[16];
    NvGlDemoMatrixIdentity(m1);

    m1[4 * 3 + 0] = x;
    m1[4 * 3 + 1] = y;
    m1[4 * 3 + 2] = z;

    NvGlDemoMatrixMultiply(m, m1);
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
    if (str) {
        free(str);
        str = NULL;
    }

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
                if (str) {
                    free(str);
                    str = NULL;
                }
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

// Sets up the projection matrix for the surface size
static void gearsResize(int width, int height)
{
    GLfloat proj_mat[16];
    GLfloat aspect;
    GLuint  index;

    NvGlDemoMatrixIdentity(proj_mat);
    if (width >= height) {
        aspect = (GLfloat)width / (GLfloat)height;
        NvGlDemoMatrixFrustum(proj_mat,
                              -aspect, aspect,
                              -1.0f, 1.0f,
                              VIEW_ZNEAR, VIEW_ZFAR);
    } else {
        aspect = (GLfloat)height / (GLfloat)width;
        NvGlDemoMatrixFrustum(proj_mat,
                              -1.0f, 1.0f,
                              -aspect, aspect,
                              VIEW_ZNEAR, VIEW_ZFAR);
    }
    index = glGetUniformLocation(gearShaderProgram, "proj_mat");
    glUniformMatrix4fv(index, 1, 0, proj_mat);
}

// Draw a gear
static void drawgear(Gear *gear)
{
    int i;

    // Set up constant normals for the front and back of the gear
    static float norm_front[3] = {0.0f, 0.0f, 1.0f};
    static float norm_back[3]  = {0.0f, 0.0f, -1.0f};

    // Enable vertex pointers, and initially disable normal pointers
    glVertexAttribPointer(pos_index, 3, GL_FLOAT, 0, 0, gear->vertices);
    glVertexAttribPointer(nrm_index, 3, GL_FLOAT, 0, 0, gear->normals);
    glEnableVertexAttribArray(pos_index);
    glDisableVertexAttribArray(nrm_index);

    // Set the constant normal for front side
    glVertexAttrib3fv(nrm_index, norm_front);

    // Draw circular part of front side
    glDrawElements(GL_TRIANGLE_STRIP, 4*gear->teeth + 2,
                   GL_UNSIGNED_SHORT, gear->frontbody);

    // Draw front side teeth
    for (i=0; i<gear->teeth; i++) {
        glDrawElements(GL_TRIANGLE_FAN, 4,
                       GL_UNSIGNED_SHORT, &gear->frontteeth[4*i]);
    }

    // Set the constant normal for back side
    glVertexAttrib3fv(nrm_index, norm_back);

    // Draw circular part of front side
    glDrawElements(GL_TRIANGLE_STRIP, 4*gear->teeth + 2,
                   GL_UNSIGNED_SHORT, gear->backbody);

    // Draw back side teeth
    for (i = 0; i < gear->teeth; i++) {
        glDrawElements(GL_TRIANGLE_FAN, 4,
                       GL_UNSIGNED_SHORT, &gear->backteeth[4*i]);
    }

    // Enable normal pointers for the inner and outer faces
    glEnableVertexAttribArray(nrm_index);

    // Draw outer faces of teeth
    glDrawElements(GL_TRIANGLE_STRIP, 16*gear->teeth + 2,
                   GL_UNSIGNED_SHORT, gear->outer);

    // Draw inside radius cylinder
    glDrawElements(GL_TRIANGLE_STRIP, 4*gear->teeth + 2,
                   GL_UNSIGNED_SHORT, gear->inner);
}

// Draw a frame
static void gearsRender(GLfloat angle)
{
    static GLfloat red  [3] = {0.8f, 0.1f, 0.0f};
    static GLfloat green[3] = {0.0f, 0.8f, 0.2f};
    static GLfloat blue [3] = {0.2f, 0.2f, 1.0f};
    GLfloat mview_mat[16];

    // Clear the buffers
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Make sure gear shader program is current
    glUseProgram(gearShaderProgram);

    // Transform, color, and draw gear 1
    MEMCPY(mview_mat, gear1_mat, 16*sizeof(float));
    NvGlDemoMatrixRotate(mview_mat, angle, 0.0f, 0.0f, 1.0f);
    glUniformMatrix4fv(mview_mat_index, 1, 0, mview_mat);
    glUniform3fv(material_index, 1, red);
    drawgear(gear1);

    // Transform, color, and draw gear 2
    MEMCPY(mview_mat, gear2_mat, 16*sizeof(float));
    NvGlDemoMatrixRotate(mview_mat, -2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glUniformMatrix4fv(mview_mat_index, 1, 0, mview_mat);
    glUniform3fv(material_index, 1, green);
    drawgear(gear2);

    // Transform, color, and draw gear 3
    MEMCPY(mview_mat, gear3_mat, 16*sizeof(float));
    NvGlDemoMatrixRotate(mview_mat, -2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glUniformMatrix4fv(mview_mat_index, 1, 0, mview_mat);
    glUniform3fv(material_index, 1, blue);
    drawgear(gear3);
}

// Top level initialization of gears library
static int gearsInit(int width, int height)
{
    // Scene constants
    const GLfloat light_pos[4] = {1.0f, 3.0f, 5.0f, 0.0f};

    GLuint  index;
    GLfloat scene_mat[16];
    GLfloat light_norm, light_dir[4];

    glClearColor(0.10f, 0.20f, 0.15f, 1.0f);

    // Create a shader program
    gearShaderProgram = loadShaderSrcStrings(vshader,
                                             sizeof(vshader),
                                             fshader,
                                             sizeof(fshader),
                                             GL_TRUE,
                                             GL_FALSE);

    // Use the program we just loaded
    if (!gearShaderProgram) return 0;
    glUseProgram(gearShaderProgram);

    // Initialize projection matrix
    gearsResize(width, height);

    // Using a directional light, so find the normalized vector and load
    light_norm = (GLfloat)(ISQRT(light_pos[0]*light_pos[0]
                                +light_pos[1]*light_pos[1]
                                +light_pos[2]*light_pos[2]
                                +light_pos[3]*light_pos[3]));
    light_dir[0] = light_pos[0] * light_norm;
    light_dir[1] = light_pos[1] * light_norm;
    light_dir[2] = light_pos[2] * light_norm;
    light_dir[3] = light_pos[3] * light_norm;
    index = glGetUniformLocation(gearShaderProgram, "light_dir");
    glUniform3fv(index, 1, light_dir);

    // Get indices for uniforms and attributes updated each frame
    mview_mat_index = glGetUniformLocation(gearShaderProgram, "mview_mat");
    material_index  = glGetUniformLocation(gearShaderProgram, "material");
    pos_index       = glGetAttribLocation(gearShaderProgram, "pos_attr");
    nrm_index       = glGetAttribLocation(gearShaderProgram, "nrm_attr");

    // Create gear data
    gear1 = makegear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    gear2 = makegear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    gear3 = makegear(1.3f, 2.0f, 0.5f, 10, 0.7f);

    // Set up the global scene matrix
    NvGlDemoMatrixIdentity(scene_mat);
    NvGlDemoMatrixTranslate(scene_mat, 0.0f, 0.0f, -VIEW_ZGEAR);
    NvGlDemoMatrixRotate(scene_mat, VIEW_ROTX, 1.0f, 0.0f, 0.0f);
    NvGlDemoMatrixRotate(scene_mat, VIEW_ROTY, 0.0f, 1.0f, 0.0f);
    NvGlDemoMatrixRotate(scene_mat, VIEW_ROTZ, 0.0f, 0.0f, 1.0f);

    // Set up the individual gear matrices
    MEMCPY(gear1_mat, scene_mat, 16*sizeof(GLfloat));
    NvGlDemoMatrixTranslate(gear1_mat, -3.0f, -2.0f, 0.0f);
    MEMCPY(gear2_mat, scene_mat, 16*sizeof(GLfloat));
    NvGlDemoMatrixTranslate(gear2_mat,  3.1f, -2.0f, 0.0f);
    MEMCPY(gear3_mat, scene_mat, 16*sizeof(GLfloat));
    NvGlDemoMatrixTranslate(gear3_mat, -3.1f,  4.2f, 0.0f);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);

    return 1;
}

// Free a gear structure
static void freegear(Gear *gear)
{
    FREE(gear->inner);
    FREE(gear->outer);
    FREE(gear->backteeth);
    FREE(gear->backbody);
    FREE(gear->frontteeth);
    FREE(gear->frontbody);
    FREE(gear->normals);
    FREE(gear->vertices);
    FREE(gear);
}

// Clean up graphics objects
static void gearsTerm(void)
{
    if (gearShaderProgram) { glDeleteProgram(gearShaderProgram); }
    if (gear1) freegear(gear1);
    if (gear2) freegear(gear2);
    if (gear3) freegear(gear3);
}

EXTENSION_LIST(EXTLST_EXTERN)
static int Init(gearsstate_t *state)
{
    static const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE,        EGL_STREAM_BIT_KHR,
        EGL_RENDERABLE_TYPE,     EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,            1,
        EGL_GREEN_SIZE,          1,
        EGL_BLUE_SIZE,           1,
        EGL_DEPTH_SIZE,          16,
        EGL_SAMPLE_BUFFERS,      0,
        EGL_SAMPLES,             0,
        EGL_NONE
    };
    static const EGLint ctxAttrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    static const EGLint srfAttrs[] = {
        EGL_WIDTH,               GEARS_WIDTH,
        EGL_HEIGHT,              GEARS_HEIGHT,
        EGL_NONE
    };
    EGLint       count;
    EGLBoolean   status;

    // Obtain a matching config
    status = eglChooseConfig(state->display, cfgAttrs,
                             &state->config, 1, &count);
    if (!status || !count) {
        LOG_ERR("Couldn't obtain config for gears rendering\n");
        return 0;
    }

    // Create EGL context for rendering gears
    state->context = eglCreateContext(state->display, state->config,
                                      NULL, ctxAttrs);
    if (state->context == EGL_NO_CONTEXT) {
        LOG_ERR("Couldn't obtain context for gears rendering\n");
        return 0;
    }

    // Create EGL surface from stream
    state->surface = eglCreateStreamProducerSurfaceKHR(
                        state->display, state->config, state->stream, srfAttrs);
    if (state->surface == EGL_NO_SURFACE) {
        LOG_ERR("Couldn't obtain surface for gears rendering\n");
        return 0;
    }

    if (!eglMakeCurrent(state->display,
                   state->surface, state->surface,
                   state->context)) {
        LOG_ERR("Couldn't make gears context current.\n");
        return 0;
    }

    // Set the viewport to occupy the full render surface
    glViewport(0, 0, GEARS_WIDTH, GEARS_HEIGHT);

    // Initialize the resources needed for gears rendering
    if (!gearsInit(GEARS_WIDTH, GEARS_HEIGHT)) {
        return 0;
    }

    CHECK_GL_ERROR();

    return 1;
}
#endif

static void Deinit(gearsstate_t *state)
{
    if (state->display != EGL_NO_DISPLAY) {

        // Make the gears context current
        if (state->context != EGL_NO_CONTEXT) {
            if (!eglMakeCurrent(state->display,
                            state->surface, state->surface,
                            state->context))
                LOG_ERR("Couldn't make gears context current.\n");
        }

        // Clean up the gears resources
        gearsTerm();

        // Clear the context/surface
        if (!eglMakeCurrent(state->display,
                        EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT))
            LOG_ERR("Error clearing gears surfaces/context.\n");

        // Delete the context
        if (state->context != EGL_NO_CONTEXT) {
            if (!eglDestroyContext(state->display, state->context))
                LOG_ERR("Error destroying gears EGL context.\n");
            state->context = EGL_NO_CONTEXT;
        }

        // Delete the surface
        if (state->surface != EGL_NO_SURFACE) {
            if (!eglDestroySurface(state->display, state->surface))
                LOG_ERR("Error destroying gears EGL surface.\n");
            state->surface = EGL_NO_SURFACE;
        }

        state->display = EGL_NO_DISPLAY;
    }
}


static int gearProducer(gearsstate_t *state)
{
    gearsRender(state->angle++);
    eglSwapBuffers(state->display, state->surface);

    return 1;
}

static NvU32 gearProducerThread(void *parserArg)
{
    gearsstate_t *state = (gearsstate_t *)parserArg;
    EGLint streamState = 0;
    //fixed, 1 loop corresponding to 300 frames
    int frameNum = state->loop * 300;
    int frameCounter = 0;

    LOG_DBG("gearProducerThread: Init\n");
    if (!Init(state)) {
        LOG_ERR("Init failed\n");
        *state->finishedFlag = 1;
        return 0;
    }

    LOG_DBG("gearProducerThread: gearProducer\n");

    while (gearProducer(state) && !signal_stop) {
        if(!eglQueryStreamKHR(
                state->display,
                state->stream,
                EGL_STREAM_STATE_KHR,
                &streamState)) {
            LOG_ERR("Nvmedia video consumer, eglQueryStreamKHR EGL_STREAM_STATE_KHR failed\n");
        }

        if(streamState == EGL_STREAM_STATE_DISCONNECTED_KHR) {
            break;
        }
        frameCounter++;
        if(frameCounter > frameNum)
            break;
    }

    // Signal end of decode
    *state->finishedFlag = 1;
    return 0;
}

#if defined(EGL_KHR_stream)
int GearProducerInit(volatile NvBool *producerFinished,
        EGLDisplay eglDisplay,
        EGLStreamKHR eglStream,
        TestArgs *args)
{
    GLTestArgs *testArgs = &gl_testArgs;
    gearsstate_t *state = &(testArgs->g_state);

    state->loop         = args->prodLoop? args->prodLoop : 1;
    // Set parser default parameters
    state->display      = eglDisplay;
    state->stream       = eglStream;
    state->context      = EGL_NO_CONTEXT;
    state->surface      = EGL_NO_SURFACE;
    state->angle        = 0;
    state->finishedFlag = producerFinished;

    // Create gear producer thread
    if(IsFailed(NvThreadCreate(&testArgs->thread, &gearProducerThread, (void *)state, NV_THREAD_PRIORITY_NORMAL))) {
          LOG_ERR("gearProducerInit: Unable to create gear producer thread\n");
          return 0;
    }

    return 1;
}
#endif

void GearProducerStop(void) {
    GLTestArgs *testArgs = &gl_testArgs;

    if(testArgs->thread) {
       LOG_DBG("wait for gearproducer thread exit\n");
       NvThreadDestroy(testArgs->thread);
    }
}

void GearProducerDeinit(void) {
    GLTestArgs *testArgs = &gl_testArgs;
    Deinit(&testArgs->g_state);
}
