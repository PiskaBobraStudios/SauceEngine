#include "postprocess_l.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

static constexpr unsigned int GL_FRAMEBUFFER_X = 0x8D40;
constexpr unsigned int GL_COLOR_ATTACHMENT0_X = 0x8CE0;
constexpr unsigned int GL_DEPTH_ATTACHMENT_X = 0x8D00;
constexpr unsigned int GL_FRAMEBUFFER_COMPLETE_X = 0x8CD5;
constexpr unsigned int GL_RGBA16F_X = 0x881A;
constexpr unsigned int GL_RGBA_X = 0x1908;
constexpr unsigned int GL_DEPTH_COMPONENT24_X = 0x81A6;
constexpr unsigned int GL_TEXTURE0_X = 0x84C0;
constexpr unsigned int GL_TEXTURE1_X = 0x84C1;
constexpr unsigned int GL_CLAMP_TO_EDGE_X = 0x812F;
constexpr unsigned int GL_FRAGMENT_SHADER_X = 0x8B30;
constexpr unsigned int GL_VERTEX_SHADER_X = 0x8B31;
constexpr unsigned int GL_COMPILE_STATUS_X = 0x8B81;
constexpr unsigned int GL_LINK_STATUS_X = 0x8B82;

using GLenumX = unsigned int;
using GLuintX = unsigned int;
using GLintX = int;
using GLsizeiX = int;
using GLcharX = char;
using GLfloatX = float;
using GLbooleanX = unsigned char;
using GLbitfieldX = unsigned int;

#if defined(_WIN32) && defined(_M_IX86)
#define POSTFX_GLAPIENTRY __stdcall
#else
#define POSTFX_GLAPIENTRY
#endif
using PFNGLGENFRAMEBUFFERSPROC_X = void (POSTFX_GLAPIENTRY *)(GLsizeiX, GLuintX*);
using PFNGLDELETEFRAMEBUFFERSPROC_X = void (POSTFX_GLAPIENTRY *)(GLsizeiX, const GLuintX*);
using PFNGLBINDFRAMEBUFFERPROC_X = void (POSTFX_GLAPIENTRY *)(GLenumX, GLuintX);
using PFNGLFRAMEBUFFERTEXTURE2DPROC_X = void (POSTFX_GLAPIENTRY *)(GLenumX, GLenumX, GLenumX, GLuintX, GLintX);
using PFNGLCHECKFRAMEBUFFERSTATUSPROC_X = GLenumX (POSTFX_GLAPIENTRY *)(GLenumX);
using PFNGLGENBUFFERSPROC_X = void (POSTFX_GLAPIENTRY *)(GLsizeiX, GLuintX*);
using PFNGLDELETEBUFFERSPROC_X = void (POSTFX_GLAPIENTRY *)(GLsizeiX, const GLuintX*);
using PFNGLUSEPROGRAMPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX);
using PFNGLCREATESHADERPROC_X = GLuintX (POSTFX_GLAPIENTRY *)(GLenumX);
using PFNGLSHADERSOURCEPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX, GLsizeiX, const GLcharX* const*, const GLintX*);
using PFNGLCOMPILESHADERPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX);
using PFNGLGETSHADERIVPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX, GLenumX, GLintX*);
using PFNGLGETSHADERINFOLOGPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX, GLsizeiX, GLsizeiX*, GLcharX*);
using PFNGLDELETESHADERPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX);
using PFNGLCREATEPROGRAMPROC_X = GLuintX (POSTFX_GLAPIENTRY *)();
using PFNGLATTACHSHADERPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX, GLuintX);
using PFNGLLINKPROGRAMPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX);
using PFNGLGETPROGRAMIVPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX, GLenumX, GLintX*);
using PFNGLGETPROGRAMINFOLOGPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX, GLsizeiX, GLsizeiX*, GLcharX*);
using PFNGLDELETEPROGRAMPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX);
using PFNGLGETUNIFORMLOCATIONPROC_X = GLintX (POSTFX_GLAPIENTRY *)(GLuintX, const GLcharX*);
using PFNGLUNIFORM1IPROC_X = void (POSTFX_GLAPIENTRY *)(GLintX, GLintX);
using PFNGLUNIFORM1FPROC_X = void (POSTFX_GLAPIENTRY *)(GLintX, GLfloatX);
using PFNGLUNIFORM2FPROC_X = void (POSTFX_GLAPIENTRY *)(GLintX, GLfloatX, GLfloatX);
using PFNGLACTIVETEXTUREPROC_X = void (POSTFX_GLAPIENTRY *)(GLenumX);
using PFNGLGENVERTEXARRAYSPROC_X = void (POSTFX_GLAPIENTRY *)(GLsizeiX, GLuintX*);
using PFNGLBINDVERTEXARRAYPROC_X = void (POSTFX_GLAPIENTRY *)(GLuintX);
using PFNGLDELETEVERTEXARRAYSPROC_X = void (POSTFX_GLAPIENTRY *)(GLsizeiX, const GLuintX*);

template <typename T>
T loadProc(const char* name) {
    return reinterpret_cast<T>(glfwGetProcAddress(name));
}

template <typename T>
T loadProcAny(const char* a, const char* b) {
    T p = loadProc<T>(a);
    return p ? p : loadProc<T>(b);
}

struct GLFns {
    PFNGLGENFRAMEBUFFERSPROC_X GenFramebuffers = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC_X DeleteFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC_X BindFramebuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC_X FramebufferTexture2D = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC_X CheckFramebufferStatus = nullptr;

    PFNGLUSEPROGRAMPROC_X UseProgram = nullptr;
    PFNGLCREATESHADERPROC_X CreateShader = nullptr;
    PFNGLSHADERSOURCEPROC_X ShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC_X CompileShader = nullptr;
    PFNGLGETSHADERIVPROC_X GetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC_X GetShaderInfoLog = nullptr;
    PFNGLDELETESHADERPROC_X DeleteShader = nullptr;
    PFNGLCREATEPROGRAMPROC_X CreateProgram = nullptr;
    PFNGLATTACHSHADERPROC_X AttachShader = nullptr;
    PFNGLLINKPROGRAMPROC_X LinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC_X GetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC_X GetProgramInfoLog = nullptr;
    PFNGLDELETEPROGRAMPROC_X DeleteProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC_X GetUniformLocation = nullptr;
    PFNGLUNIFORM1IPROC_X Uniform1i = nullptr;
    PFNGLUNIFORM1FPROC_X Uniform1f = nullptr;
    PFNGLUNIFORM2FPROC_X Uniform2f = nullptr;
    PFNGLACTIVETEXTUREPROC_X ActiveTexture = nullptr;
};

struct PostProcess_L::Impl {
    GLFns gl;

    GLuintX sceneFbo = 0;
    GLuintX sceneTex = 0;
    GLuintX depthTex = 0;
    GLuintX bloomFbo = 0;
    GLuintX bloomA = 0;
    GLuintX bloomB = 0;

    GLuintX compositeProgram = 0;
    GLuintX brightProgram = 0;
    GLuintX blurProgram = 0;

    GLintX compositeSceneLoc = -1;
    GLintX compositeBloomLoc = -1;
    GLintX compositeExposureLoc = -1;
    GLintX compositeBloomStrengthLoc = -1;
    GLintX compositeSceneSizeLoc = -1;

    GLintX brightSceneLoc = -1;
    GLintX brightTexelLoc = -1;
    GLintX brightThresholdLoc = -1;
    GLintX brightKneeLoc = -1;

    GLintX blurTexLoc = -1;
    GLintX blurTexelLoc = -1;
    GLintX blurDirectionLoc = -1;

    int width = 0;
    int height = 0;
    int bloomWidth = 0;
    int bloomHeight = 0;
    bool hdr = false;

    GLuintX sceneInternalFormat = GL_RGBA_X;

    void destroyTextures() {
        if (sceneTex) glDeleteTextures(1, &sceneTex);
        if (depthTex) glDeleteTextures(1, &depthTex);
        if (bloomA) glDeleteTextures(1, &bloomA);
        if (bloomB) glDeleteTextures(1, &bloomB);
        sceneTex = depthTex = bloomA = bloomB = 0;
    }

    void destroyTargets() {
        destroyTextures();
        if (sceneFbo && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &sceneFbo);
        if (bloomFbo && gl.DeleteFramebuffers) gl.DeleteFramebuffers(1, &bloomFbo);
        sceneFbo = bloomFbo = 0;
        width = height = bloomWidth = bloomHeight = 0;
    }

    void destroyPrograms() {
        if (compositeProgram && gl.DeleteProgram) gl.DeleteProgram(compositeProgram);
        if (brightProgram && gl.DeleteProgram) gl.DeleteProgram(brightProgram);
        if (blurProgram && gl.DeleteProgram) gl.DeleteProgram(blurProgram);
        compositeProgram = brightProgram = blurProgram = 0;
    }
};

static GLuintX compileShader(PostProcess_L::Impl& x, GLenumX type, const char* source) {
    GLuintX shader = x.gl.CreateShader(type);
    if (!shader) return 0;
    const GLintX len = (GLintX)std::strlen(source);
    x.gl.ShaderSource(shader, 1, &source, &len);
    x.gl.CompileShader(shader);

    GLintX ok = 0;
    x.gl.GetShaderiv(shader, GL_COMPILE_STATUS_X, &ok);
    if (!ok) {
        x.gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuintX makeProgram(PostProcess_L::Impl& x, const char* vs, const char* fs) {
    const GLuintX v = compileShader(x, GL_VERTEX_SHADER_X, vs);
    if (!v) return 0;
    const GLuintX f = compileShader(x, GL_FRAGMENT_SHADER_X, fs);
    if (!f) {
        x.gl.DeleteShader(v);
        return 0;
    }

    const GLuintX program = x.gl.CreateProgram();
    if (!program) {
        x.gl.DeleteShader(v);
        x.gl.DeleteShader(f);
        return 0;
    }

    x.gl.AttachShader(program, v);
    x.gl.AttachShader(program, f);
    x.gl.LinkProgram(program);
    x.gl.DeleteShader(v);
    x.gl.DeleteShader(f);

    GLintX ok = 0;
    x.gl.GetProgramiv(program, GL_LINK_STATUS_X, &ok);
    if (!ok) {
        x.gl.DeleteProgram(program);
        return 0;
    }
    return program;
}

static void drawFullscreenQuad() {
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static const char* kFullscreenVS =
    "#version 120\n"
    "void main(){ gl_Position=gl_Vertex; gl_TexCoord[0]=gl_MultiTexCoord0; }\n";

static const char* kBrightFS =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform vec2 uTexel;\n"
    "uniform float uThreshold;\n"
    "uniform float uKnee;\n"
    "void main(){\n"
    "  vec2 o=uTexel*0.5;\n"
    "  vec3 c=(texture2D(uScene,gl_TexCoord[0].xy+vec2(-o.x,-o.y)).rgb+\n"
    "          texture2D(uScene,gl_TexCoord[0].xy+vec2( o.x,-o.y)).rgb+\n"
    "          texture2D(uScene,gl_TexCoord[0].xy+vec2(-o.x, o.y)).rgb+\n"
    "          texture2D(uScene,gl_TexCoord[0].xy+vec2( o.x, o.y)).rgb)*0.25;\n"
    "  float b=max(c.r,max(c.g,c.b));\n"
    "  float soft=clamp((b-(uThreshold-uKnee))/(2.0*uKnee),0.0,1.0);\n"
    "  soft=soft*soft*(3.0-2.0*soft);\n"
    "  float hard=step(uThreshold,b);\n"
    "  gl_FragColor=vec4(c*max(hard,soft),1.0);\n"
    "}\n";

static const char* kBlurFS =
    "#version 120\n"
    "uniform sampler2D uInput;\n"
    "uniform vec2 uTexel;\n"
    "uniform vec2 uDirection;\n"
    "void main(){\n"
    "  vec2 d=uTexel*uDirection;\n"
    "  vec3 c=texture2D(uInput,gl_TexCoord[0].xy).rgb*0.227027;\n"
    "  c+=texture2D(uInput,gl_TexCoord[0].xy+d*1.384615).rgb*0.316216;\n"
    "  c+=texture2D(uInput,gl_TexCoord[0].xy-d*1.384615).rgb*0.316216;\n"
    "  c+=texture2D(uInput,gl_TexCoord[0].xy+d*3.230769).rgb*0.070270;\n"
    "  c+=texture2D(uInput,gl_TexCoord[0].xy-d*3.230769).rgb*0.070270;\n"
    "  gl_FragColor=vec4(c,1.0);\n"
    "}\n";

static const char* kCompositeFS =
    "#version 120\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uBloom;\n"
    "uniform float uExposure;\n"
    "uniform float uBloomStrength;\n"
    "uniform vec2 uSceneSize;\n"
    "void main(){\n"
    "  vec2 uv=gl_TexCoord[0].xy;\n"
    "  vec3 scene=texture2D(uScene,uv).rgb;\n"
    "  vec3 bloom=texture2D(uBloom,uv).rgb;\n"
    "  vec3 c=scene*uExposure+bloom*uBloomStrength;\n"
    "  float peak=max(c.r,max(c.g,c.b));\n"
    "  if(peak>1.0) c/=1.0+0.20*(peak-1.0);\n"
    "  c=clamp(c,0.0,1.0);\n"
    "  gl_FragColor=vec4(c,1.0);\n"
    "}\n";



PostProcess_L::~PostProcess_L() { shutdown(); }

bool PostProcess_L::init() {
    shutdown();
    m = new Impl();

    Impl& x = *m;
    x.gl.GenFramebuffers = loadProcAny<PFNGLGENFRAMEBUFFERSPROC_X>("glGenFramebuffers", "glGenFramebuffersEXT");
    x.gl.DeleteFramebuffers = loadProcAny<PFNGLDELETEFRAMEBUFFERSPROC_X>("glDeleteFramebuffers", "glDeleteFramebuffersEXT");
    x.gl.BindFramebuffer = loadProcAny<PFNGLBINDFRAMEBUFFERPROC_X>("glBindFramebuffer", "glBindFramebufferEXT");
    x.gl.FramebufferTexture2D = loadProcAny<PFNGLFRAMEBUFFERTEXTURE2DPROC_X>("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
    x.gl.CheckFramebufferStatus = loadProcAny<PFNGLCHECKFRAMEBUFFERSTATUSPROC_X>("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");

    x.gl.UseProgram = loadProc<PFNGLUSEPROGRAMPROC_X>("glUseProgram");
    x.gl.CreateShader = loadProc<PFNGLCREATESHADERPROC_X>("glCreateShader");
    x.gl.ShaderSource = loadProc<PFNGLSHADERSOURCEPROC_X>("glShaderSource");
    x.gl.CompileShader = loadProc<PFNGLCOMPILESHADERPROC_X>("glCompileShader");
    x.gl.GetShaderiv = loadProc<PFNGLGETSHADERIVPROC_X>("glGetShaderiv");
    x.gl.GetShaderInfoLog = loadProc<PFNGLGETSHADERINFOLOGPROC_X>("glGetShaderInfoLog");
    x.gl.DeleteShader = loadProc<PFNGLDELETESHADERPROC_X>("glDeleteShader");
    x.gl.CreateProgram = loadProc<PFNGLCREATEPROGRAMPROC_X>("glCreateProgram");
    x.gl.AttachShader = loadProc<PFNGLATTACHSHADERPROC_X>("glAttachShader");
    x.gl.LinkProgram = loadProc<PFNGLLINKPROGRAMPROC_X>("glLinkProgram");
    x.gl.GetProgramiv = loadProc<PFNGLGETPROGRAMIVPROC_X>("glGetProgramiv");
    x.gl.GetProgramInfoLog = loadProc<PFNGLGETPROGRAMINFOLOGPROC_X>("glGetProgramInfoLog");
    x.gl.DeleteProgram = loadProc<PFNGLDELETEPROGRAMPROC_X>("glDeleteProgram");
    x.gl.GetUniformLocation = loadProc<PFNGLGETUNIFORMLOCATIONPROC_X>("glGetUniformLocation");
    x.gl.Uniform1i = loadProc<PFNGLUNIFORM1IPROC_X>("glUniform1i");
    x.gl.Uniform1f = loadProc<PFNGLUNIFORM1FPROC_X>("glUniform1f");
    x.gl.Uniform2f = loadProc<PFNGLUNIFORM2FPROC_X>("glUniform2f");
    x.gl.ActiveTexture = loadProc<PFNGLACTIVETEXTUREPROC_X>("glActiveTexture");

    if (!x.gl.GenFramebuffers || !x.gl.DeleteFramebuffers || !x.gl.BindFramebuffer ||
        !x.gl.FramebufferTexture2D || !x.gl.CheckFramebufferStatus ||
        !x.gl.UseProgram || !x.gl.CreateShader || !x.gl.ShaderSource ||
        !x.gl.CompileShader || !x.gl.GetShaderiv || !x.gl.DeleteShader ||
        !x.gl.CreateProgram || !x.gl.AttachShader || !x.gl.LinkProgram ||
        !x.gl.GetProgramiv || !x.gl.DeleteProgram || !x.gl.GetUniformLocation ||
        !x.gl.Uniform1i || !x.gl.Uniform1f || !x.gl.Uniform2f || !x.gl.ActiveTexture) {
        shutdown();
        return false;
    }

    x.hdr = true;
    x.sceneInternalFormat = GL_RGBA16F_X;

                                                                                 
                                                                                     
    glGenTextures(1, &x.sceneTex);
    glBindTexture(GL_TEXTURE_2D, x.sceneTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)GL_RGBA16F_X, 1, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
    if (glGetError() != GL_NO_ERROR) {
        x.hdr = false;
        x.sceneInternalFormat = GL_RGBA_X;
    }
    if (x.sceneTex) glDeleteTextures(1, &x.sceneTex);
    x.sceneTex = 0;

    x.compositeProgram = makeProgram(x, kFullscreenVS, kCompositeFS);
    x.brightProgram = makeProgram(x, kFullscreenVS, kBrightFS);
    x.blurProgram = makeProgram(x, kFullscreenVS, kBlurFS);

    if (!x.compositeProgram || !x.brightProgram || !x.blurProgram) {
        shutdown();
        return false;
    }

    x.compositeSceneLoc = x.gl.GetUniformLocation(x.compositeProgram, "uScene");
    x.compositeBloomLoc = x.gl.GetUniformLocation(x.compositeProgram, "uBloom");
    x.compositeExposureLoc = x.gl.GetUniformLocation(x.compositeProgram, "uExposure");
    x.compositeBloomStrengthLoc = x.gl.GetUniformLocation(x.compositeProgram, "uBloomStrength");
    x.compositeSceneSizeLoc = x.gl.GetUniformLocation(x.compositeProgram, "uSceneSize");

    x.brightSceneLoc = x.gl.GetUniformLocation(x.brightProgram, "uScene");
    x.brightTexelLoc = x.gl.GetUniformLocation(x.brightProgram, "uTexel");
    x.brightThresholdLoc = x.gl.GetUniformLocation(x.brightProgram, "uThreshold");
    x.brightKneeLoc = x.gl.GetUniformLocation(x.brightProgram, "uKnee");

    x.blurTexLoc = x.gl.GetUniformLocation(x.blurProgram, "uInput");
    x.blurTexelLoc = x.gl.GetUniformLocation(x.blurProgram, "uTexel");
    x.blurDirectionLoc = x.gl.GetUniformLocation(x.blurProgram, "uDirection");

    m_ready = true;
    return true;
}

void PostProcess_L::shutdown() {
    if (!m) {
        m_ready = false;
        m_active = false;
        return;
    }
    m->destroyTargets();
    m->destroyPrograms();
    delete m;
    m = nullptr;
    m_ready = false;
    m_active = false;
}

bool PostProcess_L::beginScene(int width, int height) {
    if (!m_ready || !m || width < 2 || height < 2) {
        m_active = false;
        return false;
    }

    Impl& x = *m;

    const int bloomW = std::max(1, width / 2);
    const int bloomH = std::max(1, height / 2);
    if (x.width != width || x.height != height || x.bloomWidth != bloomW || x.bloomHeight != bloomH) {
        x.destroyTargets();

        x.width = width;
        x.height = height;
        x.bloomWidth = bloomW;
        x.bloomHeight = bloomH;

        glGenTextures(1, &x.sceneTex);
        glBindTexture(GL_TEXTURE_2D, x.sceneTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_X);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_X);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)x.sceneInternalFormat, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

        glGenTextures(1, &x.depthTex);
        glBindTexture(GL_TEXTURE_2D, x.depthTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_X);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_X);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24_X, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

        glGenTextures(1, &x.bloomA);
        glBindTexture(GL_TEXTURE_2D, x.bloomA);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_X);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_X);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)x.sceneInternalFormat, bloomW, bloomH, 0, GL_RGBA, GL_FLOAT, nullptr);

        glGenTextures(1, &x.bloomB);
        glBindTexture(GL_TEXTURE_2D, x.bloomB);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE_X);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE_X);
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)x.sceneInternalFormat, bloomW, bloomH, 0, GL_RGBA, GL_FLOAT, nullptr);

        x.gl.GenFramebuffers(1, &x.sceneFbo);
        x.gl.BindFramebuffer(GL_FRAMEBUFFER_X, x.sceneFbo);
        x.gl.FramebufferTexture2D(GL_FRAMEBUFFER_X, GL_COLOR_ATTACHMENT0_X, GL_TEXTURE_2D, x.sceneTex, 0);
        x.gl.FramebufferTexture2D(GL_FRAMEBUFFER_X, GL_DEPTH_ATTACHMENT_X, GL_TEXTURE_2D, x.depthTex, 0);
        const bool sceneOk = x.gl.CheckFramebufferStatus(GL_FRAMEBUFFER_X) == GL_FRAMEBUFFER_COMPLETE_X;

        x.gl.GenFramebuffers(1, &x.bloomFbo);
        x.gl.BindFramebuffer(GL_FRAMEBUFFER_X, x.bloomFbo);
        x.gl.FramebufferTexture2D(GL_FRAMEBUFFER_X, GL_COLOR_ATTACHMENT0_X, GL_TEXTURE_2D, x.bloomA, 0);
        const bool bloomOk = x.gl.CheckFramebufferStatus(GL_FRAMEBUFFER_X) == GL_FRAMEBUFFER_COMPLETE_X;

        x.gl.BindFramebuffer(GL_FRAMEBUFFER_X, 0);
        if (!sceneOk || !bloomOk) {
            x.destroyTargets();
            m_active = false;
            return false;
        }
    }

    x.gl.BindFramebuffer(GL_FRAMEBUFFER_X, x.sceneFbo);
    glViewport(0, 0, width, height);
    m_active = true;
    return true;
}

void PostProcess_L::endScene() {
    if (!m_active || !m || !m_ready) return;

    Impl& x = *m;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_CULL_FACE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

                                                      
    x.gl.BindFramebuffer(GL_FRAMEBUFFER_X, x.bloomFbo);
    glViewport(0, 0, x.bloomWidth, x.bloomHeight);
    x.gl.UseProgram(x.brightProgram);
    x.gl.ActiveTexture(GL_TEXTURE0_X);
    glBindTexture(GL_TEXTURE_2D, x.sceneTex);
    x.gl.Uniform1i(x.brightSceneLoc, 0);
    x.gl.Uniform2f(x.brightTexelLoc, 1.0f / (float)x.width, 1.0f / (float)x.height);
    x.gl.Uniform1f(x.brightThresholdLoc, 0.90f);
    x.gl.Uniform1f(x.brightKneeLoc, 0.18f);
    drawFullscreenQuad();

                                         
    x.gl.FramebufferTexture2D(GL_FRAMEBUFFER_X, GL_COLOR_ATTACHMENT0_X, GL_TEXTURE_2D, x.bloomB, 0);
    x.gl.UseProgram(x.blurProgram);
    x.gl.ActiveTexture(GL_TEXTURE0_X);
    glBindTexture(GL_TEXTURE_2D, x.bloomA);
    x.gl.Uniform1i(x.blurTexLoc, 0);
    x.gl.Uniform2f(x.blurTexelLoc, 1.0f / (float)x.bloomWidth, 1.0f / (float)x.bloomHeight);
    x.gl.Uniform2f(x.blurDirectionLoc, 1.0f, 0.0f);
    drawFullscreenQuad();

                                       
    x.gl.FramebufferTexture2D(GL_FRAMEBUFFER_X, GL_COLOR_ATTACHMENT0_X, GL_TEXTURE_2D, x.bloomA, 0);
    x.gl.ActiveTexture(GL_TEXTURE0_X);
    glBindTexture(GL_TEXTURE_2D, x.bloomB);
    x.gl.Uniform2f(x.blurDirectionLoc, 0.0f, 1.0f);
    drawFullscreenQuad();

                                                                               
                                                                 
    x.gl.BindFramebuffer(GL_FRAMEBUFFER_X, 0);
    glViewport(0, 0, x.width, x.height);
    x.gl.UseProgram(x.compositeProgram);

    x.gl.ActiveTexture(GL_TEXTURE0_X);
    glBindTexture(GL_TEXTURE_2D, x.sceneTex);
    x.gl.Uniform1i(x.compositeSceneLoc, 0);

    x.gl.ActiveTexture(GL_TEXTURE1_X);
    glBindTexture(GL_TEXTURE_2D, x.bloomA);
    x.gl.Uniform1i(x.compositeBloomLoc, 1);

    x.gl.Uniform1f(x.compositeExposureLoc, x.hdr ? 0.96f : 0.94f);
    x.gl.Uniform1f(x.compositeBloomStrengthLoc, x.hdr ? 0.07f : 0.05f);
    x.gl.Uniform2f(x.compositeSceneSizeLoc, (float)x.width, (float)x.height);

    drawFullscreenQuad();
    x.gl.UseProgram(0);

    x.gl.ActiveTexture(GL_TEXTURE1_X);
    glBindTexture(GL_TEXTURE_2D, 0);
    x.gl.ActiveTexture(GL_TEXTURE0_X);
    glBindTexture(GL_TEXTURE_2D, 0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);

    m_active = false;
}
