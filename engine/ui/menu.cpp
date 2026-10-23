#include "menu.h"
#include "menu_kv.h"
#include "multiplayer.h"
#include "texture_l.h"
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <GL/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <array>

#ifdef _MSC_VER
#pragma comment(lib, "gdi32.lib")
#endif

#include "stb_image.h"

extern bool GameConsole_IsOpen();
extern std::vector<std::pair<std::string, bool>> GameConsole_GetLines();
extern std::string GameConsole_GetInput();
extern std::vector<std::string> GameConsole_GetSuggestions();
extern void GameConsole_SetInput(const std::string& value);
extern bool GameConsole_TakeMapCommand(std::string& outMap);
extern bool GameConsole_PollPassthrough();
extern bool GameConsole_TakeJoinMapRequest(std::string& outMap);

namespace {
Menu_L* g_scrollMenu = nullptr;

static void MenuScrollCallback(GLFWwindow*, double, double yoffset) {
    if (g_scrollMenu) g_scrollMenu->addHostScroll(yoffset);
}

static std::string lowerText(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static std::string stripExtension(std::string s) {
    s = lowerText(s);
    const size_t slash = s.find_last_of('/');
    const size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) s.resize(dot);
    return s;
}

static std::string withoutMaterials(std::string s) {
    s = lowerText(s);
    if (s.rfind("materials/", 0) == 0) s.erase(0, 10);
    return s;
}

static bool hasExtension(const std::string& s, const char* ext) {
    const size_t n = std::strlen(ext);
    return s.size() >= n && lowerText(s).compare(s.size() - n, n, ext) == 0;
}

static std::string firstMapFromLegacyCfg(const std::string& text) {
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        const std::string low = lowerText(line);
        const size_t p = low.find("map ");
        if (p == std::string::npos) continue;
        const size_t s = line.find_first_not_of(" \t", p + 3);
        if (s == std::string::npos) continue;
        const size_t e = line.find_first_of(" \t\r\n", s);
        return line.substr(s, e == std::string::npos ? std::string::npos : e - s);
    }
    return {};
}

static std::string asciiSafe(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char c : input) {
        if (c >= 32 && c < 127) out.push_back(static_cast<char>(c));
        else if (c == '\t') out.push_back(' ');
        else out.push_back('?');
    }
    return out;
}

static HFONT makeSystemFont(int pixelHeight, int weight) {
    return CreateFontA(-pixelHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       "MS Shell Dlg 2");
}

static bool readTextResource(Texture_L* fs, const std::string& gameDirectory,
                             const std::string& relativePath, std::string& out) {
    out.clear();
    const std::vector<std::string> paths =
        relativePath == "cfg/menu.txt"
            ? std::vector<std::string>{"cfg/menu.txt", "menu.txt"}
            : std::vector<std::string>{relativePath};

    for (const std::string& candidate : paths) {
        if (fs) {
            std::vector<char> raw;
            if (fs->getFileRawData(lowerText(candidate), raw) && !raw.empty()) {
                out.assign(raw.begin(), raw.end());
                return true;
            }
        }
        const std::filesystem::path path = std::filesystem::path(gameDirectory) / candidate;
        if (MenuKV::readFile(path.string(), out)) return true;
    }
    return false;
}

static void addUnique(std::vector<std::string>& out, std::set<std::string>& seen,
                      const std::string& candidate) {
    const std::string normalized = lowerText(candidate);
    if (normalized.empty()) return;
    if (seen.insert(normalized).second) out.push_back(normalized);
}

static bool looksLikeTextureEntry(const std::string& path) {
    return hasExtension(path, ".vmt") || hasExtension(path, ".vtf") ||
           hasExtension(path, ".png") || hasExtension(path, ".jpg") ||
           hasExtension(path, ".jpeg") || hasExtension(path, ".tga");
}

static std::string quotedKeyValue(const std::string& text,const std::vector<std::string>& keys){
    for(size_t p=0;p<text.size();){
        p=text.find('"',p); if(p==std::string::npos) break;
        size_t e=text.find('"',p+1); if(e==std::string::npos) break;
        std::string k=lowerText(text.substr(p+1,e-p-1)); p=e+1;
        for(const std::string& wanted:keys) if(k==wanted){
            size_t q=text.find('"',p); if(q==std::string::npos) break; size_t qe=text.find('"',q+1); if(qe==std::string::npos) break;
            return text.substr(q+1,qe-q-1);
        }
    }
    return {};
}

static std::string entityValue(const std::string& block,const std::string& key){
    std::string needle="\""+key+"\""; size_t p=block.find(needle); if(p==std::string::npos) return {};
    size_t q=block.find('"',p+needle.size()); if(q==std::string::npos) return {}; size_t e=block.find('"',q+1); if(e==std::string::npos) return {}; return block.substr(q+1,e-q-1);
}
}

Menu_L::~Menu_L() {
    if (resourcesActive && glfwGetCurrentContext()) shutdown();
}

void Menu_L::shutdown() {
    if (!resourcesActive || !glfwGetCurrentContext()) return;
    if (g_scrollMenu == this) g_scrollMenu = nullptr;
    previewMap.releaseRenderResources();
    for(auto* m:previewProps.models) delete m;
    previewProps.models.clear();
    previewProps.instances.clear();
    previewProps.modelNames.clear();
    previewMapLoaded=false;
    destroyFonts();
    destroyOwnedTextures();
    if (pauseFrameTexture) glDeleteTextures(1, &pauseFrameTexture);
    pauseFrameTexture = 0;
    pauseFrameWidth = pauseFrameHeight = 0;
    ownerWindow = nullptr;
    fileSystem = nullptr;
    resourcesActive = false;
}

int Menu_L::hostPort() const {
    try {
        int value = std::stoi(hostPortText);
        return std::max(1, std::min(65535, value));
    } catch (...) {
        return 27015;
    }
}

bool Menu_L::init(GLFWwindow* window, Texture_L* fs,
                  const std::string& gDir, const std::string& gameTitle) {
    ownerWindow = window;
    fileSystem = fs;
    gameDirectory = gDir;
    title = gameTitle.empty() || gameTitle == "Sauce name" ? "Sauce" : gameTitle;
    selectedMapName.clear();
    selectedChapter = 0;
    hostMapIndex = 0;
    hostMapScroll = 0;
    hostDropdownOpen = false;
    hostGameRequested_ = false;
    hostField = 0;
    hostPortText = "27015";
    hostModelPath = "models/player.mdl";
    hostPasteWasDown = false;
    hostBackspaceWasDown = false;
    hostDigitWasDown.fill(false);
    chapterPageStart = 0;
    g_scrollMenu = this;
    glfwSetScrollCallback(window, MenuScrollCallback);
    loadContent();
    fontsReady = initFonts();
    resourcesActive = true;
    return fontsReady;
}

void Menu_L::begin2D(int width, int height, bool clear) {
    glViewport(0, 0, width, height);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    if (clear) {
        glClearColor(0.01f, 0.012f, 0.014f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

void Menu_L::end2D() {
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

void Menu_L::drawTexture(const TextureRef& tex, float x, float y, float w, float h,
                         float alpha, bool crop, bool sourceBottomUp) {
    if (!tex.id || w <= 0.0f || h <= 0.0f) return;
    glColor4f(1, 1, 1, alpha);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    const float imageAspect = tex.height > 0 ? static_cast<float>(tex.width) / static_cast<float>(tex.height) : 1.0f;
    const float boxAspect = h > 0.0f ? w / h : imageAspect;
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    if (crop && tex.width > 1 && tex.height > 1) {
        if (imageAspect > boxAspect) {
            const float visible = boxAspect / imageAspect;
            u0 = (1.0f - visible) * 0.5f;
            u1 = 1.0f - u0;
        } else if (imageAspect < boxAspect) {
            const float visible = imageAspect / boxAspect;
            v0 = (1.0f - visible) * 0.5f;
            v1 = 1.0f - v0;
        }
    }
                                                                         
                                                                               
                                                                              
                                                                  
    if (sourceBottomUp) std::swap(v0, v1);

    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x, y);
    glTexCoord2f(u1, v0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1); glVertex2f(x, y + h);
    glEnd();
}

void Menu_L::drawRect(float x, float y, float w, float h, float r, float g, float b, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

void Menu_L::drawRoundedRect(float x, float y, float w, float h, float radius,
                              float r, float g, float b, float alpha) {
    radius = std::max(0.0f, std::min(radius, std::min(w, h) * 0.5f));
    if (radius <= 0.0f) { drawRect(x, y, w, h, r, g, b, alpha); return; }
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, alpha);
    glBegin(GL_QUADS);
    glVertex2f(x + radius, y); glVertex2f(x + w - radius, y);
    glVertex2f(x + w - radius, y + h); glVertex2f(x + radius, y + h);
    glVertex2f(x, y + radius); glVertex2f(x + radius, y + radius);
    glVertex2f(x + radius, y + h - radius); glVertex2f(x, y + h - radius);
    glVertex2f(x + w - radius, y + radius); glVertex2f(x + w, y + radius);
    glVertex2f(x + w, y + h - radius); glVertex2f(x + w - radius, y + h - radius);
    glEnd();
    auto corner = [&](float cx, float cy, float a0) {
        glBegin(GL_TRIANGLE_FAN);
        glVertex2f(cx, cy);
        for (int i = 0; i <= 12; ++i) {
            const float a = a0 + (3.14159265358979323846f * 0.5f) * (static_cast<float>(i) / 12.0f);
            glVertex2f(cx + std::cos(a) * radius, cy + std::sin(a) * radius);
        }
        glEnd();
    };
    corner(x + radius, y + radius, 3.14159265358979323846f);
    corner(x + w - radius, y + radius, 4.7123889803846898577f);
    corner(x + w - radius, y + h - radius, 0.0f);
    corner(x + radius, y + h - radius, 1.5707963267948966192f);
    glEnable(GL_TEXTURE_2D);
}

void Menu_L::drawLine(float x1, float y1, float x2, float y2, float width, float alpha) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.84f, 0.84f, 0.84f, alpha);
    glLineWidth(width);
    glBegin(GL_LINES);
    glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
    glEnable(GL_TEXTURE_2D);
}

float Menu_L::textWidth(const std::string& input, const Font& font) const {
    if (!font.valid || !font.hfont || !ownerWindow) return 0.0f;
    HWND hwnd = reinterpret_cast<HWND>(glfwGetWin32Window(ownerWindow));
    HDC dc = GetDC(hwnd);
    if (!dc) return 0.0f;
    HGDIOBJ old = SelectObject(dc, static_cast<HFONT>(font.hfont));
    SIZE size{0, 0};
    const std::string text = asciiSafe(input);
    GetTextExtentPoint32A(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return static_cast<float>(size.cx);
}

void Menu_L::drawText(const std::string& input, float x, float y, const Font& font,
                      float r, float g, float b, float alpha) {
    if (!font.valid || !font.listBase || input.empty()) return;
    const std::string text = asciiSafe(input);
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, alpha);
    glRasterPos2f(x, y + font.pixelHeight);
    glListBase(font.listBase - 32);
    glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, text.c_str());
    glEnable(GL_TEXTURE_2D);
}

void Menu_L::renderPauseFrame(int width, int height) {
    if (pauseFrameTexture) {
        TextureRef frame;
        frame.id = pauseFrameTexture;
        frame.width = pauseFrameWidth;
        frame.height = pauseFrameHeight;
        drawTexture(frame, 0, 0, static_cast<float>(width), static_cast<float>(height), 1.0f, false, true);
    } else {
        drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), 0.03f, 0.03f, 0.035f, 1.0f);
    }
    drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f, 0.0f, 0.48f);
}

void Menu_L::loadMenuPreview() {
    previewMapLoaded=false;
    std::string config;
    for(const std::string& path: {std::string("gameinfo.gfc"),std::string("gameinfo.txt"),std::string("cfg/gameinfo.gfc")}) {
        if(readTextResource(fileSystem,gameDirectory,path,config)) {
            std::string value=quotedKeyValue(config,{"menumap","menu_map","backgroundmap","background_map","menu_background_map","mainmenumap","main_menu_map","startmap"});
            if(value.empty()) continue;
            value=stripExtension(value);
            if(value.rfind("maps/",0)==0)value.erase(0,5);
            std::vector<char> raw;
            if(fileSystem && fileSystem->getFileRawData(lowerText("maps/"+value+".bsp"),raw) && !raw.empty()) {
                if(previewMap.loadMapFromData(raw,value)) {
                    previewMap.setupTextures(*fileSystem);
                    previewProps.parseFromBSP(previewMap.staticPropData,previewMap.staticPropVersion);
                    previewProps.loadModels(*fileSystem);
                    const bool hasCamera=findPreviewCamera();
                    if(!hasCamera) {
                        size_t pos=0;
                        while((pos=previewMap.entityData.find('{',pos))!=std::string::npos) {
                            size_t end=previewMap.entityData.find('}',pos); if(end==std::string::npos) break;
                            std::string block=previewMap.entityData.substr(pos,end-pos);
                            std::string cls=lowerText(entityValue(block,"classname"));
                            if(cls=="info_player_start"||cls=="info_player_deathmatch") {
                                std::istringstream o(entityValue(block,"origin"));
                                float x=0,y=0,z=0; if(o>>x>>y>>z) { previewCameraPos={x,z,-y}; std::istringstream a(entityValue(block,"angles")); a>>previewCameraAngles.x>>previewCameraAngles.y>>previewCameraAngles.z; break; }
                            }
                            pos=end+1;
                        }
                    }
                    previewMapLoaded=true;
                    return;
                }
            }
        }
    }
}

bool Menu_L::findPreviewCamera() {
    size_t pos=0;
    while((pos=previewMap.entityData.find('{',pos))!=std::string::npos) {
        size_t end=previewMap.entityData.find('}',pos); if(end==std::string::npos) break;
        std::string block=previewMap.entityData.substr(pos,end-pos);
        std::string cls=lowerText(entityValue(block,"classname"));
        if(cls=="point_camera"||cls=="point_viewcontrol") {
            std::istringstream o(entityValue(block,"origin"));
            float x=0,y=0,z=0; if(o>>x>>y>>z) previewCameraPos={x,z,-y}; else {pos=end+1;continue;}
            std::istringstream a(entityValue(block,"angles"));
            a>>previewCameraAngles.x>>previewCameraAngles.y>>previewCameraAngles.z;
            return true;
        }
        pos=end+1;
    }
    return false;
}

void Menu_L::renderPreview3D(int width,int height) {
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glDisable(GL_LIGHTING);
    glViewport(0,0,width,height);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    float aspect=height>0?(float)width/(float)height:1.777f;float f=std::tan(90.0f*3.14159265f/360.0f);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-f*aspect,f*aspect,-f,f,1.0f,30000.0f);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glRotatef(previewCameraAngles.x,1,0,0);glRotatef(previewCameraAngles.y,0,1,0);glRotatef(previewCameraAngles.z,0,0,1);glTranslatef(-previewCameraPos.x,-previewCameraPos.y,-previewCameraPos.z);
    previewMap.renderWorld();previewMap.renderLightmaps();previewMap.renderTransparentWorld();previewProps.render(&previewMap);
    glDisable(GL_DEPTH_TEST);
}

void Menu_L::renderMainMenu(int width, int height) {
    if (pauseMode) {
        renderPauseFrame(width, height);
    } else if (!previewMapLoaded && !backgroundTextures.empty()) {
        drawTexture(backgroundTextures.front(), 0, 0, static_cast<float>(width), static_cast<float>(height), 1.0f, true);
        drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), 0, 0, 0, 0.20f);
    } else {
        drawRect(0, 0, static_cast<float>(width), static_cast<float>(height), 0.055f, 0.06f, 0.065f, 1.0f);
    }

    const float base = std::min(width / 1920.0f, height / 1080.0f);
    const float s = std::max(0.65f, std::min(1.45f, base));
    const float left = 110.0f * s;
    const float titleY = 218.0f * s;
    const float menuY = 455.0f * s;
    const float row = 31.0f * s;

    drawText(upperTitle(title), left, titleY, fontTitle, 0.72f, 0.73f, 0.74f, 0.58f);

    const std::vector<std::string> items = pauseMode
        ? std::vector<std::string>{"Resume", "New Game", "Load Game", "Options", "Main Menu", "Quit"}
        : std::vector<std::string>{"New Game", "Host a Game", "Load Game", "Options", "Quit"};

    for (size_t i = 0; i < items.size(); ++i) {
        const bool pressed = pressedControl == static_cast<int>(i) && mouseWasDown;
        const float y = menuY + row * static_cast<float>(i);
        if (pressed) drawRect(left - 5.0f * s, y + 1.0f * s,
                              textWidth(items[i], fontMenu) + 10.0f * s, 20.0f * s,
                              0.08f, 0.08f, 0.08f, 0.50f);
        drawText(items[i], left, y, fontMenu, 0.92f, 0.92f, 0.92f, 0.94f);
    }

    if (dialog == DialogKind::None && pauseMode)
        drawText("ESC", left, height - 44.0f * s, fontTiny, 0.76f, 0.76f, 0.76f, 0.48f);
}

void Menu_L::centerDialog(int width, int height) {
    const float base = std::min(width / 1920.0f, height / 1080.0f);
    const float s = std::max(0.55f, std::min(1.35f, base));
    if (dialog == DialogKind::Host) {
        dialogW = 470.0f * s;
        dialogH = 610.0f * s;
        dialogX = (width - dialogW) * 0.5f;
        dialogY = (height - dialogH) * 0.5f;
    } else {
        dialogW = 790.0f * s;
        dialogH = 392.0f * s;
        dialogX = (width - dialogW) * 0.5f + 70.0f * s;
        dialogY = (height - dialogH) * 0.5f;
    }
    clampDialog(width, height);
}

void Menu_L::clampDialog(int width, int height) {
    const float margin = 10.0f;
    dialogX = std::max(margin, std::min(dialogX, static_cast<float>(width) - dialogW - margin));
    dialogY = std::max(margin, std::min(dialogY, static_cast<float>(height) - dialogH - margin));
}

void Menu_L::openChapterSelector(int width, int height) {
    dialog = DialogKind::Chapters;
    dialogTitle = "NEW GAME";
    if (chapters.empty()) { selectedChapter = 0; chapterPageStart = 0; }
    else {
        selectedChapter = std::min(selectedChapter, chapters.size() - 1);
        const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
        chapterPageStart = (selectedChapter / pageSize) * pageSize;
    }
    centerDialog(width, height);
}

void Menu_L::openEmptyDialog(const std::string& titleText, int width, int height) {
    dialog = DialogKind::Empty;
    dialogTitle = upperTitle(titleText);
    centerDialog(width, height);
}

void Menu_L::openHostDialog(int width, int height) {
    dialog = DialogKind::Host;
    dialogTitle = "HOST A GAME";
    hostDropdownOpen = false;
    hostField = 0;
    hostPortText = "27015";
    hostModelPath = "models/player.mdl";
    hostMapIndex = chapters.empty() ? 0 : std::min(hostMapIndex, chapters.size() - 1);
    if (!chapters.empty()) {
        const size_t visible = std::min<size_t>(8, chapters.size());
        hostMapScroll = std::min(hostMapScroll, chapters.size() - visible);
    } else {
        hostMapScroll = 0;
    }
    centerDialog(width, height);
}

void Menu_L::closeDialog() {
    dialog = DialogKind::None;
    draggingDialog = false;
    hostDropdownOpen = false;
    hostField = 0;
    hostGameRequested_ = false;
}

void Menu_L::renderDialog(int width, int height) {
    if (dialog == DialogKind::None) return;
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));

                                                                                  
    drawRoundedRect(dialogX, dialogY, dialogW, dialogH, 8.0f, 0.43f, 0.43f, 0.43f, 0.78f);
    drawRoundedRect(dialogX + 2.0f, dialogY + 2.0f, dialogW - 4.0f, dialogH - 4.0f, 6.0f,
                    0.28f, 0.29f, 0.30f, 0.76f);
    drawRoundedRect(dialogX + 2.0f, dialogY + 2.0f, dialogW - 4.0f, 27.0f * s,
                    6.0f, 0.36f, 0.37f, 0.38f, 0.84f);

    drawText(dialogTitle, dialogX + 12.0f * s, dialogY + 6.0f * s,
             fontTiny, 0.96f, 0.96f, 0.96f, 0.96f);

    const float closeX = dialogX + dialogW - 20.0f * s;
    const bool closePressed = pressedControl == 900 && mouseWasDown;
    if (closePressed) drawRect(closeX - 3.0f * s, dialogY + 4.0f * s, 16.0f * s, 16.0f * s,
                               0.12f, 0.12f, 0.12f, 0.50f);
    drawText("X", closeX, dialogY + 5.0f * s, fontTiny, 0.96f, 0.96f, 0.96f, 0.85f);

    if (dialog == DialogKind::Chapters) renderChapterSelector(width, height);
    else if (dialog == DialogKind::Host) renderHostDialog(width, height);
    else renderEmptyDialog(width, height);
}

void Menu_L::renderEmptyDialog(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float innerX = dialogX + 18.0f * s;
    const float innerY = dialogY + 45.0f * s;
    const float innerW = dialogW - 36.0f * s;
    const float innerH = dialogH - 78.0f * s;
    drawRect(innerX, innerY, innerW, innerH, 0.64f, 0.64f, 0.64f, 0.08f);

    const bool cancelPressed = pressedControl == 901 && mouseWasDown;
    const float bw = 76.0f * s;
    const float bh = 24.0f * s;
    const float bx = dialogX + dialogW - bw - 16.0f * s;
    const float by = dialogY + dialogH - bh - 14.0f * s;
    drawRect(bx, by, bw, bh,
             cancelPressed ? 0.18f : 0.42f, cancelPressed ? 0.18f : 0.42f,
             cancelPressed ? 0.18f : 0.42f, cancelPressed ? 0.56f : 0.66f);
    drawLine(bx, by, bx + bw, by, 1.0f, 0.62f);
    drawLine(bx, by + bh, bx + bw, by + bh, 1.0f, 0.46f);
    drawText("Close", bx + 12.0f * s, by + 4.0f * s, fontTiny, 0.95f, 0.95f, 0.95f, 0.95f);
}

void Menu_L::renderHostDialog(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float x = dialogX + 24.0f * s;
    const float w = dialogW - 48.0f * s;
    const float labelH = 18.0f * s;
    const float fieldH = 34.0f * s;
    const float gap = 25.0f * s;
    float y = dialogY + 46.0f * s;

    drawText("Map", x, y, fontTiny, 0.78f, 0.78f, 0.78f, 0.90f);
    y += labelH;
    const std::string mapLabel = chapters.empty() ? "No maps found" : chapters[hostMapIndex].mapName;
    drawRoundedRect(x, y, w, fieldH, 4.0f, 0.16f, 0.18f, 0.19f, 0.94f);
    drawText(mapLabel, x + 10.0f * s, y + 7.0f * s, fontSmall, 0.95f, 0.95f, 0.95f, 0.98f);
    drawText(hostDropdownOpen ? "^" : "v", x + w - 20.0f * s, y + 7.0f * s, fontTiny, 0.78f, 0.78f, 0.78f, 0.90f);

    y += fieldH + gap;
    drawText("Port", x, y, fontTiny, 0.78f, 0.78f, 0.78f, 0.90f);
    y += labelH;
    drawRoundedRect(x, y, w, fieldH, 4.0f, hostField == 1 ? 0.20f : 0.16f, hostField == 1 ? 0.22f : 0.18f, hostField == 1 ? 0.23f : 0.19f, 0.94f);
    drawText(hostPortText, x + 10.0f * s, y + 7.0f * s, fontSmall, 0.95f, 0.95f, 0.95f, 0.98f);

    y += fieldH + gap;
    drawText("IP", x, y, fontTiny, 0.78f, 0.78f, 0.78f, 0.90f);
    y += labelH;
    drawRoundedRect(x, y, w, fieldH, 4.0f, 0.13f, 0.14f, 0.15f, 0.90f);
    drawText("0.0.0.0", x + 10.0f * s, y + 7.0f * s, fontSmall, 0.68f, 0.68f, 0.68f, 0.90f);

    y += fieldH + gap;
    drawText("Player model", x, y, fontTiny, 0.78f, 0.78f, 0.78f, 0.90f);
    y += labelH;
    drawRoundedRect(x, y, w, fieldH, 4.0f, hostField == 2 ? 0.20f : 0.16f, hostField == 2 ? 0.22f : 0.18f, hostField == 2 ? 0.23f : 0.19f, 0.94f);
    drawText(hostModelPath, x + 10.0f * s, y + 7.0f * s, fontTiny, 0.95f, 0.95f, 0.95f, 0.98f);
    drawText("Ctrl+V", x + w - 52.0f * s, y + 8.0f * s, fontTiny, 0.56f, 0.56f, 0.56f, 0.88f);

    y += fieldH + gap;
    drawText("Player", x, y, fontTiny, 0.78f, 0.78f, 0.78f, 0.90f);
    y += labelH;
    drawRoundedRect(x, y, w, fieldH, 4.0f, hostField == 3 ? 0.20f : 0.13f, hostField == 3 ? 0.22f : 0.14f, hostField == 3 ? 0.23f : 0.15f, 0.94f);
    drawText(playerNick, x + 10.0f * s, y + 7.0f * s, fontSmall, 0.95f, 0.95f, 0.95f, 0.98f);
    drawText("Ctrl+C/V", x + w - 66.0f * s, y + 8.0f * s, fontTiny, 0.56f, 0.56f, 0.56f, 0.88f);

    if (hostDropdownOpen && !chapters.empty()) {
        const size_t visible = std::min<size_t>(8, chapters.size());
        const float mapFieldY = dialogY + 46.0f * s + 18.0f * s;
        const float listY = mapFieldY + fieldH + 4.0f * s;
        const float listH = visible * 27.0f * s;
        drawRoundedRect(x, listY, w, listH, 4.0f, 0.10f, 0.12f, 0.13f, 0.98f);
        size_t startIndex = std::min(hostMapScroll, chapters.size() - visible);
        for (size_t i = 0; i < visible && startIndex + i < chapters.size(); ++i) {
            const size_t index = startIndex + i;
            const float iy = listY + i * 27.0f * s;
            const bool selected = index == hostMapIndex;
            if (selected) drawRect(x + 2.0f * s, iy + 2.0f * s, w - 4.0f * s, 23.0f * s, 0.27f, 0.29f, 0.30f, 0.92f);
            drawText(chapters[index].mapName, x + 10.0f * s, iy + 5.0f * s, fontTiny, 0.94f, 0.94f, 0.94f, 0.96f);
        }
    }

    const float bh = 28.0f * s;
    const float closeW = 76.0f * s;
    const float startW = 76.0f * s;
    const float by = dialogY + dialogH - bh - 16.0f * s;
    const float closeX = dialogX + dialogW - closeW - 16.0f * s;
    const float startX = closeX - startW - 10.0f * s;
    const bool startPressed = pressedControl == 923 && mouseWasDown;
    const bool closePressed = pressedControl == 924 && mouseWasDown;
    drawRect(startX, by, startW, bh, startPressed ? 0.18f : 0.42f, startPressed ? 0.18f : 0.42f, startPressed ? 0.18f : 0.42f, 0.70f);
    drawText("Start", startX + 15.0f * s, by + 5.0f * s, fontTiny, 0.96f, 0.96f, 0.96f, 0.96f);
    drawRect(closeX, by, closeW, bh, closePressed ? 0.18f : 0.42f, closePressed ? 0.18f : 0.42f, closePressed ? 0.18f : 0.42f, 0.70f);
    drawText("Close", closeX + 12.0f * s, by + 5.0f * s, fontTiny, 0.96f, 0.96f, 0.96f, 0.96f);
}

void Menu_L::renderChapterSelector(int width, int height) {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const float bodyX = dialogX + 18.0f * s;
    const float bodyY = dialogY + 45.0f * s;
    const float gap = 14.0f * s;
    const int pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
    const int visible = std::min(pageSize, static_cast<int>(chapters.size() - std::min(chapterPageStart, chapters.size())));

    if (chapters.empty()) {
        drawRect(bodyX, bodyY, dialogW - 36.0f * s, 175.0f * s, 0.82f, 0.82f, 0.82f, 0.12f);
        drawText("No chapter data found.", bodyX + 16.0f * s, bodyY + 18.0f * s,
                 fontSmall, 0.92f, 0.92f, 0.92f, 0.90f);
        drawText("Create cfg/menu.txt or cfg/chapterN.cfg", bodyX + 16.0f * s, bodyY + 46.0f * s,
                 fontTiny, 0.78f, 0.78f, 0.78f, 0.76f);
        return;
    }

    const float innerW = dialogW - 36.0f * s;
    const float cardW = std::min(218.0f * s, (innerW - gap * 2.0f) / 3.0f);
    const float imageH = cardW * 0.56f;
    const float textBlockH = 34.0f * s;
    const float cardTop = bodyY + 2.0f * s;

    for (int slot = 0; slot < visible; ++slot) {
        const size_t index = chapterPageStart + static_cast<size_t>(slot);
        if (index >= chapters.size()) break;
        const MenuChapter& chapter = chapters[index];
        const float x = bodyX + slot * (cardW + gap);
        const bool selected = index == selectedChapter;
        const bool pressed = pressedControl == (100 + slot) && mouseWasDown;

        char counter[32];
        std::snprintf(counter, sizeof(counter), "%02d", chapter.number > 0 ? chapter.number : static_cast<int>(index + 1));
        drawText(counter, x, cardTop, fontTiny, 0.82f, 0.82f, 0.82f, 0.42f);
        drawText(chapter.name, x, cardTop + 13.0f * s, fontSmall, 0.96f, 0.96f, 0.96f, 0.96f);

        const float imageY = cardTop + textBlockH;
        const float border = selected ? 2.0f * s : 1.0f * s;
        drawRect(x - border, imageY - border, cardW + border * 2.0f, imageH + border * 2.0f,
                 0.90f, 0.90f, 0.90f, selected ? 0.84f : 0.30f);
        const unsigned int resolvedId = resolveChapterTexture(index);
        TextureRef resolved = chapterTextures[index];
        if (!resolved.id) resolved.id = resolvedId;
        drawTexture(resolved, x, imageY, cardW, imageH, pressed ? 0.46f : 1.0f, true);

        if (pressed) drawRect(x, imageY, cardW, imageH, 0.08f, 0.08f, 0.08f, 0.26f);
    }

    const float navY = cardTop + imageH + 66.0f * s;
    const float arrowW = 26.0f * s;
    const bool leftPressed = pressedControl == 110 && mouseWasDown;
    const bool rightPressed = pressedControl == 111 && mouseWasDown;
    if (leftPressed) drawRect(bodyX, navY - 2, arrowW, 22.0f * s, 0.12f, 0.12f, 0.12f, 0.55f);
    if (rightPressed) drawRect(bodyX + innerW - arrowW, navY - 2, arrowW, 22.0f * s, 0.12f, 0.12f, 0.12f, 0.55f);
    drawText("<", bodyX + 8.0f * s, navY, fontSmall, 0.94f, 0.94f, 0.94f, 0.84f);
    drawText(">", bodyX + innerW - 17.0f * s, navY, fontSmall, 0.94f, 0.94f, 0.94f, 0.84f);

    const std::string counterText = std::to_string(selectedChapter + 1) + " / " + std::to_string(chapters.size());
    const float counterWidth = textWidth(counterText, fontTiny);
    drawText(counterText, dialogX + (dialogW - counterWidth) * 0.5f, navY + 4.0f * s,
             fontTiny, 0.74f, 0.74f, 0.74f, 0.55f);

    const float bh = 24.0f * s;
    const float startW = 88.0f * s;
    const float cancelW = 74.0f * s;
    const float by = dialogY + dialogH - bh - 15.0f * s;
    const float cancelX = dialogX + dialogW - cancelW - 15.0f * s;
    const float startX = cancelX - startW - 8.0f * s;

    for (const auto& button : {
        std::pair<int, std::pair<float, std::pair<float, std::string>>>{104, {startX, {startW, "Start"}}},
        std::pair<int, std::pair<float, std::pair<float, std::string>>>{103, {cancelX, {cancelW, "Cancel"}}}
    }) {
        const int id = button.first;
        const float x = button.second.first;
        const float w = button.second.second.first;
        const std::string& label = button.second.second.second;
        const bool pressed = pressedControl == id && mouseWasDown;
        const float c = pressed ? 0.17f : 0.42f;
        drawRect(x, by, w, bh, c, c, c, 0.68f);
        drawLine(x, by, x + w, by, 1.0f, 0.62f);
        drawLine(x, by + bh, x + w, by + bh, 1.0f, 0.44f);
        drawText(label, x + 10.0f * s, by + 4.0f * s, fontTiny, 0.95f, 0.95f, 0.95f, 0.94f);
    }
}

bool Menu_L::pointIn(float px, float py, float x, float y, float w, float h) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

int Menu_L::hitMainControl(float mx, float my, int width, int height) const {
    const float s = std::max(0.65f, std::min(1.45f, std::min(width / 1920.0f, height / 1080.0f)));
    const float left = 110.0f * s;
    const float top = 455.0f * s;
    const float row = 31.0f * s;
    const std::vector<std::string> items = pauseMode
        ? std::vector<std::string>{"Resume", "New Game", "Load Game", "Options", "Main Menu", "Quit"}
        : std::vector<std::string>{"New Game", "Host a Game", "Load Game", "Options", "Quit"};
    for (size_t i = 0; i < items.size(); ++i) {
        const float w = textWidth(items[i], fontMenu) + 10.0f * s;
        if (pointIn(mx, my, left - 5.0f * s, top + row * i, w, 22.0f * s)) return static_cast<int>(i);
    }
    return -1;
}

int Menu_L::hitDialogControl(float mx, float my, int width, int height) const {
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    if (pointIn(mx, my, dialogX + dialogW - 28.0f * s, dialogY + 2.0f * s, 24.0f * s, 24.0f * s)) return 900;
    if (dialog == DialogKind::Empty) {
        const float bw = 76.0f * s;
        const float bh = 24.0f * s;
        const float bx = dialogX + dialogW - bw - 16.0f * s;
        const float by = dialogY + dialogH - bh - 14.0f * s;
        if (pointIn(mx, my, bx, by, bw, bh)) return 901;
    } else if (dialog == DialogKind::Host) {
        const float x = dialogX + 24.0f * s;
        const float w = dialogW - 48.0f * s;
        const float fieldH = 34.0f * s;
        float y = dialogY + 46.0f * s + 18.0f * s;
        if (pointIn(mx, my, x, y, w, fieldH)) return 920;
        if (hostDropdownOpen && !chapters.empty()) {
            const size_t visible = std::min<size_t>(8, chapters.size());
            const float listY = y + fieldH + 4.0f * s;
            const float listH = visible * 27.0f * s;
            if (pointIn(mx, my, x, listY, w, listH)) {
                size_t startIndex = std::min(hostMapScroll, chapters.size() - visible);
                const int row = std::max(0, std::min((int)visible - 1, (int)((my - listY) / (27.0f * s))));
                if (startIndex + (size_t)row < chapters.size()) return 930 + row;
            }
        }
        y += fieldH + 25.0f * s;
        y += 18.0f * s;
        if (pointIn(mx, my, x, y, w, fieldH)) return 921;
        y += fieldH + 25.0f * s;
        y += 18.0f * s;
        y += fieldH + 25.0f * s;
        y += 18.0f * s;
        if (pointIn(mx, my, x, y, w, fieldH)) return 922;
        y += fieldH + 25.0f * s;
        y += 18.0f * s;
        if (pointIn(mx, my, x, y, w, fieldH)) return 925;
        const float bh = 28.0f * s;
        const float closeW = 76.0f * s;
        const float startW = 76.0f * s;
        const float by = dialogY + dialogH - bh - 16.0f * s;
        const float closeX = dialogX + dialogW - closeW - 16.0f * s;
        const float startX = closeX - startW - 10.0f * s;
        if (pointIn(mx, my, startX, by, startW, bh)) return 923;
        if (pointIn(mx, my, closeX, by, closeW, bh)) return 924;
    }
    return -1;
}

void Menu_L::addHostScroll(double yoffset) {
    if (!hostDropdownOpen || chapters.empty()) return;
    const size_t visible = std::min<size_t>(8, chapters.size());
    const size_t maxScroll = chapters.size() > visible ? chapters.size() - visible : 0;
    const size_t step = static_cast<size_t>(std::max(1.0, std::floor(std::abs(yoffset))));
    if (yoffset > 0.0) hostMapScroll = hostMapScroll > step ? hostMapScroll - step : 0;
    else if (yoffset < 0.0) hostMapScroll = std::min(maxScroll, hostMapScroll + step);
    if (hostMapIndex < hostMapScroll) hostMapIndex = hostMapScroll;
    if (hostMapIndex >= hostMapScroll + visible) hostMapIndex = hostMapScroll + visible - 1;
}

bool Menu_L::isKeyPressed(GLFWwindow* window, int key) const { return keyPressed(window, key); }
bool Menu_L::keyPressed(GLFWwindow* window, int key) { return window && glfwGetKey(window, key) == GLFW_PRESS; }

void Menu_L::selectCurrentChapter() {
    if (chapters.empty() || selectedChapter >= chapters.size()) return;
    selectedMapName = chapters[selectedChapter].mapName;
}

void Menu_L::activateMainControl(int control, int width, int height, MenuResult& result) {
    if (control < 0) return;
    if (!pauseMode) {
        switch (control) {
            case 0: openChapterSelector(width, height); break;
            case 1: openHostDialog(width, height); break;
            case 2: openEmptyDialog("Load Game", width, height); break;
            case 3: openEmptyDialog("Options", width, height); break;
            case 4: result = MenuResult::Quit; break;
            default: break;
        }
    } else {
        switch (control) {
            case 0: result = MenuResult::Resume; break;
            case 1: openChapterSelector(width, height); break;
            case 2: openEmptyDialog("Load Game", width, height); break;
            case 3: openEmptyDialog("Options", width, height); break;
            case 4: result = MenuResult::MainMenu; break;
            case 5: result = MenuResult::Quit; break;
            default: break;
        }
    }
}

void Menu_L::activateDialogControl(int control, int width, int height, MenuResult& result) {
    (void)width; (void)height;
    if (control == 900) { closeDialog(); return; }
    if (dialog == DialogKind::Empty && control == 901) { closeDialog(); return; }
    if (dialog == DialogKind::Host) {
        if (control == 923) {
            if (!chapters.empty()) {
                selectedMapName = chapters[hostMapIndex].mapName;
                hostGameRequested_ = !selectedMapName.empty();
                if (hostGameRequested_) result = MenuResult::StartGame;
            }
            return;
        }
        if (control == 924) {
            closeDialog();
            return;
        }
        if (control == 920) {
            hostDropdownOpen = !hostDropdownOpen;
            hostField = 0;
            if (hostDropdownOpen && !chapters.empty()) {
                const size_t visible = std::min<size_t>(8, chapters.size());
                hostMapScroll = std::min(hostMapIndex, chapters.size() - visible);
            }
            return;
        }
        if (control == 921) { hostField = 1; hostDropdownOpen = false; return; }
        if (control == 922) { hostField = 2; hostDropdownOpen = false; return; }
        if (control == 925) { hostField = 3; hostDropdownOpen = false; return; }
        if (control >= 930 && control < 960 && !chapters.empty()) {
            const size_t visible = std::min<size_t>(8, chapters.size());
            const size_t startIndex = std::min(hostMapScroll, chapters.size() - visible);
            const size_t row = static_cast<size_t>(control - 930);
            if (startIndex + row < chapters.size()) {
                hostMapIndex = startIndex + row;
                if (hostMapIndex < hostMapScroll) hostMapScroll = hostMapIndex;
                if (hostMapIndex >= hostMapScroll + visible) hostMapScroll = hostMapIndex - visible + 1;
            }
            hostDropdownOpen = false;
            hostField = 0;
            return;
        }
    }
    if (dialog != DialogKind::Chapters) return;

    if (control >= 100 && control <= 102) {
        const int slot = control - 100;
        const size_t index = chapterPageStart + static_cast<size_t>(slot);
        if (index < chapters.size()) selectedChapter = index;
        return;
    }
    if (control == 103) { closeDialog(); return; }
    if (control == 104) {
        selectCurrentChapter();
        if (!selectedMapName.empty()) result = MenuResult::StartGame;
        return;
    }
    if (control == 110) {
        if (chapterPageStart > 0) {
            const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
            chapterPageStart = chapterPageStart >= pageSize ? chapterPageStart - pageSize : 0;
            selectedChapter = chapterPageStart;
        }
        return;
    }
    if (control == 111) {
        const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
        if (chapterPageStart + pageSize < chapters.size()) {
            chapterPageStart += pageSize;
            selectedChapter = chapterPageStart;
        }
    }
}

void Menu_L::handleCharInput(unsigned int codepoint) {
    if (dialog != DialogKind::Host || hostField != 3 || codepoint < 32 || codepoint == 127) return;
    std::string encoded;
    if (codepoint <= 0x7F) {
        encoded.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        encoded.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        encoded.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        encoded.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        encoded.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
    if (!encoded.empty() && playerNick.size() + encoded.size() <= 64) playerNick += encoded;
}

bool Menu_L::handleDialogInput(GLFWwindow* window, int width, int height, MenuResult& result) {
    if (GameConsole_IsOpen()) return false;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const int hit = hitDialogControl(mx, my, width, height);
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const bool titleHit = pointIn((float)mx, (float)my, dialogX, dialogY, dialogW, 27.0f * s);

    if (dialog == DialogKind::Host) {
        const bool ctrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        const bool paste = ctrl && glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS;
        const bool copy = ctrl && glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
        const bool backspace = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        if (paste && !hostPasteWasDown && hostField == 2) {
            const char* clip = glfwGetClipboardString(window);
            if (clip && *clip) {
                std::string value = clip;
                value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
                value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
                std::replace(value.begin(), value.end(), '\\', '/');
                if (value.rfind("models/", 0) == 0) hostModelPath = value.substr(0, 160);
            }
        }
        if (paste && !hostPasteWasDown && hostField == 3) {
            const char* clip = glfwGetClipboardString(window);
            if (clip && *clip) {
                std::string value = clip;
                value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
                value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
                while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
                if (value.size() > 64) value.resize(64);
                if (!value.empty()) playerNick = value;
            }
        }
        if (copy && hostField == 3) glfwSetClipboardString(window, playerNick.c_str());
        if (backspace && !hostBackspaceWasDown && hostField == 2 && !hostModelPath.empty()) hostModelPath.pop_back();
        if (backspace && !hostBackspaceWasDown && hostField == 1 && !hostPortText.empty()) hostPortText.pop_back();
        if (backspace && !hostBackspaceWasDown && hostField == 3 && !playerNick.empty()) {
            while (!playerNick.empty() && (static_cast<unsigned char>(playerNick.back()) & 0xC0) == 0x80) playerNick.pop_back();
            if (!playerNick.empty()) playerNick.pop_back();
        }
        for (int d = 0; d < 10; ++d) {
            const bool down = glfwGetKey(window, GLFW_KEY_0 + d) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_KP_0 + d) == GLFW_PRESS;
            if (down && !hostDigitWasDown[(size_t)d] && hostField == 1 && hostPortText.size() < 5) hostPortText.push_back((char)('0' + d));
            hostDigitWasDown[(size_t)d] = down;
        }
        if (hostPortText.empty()) hostPortText = "0";
        hostPasteWasDown = paste;
        hostBackspaceWasDown = backspace;
    }

    if (mouseDown && !mouseWasDown) {
        if (hit == 900 || hit == 901 || hit == 923 || hit == 924 || (hit >= 930 && hit < 960)) pressedControl = hit;
        else if (dialog == DialogKind::Chapters) {
            const float bodyX = dialogX + 18.0f * s;
            const float innerW = dialogW - 36.0f * s;
            const float gap = 14.0f * s;
            const float cardW = std::min(218.0f * s, (innerW - gap * 2.0f) / 3.0f);
            const float imageH = cardW * 0.56f;
            const float cardTop = dialogY + 47.0f * s;
            for (int slot = 0; slot < 3; ++slot) {
                const float x = bodyX + slot * (cardW + gap);
                if (pointIn((float)mx, (float)my, x, cardTop + 34.0f * s, cardW, imageH)) { pressedControl = 100 + slot; break; }
            }
            if (pressedControl == -1) {
                if (pointIn((float)mx, (float)my, bodyX, cardTop + imageH + 66.0f * s, 30.0f * s, 28.0f * s)) pressedControl = 110;
                else if (pointIn((float)mx, (float)my, bodyX + innerW - 30.0f * s, cardTop + imageH + 66.0f * s, 30.0f * s, 28.0f * s)) pressedControl = 111;
                else {
                    const float bh = 24.0f * s;
                    const float startW = 88.0f * s;
                    const float cancelW = 74.0f * s;
                    const float by = dialogY + dialogH - bh - 15.0f * s;
                    const float cancelX = dialogX + dialogW - cancelW - 15.0f * s;
                    const float startX = cancelX - startW - 8.0f * s;
                    if (pointIn((float)mx, (float)my, startX, by, startW, bh)) pressedControl = 104;
                    else if (pointIn((float)mx, (float)my, cancelX, by, cancelW, bh)) pressedControl = 103;
                }
            }
        } else if (dialog == DialogKind::Host && hit != -1) {
            pressedControl = hit;
        } else if (titleHit) {
            draggingDialog = true;
            dragOffsetX = static_cast<float>(mx) - dialogX;
            dragOffsetY = static_cast<float>(my) - dialogY;
        }
    }

    if (draggingDialog && mouseDown) {
        dialogX = static_cast<float>(mx) - dragOffsetX;
        dialogY = static_cast<float>(my) - dragOffsetY;
        clampDialog(width, height);
    }

    if (!mouseDown && mouseWasDown) {
        if (pressedControl >= 0) {
            const int releaseControl = pressedControl;
            if (releaseControl == 900 || releaseControl == 901 || releaseControl == 923 || releaseControl == 924 ||
                (releaseControl >= 930 && releaseControl < 960) || releaseControl == 920 || releaseControl == 921 || releaseControl == 922 ||
                releaseControl == 100 || releaseControl == 101 || releaseControl == 102 || releaseControl == 103 ||
                releaseControl == 104 || releaseControl == 110 || releaseControl == 111) {
                activateDialogControl(releaseControl, width, height, result);
            }
        }
        pressedControl = -1;
        draggingDialog = false;
    }

    const bool left = isKeyPressed(window, GLFW_KEY_LEFT);
    const bool right = isKeyPressed(window, GLFW_KEY_RIGHT);
    const bool up = isKeyPressed(window, GLFW_KEY_UP);
    const bool down = isKeyPressed(window, GLFW_KEY_DOWN);
    const bool enter = isKeyPressed(window, GLFW_KEY_ENTER) || isKeyPressed(window, GLFW_KEY_KP_ENTER);
    const bool escape = isKeyPressed(window, GLFW_KEY_ESCAPE);

    if (dialog == DialogKind::Chapters) {
        if (left && !leftWasDown) {
            if (selectedChapter > 0) --selectedChapter;
            const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
            chapterPageStart = (selectedChapter / pageSize) * pageSize;
        }
        if (right && !rightWasDown) {
            if (selectedChapter + 1 < chapters.size()) ++selectedChapter;
            const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
            chapterPageStart = (selectedChapter / pageSize) * pageSize;
        }
        if (enter && !enterWasDown) {
            selectCurrentChapter();
            if (!selectedMapName.empty()) result = MenuResult::StartGame;
        }
    } else if (dialog == DialogKind::Host) {
        if (hostDropdownOpen && !chapters.empty()) {
            const size_t visible = std::min<size_t>(8, chapters.size());
            if (up && !upWasDown) {
                if (hostMapIndex > 0) --hostMapIndex;
                if (hostMapIndex < hostMapScroll) hostMapScroll = hostMapIndex;
            }
            if (down && !downWasDown) {
                if (hostMapIndex + 1 < chapters.size()) ++hostMapIndex;
                if (hostMapIndex >= hostMapScroll + visible) hostMapScroll = hostMapIndex - visible + 1;
            }
        }
        if (enter && !enterWasDown && !hostDropdownOpen && !chapters.empty()) {
            selectedMapName = chapters[hostMapIndex].mapName;
            hostGameRequested_ = !selectedMapName.empty();
            if (hostGameRequested_) result = MenuResult::StartGame;
        }
    }

    if (escape && !escapeWasDown) closeDialog();
    mouseWasDown = mouseDown;
    leftWasDown = left;
    rightWasDown = right;
    upWasDown = up;
    downWasDown = down;
    enterWasDown = enter;
    escapeWasDown = escape;
    return result != MenuResult::None;
}

bool Menu_L::handleMenuInput(GLFWwindow* window, int width, int height, MenuResult& result) {
    if (GameConsole_IsOpen()) return false;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    const int hoverControl = hitMainControl(mx, my, width, height);
    if (mouseDown && !mouseWasDown) pressedControl = hoverControl;
    if (!mouseDown && mouseWasDown) {
        const int clicked = (pressedControl >= 0 && pressedControl == hoverControl) ? pressedControl : -1;
        pressedControl = -1;
        if (clicked >= 0) activateMainControl(clicked, width, height, result);
    }

    const bool up = isKeyPressed(window, GLFW_KEY_UP);
    const bool down = isKeyPressed(window, GLFW_KEY_DOWN);
    const bool enter = isKeyPressed(window, GLFW_KEY_ENTER) || isKeyPressed(window, GLFW_KEY_KP_ENTER);
    const bool escape = isKeyPressed(window, GLFW_KEY_ESCAPE);
    const int itemCount = pauseMode ? 6 : 5;
    if (up && !upWasDown) mainSelection = (mainSelection + itemCount - 1) % itemCount;
    if (down && !downWasDown) mainSelection = (mainSelection + 1) % itemCount;
    if (enter && !enterWasDown) activateMainControl(mainSelection, width, height, result);

    if (escape && !escapeWasDown) {
        if (pauseMode) result = MenuResult::Resume;
        else result = MenuResult::Quit;
    }

    mouseWasDown = mouseDown;
    upWasDown = up;
    downWasDown = down;
    enterWasDown = enter;
    escapeWasDown = escape;
    return result != MenuResult::None;
}

std::string Menu_L::upperTitle(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

void Menu_L::handleConsoleOverlayInput(GLFWwindow* window, int width, int height) {
    if (!window || !GameConsole_IsOpen() || width <= 0 || height <= 0) return;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const float headerH = 28.0f;
    const float grip = 18.0f;
    const float inputH = 28.0f;
    const auto suggestions = GameConsole_GetSuggestions();
    const int suggestionCount = std::min(5, static_cast<int>(suggestions.size()));
    const float suggestionRowH = 22.0f;
    const float suggestionH = suggestionCount > 0 ? suggestionCount * suggestionRowH : 0.0f;
    const float suggestionY = consoleY + consoleH - 9.0f - suggestionH;
    const float inputY = suggestionY - inputH - (suggestionCount > 0 ? 4.0f : 0.0f);
    if (down && !consoleMouseWasDown) {
        for (int i = 0; i < suggestionCount; ++i) {
            if (pointIn((float)mx, (float)my, consoleX + 8.0f, suggestionY + i * suggestionRowH, consoleW - 16.0f, suggestionRowH - 2.0f)) {
                GameConsole_SetInput(suggestions[static_cast<size_t>(i)]);
                consoleMouseWasDown = down;
                return;
            }
        }
        if (pointIn((float)mx, (float)my, consoleX, consoleY, consoleW, headerH)) {
            consoleDragging = true;
            consoleDragOffsetX = (float)mx - consoleX;
            consoleDragOffsetY = (float)my - consoleY;
        } else if (pointIn((float)mx, (float)my, consoleX + consoleW - grip, consoleY + consoleH - grip, grip, grip)) {
            consoleResizing = true;
        }
    }
    if (down && consoleDragging) {
        consoleX = (float)mx - consoleDragOffsetX;
        consoleY = (float)my - consoleDragOffsetY;
    }
    if (down && consoleResizing) {
        consoleW = std::max(360.0f, (float)mx - consoleX);
        consoleH = std::max(200.0f, (float)my - consoleY);
    }
    if (!down && consoleMouseWasDown) {
        consoleDragging = false;
        consoleResizing = false;
    }
    consoleW = std::min(consoleW, std::max(360.0f, width - 20.0f));
    consoleH = std::min(consoleH, std::max(200.0f, height - 20.0f));
    consoleX = std::max(10.0f, std::min(consoleX, width - consoleW - 10.0f));
    consoleY = std::max(10.0f, std::min(consoleY, height - consoleH - 10.0f));
    consoleMouseWasDown = down;
}

void Menu_L::renderConsoleOverlay(int width, int height) {
    if (!GameConsole_IsOpen() || !fontsReady || width <= 0 || height <= 0) return;
    consoleW = std::min(consoleW, std::max(360.0f, width - 20.0f));
    consoleH = std::min(consoleH, std::max(200.0f, height - 20.0f));
    consoleX = std::max(10.0f, std::min(consoleX, width - consoleW - 10.0f));
    consoleY = std::max(10.0f, std::min(consoleY, height - consoleH - 10.0f));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                                                                                
                                                                              
    drawRoundedRect(consoleX, consoleY, consoleW, consoleH, 8.0f,
                    0.46f, 0.46f, 0.46f, 0.78f);
    drawRoundedRect(consoleX + 1.0f, consoleY + 1.0f, consoleW - 2.0f, 27.0f, 6.0f,
                    0.36f, 0.37f, 0.38f, 0.88f);
    drawText("Console", consoleX + 12.0f, consoleY + 5.0f, fontSmall,
             0.96f, 0.96f, 0.96f, 0.98f);
    drawText("~", consoleX + consoleW - 24.0f, consoleY + 5.0f, fontSmall,
             0.86f, 0.86f, 0.86f, 0.90f);

    const float inputH = 28.0f;
    const float pad = 10.0f;
    const auto suggestions = GameConsole_GetSuggestions();
    const int suggestionCount = std::min(5, static_cast<int>(suggestions.size()));
    const float suggestionRowH = 22.0f;
    const float suggestionH = suggestionCount > 0 ? suggestionCount * suggestionRowH : 0.0f;
    const float suggestionY = consoleY + consoleH - 9.0f - suggestionH;
    const float inputY = suggestionY - inputH - (suggestionCount > 0 ? 4.0f : 0.0f);
    const float contentTop = consoleY + 34.0f;
    const float contentBottom = inputY - 12.0f;
    const float lineH = std::max(14.0f, fontTiny.pixelHeight + 2.0f);
    const int maxLines = std::max(1, static_cast<int>((contentBottom - contentTop) / lineH));
    const auto lines = GameConsole_GetLines();
    int startLine = static_cast<int>(lines.size()) - maxLines;
    if (startLine < 0) startLine = 0;
    for (int i = startLine; i < static_cast<int>(lines.size()); ++i) {
        const auto& line = lines[static_cast<size_t>(i)];
        drawText(line.first, consoleX + pad, contentTop + (i - startLine) * lineH,
                 fontTiny, line.second ? 0.96f : 0.91f,
                 line.second ? 0.24f : 0.91f,
                 line.second ? 0.24f : 0.91f, 0.98f);
    }

    drawRoundedRect(consoleX + pad - 2.0f, inputY,
                    consoleW - pad * 2.0f + 4.0f, inputH, 4.0f,
                    0.18f, 0.20f, 0.21f, 0.88f);
    drawText("> " + GameConsole_GetInput(), consoleX + pad + 4.0f,
             inputY + 5.0f, fontTiny,
             0.96f, 0.96f, 0.96f, 1.0f);

    for (int i = 0; i < suggestionCount; ++i) {
        const float sy = suggestionY + i * suggestionRowH;
        drawRoundedRect(consoleX + 8.0f, sy, consoleW - 16.0f, suggestionRowH - 2.0f, 3.0f,
                        0.14f, 0.16f, 0.17f, 0.92f);
        drawText(suggestions[static_cast<size_t>(i)], consoleX + 16.0f, sy + 3.0f, fontTiny,
                 0.88f, 0.91f, 0.94f, 0.98f);
    }

    drawLine(consoleX + consoleW - 12.0f, consoleY + consoleH - 4.0f,
             consoleX + consoleW - 4.0f, consoleY + consoleH - 12.0f, 1.0f, 0.55f);
    drawLine(consoleX + consoleW - 8.0f, consoleY + consoleH - 4.0f,
             consoleX + consoleW - 4.0f, consoleY + consoleH - 8.0f, 1.0f, 0.55f);
    glDisable(GL_BLEND);
}

MenuResult Menu_L::runMainMenu(GLFWwindow* window) {
    if (!window) return MenuResult::Quit;
    ownerWindow = window;
    pauseMode = false;
    closeDialog();
    selectedMapName.clear();
    hostGameRequested_ = false;
    mainSelection = 0;
    pressedControl = -1;
    mouseWasDown = false;
    upWasDown = downWasDown = leftWasDown = rightWasDown = enterWasDown = false;
    escapeWasDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 0 || h <= 0) continue;

        MenuResult result = MenuResult::None;
        if(previewMapLoaded) {
            renderPreview3D(w,h);
            begin2D(w,h,false);
        } else begin2D(w,h,true);
        renderMainMenu(w,h);
        if (dialog != DialogKind::None) renderDialog(w, h);
                                                                                
                                                                          
        handleConsoleOverlayInput(window, w, h);
        if (GameConsole_IsOpen()) {
            renderConsoleOverlay(w, h);
        }
        if (dialog != DialogKind::None) handleDialogInput(window, w, h, result);
        else handleMenuInput(window, w, h, result);

        std::string forcedMap;
        if (result == MenuResult::None && GameConsole_TakeMapCommand(forcedMap)) {
            selectedMapName = forcedMap;
            result = MenuResult::StartGame;
        }

        Multiplayer_L::tick(0.0f);
        while (result == MenuResult::None && GameConsole_PollPassthrough()) {}

        std::string joinMap;
        if (result == MenuResult::None && GameConsole_TakeJoinMapRequest(joinMap)) {
            selectedMapName = joinMap;
            result = MenuResult::StartGame;
        }

        end2D();
        glfwSwapBuffers(window);

        if (result == MenuResult::Quit || result == MenuResult::StartGame) return result;
    }
    return MenuResult::Quit;
}

void Menu_L::captureCurrentFrame(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (!pauseFrameTexture || pauseFrameWidth != width || pauseFrameHeight != height) {
        if (pauseFrameTexture) glDeleteTextures(1, &pauseFrameTexture);
        glGenTextures(1, &pauseFrameTexture);
        glBindTexture(GL_TEXTURE_2D, pauseFrameTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        pauseFrameWidth = width;
        pauseFrameHeight = height;
    }
    glBindTexture(GL_TEXTURE_2D, pauseFrameTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
}

MenuResult Menu_L::runPauseMenu(GLFWwindow* window) {
    if (!window) return MenuResult::Resume;
    ownerWindow = window;
    pauseMode = true;
    closeDialog();
    mainSelection = 0;
    pressedControl = -1;
    mouseWasDown = false;
    upWasDown = downWasDown = leftWasDown = rightWasDown = enterWasDown = false;
    escapeWasDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 0 || h <= 0) continue;

        MenuResult result = MenuResult::None;
        begin2D(w, h, false);
        renderMainMenu(w, h);
        if (dialog != DialogKind::None) renderDialog(w, h);
        handleConsoleOverlayInput(window, w, h);
        if (GameConsole_IsOpen()) renderConsoleOverlay(w, h);
        if (dialog != DialogKind::None) handleDialogInput(window, w, h, result);
        else handleMenuInput(window, w, h, result);

        std::string forcedMap;
        if (result == MenuResult::None && GameConsole_TakeMapCommand(forcedMap)) {
            selectedMapName = forcedMap;
            result = MenuResult::StartGame;
        }

        Multiplayer_L::tick(0.0f);
        while (result == MenuResult::None && GameConsole_PollPassthrough()) {}

        std::string joinMap;
        if (result == MenuResult::None && GameConsole_TakeJoinMapRequest(joinMap)) {
            selectedMapName = joinMap;
            result = MenuResult::StartGame;
        }

        end2D();
        glfwSwapBuffers(window);

        if (result != MenuResult::None)
            return result;
    }
    return MenuResult::MainMenu;
}
