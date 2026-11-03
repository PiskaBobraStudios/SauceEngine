#include "model_lightmap_l.h"
#include "bsp_l.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {
constexpr unsigned int GL_TEXTURE0_ML = 0x84C0;
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
constexpr unsigned int GL_TEXTURE1_ML = 0x84C1;
constexpr unsigned int GL_TEXTURE_3D_ML = 0x806F;
constexpr unsigned int GL_TEXTURE_BINDING_3D_ML = 0x806A;
constexpr unsigned int GL_CURRENT_PROGRAM_ML = 0x8B8D;
constexpr unsigned int GL_VERTEX_SHADER_ML = 0x8B31;
constexpr unsigned int GL_FRAGMENT_SHADER_ML = 0x8B30;
constexpr unsigned int GL_COMPILE_STATUS_ML = 0x8B81;
constexpr unsigned int GL_LINK_STATUS_ML = 0x8B82;

using GLenumML = unsigned int;
using GLuintML = unsigned int;
using GLintML = int;
using GLsizeiML = int;
using GLcharML = char;
using GLfloatML = float;
using GLbooleanML = unsigned char;

#if defined(_WIN32) && defined(_M_IX86)
#define MODEL_LIGHTMAP_GLAPIENTRY __stdcall
#else
#define MODEL_LIGHTMAP_GLAPIENTRY
#endif

using PFNGLUSEPROGRAMPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML);
using PFNGLCREATESHADERPROC_ML = GLuintML (MODEL_LIGHTMAP_GLAPIENTRY *)(GLenumML);
using PFNGLSHADERSOURCEPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, GLsizeiML, const GLcharML* const*, const GLintML*);
using PFNGLCOMPILESHADERPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML);
using PFNGLGETSHADERIVPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, GLenumML, GLintML*);
using PFNGLGETSHADERINFOLOGPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, GLsizeiML, GLsizeiML*, GLcharML*);
using PFNGLDELETESHADERPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML);
using PFNGLCREATEPROGRAMPROC_ML = GLuintML (MODEL_LIGHTMAP_GLAPIENTRY *)();
using PFNGLATTACHSHADERPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, GLuintML);
using PFNGLLINKPROGRAMPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML);
using PFNGLGETPROGRAMIVPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, GLenumML, GLintML*);
using PFNGLGETPROGRAMINFOLOGPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, GLsizeiML, GLsizeiML*, GLcharML*);
using PFNGLDELETEPROGRAMPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML);
using PFNGLGETUNIFORMLOCATIONPROC_ML = GLintML (MODEL_LIGHTMAP_GLAPIENTRY *)(GLuintML, const GLcharML*);
using PFNGLUNIFORM1IPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLintML, GLintML);
using PFNGLUNIFORM3FPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLintML, GLfloatML, GLfloatML, GLfloatML);
using PFNGLUNIFORMMATRIX4FVPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLintML, GLsizeiML, GLbooleanML, const GLfloatML*);
using PFNGLACTIVETEXTUREPROC_ML = void (MODEL_LIGHTMAP_GLAPIENTRY *)(GLenumML);

struct ModelLightmapState {
    PFNGLUSEPROGRAMPROC_ML UseProgram = nullptr;
    PFNGLCREATESHADERPROC_ML CreateShader = nullptr;
    PFNGLSHADERSOURCEPROC_ML ShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC_ML CompileShader = nullptr;
    PFNGLGETSHADERIVPROC_ML GetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC_ML GetShaderInfoLog = nullptr;
    PFNGLDELETESHADERPROC_ML DeleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC_ML CreateProgram = nullptr;
    PFNGLATTACHSHADERPROC_ML AttachShader = nullptr;
    PFNGLLINKPROGRAMPROC_ML LinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC_ML GetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC_ML GetProgramInfoLog = nullptr;
    PFNGLDELETEPROGRAMPROC_ML DeleteProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC_ML GetUniformLocation = nullptr;
    PFNGLUNIFORM1IPROC_ML Uniform1i = nullptr;
    PFNGLUNIFORM3FPROC_ML Uniform3f = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC_ML UniformMatrix4fv = nullptr;
    PFNGLACTIVETEXTUREPROC_ML ActiveTexture = nullptr;
    GLuintML program = 0;
    GLintML inverseViewLoc = -1;
    GLintML volumeMinLoc = -1;
    GLintML volumeExtentLoc = -1;
    GLintML baseTextureLoc = -1;
    GLintML lightVolumeLoc = -1;
    GLintML previousProgram = 0;
    GLintML previousActiveTexture = static_cast<GLintML>(GL_TEXTURE0_ML);
    GLintML previousVolumeBinding = 0;
    bool initialized = false;
    bool active = false;
    GLfloatML inverseView[16]{};
    Vec3 volumeMin{0,0,0};
    Vec3 volumeExtent{1,1,1};
};

ModelLightmapState& modelLightmapState() {
    static ModelLightmapState state;
    return state;
}

template <typename T>
T loadModelLightmapProc(const char* name) {
    return reinterpret_cast<T>(glfwGetProcAddress(name));
}

bool invertMatrix(const GLfloatML* source, GLfloatML* destination) {
    double a[4][8]{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) a[row][col] = source[col * 4 + row];
        a[row][row + 4] = 1.0;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row) {
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        }
        if (std::fabs(a[pivot][col]) < 1.0e-12) return false;
        if (pivot != col) for (int k = 0; k < 8; ++k) std::swap(a[pivot][k], a[col][k]);
        const double divisor = a[col][col];
        for (int k = 0; k < 8; ++k) a[col][k] /= divisor;
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            const double factor = a[row][col];
            for (int k = 0; k < 8; ++k) a[row][k] -= factor * a[col][k];
        }
    }
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) destination[col * 4 + row] = static_cast<GLfloatML>(a[row][col + 4]);
    }
    return true;
}

GLuintML compileModelLightmapShader(ModelLightmapState& state, GLenumML type, const char* source) {
    const GLuintML shader = state.CreateShader(type);
    if (!shader) return 0;
    const GLintML length = static_cast<GLintML>(std::strlen(source));
    state.ShaderSource(shader, 1, &source, &length);
    state.CompileShader(shader);
    GLintML compiled = 0;
    state.GetShaderiv(shader, GL_COMPILE_STATUS_ML, &compiled);
    if (!compiled) {
        char log[2048]{};
        GLsizeiML written = 0;
        if (state.GetShaderInfoLog) state.GetShaderInfoLog(shader, static_cast<GLsizeiML>(sizeof(log) - 1), &written, log);
        if (written > 0) std::cout << "[ModelLightmap] Shader compile failed: " << log << std::endl;
        state.DeleteShader(shader);
        return 0;
    }
    return shader;
}

bool initializeModelLightmap(ModelLightmapState& state) {
    if (state.initialized) return state.program != 0 && state.inverseViewLoc >= 0 && state.volumeMinLoc >= 0 && state.volumeExtentLoc >= 0 && state.baseTextureLoc >= 0 && state.lightVolumeLoc >= 0;
    state.initialized = true;
    state.UseProgram = loadModelLightmapProc<PFNGLUSEPROGRAMPROC_ML>("glUseProgram");
    state.CreateShader = loadModelLightmapProc<PFNGLCREATESHADERPROC_ML>("glCreateShader");
    state.ShaderSource = loadModelLightmapProc<PFNGLSHADERSOURCEPROC_ML>("glShaderSource");
    state.CompileShader = loadModelLightmapProc<PFNGLCOMPILESHADERPROC_ML>("glCompileShader");
    state.GetShaderiv = loadModelLightmapProc<PFNGLGETSHADERIVPROC_ML>("glGetShaderiv");
    state.GetShaderInfoLog = loadModelLightmapProc<PFNGLGETSHADERINFOLOGPROC_ML>("glGetShaderInfoLog");
    state.DeleteShader = loadModelLightmapProc<PFNGLDELETESHADERPROC_ML>("glDeleteShader");
    state.CreateProgram = loadModelLightmapProc<PFNGLCREATEPROGRAMPROC_ML>("glCreateProgram");
    state.AttachShader = loadModelLightmapProc<PFNGLATTACHSHADERPROC_ML>("glAttachShader");
    state.LinkProgram = loadModelLightmapProc<PFNGLLINKPROGRAMPROC_ML>("glLinkProgram");
    state.GetProgramiv = loadModelLightmapProc<PFNGLGETPROGRAMIVPROC_ML>("glGetProgramiv");
    state.GetProgramInfoLog = loadModelLightmapProc<PFNGLGETPROGRAMINFOLOGPROC_ML>("glGetProgramInfoLog");
    state.DeleteProgram = loadModelLightmapProc<PFNGLDELETEPROGRAMPROC_ML>("glDeleteProgram");
    state.GetUniformLocation = loadModelLightmapProc<PFNGLGETUNIFORMLOCATIONPROC_ML>("glGetUniformLocation");
    state.Uniform1i = loadModelLightmapProc<PFNGLUNIFORM1IPROC_ML>("glUniform1i");
    state.Uniform3f = loadModelLightmapProc<PFNGLUNIFORM3FPROC_ML>("glUniform3f");
    state.UniformMatrix4fv = loadModelLightmapProc<PFNGLUNIFORMMATRIX4FVPROC_ML>("glUniformMatrix4fv");
    state.ActiveTexture = loadModelLightmapProc<PFNGLACTIVETEXTUREPROC_ML>("glActiveTexture");
    if (!state.UseProgram || !state.CreateShader || !state.ShaderSource || !state.CompileShader ||
        !state.GetShaderiv || !state.DeleteShader || !state.CreateProgram || !state.AttachShader ||
        !state.LinkProgram || !state.GetProgramiv || !state.DeleteProgram || !state.GetUniformLocation ||
        !state.Uniform1i || !state.Uniform3f || !state.UniformMatrix4fv || !state.ActiveTexture) return false;

    static const char* vertexSource =
        "#version 120\n"
        "uniform mat4 uInverseView;\n"
        "varying vec3 vWorldPosition;\n"
        "void main(){\n"
        "  vec4 eyePosition=gl_ModelViewMatrix*gl_Vertex;\n"
        "  vec4 worldPosition=uInverseView*eyePosition;\n"
        "  vWorldPosition=worldPosition.xyz/max(abs(worldPosition.w),0.000001);\n"
        "  gl_Position=gl_ProjectionMatrix*eyePosition;\n"
        "  gl_TexCoord[0]=gl_MultiTexCoord0;\n"
        "  gl_FrontColor=gl_Color;\n"
        "  gl_BackColor=gl_Color;\n"
        "}\n";
    static const char* fragmentSource =
        "#version 120\n"
        "uniform sampler2D uBaseTexture;\n"
        "uniform sampler3D uLightVolume;\n"
        "uniform vec3 uVolumeMin;\n"
        "uniform vec3 uVolumeExtent;\n"
        "varying vec3 vWorldPosition;\n"
        "void main(){\n"
        "  vec4 baseColor=texture2D(uBaseTexture,gl_TexCoord[0].xy)*gl_Color;\n"
        "  vec3 uvw=(vWorldPosition-uVolumeMin)/uVolumeExtent;\n"
        "  vec3 lightColor=vec3(1.0);\n"
        "  if(all(greaterThanEqual(uvw,vec3(0.0)))&&all(lessThanEqual(uvw,vec3(1.0))))\n"
        "    lightColor=texture3D(uLightVolume,uvw).rgb;\n"
        "  gl_FragColor=vec4(baseColor.rgb*lightColor,baseColor.a);\n"
        "}\n";

    const GLuintML vertex = compileModelLightmapShader(state, GL_VERTEX_SHADER_ML, vertexSource);
    if (!vertex) return false;
    const GLuintML fragment = compileModelLightmapShader(state, GL_FRAGMENT_SHADER_ML, fragmentSource);
    if (!fragment) {
        state.DeleteShader(vertex);
        return false;
    }
    state.program = state.CreateProgram();
    if (!state.program) {
        state.DeleteShader(vertex);
        state.DeleteShader(fragment);
        return false;
    }
    state.AttachShader(state.program, vertex);
    state.AttachShader(state.program, fragment);
    state.LinkProgram(state.program);
    state.DeleteShader(vertex);
    state.DeleteShader(fragment);
    GLintML linked = 0;
    state.GetProgramiv(state.program, GL_LINK_STATUS_ML, &linked);
    if (!linked) {
        char log[2048]{};
        GLsizeiML written = 0;
        if (state.GetProgramInfoLog) state.GetProgramInfoLog(state.program, static_cast<GLsizeiML>(sizeof(log) - 1), &written, log);
        if (written > 0) std::cout << "[ModelLightmap] Program link failed: " << log << std::endl;
        state.DeleteProgram(state.program);
        state.program = 0;
        return false;
    }
    state.inverseViewLoc = state.GetUniformLocation(state.program, "uInverseView");
    state.volumeMinLoc = state.GetUniformLocation(state.program, "uVolumeMin");
    state.volumeExtentLoc = state.GetUniformLocation(state.program, "uVolumeExtent");
    state.baseTextureLoc = state.GetUniformLocation(state.program, "uBaseTexture");
    state.lightVolumeLoc = state.GetUniformLocation(state.program, "uLightVolume");
    return state.inverseViewLoc >= 0 && state.volumeMinLoc >= 0 && state.volumeExtentLoc >= 0 &&
           state.baseTextureLoc >= 0 && state.lightVolumeLoc >= 0;
}
}

bool ModelLightmap_L::Begin(const BSP_L& map, bool enabled) {
    ModelLightmapState& state = modelLightmapState();
    if (state.active) End();
    if (!enabled || !map.modelLightmapVolumeTexture || !initializeModelLightmap(state)) return false;
    GLfloatML viewMatrix[16]{};
    glGetFloatv(GL_MODELVIEW_MATRIX, viewMatrix);
    if (!invertMatrix(viewMatrix, state.inverseView)) return false;
    state.volumeMin = map.modelLightmapVolumeMin;
    state.volumeExtent = map.modelLightmapVolumeMax - map.modelLightmapVolumeMin;
    state.volumeExtent.x = std::max(0.001f, state.volumeExtent.x);
    state.volumeExtent.y = std::max(0.001f, state.volumeExtent.y);
    state.volumeExtent.z = std::max(0.001f, state.volumeExtent.z);
    glGetIntegerv(GL_CURRENT_PROGRAM_ML, &state.previousProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.previousActiveTexture);
    state.ActiveTexture(GL_TEXTURE1_ML);
    glGetIntegerv(GL_TEXTURE_BINDING_3D_ML, &state.previousVolumeBinding);
    glBindTexture(GL_TEXTURE_3D_ML, map.modelLightmapVolumeTexture);
    state.ActiveTexture(GL_TEXTURE0_ML);
    state.UseProgram(state.program);
    state.UniformMatrix4fv(state.inverseViewLoc, 1, 0, state.inverseView);
    state.Uniform3f(state.volumeMinLoc, state.volumeMin.x, state.volumeMin.y, state.volumeMin.z);
    state.Uniform3f(state.volumeExtentLoc, state.volumeExtent.x, state.volumeExtent.y, state.volumeExtent.z);
    state.Uniform1i(state.baseTextureLoc, 0);
    state.Uniform1i(state.lightVolumeLoc, 1);
    state.active = true;
    return true;
}

void ModelLightmap_L::End() {
    ModelLightmapState& state = modelLightmapState();
    if (!state.active) return;
    state.UseProgram(static_cast<GLuintML>(std::max(0, state.previousProgram)));
    state.ActiveTexture(GL_TEXTURE1_ML);
    glBindTexture(GL_TEXTURE_3D_ML, static_cast<GLuintML>(std::max(0, state.previousVolumeBinding)));
    state.ActiveTexture(static_cast<GLenumML>(state.previousActiveTexture));
    state.active = false;
}
