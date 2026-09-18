#include <windows.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <csignal>
#include <thread>
#include <mutex>
#include <queue>
#include <utility>
#include <deque>
#include <streambuf>
#include <chrono>
#include <exception>
#include <cstdio>
#include <cmath>
#include <cctype>
#ifdef _WIN32
#include <excpt.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "camera.h"
#include "bsp_l.h"
#include "texture_l.h"
#include "prop_static.h"
#include "prop_dynamic.h"
#include "prop_physics.h"
#include "light_l.h"
#include "step_sound_l.h"
#include "sound_l.h"
#include "entity_l.h"
#include "item_suit.h"
#include "weapon_l.h"
#include "hud_l.h"
#include "postprocess_l.h"
#include "menu.h"
#include "menu_kv.h"

static void ShowEngineError(const std::string& message) {
    MessageBoxA(NULL, message.c_str(), "Engine Error", MB_ICONERROR | MB_OK);
}

void CrashHandler(int signal) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer),
        "The engine crashed.\n\nSignal: %d", signal);
    ShowEngineError(buffer);
    std::_Exit(1);
}

#ifdef _WIN32
LONG WINAPI EngineUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    ULONG_PTR address = (info && info->ExceptionRecord)
        ? reinterpret_cast<ULONG_PTR>(info->ExceptionRecord->ExceptionAddress) : 0;

    char buffer[384]{};
    std::snprintf(buffer, sizeof(buffer),
        "The engine crashed.\n\nException code: 0x%08lX\nAddress: 0x%llX",
        static_cast<unsigned long>(code),
        static_cast<unsigned long long>(address));
    ShowEngineError(buffer);
    ExitProcess(1);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

struct ConsoleLineEntry {
    std::string text;
    bool error = false;
};

std::queue<std::string> consoleQueue;
std::mutex consoleMutex;
std::mutex consoleLogMutex;
std::deque<ConsoleLineEntry> consoleLines;
std::vector<std::string> consoleHistory;
std::string consoleInput;
int consoleHistoryIndex = -1;
bool consoleOpen = false;
double consoleInputLockUntil = 0.0;
bool inGameplay = false;
bool pauseMenuRequest = false;
bool pauseMenuActive = false;

static bool ConsoleLooksLikeError(const std::string& text) {
    std::string s = text;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.find("error") != std::string::npos ||
           s.find("failed") != std::string::npos ||
           s.find("missing") != std::string::npos ||
           s.find("cannot") != std::string::npos ||
           s.find("not found") != std::string::npos ||
           s.find("overflow") != std::string::npos;
}

static void ConsolePushLine(const std::string& text, bool forceError = false) {
    std::string value = text;
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    if (value.empty()) return;
    std::lock_guard<std::mutex> lock(consoleLogMutex);
    consoleLines.push_back({value, forceError || ConsoleLooksLikeError(value)});
    while (consoleLines.size() > 800) consoleLines.pop_front();
}

class ConsoleCaptureBuf final : public std::streambuf {
public:
    explicit ConsoleCaptureBuf(bool isError) : errorStream(isError) {}
    ~ConsoleCaptureBuf() override = default;

protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) append(static_cast<char>(ch));
        return ch;
    }

    std::streamsize xsputn(const char* str, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i) append(str[i]);
        return count;
    }

    int sync() override {
        flushPending();
        return 0;
    }

private:
    bool errorStream;
    std::string pending;

    void append(char c) {
        if (c == '\n') flushPending();
        else if (c != '\r') pending.push_back(c);
    }

    void flushPending() {
        if (!pending.empty()) {
            ConsolePushLine(pending, errorStream);
            pending.clear();
        }
    }
};

static ConsoleCaptureBuf gConsoleOutBuf(false);
static ConsoleCaptureBuf gConsoleErrBuf(true);
static std::streambuf* gOriginalCout = nullptr;
static std::streambuf* gOriginalCerr = nullptr;

static void InstallGameConsoleOutputCapture() {
    gOriginalCout = std::cout.rdbuf(&gConsoleOutBuf);
    gOriginalCerr = std::cerr.rdbuf(&gConsoleErrBuf);
}

bool GameConsole_IsOpen() {
    return consoleOpen;
}

std::vector<std::pair<std::string, bool>> GameConsole_GetLines() {
    std::lock_guard<std::mutex> lock(consoleLogMutex);
    std::vector<std::pair<std::string, bool>> result;
    result.reserve(consoleLines.size());
    for (const ConsoleLineEntry& entry : consoleLines)
        result.emplace_back(entry.text, entry.error);
    return result;
}

std::string GameConsole_GetInput() {
    return consoleInput;
}

static void GameConsoleSubmitInput() {
    std::string line = consoleInput;
    size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos) {
        size_t last = line.find_last_not_of(" \t");
        line = line.substr(first, last - first + 1);
    } else {
        line.clear();
    }
    if (line.empty()) return;

    ConsolePushLine("> " + line);
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        consoleQueue.push(line);
    }
    if (consoleHistory.empty() || consoleHistory.back() != line)
        consoleHistory.push_back(line);
    if (consoleHistory.size() > 128) consoleHistory.erase(consoleHistory.begin());
    consoleHistoryIndex = -1;
    consoleInput.clear();
}

bool GameConsole_TakeMapCommand(std::string& outMap) {
    outMap.clear();

    std::lock_guard<std::mutex> lock(consoleMutex);
    if (consoleQueue.empty()) return false;

    std::queue<std::string> remaining;
    bool found = false;

    while (!consoleQueue.empty()) {
        std::string cmd = consoleQueue.front();
        consoleQueue.pop();

        std::string trimmed = cmd;
        size_t first = trimmed.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        size_t last = trimmed.find_last_not_of(" \t\r\n");
        trimmed = trimmed.substr(first, last - first + 1);

        size_t split = trimmed.find_first_of(" \t");
        std::string verb = (split == std::string::npos) ? trimmed : trimmed.substr(0, split);
        std::transform(verb.begin(), verb.end(), verb.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (!found && verb == "map" && split != std::string::npos) {
            size_t argStart = trimmed.find_first_not_of(" \t", split);
            if (argStart != std::string::npos) {
                size_t argEnd = trimmed.find_last_not_of(" \t");
                outMap = trimmed.substr(argStart, argEnd - argStart + 1);
                if (!outMap.empty()) {
                    found = true;
                    continue;
                }
            }
        }

        remaining.push(trimmed);
    }

    consoleQueue.swap(remaining);

    if (found) {
                                                                                
                                                                              
        consoleOpen = false;
        consoleInput.clear();
        consoleHistoryIndex = -1;
        consoleInputLockUntil = 0.0;
    }
    return found;
}

static void GameConsoleApplyOpenState(GLFWwindow* window) {
    if (!window) return;
    if (consoleOpen) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (pauseMenuActive || !inGameplay) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void GameConsole_HandleMenuToggle(GLFWwindow* window) {
    GameConsoleApplyOpenState(window);
}

static void GameConsoleKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (consoleOpen) {
                                                                                 
                                                                         
            consoleOpen = false;
            GameConsoleApplyOpenState(window);
        } else if (inGameplay) {
            pauseMenuRequest = true;
        }
        return;
    }

    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        if (consoleOpen) {
                                                                                   
                                                                                 
            consoleOpen = false;
            GameConsoleApplyOpenState(window);
        } else if (inGameplay) {
                                                                                
                                                              
            consoleOpen = true;
            consoleInput.clear();
            consoleHistoryIndex = -1;
            consoleInputLockUntil = glfwGetTime() + 0.20;
            pauseMenuRequest = true;
            GameConsoleApplyOpenState(window);
        } else if (pauseMenuActive || !inGameplay) {
                                                                              
                                                                                
            consoleOpen = true;
            consoleInput.clear();
            consoleHistoryIndex = -1;
            consoleInputLockUntil = glfwGetTime() + 0.20;
            GameConsoleApplyOpenState(window);
        }
        return;
    }

    if (!consoleOpen) return;
    if (glfwGetTime() < consoleInputLockUntil) return;

    if (key == GLFW_KEY_BACKSPACE) {
        if (!consoleInput.empty()) consoleInput.pop_back();
        return;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        GameConsoleSubmitInput();
        return;
    }
    if (key == GLFW_KEY_UP) {
        if (!consoleHistory.empty()) {
            if (consoleHistoryIndex < 0) consoleHistoryIndex = static_cast<int>(consoleHistory.size()) - 1;
            else consoleHistoryIndex = std::max(0, consoleHistoryIndex - 1);
            consoleInput = consoleHistory[static_cast<size_t>(consoleHistoryIndex)];
        }
        return;
    }
    if (key == GLFW_KEY_DOWN) {
        if (!consoleHistory.empty() && consoleHistoryIndex >= 0) {
            if (consoleHistoryIndex + 1 < static_cast<int>(consoleHistory.size())) {
                ++consoleHistoryIndex;
                consoleInput = consoleHistory[static_cast<size_t>(consoleHistoryIndex)];
            } else {
                consoleHistoryIndex = -1;
                consoleInput.clear();
            }
        }
    }
}

static void GameConsoleCharCallback(GLFWwindow* window, unsigned int codepoint) {
    (void)window;
    if (!consoleOpen) return;
    if (glfwGetTime() < consoleInputLockUntil) return;
    if (codepoint >= 32 && codepoint != 127 && consoleInput.size() < 512)
        consoleInput.push_back(static_cast<char>(codepoint < 128 ? codepoint : '?'));
}

struct TransitionState {
    bool active = false;
    Vec3 relativePos{ 0, 0, 0 };
    Vec3 velocity{ 0, 0, 0 };
    float yaw = 0.0f;
    float pitch = 0.0f;
    std::string landmarkName = "";
    int health = 100;
    float sprintEnergy = 100.0f;
    bool hasSuit = false;
    std::vector<std::string> weaponInventory;
    int activeWeaponSlot = 0;
};

std::string selectedMap = "";
std::string gameDir = "hl2";
std::string gameTitle = "Sauce name";
bool disableTextures = false;
bool disableLighting = false;
int windowWidth = 800;
int windowHeight = 600;
unsigned int logoTexture = 0;
int logoWidth = 0, logoHeight = 0;
bool logoLoaded = false;

void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    if (width > 0 && height > 0) {
        windowWidth = width;
        windowHeight = height;
        glViewport(0, 0, width, height);
    }
}

void DrawLogoOverlay(int w, int h) {
    if (!logoLoaded || logoTexture == 0) return;

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBindTexture(GL_TEXTURE_2D, logoTexture);

    float maxDim = static_cast<float>(std::min(w, h));
    float size = maxDim * 0.15f;
    if (size > 200.0f) size = 200.0f;
    float aspect = (logoHeight > 0) ? static_cast<float>(logoWidth) / static_cast<float>(logoHeight) : 1.0f;
    float drawW = size * aspect;
    float drawH = size;
    if (aspect > 1.0f) { drawW = size; drawH = size / aspect; }

    float cx = w / 2.0f;
    float cy = h / 2.0f;
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(cx - drawW / 2, cy - drawH / 2);
    glTexCoord2f(1, 1); glVertex2f(cx + drawW / 2, cy - drawH / 2);
    glTexCoord2f(1, 0); glVertex2f(cx + drawW / 2, cy + drawH / 2);
    glTexCoord2f(0, 0); glVertex2f(cx - drawW / 2, cy + drawH / 2);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
}

void DrawSkybox(const Vec3& camPos, unsigned int tex[6]) {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);

    float s = 1000.0f;
    float cx = camPos.x, cy = camPos.y, cz = camPos.z;
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex3f(cx + s, cy - s, cz + s);
    glTexCoord2f(1, 1); glVertex3f(cx + s, cy - s, cz - s);
    glTexCoord2f(1, 0); glVertex3f(cx + s, cy + s, cz - s);
    glTexCoord2f(0, 0); glVertex3f(cx + s, cy + s, cz + s);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex3f(cx - s, cy - s, cz - s);
    glTexCoord2f(1, 1); glVertex3f(cx - s, cy - s, cz + s);
    glTexCoord2f(1, 0); glVertex3f(cx - s, cy + s, cz + s);
    glTexCoord2f(0, 0); glVertex3f(cx - s, cy + s, cz - s);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, tex[2]);
    glBegin(GL_QUADS);
    glTexCoord2f(1, 1); glVertex3f(cx + s, cy + s, cz - s);
    glTexCoord2f(0, 1); glVertex3f(cx - s, cy + s, cz - s);
    glTexCoord2f(0, 0); glVertex3f(cx - s, cy + s, cz + s);
    glTexCoord2f(1, 0); glVertex3f(cx + s, cy + s, cz + s);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, tex[3]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(cx - s, cy - s, cz - s);
    glTexCoord2f(1, 0); glVertex3f(cx + s, cy - s, cz - s);
    glTexCoord2f(1, 1); glVertex3f(cx + s, cy - s, cz + s);
    glTexCoord2f(0, 1); glVertex3f(cx - s, cy - s, cz + s);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, tex[4]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex3f(cx + s, cy - s, cz - s);
    glTexCoord2f(1, 1); glVertex3f(cx - s, cy - s, cz - s);
    glTexCoord2f(1, 0); glVertex3f(cx - s, cy + s, cz - s);
    glTexCoord2f(0, 0); glVertex3f(cx + s, cy + s, cz - s);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, tex[5]);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex3f(cx - s, cy - s, cz + s);
    glTexCoord2f(1, 1); glVertex3f(cx + s, cy - s, cz + s);
    glTexCoord2f(1, 0); glVertex3f(cx + s, cy + s, cz + s);
    glTexCoord2f(0, 0); glVertex3f(cx - s, cy + s, cz + s);
    glEnd();

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

static bool FindDirectKVValue(const MenuKVNode& node,
                              const char* key1, const char* key2,
                              std::string& out) {
    for (const auto& child : node.children) {
        std::string name = child.name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (child.children.empty() && !child.values.empty() &&
            (name == key1 || (key2 && name == key2))) {
            if (!child.values.front().empty()) {
                out = child.values.front();
                return true;
            }
        }
    }
    return false;
}

static bool FindKVValueRecursive(const std::vector<MenuKVNode>& nodes,
                                 const char* key1, const char* key2,
                                 std::string& out, int depth = 0) {
    if (depth > 8) return false;
    for (const auto& node : nodes) {
        if (FindDirectKVValue(node, key1, key2, out)) return true;
        if (!node.children.empty() && FindKVValueRecursive(node.children, key1, key2, out, depth + 1)) return true;
    }
    return false;
}

void parseGameInfo(Texture_L* fileSystem) {
    std::string text;
    if (fileSystem) {
        std::vector<char> raw;
        if (fileSystem->getFileRawData("gameinfo.txt", raw) && !raw.empty()) text.assign(raw.begin(), raw.end());
    }
    if (text.empty()) {
        std::ifstream gi(gameDir + "/gameinfo.txt", std::ios::binary);
        if (gi.is_open()) text.assign(std::istreambuf_iterator<char>(gi), std::istreambuf_iterator<char>());
    }
    if (text.empty()) return;

    const auto roots = MenuKV::parse(text);
    std::string value;
    for (const char* key : {"title", "game"}) {
        if (FindKVValueRecursive(roots, key, nullptr, value)) {
            gameTitle = value;
            return;
        }
    }
}

static Vec3 GetViewDirection(const Camera& player) {
    const float deg = 3.14159265358979323846f / 180.0f;
    const float yaw = player.yaw * deg;
    const float pitch = player.pitch * deg;
    const float cp = std::cos(pitch);
    return Vec3{std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp}.normalize();
}

static std::string TrimCommand(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

static std::string LowerCommand(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static Vec3 GetCrosshairSpawnPosition(Camera& player,
                                       BSP_L& mapParser,
                                       PropStatic_L& propManager,
                                       EntityManager& entManager) {
    const Vec3 dir = GetViewDirection(player);
    const float maxDistance = 4096.0f;
    float bestDistance = maxDistance;
    Vec3 bestNormal{0, 1, 0};
    bool hit = false;

    float d = maxDistance;
    Vec3 n;
    if (mapParser.traceRay(player.pos, dir, d, n) && d >= 0.0f && d < bestDistance) {
        bestDistance = d; bestNormal = n; hit = true;
    }
    d = maxDistance;
    if (propManager.traceRay(player.pos, dir, d, n) && d >= 0.0f && d < bestDistance) {
        bestDistance = d; bestNormal = n; hit = true;
    }
    d = maxDistance;
    if (entManager.traceRay(player.pos, dir, d, n) && d >= 0.0f && d < bestDistance) {
        bestDistance = d; bestNormal = n; hit = true;
    }

    if (hit) {
        const float normalLength = bestNormal.length();
        Vec3 offsetNormal = normalLength > 0.001f ? bestNormal * (1.0f / normalLength) : Vec3{0, 1, 0};
        return player.pos + dir * bestDistance + offsetNormal * 4.0f;
    }
    return player.pos + dir * 128.0f;
}

static bool FinishPauseResult(MenuResult pauseResult, Menu_L& menu,
                              GLFWwindow* window, bool& appQuitRequested,
                              std::string& selectedMapOut, bool& seamlessTransitionOut) {
    if (pauseResult == MenuResult::Resume) return true;
    if (pauseResult == MenuResult::StartGame) {
        if (!menu.selectedMap().empty()) {
            selectedMapOut = menu.selectedMap();
            seamlessTransitionOut = false;
        }
        return false;
    }
    if (pauseResult == MenuResult::MainMenu) {
        selectedMapOut.clear();
        seamlessTransitionOut = false;
        return false;
    }
    if (pauseResult == MenuResult::Quit) {
        appQuitRequested = true;
        return false;
    }
    (void)window;
    return true;
}

int main(int argc, char* argv[]) {
    std::signal(SIGSEGV, CrashHandler);
    std::signal(SIGABRT, CrashHandler);
    std::signal(SIGFPE, CrashHandler);
    std::signal(SIGILL, CrashHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(EngineUnhandledExceptionFilter);
    FreeConsole();
#endif
    InstallGameConsoleOutputCapture();
    ConsolePushLine("Game console initialized.");

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-game" && i + 1 < argc) gameDir = argv[++i];
        else if (arg == "-map" && i + 1 < argc) selectedMap = argv[++i];
        else if (arg == "-notexture") disableTextures = true;
        else if (arg == "-nolight") disableLighting = true;
    }

    if (!glfwInit()) {
        ShowEngineError("Could not initialize the graphics subsystem.");
        return 1;
    }

    {
        Texture_L bootstrapFileSystem;
        bootstrapFileSystem.disableTextures = true;
        bootstrapFileSystem.init(gameDir);
        parseGameInfo(&bootstrapFileSystem);
    }

    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, gameTitle.c_str(), NULL, NULL);
    if (!window) {
        glfwTerminate();
        ShowEngineError("Could not create the rendering window.");
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, GameConsoleKeyCallback);
    glfwSetCharCallback(window, GameConsoleCharCallback);

    {
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW > 0 && fbH > 0) {
            windowWidth = fbW;
            windowHeight = fbH;
            glViewport(0, 0, fbW, fbH);
        }
    }

    static PostProcess_L postProcess;
    postProcess.init();

    Texture_L menuTextureParser;
    menuTextureParser.disableTextures = disableTextures;
    menuTextureParser.init(gameDir);
    parseGameInfo(&menuTextureParser);

    Menu_L menu;
    menu.init(window, &menuTextureParser, gameDir, gameTitle);
    glfwSetWindowTitle(window, gameTitle.c_str());

    stbi_set_flip_vertically_on_load(false);
    int channels = 0;
    unsigned char* imgData = stbi_load("logo.png", &logoWidth, &logoHeight, &channels, 4);
    if (imgData) {
        glGenTextures(1, &logoTexture);
        glBindTexture(GL_TEXTURE_2D, logoTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logoWidth, logoHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, imgData);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        stbi_image_free(imgData);
        logoLoaded = true;
    } else {
        std::cout << "[FileSystem] missing: logo.png" << std::endl;
    }

    TransitionState transState;
    bool seamlessTransition = false;

    if (selectedMap.empty()) {
        inGameplay = false;
        GameConsoleApplyOpenState(window);
        MenuResult menuResult = menu.runMainMenu(window);
        if (menuResult == MenuResult::Quit || menu.selectedMap().empty()) {
            menu.shutdown();
            postProcess.shutdown();
            if (logoLoaded) glDeleteTextures(1, &logoTexture);
            glfwDestroyWindow(window);
            glfwTerminate();
            return 0;
        }
        selectedMap = menu.selectedMap();
    }

    while (!glfwWindowShouldClose(window)) {
        try {
            if (selectedMap.empty()) {
                inGameplay = false;
                GameConsoleApplyOpenState(window);
                MenuResult menuResult = menu.runMainMenu(window);
                if (menuResult == MenuResult::Quit || menu.selectedMap().empty()) break;
                selectedMap = menu.selectedMap();
                seamlessTransition = false;
            }

            inGameplay = true;
            GameConsoleApplyOpenState(window);

            if (seamlessTransition && logoLoaded) {
                glFlush();
                double startTime = glfwGetTime();
                while (!glfwWindowShouldClose(window) && glfwGetTime() - startTime < 1.2) {
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    DrawLogoOverlay(windowWidth, windowHeight);
                    glfwSwapBuffers(window);
                    glfwPollEvents();
                }
            }

            Texture_L materialParser;
            materialParser.disableTextures = disableTextures;
            materialParser.init(gameDir);

            BSP_L mapParser;
            Light_L lightManager;
            lightManager.disableLighting = disableLighting;
            Sound_L::Get().init(gameDir, &materialParser);
            StepSound_L stepSound;
            stepSound.init(gameDir, &materialParser);

            EntityManager entManager;
            entManager.init(gameDir, &materialParser);
            entManager.registerEntity("item_suit", &ItemSuit::Create);
            entManager.registerEntity("prop_dynamic", &PropDynamic::Create);
            entManager.registerEntity("prop_dynamic_override", &PropDynamic::Create);
            entManager.registerEntity("prop_physics", &PropPhysics::Create);
            entManager.registerEntity("prop_physics_override", &PropPhysics::Create);

            HUD_L hudRenderer;
            Camera player;

            std::string finalMapName = selectedMap;
            std::string fullMapPath;
            if (finalMapName.find("/") != std::string::npos || finalMapName.find("\\") != std::string::npos) {
                fullMapPath = finalMapName;
            } else {
                if (finalMapName.find(".bsp") == std::string::npos) finalMapName += ".bsp";
                fullMapPath = gameDir + "/maps/" + finalMapName;
            }

            PhysicsWorld::init();
            bool mapLoaded = false;
            std::vector<char> mapData;
            if (finalMapName.find("/") == std::string::npos && finalMapName.find("\\") == std::string::npos) {
                const std::string virtualMapPath = "maps/" + finalMapName;
                if (materialParser.getFileRawData(virtualMapPath, mapData))
                    mapLoaded = mapParser.loadMapFromData(mapData, virtualMapPath);
            }
            if (!mapLoaded) mapLoaded = mapParser.loadMap(fullMapPath);

            if (!mapLoaded) {
                PhysicsWorld::shutdown();
                Sound_L::Get().shutdown();
                std::string errorMsg = "Failed to load map file:\n" + fullMapPath;
                MessageBoxA(NULL, errorMsg.c_str(), "Engine Error", MB_ICONERROR | MB_OK);
                selectedMap.clear();
                seamlessTransition = false;
                continue;
            }

            mapParser.setupTextures(materialParser);

            std::vector<Vec3> bspCollisionVertices;
            std::vector<unsigned int> bspCollisionIndices;
            for (size_t fIdx = 0; fIdx < mapParser.faces.size(); ++fIdx) {
                const auto& f = mapParser.faces[fIdx];
                int se0 = mapParser.surfedges[f.firstedge];
                int e0 = std::abs(se0);
                int vi0 = mapParser.edges[e0].v[se0 < 0 ? 1 : 0];
                Vec3 v0 = mapParser.vertices[vi0].position;
                for (int i = 1; i < f.numedges - 1; ++i) {
                    int se1 = mapParser.surfedges[f.firstedge + i];
                    int se2 = mapParser.surfedges[f.firstedge + i + 1];
                    int e1 = std::abs(se1);
                    int e2 = std::abs(se2);
                    int vi1 = mapParser.edges[e1].v[se1 < 0 ? 1 : 0];
                    int vi2 = mapParser.edges[e2].v[se2 < 0 ? 1 : 0];
                    Vec3 v1 = mapParser.vertices[vi1].position;
                    Vec3 v2 = mapParser.vertices[vi2].position;
                    unsigned int baseIdx = static_cast<unsigned int>(bspCollisionVertices.size());
                    bspCollisionVertices.push_back(v0);
                    bspCollisionVertices.push_back(v1);
                    bspCollisionVertices.push_back(v2);
                    bspCollisionIndices.push_back(baseIdx);
                    bspCollisionIndices.push_back(baseIdx + 1);
                    bspCollisionIndices.push_back(baseIdx + 2);
                }
            }
            for (const auto& tri : mapParser.dispTriangles) {
                unsigned int baseIdx = static_cast<unsigned int>(bspCollisionVertices.size());
                bspCollisionVertices.push_back(tri.v[0]);
                bspCollisionVertices.push_back(tri.v[1]);
                bspCollisionVertices.push_back(tri.v[2]);
                bspCollisionIndices.push_back(baseIdx);
                bspCollisionIndices.push_back(baseIdx + 1);
                bspCollisionIndices.push_back(baseIdx + 2);
            }
            PhysicsWorld::setWorldCollision(bspCollisionVertices, bspCollisionIndices);

            lightManager.parseLights(mapParser.entityData);
            entManager.parseMapEntities(mapParser.entityData);

            unsigned int skyTex[6] = { 0 };
            std::string sname = mapParser.mapSkyboxName;
            if (sname.empty()) sname = "sky_day01_01";
            skyTex[0] = materialParser.getMaterial("skybox/" + sname + "rt");
            skyTex[1] = materialParser.getMaterial("skybox/" + sname + "lf");
            skyTex[2] = materialParser.getMaterial("skybox/" + sname + "up");
            skyTex[3] = materialParser.getMaterial("skybox/" + sname + "dn");
            skyTex[4] = materialParser.getMaterial("skybox/" + sname + "ft");
            skyTex[5] = materialParser.getMaterial("skybox/" + sname + "bk");
            for (int i = 0; i < 6; ++i) {
                if (skyTex[i] != 0) {
                    glBindTexture(GL_TEXTURE_2D, skyTex[i]);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
                }
            }

            PropStatic_L propManager;
            if (!mapParser.staticPropData.empty()) {
                propManager.parseFromBSP(mapParser.staticPropData, mapParser.staticPropVersion);
                propManager.loadModels(materialParser);
            }

            player.pos = mapParser.getPlayerSpawn();
            if (transState.active) {
                bool placed = false;
                if (!transState.landmarkName.empty()) {
                    Vec3 targetLandmarkPos;
                    if (mapParser.findLandmark(transState.landmarkName, targetLandmarkPos)) {
                        player.pos = targetLandmarkPos + transState.relativePos;
                        placed = true;
                    }
                }
                if (!placed) player.pos = transState.relativePos;
                player.velocity = transState.velocity;
                player.yaw = transState.yaw;
                player.pitch = transState.pitch;
                player.health = transState.health;
                player.sprintEnergy = transState.sprintEnergy;
                player.hasSuit = transState.hasSuit;
                player.weaponInventory = transState.weaponInventory;
                player.activeWeaponSlot = transState.activeWeaponSlot;
                transState.active = false;
            }
            for (const std::string& weaponClass : player.weaponInventory) entManager.spawn(weaponClass, player.pos);

            if (logoLoaded && !seamlessTransition) {
                double startTime = glfwGetTime();
                while (!glfwWindowShouldClose(window) && glfwGetTime() - startTime < 1.2) {
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                    DrawLogoOverlay(windowWidth, windowHeight);
                    glfwSwapBuffers(window);
                    glfwPollEvents();
                }
            }

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            double curX = 0.0, curY = 0.0;
            glfwGetCursorPos(window, &curX, &curY);

            glEnable(GL_DEPTH_TEST);
            glEnable(GL_TEXTURE_2D);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

            float changelevelCooldown = 2.0f;
            double lastTime = glfwGetTime();
            bool levelChangedViaTrigger = false;
            bool appQuitRequested = false;
            PropPhysics* heldProp = nullptr;
            float cameraBobTime = 0.0f;
            float cameraBobBlend = 0.0f;
            bool useWasDown = false;
            bool lmbWasDown = false;
            bool rmbWasDown = false;
            bool crouchWasDown = false;
            bool escapeWasDown = false;
            bool noclip = false;
            constexpr float standingHeight = 64.0f;
            constexpr float crouchHeight = 46.0f;

            while (!glfwWindowShouldClose(window)) {
                double curTime = glfwGetTime();
                float dt = static_cast<float>(curTime - lastTime);
                if (dt > 0.1f) dt = 0.1f;
                if (dt < 0.0f) dt = 0.0f;
                lastTime = curTime;

                glfwPollEvents();

                std::string cmd;
                {
                    std::lock_guard<std::mutex> lock(consoleMutex);
                    if (!consoleQueue.empty()) {
                        cmd = consoleQueue.front();
                        consoleQueue.pop();
                    }
                }

                if (!cmd.empty()) {
                    cmd = TrimCommand(cmd);
                    std::string lowCmd = LowerCommand(cmd);

                    if (lowCmd == "noclip") {
                        noclip = !noclip;
                        player.velocity = Vec3{0, 0, 0};
                        player.onGround = false;
                        std::cout << "[Console] noclip " << (noclip ? "ON" : "OFF") << std::endl;
                    } else if (lowCmd == "impulse 101") {
                        const auto& weapons = Weapon_L::knowledgeBase();
                        int given = 0;
                        for (const auto& kv : weapons) {
                            if (player.hasWeapon(kv.first)) continue;
                            const size_t before = entManager.entities.size();
                            entManager.spawn(kv.first, player.pos, false);
                            for (size_t i = before; i < entManager.entities.size(); ++i) {
                                Weapon_L* weapon = dynamic_cast<Weapon_L*>(entManager.entities[i]);
                                if (weapon && weapon->pickup(player)) { ++given; break; }
                            }
                        }
                        std::cout << "[Console] impulse 101 -> gave " << given << " weapons" << std::endl;
                    } else if (lowCmd.rfind("give ", 0) == 0) {
                        std::string giveClass = TrimCommand(cmd.substr(5));
                        if (!giveClass.empty()) {
                            std::string resolved = LowerCommand(giveClass);
                            if (resolved.find("weapon_") != 0 && resolved.find("item_") != 0 && resolved.find("prop_") != 0)
                                resolved = "weapon_" + resolved;
                            entManager.spawn(resolved, player.pos, false);
                            std::cout << "[Console] give " << resolved << std::endl;
                        }
                    } else if (lowCmd.rfind("ent_create ", 0) == 0) {
                        std::string entClass = TrimCommand(cmd.substr(11));
                        if (!entClass.empty()) {
                            Vec3 spawnPos = GetCrosshairSpawnPosition(player, mapParser, propManager, entManager);
                            entManager.spawn(LowerCommand(entClass), spawnPos, false);
                            std::cout << "[Console] ent_create " << LowerCommand(entClass) << " at crosshair" << std::endl;
                        }
                    } else if (lowCmd.rfind("map ", 0) == 0) {
                        std::string requestedMap = TrimCommand(cmd.substr(4));
                        if (!requestedMap.empty()) {
                            selectedMap = requestedMap;
                            seamlessTransition = false;
                            levelChangedViaTrigger = false;
                            std::cout << "[Console] map " << requestedMap << std::endl;
                            break;
                        }
                    } else if (lowCmd.rfind("changelevel ", 0) == 0) {
                        selectedMap = TrimCommand(cmd.substr(12));
                        seamlessTransition = false;
                        levelChangedViaTrigger = false;
                        break;
                    }
                }

                                                                                         
                                                                                        
                                            
                const bool openPauseMenu = pauseMenuRequest;
                pauseMenuRequest = false;

                if (openPauseMenu) {
                                                                                    
                                                                                     
                }

                if (changelevelCooldown > 0.0f) changelevelCooldown -= dt;

                const bool postProcessActive = postProcess.beginScene(windowWidth, windowHeight);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                double ncx = curX, ncy = curY;
                glfwGetCursorPos(window, &ncx, &ncy);
                if (!consoleOpen) {
                    player.yaw += static_cast<float>(ncx - curX) * 0.1f;
                    player.pitch += static_cast<float>(ncy - curY) * 0.1f;
                    curX = ncx;
                    curY = ncy;
                }
                if (player.pitch > 89.0f) player.pitch = 89.0f;
                if (player.pitch < -89.0f) player.pitch = -89.0f;

                bool useDown = !consoleOpen && glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
                bool lmbDown = !consoleOpen && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                bool rmbDown = !consoleOpen && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                Vec3 viewDir = GetViewDirection(player);

                int weaponSlotPressed = 0;
                const int weaponKeys[9] = { GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
                                            GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9 };
                for (int wi = 0; wi < 9; ++wi) {
                    if (glfwGetKey(window, weaponKeys[wi]) == GLFW_PRESS) {
                        weaponSlotPressed = wi + 1;
                        break;
                    }
                }
                static int lastWeaponSlotPressed = 0;
                if (weaponSlotPressed > 0 && weaponSlotPressed != lastWeaponSlotPressed)
                    entManager.selectWeaponSlot(weaponSlotPressed, player);
                lastWeaponSlotPressed = weaponSlotPressed;

                bool reloadDown = !consoleOpen && glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
                static bool reloadWasDown = false;
                if (reloadDown && !reloadWasDown) {
                    for (Entity* ent : entManager.entities) {
                        Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                        if (weapon && weapon->isActive(player)) { weapon->reload(player); break; }
                    }
                }
                reloadWasDown = reloadDown;

                if (useDown && !useWasDown) {
                    if (heldProp) {
                        heldProp->drop();
                        heldProp = nullptr;
                    } else {
                        bool pickedWeapon = false;
                        float bestWeaponDist = 128.0f;
                        Weapon_L* bestWeapon = nullptr;
                        for (Entity* ent : entManager.entities) {
                            Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                            if (!weapon || weapon->pickedUp) continue;
                            float along = 0.0f;
                            float score = weapon->aimScore(player.pos, viewDir, 128.0f, along);
                            if (score < bestWeaponDist) { bestWeapon = weapon; bestWeaponDist = score; }
                        }
                        if (bestWeapon && bestWeapon->pickup(player)) pickedWeapon = true;
                        if (!pickedWeapon) {
                            PropPhysics* candidate = entManager.findPickupProp(player.pos, viewDir, 128.0f, player.pos);
                            if (candidate && candidate->pickup()) heldProp = candidate;
                        }
                    }
                }

                if (heldProp && rmbDown && !rmbWasDown) {
                    heldProp->drop();
                    heldProp = nullptr;
                }

                if (heldProp && lmbDown && !lmbWasDown) {
                    Vec3 throwVelocity = viewDir * 850.0f + player.velocity * 0.35f;
                    heldProp->drop(throwVelocity);
                    heldProp = nullptr;
                } else if (!heldProp && lmbDown) {
                    for (Entity* ent : entManager.entities) {
                        Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                        if (weapon && weapon->isActive(player)) {
                            if (!lmbWasDown || weapon->allowsHeldPrimary()) weapon->primaryAttack(player);
                            break;
                        }
                    }
                } else if (!heldProp && rmbDown && !rmbWasDown) {
                    for (Entity* ent : entManager.entities) {
                        Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                        if (weapon && weapon->isActive(player)) { weapon->secondaryAttack(player); break; }
                    }
                }

                useWasDown = useDown;
                lmbWasDown = lmbDown;
                rmbWasDown = rmbDown;

                float radY = player.yaw * 3.14159f / 180.0f;
                float fx = std::sin(radY), fz = -std::cos(radY);
                float rx = std::cos(radY), rz = std::sin(radY);

                Vec3 wishDir{0, 0, 0};
                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) { wishDir.x += fx; wishDir.z += fz; }
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) { wishDir.x -= fx; wishDir.z -= fz; }
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) { wishDir.x -= rx; wishDir.z -= rz; }
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) { wishDir.x += rx; wishDir.z += rz; }
                if (wishDir.length() > 0) wishDir = wishDir.normalize();

                std::string currentFloorTex;

                if (noclip) {
                                                                                   
                                                                                 
                    const Vec3 noclipForward{fx, 0.0f, fz};
                    const Vec3 noclipRight{rx, 0.0f, rz};
                    Vec3 flyDir{0, 0, 0};
                    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) flyDir = flyDir + noclipForward;
                    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) flyDir = flyDir - noclipForward;
                    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) flyDir = flyDir - noclipRight;
                    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) flyDir = flyDir + noclipRight;
                    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) flyDir.y += 1.0f;
                    if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                        glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) flyDir.y -= 1.0f;
                    if (flyDir.length() > 0.001f) flyDir = flyDir.normalize();
                    player.pos = player.pos + flyDir * (600.0f * dt);
                    player.velocity = Vec3{0, 0, 0};
                    player.onGround = false;
                } else {
                    bool crouchDown = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                                       (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
                    float targetHeight = crouchDown ? crouchHeight : standingHeight;

                    if (!crouchDown && player.height < standingHeight - 0.01f) {
                        float wantedIncrease = standingHeight - player.height;
                        float dCeilStand = 999999.0f;
                        Vec3 nCeilStand;
                        if (mapParser.traceRay(player.pos, {0, 1, 0}, dCeilStand, nCeilStand) &&
                            dCeilStand < wantedIncrease + 2.0f) targetHeight = player.height;
                    }

                    const float crouchRate = 180.0f;
                    float heightDelta = targetHeight - player.height;
                    float maxHeightStep = crouchRate * dt;
                    if (heightDelta > maxHeightStep) heightDelta = maxHeightStep;
                    if (heightDelta < -maxHeightStep) heightDelta = -maxHeightStep;
                    const bool wasGroundedBeforeCrouch = player.onGround;
                    player.height += heightDelta;
                    if (wasGroundedBeforeCrouch) player.pos.y += heightDelta;
                    crouchWasDown = crouchDown;

                    float maxSpeed = 190.0f;
                    bool isSprinting = false;
                    if (player.hasSuit) {
                        maxSpeed = 270.0f;
                        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS &&
                            player.onGround && wishDir.length() > 0) {
                            if (player.sprintEnergy > 0) {
                                maxSpeed = 380.0f;
                                isSprinting = true;
                                player.sprintEnergy -= 18.0f * dt;
                            }
                        }
                    }
                    if (!isSprinting && player.sprintEnergy < 100.0f) {
                        player.sprintEnergy += 12.0f * dt;
                        if (player.sprintEnergy > 100.0f) player.sprintEnergy = 100.0f;
                    }

                    float accel = 10.0f, airAccel = 100.0f, friction = 6.0f;
                    if (player.onGround) {
                        float speed = std::sqrt(player.velocity.x * player.velocity.x + player.velocity.z * player.velocity.z);
                        if (speed > 0) {
                            float drop = speed * friction * dt;
                            float newSpeed = std::max(0.0f, speed - drop) / speed;
                            player.velocity.x *= newSpeed;
                            player.velocity.z *= newSpeed;
                        }
                    }

                    float currentSpeed = player.velocity.x * wishDir.x + player.velocity.z * wishDir.z;
                    float addSpeed = maxSpeed - currentSpeed;
                    if (addSpeed > 0) {
                        float accelSpeed = std::min(addSpeed, (player.onGround ? accel : airAccel) * maxSpeed * dt);
                        player.velocity.x += wishDir.x * accelSpeed;
                        player.velocity.z += wishDir.z * accelSpeed;
                    }

                    player.velocity.y -= 800.0f * dt;
                    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.onGround) {
                        player.velocity.y = 280.0f;
                        player.onGround = false;
                    }

                    Vec3 move = player.velocity * dt;
                    for (int i = 0; i < 4; ++i) {
                        if (move.length() < 0.001f) break;
                        float d = 999999.0f;
                        Vec3 n;
                        bool hitWorld = mapParser.traceRay(player.pos, move.normalize(), d, n);
                        float dProp = d;
                        Vec3 nProp;
                        if (propManager.traceRay(player.pos, move.normalize(), dProp, nProp)) {
                            if (dProp < d) { d = dProp; n = nProp; hitWorld = true; }
                        }
                        float dEnt = d;
                        Vec3 nEnt;
                        if (entManager.traceRay(player.pos, move.normalize(), dEnt, nEnt)) {
                            if (dEnt < d) { d = dEnt; n = nEnt; hitWorld = true; }
                        }
                        if (hitWorld && d < player.radius + move.length()) {
                            float fraction = std::max(0.0f, d - player.radius - 0.1f) / move.length();
                            Vec3 safeMove = move * fraction;
                            player.pos = player.pos + safeMove;
                            move = ClipVelocity(move - safeMove, n, 1.0f);
                            player.velocity = ClipVelocity(player.velocity, n, 1.0f);
                        } else {
                            player.pos = player.pos + move;
                            break;
                        }
                    }

                    float dFloor;
                    Vec3 nFloor;
                    if (mapParser.traceRay(player.pos, {0, -1, 0}, dFloor, nFloor) && dFloor <= player.height + 0.1f) {
                        if (player.velocity.y <= 0) {
                            player.pos.y += (player.height - dFloor);
                            player.velocity.y = 0;
                            player.onGround = true;
                            currentFloorTex = mapParser.getHitTextureName(player.pos, {0, -1, 0});
                        }
                    } else {
                        player.onGround = false;
                    }
                }

                float soundYaw = player.yaw * 3.14159265f / 180.0f;
                float soundPitch = player.pitch * 3.14159265f / 180.0f;
                Vec3 soundForward = {
                    std::sin(soundYaw) * std::cos(soundPitch), -std::sin(soundPitch),
                    -std::cos(soundYaw) * std::cos(soundPitch)
                };
                Vec3 soundRight = { std::cos(soundYaw), 0.0f, std::sin(soundYaw) };
                Vec3 soundUp = soundRight.cross(soundForward).normalize();
                Sound_L::Get().setListener(player.pos, soundForward.normalize(), soundRight.normalize(), soundUp, &mapParser);
                Sound_L::Get().update(dt);
                stepSound.update(dt, player.onGround, player.velocity, currentFloorTex);

                if (!noclip) {
                    float dCeil;
                    Vec3 nCeil;
                    if (player.velocity.y > 0 && mapParser.traceRay(player.pos, {0, 1, 0}, dCeil, nCeil) && dCeil < 10.0f) {
                        player.velocity.y = 0;
                        player.pos.y -= (10.0f - dCeil);
                    }
                }

                if (!noclip) PhysicsWorld::syncPlayer(player.pos, player.radius, player.height, dt);

                if (heldProp) {
                    Vec3 holdTarget = player.pos + viewDir * 58.0f;
                    holdTarget.y -= player.height * 0.12f;
                    heldProp->updateHeld(holdTarget, dt);
                }
                PhysicsWorld::step(dt);

                if (changelevelCooldown <= 0.0f && !noclip) {
                    TriggerResult trigRes = mapParser.checkTriggers(player.pos, player.radius, player.height);
                    if (!trigRes.targetMap.empty()) {
                        transState.active = true;
                        transState.velocity = player.velocity;
                        transState.yaw = player.yaw;
                        transState.pitch = player.pitch;
                        transState.landmarkName = trigRes.landmark;
                        transState.health = player.health;
                        transState.sprintEnergy = player.sprintEnergy;
                        transState.hasSuit = player.hasSuit;
                        transState.weaponInventory = player.weaponInventory;
                        transState.activeWeaponSlot = player.activeWeaponSlot;
                        if (!trigRes.landmark.empty()) {
                            Vec3 originLandmarkPos;
                            if (mapParser.findLandmark(trigRes.landmark, originLandmarkPos))
                                transState.relativePos = player.pos - originLandmarkPos;
                            else transState.relativePos = player.pos;
                        } else transState.relativePos = player.pos;
                        selectedMap = trigRes.targetMap;
                        seamlessTransition = true;
                        levelChangedViaTrigger = true;
                        break;
                    }
                }

                entManager.updateAll(dt, player, &mapParser);
                for (Entity* ent : entManager.entities) {
                    Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                    if (weapon) weapon->updateWeapon(dt, player, &mapParser, entManager);
                }

                float horizontalSpeed = std::sqrt(player.velocity.x * player.velocity.x + player.velocity.z * player.velocity.z);
                float bobAmount = std::min(horizontalSpeed / 190.0f, 1.35f);
                if (player.height < standingHeight - 0.5f) bobAmount *= 0.72f;
                if (!player.onGround) bobAmount *= 0.15f;
                float targetBob = std::min(bobAmount, 1.0f);
                float bobBlendSpeed = player.onGround ? 10.0f : 6.0f;
                cameraBobBlend += (targetBob - cameraBobBlend) * std::min(1.0f, bobBlendSpeed * dt);

                if (cameraBobBlend > 0.001f && horizontalSpeed > 2.0f) {
                    float bobFrequency = 8.5f + std::min(horizontalSpeed / 190.0f, 1.0f) * 2.0f;
                    cameraBobTime += dt * bobFrequency;
                } else {
                    cameraBobTime += dt * 2.0f;
                }

                const float bobSin = std::sin(cameraBobTime);
                const float bobCos = std::cos(cameraBobTime * 2.0f);
                const float bobSide = bobSin * 0.85f * cameraBobBlend;
                const float bobUp = (std::abs(bobCos) * 1.35f - 0.35f) * cameraBobBlend;
                const float bobRoll = bobSin * 0.65f * cameraBobBlend;

                float camYawRad = player.yaw * 3.14159f / 180.0f;
                Vec3 camRight = {std::cos(camYawRad), 0.0f, std::sin(camYawRad)};
                Vec3 renderCameraPos = player.pos + camRight * bobSide;
                renderCameraPos.y += bobUp;

                float aspect = (windowHeight > 0) ? static_cast<float>(windowWidth) / static_cast<float>(windowHeight) : 1.77f;
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                float fH = tan(90.0f / 360.0f * 3.14159f);
                float fW = fH * aspect;
                glFrustum(-fW, fW, -fH, fH, 1.0f, 30000.0f);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glRotatef(player.pitch, 1.0f, 0.0f, 0.0f);
                glRotatef(player.yaw, 0.0f, 1.0f, 0.0f);
                glRotatef(bobRoll, 0.0f, 0.0f, 1.0f);

                DrawSkybox({0, 0, 0}, skyTex);
                lightManager.applyLights(renderCameraPos);
                glTranslatef(-renderCameraPos.x, -renderCameraPos.y, -renderCameraPos.z);
                mapParser.renderWorld();
                if (!disableLighting) mapParser.renderLightmaps();
                propManager.render(&mapParser);
                entManager.renderAll();
                for (Entity* ent : entManager.entities) {
                    Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                    if (weapon && weapon->isActive(player)) {
                        weapon->renderViewModel(player, bobSide, bobUp, bobRoll, cameraBobBlend, cameraBobTime);
                        break;
                    }
                }
                lightManager.disableAll();
                if (postProcessActive) postProcess.endScene();

                hudRenderer.render(windowWidth, windowHeight, player, entManager.getActiveWeapon(player));

                                                                                      
                                                                                     
                                                                       
                if (openPauseMenu) menu.captureCurrentFrame(windowWidth, windowHeight);

                menu.handleConsoleOverlayInput(window, windowWidth, windowHeight);
                menu.renderConsoleOverlay(windowWidth, windowHeight);

                glfwSwapBuffers(window);

                if (openPauseMenu) {
                                                                                   
                                                              
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    inGameplay = false;
                    pauseMenuActive = true;
                    MenuResult pauseResult = menu.runPauseMenu(window);
                    pauseMenuActive = false;
                    inGameplay = true;
                    GameConsoleApplyOpenState(window);
                    glfwGetCursorPos(window, &curX, &curY);
                    escapeWasDown = (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS);

                    if (!FinishPauseResult(pauseResult, menu, window, appQuitRequested,
                                            selectedMap, seamlessTransition))
                        break;
                    if (appQuitRequested) break;
                }
            }

            if (heldProp) {
                heldProp->drop();
                heldProp = nullptr;
            }
            entManager.clear();
            PhysicsWorld::shutdown();
            Sound_L::Get().shutdown();

            if (appQuitRequested) break;
            if (glfwWindowShouldClose(window)) break;

            if (levelChangedViaTrigger) {
                seamlessTransition = true;
                continue;
            }

            seamlessTransition = false;
            if (selectedMap.empty()) continue;
        }
        catch (const std::exception& ex) {
            ShowEngineError(std::string("The engine encountered an error.\n\n") + ex.what());
            break;
        }
        catch (...) {
            ShowEngineError("The engine encountered an unknown error.");
            break;
        }
    }

    menu.shutdown();
    postProcess.shutdown();
    if (logoLoaded) glDeleteTextures(1, &logoTexture);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
