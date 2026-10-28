#include <windows.h>
#include <shellapi.h>
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
#include <unordered_map>
#include <set>
#include <streambuf>
#include <sstream>
#include <chrono>
#include <exception>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <functional>
#ifdef _WIN32
#include <excpt.h>
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "camera.h"
#include "bsp_l.h"
#include "cubemap_l.h"
#include "texture_l.h"
#include "prop_static.h"
#include "prop_dynamic.h"
#include "prop_physics.h"
#include "prop_lighting.h"
#include "light_l.h"
#include "step_sound_l.h"
#include "sound_l.h"
#include "entity_l.h"
#include "npc_l.h"
#include "item_suit.h"
#include "weapon_l.h"
#include "hud_l.h"
#include "postprocess_l.h"
#include "menu.h"
#include "menu_kv.h"
#include "multiplayer.h"
#include "mdl_l.h"
#include "engine_console.h"
#include "fs_compat.h"

static void ShowEngineError(const std::string& message) {
    MessageBoxA(NULL, message.c_str(), "Engine Error", MB_ICONERROR | MB_OK);
}

static std::filesystem::path executableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

static void appendUniquePath(std::vector<std::filesystem::path>& paths,
                             const std::filesystem::path& candidate) {
    if (candidate.empty()) return;
    const std::filesystem::path normalized = candidate.lexically_normal();
    const std::string key = FsCompat::toUtf8(normalized);
    for (const auto& path : paths) {
        if (FsCompat::toUtf8(path.lexically_normal()) == key) return;
    }
    paths.push_back(normalized);
}

static std::filesystem::path locateNearbyGame() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    std::vector<std::filesystem::path> bases;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) appendUniquePath(bases, cwd);
    const auto exeDir = executableDirectory();
    appendUniquePath(bases, exeDir);
    for (const auto& initial : std::vector<std::filesystem::path>{cwd, exeDir}) {
        if (initial.empty()) continue;
        auto current = initial;
        for (int depth = 0; depth < 4; ++depth) {
            const auto parent = current.parent_path();
            if (parent.empty() || parent == current) break;
            appendUniquePath(bases, parent);
            current = parent;
        }
    }
    for (const auto& base : bases) {
        appendUniquePath(candidates, base);
        appendUniquePath(candidates, base / L"hl2");
        appendUniquePath(candidates, base / L"Half-Life 2" / L"hl2");
        appendUniquePath(candidates, base / L"Half-Life 2 Update" / L"hl2");
    }
    for (const auto& candidate : candidates) {
        if (FsCompat::isRegularFile(candidate / L"gameinfo.txt")) return candidate;
    }
    return {};
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
bool gamePathProvided = false;
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
        std::ifstream gi(FsCompat::fromUtf8(gameDir) / "gameinfo.txt", std::ios::binary);
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
    return Vec3{std::sin(yaw) * cp, -std::sin(pitch), -std::cos(yaw) * cp}.normalize();
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


static void MarkMultiplayerSpawn(Entity* ent) {
    if (!ent || !Multiplayer_L::active() || Multiplayer_L::suppressed() || ent->multiplayerNetId != 0) return;
    std::string classname;
    std::string modelPath;
    Vec3 angles{0,0,0};
    float scale = 1.0f;
    Vec3 position = ent->pos;
    if (auto* prop = dynamic_cast<PropPhysics*>(ent)) {
        classname = "prop_physics";
        modelPath = prop->model.modelName.empty() ? prop->physicsModelPath : prop->model.modelName;
        angles = prop->body.getAngles();
        position = prop->body.getPosition();
        scale = prop->scale;
    } else if (auto* prop = dynamic_cast<PropDynamic*>(ent)) {
        classname = "prop_dynamic";
        modelPath = prop->model.modelName;
        angles = prop->angles;
        scale = prop->scale;
    } else {
        return;
    }
    if (modelPath.empty()) return;
    ent->multiplayerNetId = Multiplayer_L::allocateNetId();
    ent->multiplayerOwner = Multiplayer_L::localOwnerId();
    ent->multiplayerRemote = false;
    Multiplayer_L::setPropOwner(ent->multiplayerNetId, Multiplayer_L::localOwnerId());
    Multiplayer_L::noteLocalSpawn(ent->multiplayerNetId, classname, modelPath, position, angles, scale);
}

static void ApplyMultiplayerEvents(EntityManager& entManager) {
    if (!Multiplayer_L::active()) return;
    const std::vector<MPPropSpawnEv> spawns = Multiplayer_L::takeSpawns();
    for (const MPPropSpawnEv& ev : spawns) {
        if (entManager.findMultiplayerEntity(ev.netId)) continue;
        Multiplayer_L::setPropOwner(ev.netId, ev.owner);
        entManager.spawnNetwork(ev.classname, ev.pos, ev.model, ev.angles, ev.scale, ev.netId, ev.owner);
    }
    const std::vector<MPPropMoveEv> moves = Multiplayer_L::takeMoves();
    for (const MPPropMoveEv& ev : moves) {
        Entity* ent = entManager.findMultiplayerEntity(ev.netId);
        if (!ent || !ent->multiplayerRemote) continue;
        if (auto* prop = dynamic_cast<PropPhysics*>(ent)) prop->setNetworkTransform(ev.pos, ev.angles);
        else if (auto* prop = dynamic_cast<PropDynamic*>(ent)) prop->setNetworkTransform(ev.pos, ev.angles);
    }
}

static void SendMultiplayerPropUpdates(const EntityManager& entManager) {
    if (!Multiplayer_L::active() || Multiplayer_L::suppressed()) return;
    for (Entity* ent : entManager.entities) {
        if (!ent || ent->multiplayerNetId <= 0 || ent->multiplayerRemote) continue;
        if (!Multiplayer_L::ownsProp(ent->multiplayerNetId)) continue;
        if (auto* prop = dynamic_cast<PropPhysics*>(ent)) {
            Multiplayer_L::queueMove(ent->multiplayerNetId, prop->body.getPosition(), prop->body.getAngles());
        } else if (auto* prop = dynamic_cast<PropDynamic*>(ent)) {
            Multiplayer_L::queueMove(ent->multiplayerNetId, prop->pos, prop->angles);
        }
    }
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


static void RenderFlashlight(Camera& player,const Vec3& renderCameraPos,BSP_L& map,PropStatic_L& statics,EntityManager& entities,bool renderWorld,bool renderEntities) {
    const Vec3 pos=renderCameraPos;
    const Vec3 dir=GetViewDirection(player).normalize();
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    if(renderWorld) {
        map.renderFlashlight(pos,dir,1200.0f,42.0f);
    }
    if(renderEntities) {
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE,GL_ONE);
        glDisable(GL_ALPHA_TEST);
        glEnable(GL_LIGHTING);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK,GL_AMBIENT_AND_DIFFUSE);
        glEnable(GL_LIGHT0);
        GLfloat lp[]={pos.x,pos.y,pos.z,1.0f};
        GLfloat ld[]={dir.x,dir.y,dir.z,0.0f};
        GLfloat diffuse[]={1.0f,1.0f,0.94f,1.0f};
        GLfloat specular[]={0.18f,0.18f,0.16f,1.0f};
        GLfloat ambient[]={0.0f,0.0f,0.0f,1.0f};
        glLightfv(GL_LIGHT0,GL_POSITION,lp);
        glLightfv(GL_LIGHT0,GL_SPOT_DIRECTION,ld);
        glLightfv(GL_LIGHT0,GL_DIFFUSE,diffuse);
        glLightfv(GL_LIGHT0,GL_SPECULAR,specular);
        glLightfv(GL_LIGHT0,GL_AMBIENT,ambient);
        glLightf(GL_LIGHT0,GL_SPOT_CUTOFF,42.0f);
        glLightf(GL_LIGHT0,GL_SPOT_EXPONENT,16.0f);
        glLightf(GL_LIGHT0,GL_CONSTANT_ATTENUATION,0.65f);
        glLightf(GL_LIGHT0,GL_LINEAR_ATTENUATION,0.00115f);
        glLightf(GL_LIGHT0,GL_QUADRATIC_ATTENUATION,0.0000022f);
        glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER,GL_TRUE);
        glMaterialfv(GL_FRONT_AND_BACK,GL_SPECULAR,specular);
        glMaterialf(GL_FRONT_AND_BACK,GL_SHININESS,24.0f);
        gPropFlashlightPass=true;
        statics.render(nullptr);
        entities.renderAll();
        gPropFlashlightPass=false;
    }
    glPopAttrib();
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

    std::vector<std::string> commandArgs;
#ifdef _WIN32
    int wideArgc = 0;
    LPWSTR* wideArgv = CommandLineToArgvW(GetCommandLineW(), &wideArgc);
    if (wideArgv) {
        commandArgs.reserve(static_cast<size_t>(wideArgc));
        for (int i = 0; i < wideArgc; ++i) commandArgs.push_back(FsCompat::wideToUtf8(wideArgv[i]));
        LocalFree(wideArgv);
    }
#endif
    if (commandArgs.empty()) {
        commandArgs.reserve(static_cast<size_t>(argc));
        for (int i = 0; i < argc; ++i) commandArgs.emplace_back(argv[i] ? argv[i] : "");
    }
    for (size_t i = 1; i < commandArgs.size(); ++i) {
        const std::string& arg = commandArgs[i];
        if (arg == "-game" && i + 1 < commandArgs.size()) { gameDir = commandArgs[++i]; gamePathProvided = true; }
        else if (arg == "-map" && i + 1 < commandArgs.size()) selectedMap = commandArgs[++i];
        else if (arg == "-notexture") disableTextures = true;
        else if (arg == "-nolight") disableLighting = true;
    }
    {
        std::error_code pathError;
        const std::filesystem::path resolvedGameDir = std::filesystem::absolute(FsCompat::fromUtf8(gameDir), pathError);
        if (!pathError) gameDir = FsCompat::toUtf8(resolvedGameDir.lexically_normal());
        if (!FsCompat::isRegularFile(FsCompat::fromUtf8(gameDir) / "gameinfo.txt") && !gamePathProvided) {
            const std::filesystem::path fallback = locateNearbyGame();
            if (!fallback.empty()) gameDir = FsCompat::toUtf8(fallback);
        }
        if (!FsCompat::isRegularFile(FsCompat::fromUtf8(gameDir) / "gameinfo.txt")) {
            ShowEngineError("The selected game folder is missing gameinfo.txt. Use -game with the exact HL2\\hl2 folder.");
            return 1;
        }
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

    glfwWindowHint(GLFW_STENCIL_BITS, 8);
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
    glfwSetWindowUserPointer(window, &menu);
    Multiplayer_L::setLocalIdentity(Multiplayer_L::steamNick(), "models/player.mdl");
    menu.setPlayerNick(Multiplayer_L::steamNick());
    glfwSetWindowTitle(window, gameTitle.c_str());

    consolePassthrough = [](const std::string& command) -> bool {
        std::string trimmed = TrimCommand(command);
        std::string lowered = LowerCommand(trimmed);
        if (lowered == "connect" || lowered.rfind("connect ", 0) == 0) {
            std::string args = TrimCommand(trimmed.size() > 7 ? trimmed.substr(7) : std::string());
            std::string ip;
            int port = Multiplayer_L::defaultPort();
            std::istringstream iss(args);
            std::string first, second;
            iss >> first;
            iss >> second;
            bool valid = !first.empty() && Multiplayer_L::parseAddress(first, ip, port);
            if (valid && !second.empty()) {
                int extraPort = port;
                if (!Multiplayer_L::parsePort(second, extraPort)) valid = false;
                else port = extraPort;
            }
            if (!valid || ip.empty()) {
                std::cout << "[Multiplayer] usage: connect <ip>:<port> or connect <ip> <port>" << std::endl;
                return true;
            }
            if (Multiplayer_L::connectTo(ip, port)) {
                Multiplayer_L::setLocalIdentity(Multiplayer_L::steamNick(), "models/player.mdl");
                consoleJoinArmed = true;
                std::cout << "[Multiplayer] connected to " << ip << ":" << port << std::endl;
                const std::string hostMap = Multiplayer_L::remoteState().mapName;
                if (!hostMap.empty()) {
                    networkMapName = hostMap;
                    Multiplayer_L::setCurrentMap(hostMap);
                    consoleJoinMap = hostMap;
                    consoleJoinArmed = false;
                    std::cout << "[Multiplayer] host map: " << hostMap << " - loading it now" << std::endl;
                } else {
                    std::cout << "[Multiplayer] waiting for the host map, it will load automatically" << std::endl;
                }
            } else {
                std::cout << "[Multiplayer] connection failed: " << ip << ":" << port << std::endl;
                const std::string& detail = Multiplayer_L::lastError();
                if (!detail.empty()) std::cout << "[Multiplayer] reason: " << detail << std::endl;
                std::cout << "[Multiplayer] local addresses: 127.0.0.1 and " << Multiplayer_L::localAddress() << std::endl;
                std::cout << "[Multiplayer] try: connect 127.0.0.1:" << port << std::endl;
            }
            return true;
        }
        if (lowered == "disconnect") {
            Multiplayer_L::shutdown();
            std::cout << "[Multiplayer] disconnected" << std::endl;
            return true;
        }
        if (lowered == "host" || lowered.rfind("host ", 0) == 0) {
            std::istringstream hostArgs(trimmed.size() > 4 ? trimmed.substr(4) : std::string());
            std::string portToken;
            hostArgs >> portToken;
            int hostPort = Multiplayer_L::defaultPort();
            if (!portToken.empty() && !Multiplayer_L::parsePort(portToken, hostPort)) {
                std::cout << "[Multiplayer] usage: host <port>" << std::endl;
                return true;
            }
            Multiplayer_L::shutdown();
            if (Multiplayer_L::host(hostPort)) {
                Multiplayer_L::setLocalIdentity(Multiplayer_L::steamNick(), "models/player.mdl");
                if (!networkMapName.empty()) Multiplayer_L::setCurrentMap(networkMapName);
                std::cout << "[Multiplayer] hosting on port " << hostPort
                          << " - join with: connect 127.0.0.1:" << hostPort
                          << " (or connect " << Multiplayer_L::localAddress() << ":" << hostPort << ")" << std::endl;
            } else {
                std::cout << "[Multiplayer] could not host on port " << hostPort << std::endl;
                const std::string& detail = Multiplayer_L::lastError();
                if (!detail.empty()) std::cout << "[Multiplayer] reason: " << detail << std::endl;
            }
            return true;
        }
        if (lowered == "status") {
            std::cout << Multiplayer_L::statusLine(gameTitle, networkMapName) << std::endl;
            return true;
        }
        return false;
    };

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
        bool menuStarted = false;
        while (!menuStarted && !glfwWindowShouldClose(window)) {
            inGameplay = false;
            GameConsoleApplyOpenState(window);
            MenuResult menuResult = menu.runMainMenu(window);
            if (menuResult == MenuResult::Quit || menu.selectedMap().empty()) {
                menu.shutdown();
                postProcess.shutdown();
                if (logoLoaded) glDeleteTextures(1, &logoTexture);
                Multiplayer_L::shutdown();
                glfwDestroyWindow(window);
                glfwTerminate();
                return 0;
            }
            selectedMap = menu.selectedMap();
            if (menu.hostGameRequested()) {
                if (!Multiplayer_L::host(menu.hostPort())) {
                    MessageBoxA(NULL, "Could not start the multiplayer host on the selected port.", "Multiplayer Error", MB_ICONERROR | MB_OK);
                    selectedMap.clear();
                    continue;
                }
                Multiplayer_L::setLocalIdentity(menu.getPlayerNick(), menu.hostModel());
                std::cout << "[Multiplayer] hosting on port " << Multiplayer_L::localPort()
                          << " - join with: connect 127.0.0.1:" << Multiplayer_L::localPort() << std::endl;
            } else {
                if (Multiplayer_L::isHost()) Multiplayer_L::shutdown();
                if (Multiplayer_L::connected() && !Multiplayer_L::isHost()) {
                    const std::string hostMap = Multiplayer_L::remoteState().mapName;
                    if (!hostMap.empty()) {
                        selectedMap = hostMap;
                        std::cout << "[Multiplayer] joined, loading host map " << selectedMap << std::endl;
                    }
                }
            }
            menuStarted = true;
        }
    }

    while (!glfwWindowShouldClose(window)) {
        try {
            if (selectedMap.empty()) {
                bool menuStarted = false;
                while (!menuStarted && !glfwWindowShouldClose(window)) {
                    inGameplay = false;
                    GameConsoleApplyOpenState(window);
                    menu.setPlayerNick(Multiplayer_L::steamNick());
                    MenuResult menuResult = menu.runMainMenu(window);
                    if (menuResult == MenuResult::Quit) break;
                    if (menu.selectedMap().empty()) continue;
                    selectedMap = menu.selectedMap();
                    if (menu.hostGameRequested()) {
                        if (!Multiplayer_L::host(menu.hostPort())) {
                            MessageBoxA(NULL, "Could not start the multiplayer host on the selected port.", "Multiplayer Error", MB_ICONERROR | MB_OK);
                            selectedMap.clear();
                            continue;
                        }
                        Multiplayer_L::setLocalIdentity(menu.getPlayerNick(), menu.hostModel());
                        std::cout << "[Multiplayer] hosting on port " << Multiplayer_L::localPort()
                                  << " - join with: connect 127.0.0.1:" << Multiplayer_L::localPort() << std::endl;
                    } else {
                        if (Multiplayer_L::isHost()) Multiplayer_L::shutdown();
                        if (Multiplayer_L::connected() && !Multiplayer_L::isHost()) {
                            const std::string hostMap = Multiplayer_L::remoteState().mapName;
                            if (!hostMap.empty()) {
                                selectedMap = hostMap;
                                std::cout << "[Multiplayer] joined, loading host map " << selectedMap << std::endl;
                            }
                        }
                    }
                    menuStarted = true;
                    seamlessTransition = false;
                }
                if (!menuStarted) break;
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
            Cubemap_L cubemapManager;
            Light_L lightManager;
            lightManager.disableLighting = disableLighting;
            Sound_L::Get().init(gameDir, &materialParser);
            StepSound_L stepSound;
            stepSound.init(gameDir, &materialParser);

            EntityManager entManager;
            entManager.init(gameDir, &materialParser);
            entManager.registerEntity("item_suit", &ItemSuit::Create);
            entManager.registerEntity("npc_l", &NPC_L::Create);
            entManager.registerEntity("prop_dynamic", &PropDynamic::Create);
            entManager.registerEntity("prop_dynamic_override", &PropDynamic::Create);
            entManager.registerEntity("prop_physics", &PropPhysics::Create);
            entManager.registerEntity("prop_physics_override", &PropPhysics::Create);

            HUD_L hudRenderer;
            Camera player;
            MDL_L remotePlayerModel;
            std::string remotePlayerModelPath;
            float multiplayerMoveSendTimer = 0.0f;

            std::string finalMapName = selectedMap;
            std::string fullMapPath;
            if (finalMapName.find("/") != std::string::npos || finalMapName.find("\\") != std::string::npos) {
                fullMapPath = finalMapName;
            } else {
                if (finalMapName.find(".bsp") == std::string::npos) finalMapName += ".bsp";
                fullMapPath = gameDir + "/maps/" + finalMapName;
            }

            networkMapName = finalMapName;

            if (Multiplayer_L::active()) {
                Multiplayer_L::setCurrentMap(finalMapName);
                Multiplayer_L::resetProps();
                Multiplayer_L::takeSpawns();
                Multiplayer_L::takeMoves();
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
            cubemapManager.load(finalMapName, mapParser.entityData, materialParser);

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
            entManager.parseMapEntities(mapParser.entityData, &mapParser);

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
                player.setHealth(transState.health);
                player.sprintEnergy = transState.sprintEnergy;
                player.hasSuit = transState.hasSuit;
                player.weaponInventory = transState.weaponInventory;
                player.activeWeaponSlot = transState.activeWeaponSlot;
                transState.active = false;
            }
            for (const std::string& weaponClass : player.weaponInventory) {
                Entity* spawned = entManager.spawn(weaponClass, player.pos);
                MarkMultiplayerSpawn(spawned);
            }

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
            bool wasDead = false;
            bool deathNeedsRelease = false;
            bool crouchWasDown = false;
            bool escapeWasDown = false;
            bool noclip = false;
            bool flashlightEnabled = false;
            bool flashlightWasDown = false;
            constexpr float standingHeight = 64.0f;
            constexpr float crouchHeight = 46.0f;

            while (!glfwWindowShouldClose(window)) {
                double curTime = glfwGetTime();
                float dt = static_cast<float>(curTime - lastTime);
                if (dt > 0.1f) dt = 0.1f;
                if (dt < 0.0f) dt = 0.0f;
                lastTime = curTime;

                glfwPollEvents();
                GameConsoleProcessBinds(window);

                Multiplayer_L::tick(dt);
                ApplyMultiplayerEvents(entManager);
                if (Multiplayer_L::connected() && !Multiplayer_L::isHost()) {
                    const MPPlayerState& remote = Multiplayer_L::remoteState();
                    if (!remote.mapName.empty() && remote.mapName != finalMapName) {
                        selectedMap = remote.mapName;
                        seamlessTransition = false;
                        levelChangedViaTrigger = true;
                        break;
                    }
                }

                std::string cmd;
                {
                    std::lock_guard<std::mutex> lock(consoleMutex);
                    if (!consoleQueue.empty()) {
                        cmd = consoleQueue.front();
                        consoleQueue.pop_front();
                    }
                }

                if (!cmd.empty()) {
                    cmd = TrimCommand(cmd);
                    std::string lowCmd = LowerCommand(cmd);

                    if (lowCmd.rfind("bind", 0) == 0 && (lowCmd == "bind" || lowCmd.size() > 4 && std::isspace(static_cast<unsigned char>(lowCmd[4])))) {
                        std::istringstream iss(cmd);
                        std::string verb, keyName;
                        iss >> verb >> keyName;
                        std::string bindCommand;
                        std::getline(iss, bindCommand);
                        size_t first = bindCommand.find_first_not_of(" \t");
                        if (first != std::string::npos) bindCommand.erase(0, first);
                        if (keyName.empty() || bindCommand.empty()) {
                            std::cout << "Error: invalid bind structure. Use: bind <key> <command>" << std::endl;
                        } else {
                            const int bindKey = ConsoleParseBindKey(keyName);
                            if (bindKey == GLFW_KEY_UNKNOWN) {
                                std::cout << "Error: unknown key. Use: bind <key> <command>" << std::endl;
                            } else {
                                consoleBinds[bindKey] = bindCommand;
                                consoleBindStates[bindKey] = false;
                                std::cout << "[Console] bind " << keyName << " " << bindCommand << std::endl;
                            }
                        }
                    } else if (lowCmd.rfind("unbind", 0) == 0 && (lowCmd == "unbind" || lowCmd.size() > 6 && std::isspace(static_cast<unsigned char>(lowCmd[6])))) {
                        std::istringstream iss(cmd);
                        std::string verb, keyName;
                        iss >> verb >> keyName;
                        if (keyName.empty()) {
                            std::cout << "Error: invalid unbind structure. Use: unbind <key>" << std::endl;
                        } else {
                            const int bindKey = ConsoleParseBindKey(keyName);
                            if (bindKey == GLFW_KEY_UNKNOWN) {
                                std::cout << "Error: unknown key. Use: unbind <key>" << std::endl;
                            } else {
                                consoleBinds.erase(bindKey);
                                consoleBindStates.erase(bindKey);
                                std::cout << "[Console] unbind " << keyName << std::endl;
                            }
                        }
                    } else if (lowCmd.rfind("sauce_use_fixed_weaponspreads", 0) == 0) {
                        std::istringstream iss(cmd);
                        std::string verb, value;
                        iss >> verb >> value;
                        if (value != "0" && value != "1") {
                            std::cout << "Error: invalid value. Use: sauce_use_fixed_weaponspreads 0 or 1" << std::endl;
                        } else {
                            Weapon_L::setFixedWeaponSpreads(value == "1");
                            std::cout << "[Console] sauce_use_fixed_weaponspreads " << value << std::endl;
                        }
                    } else if (lowCmd == "noclip") {
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
                            MarkMultiplayerSpawn(entManager.spawn(kv.first, player.pos, false));
                            for (size_t i = before; i < entManager.entities.size(); ++i) {
                                Weapon_L* weapon = dynamic_cast<Weapon_L*>(entManager.entities[i]);
                                if (weapon && weapon->pickup(player)) { ++given; break; }
                            }
                        }
                        player.hasSuit = true;
                        std::cout << "[Console] impulse 101 -> gave " << given << " weapons and suit" << std::endl;
                    } else if (lowCmd.rfind("give ", 0) == 0) {
                        std::string giveClass = TrimCommand(cmd.substr(5));
                        if (!giveClass.empty()) {
                            std::string resolved = LowerCommand(giveClass);
                            if (resolved.find("weapon_") != 0 && resolved.find("item_") != 0 && resolved.find("prop_") != 0 && resolved.find("npc_") != 0)
                                resolved = "weapon_" + resolved;
                            const size_t before = entManager.entities.size();
                            MarkMultiplayerSpawn(entManager.spawn(resolved, player.pos, false));
                            for (size_t i = before; i < entManager.entities.size(); ++i) MarkMultiplayerSpawn(entManager.entities[i]);
                            std::cout << "[Console] give " << resolved << std::endl;
                        }
                    } else if (lowCmd.rfind("npc_create ", 0) == 0 || lowCmd.rfind("npc_spawn ", 0) == 0) {
                        const size_t prefix = lowCmd.rfind("npc_create ", 0) == 0 ? 11 : 10;
                        std::string npcClass = LowerCommand(TrimCommand(cmd.substr(prefix)));
                        if (!npcClass.empty()) {
                            if (npcClass.rfind("npc_", 0) != 0) npcClass = "npc_" + npcClass;
                            Vec3 spawnPos = GetCrosshairSpawnPosition(player, mapParser, propManager, entManager);
                            MarkMultiplayerSpawn(entManager.spawn(npcClass, spawnPos, false));
                            std::cout << "[Console] npc_create " << npcClass << " at crosshair" << std::endl;
                        }
                    } else if (lowCmd == "npc_list") {
                        int count = 0;
                        for (Entity* ent : entManager.entities) if (dynamic_cast<NPC_L*>(ent)) ++count;
                        std::cout << "[Console] npc_list -> " << count << " NPCs" << std::endl;
                    } else if (lowCmd.rfind("ent_create ", 0) == 0) {
                        std::string entClass = TrimCommand(cmd.substr(11));
                        if (!entClass.empty()) {
                            Vec3 spawnPos = GetCrosshairSpawnPosition(player, mapParser, propManager, entManager);
                            const size_t before = entManager.entities.size();
                            MarkMultiplayerSpawn(entManager.spawn(LowerCommand(entClass), spawnPos, false));
                            for (size_t i = before; i < entManager.entities.size(); ++i) MarkMultiplayerSpawn(entManager.entities[i]);
                            std::cout << "[Console] ent_create " << LowerCommand(entClass) << " at crosshair" << std::endl;
                        }
                    } else if (lowCmd == "connect" || lowCmd.rfind("connect ", 0) == 0 ||
                               lowCmd == "disconnect" || lowCmd == "status" ||
                               lowCmd == "host" || lowCmd.rfind("host ", 0) == 0) {
                        networkMapName = finalMapName;
                        if (consolePassthrough) consolePassthrough(cmd);
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

                const bool deathInput = glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ||
                    glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
                    glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                    glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                bool justRespawned = false;
                if (player.dead) {
                    player.deathTime += dt;
                    player.velocity = Vec3{0, 0, 0};
                    if (!wasDead) deathNeedsRelease = true;
                    if (!deathInput) deathNeedsRelease = false;
                    if (!deathNeedsRelease && player.deathTime > 0.35f && deathInput) {
                        player.pos = mapParser.getPlayerSpawn();
                        player.velocity = Vec3{0, 0, 0};
                        player.height = standingHeight;
                        player.onGround = false;
                        player.health = std::max(1, player.respawnHealth);
                        player.dead = false;
                        player.damageFlash = 0.0f;
                        player.deathTime = 0.0f;
                        wasDead = false;
                        deathNeedsRelease = false;
                        justRespawned = true;
                        cameraBobTime = 0.0f;
                        cameraBobBlend = 0.0f;
                    }
                } else if (!player.dead) {
                    player.damageFlash = std::max(0.0f, player.damageFlash - dt * 2.4f);
                }
                wasDead = player.dead;

                double ncx = curX, ncy = curY;
                glfwGetCursorPos(window, &ncx, &ncy);
                if (!consoleOpen && !player.dead) {
                    player.yaw += static_cast<float>(ncx - curX) * 0.1f;
                    player.pitch += static_cast<float>(ncy - curY) * 0.1f;
                    curX = ncx;
                    curY = ncy;
                }
                if (player.pitch > 89.0f) player.pitch = 89.0f;
                if (player.pitch < -89.0f) player.pitch = -89.0f;

                bool useDown = !consoleOpen && !player.dead && glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
                bool lmbDown = !consoleOpen && !player.dead && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                bool rmbDown = !consoleOpen && !player.dead && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
                if (justRespawned) {
                    useDown = false;
                    lmbDown = false;
                    rmbDown = false;
                }
                bool flashlightDown = !consoleOpen && !player.dead && glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
                if(flashlightDown&&!flashlightWasDown&&player.hasSuit){flashlightEnabled=!flashlightEnabled;Sound_L::Get().play("items/flashlight1.wav",player.pos,0.8f,false,false);}
                if(!player.hasSuit)flashlightEnabled=false;
                flashlightWasDown=flashlightDown;
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
                if (weaponSlotPressed > 0 && weaponSlotPressed != lastWeaponSlotPressed && !player.dead)
                    entManager.selectWeaponSlot(weaponSlotPressed, player);
                lastWeaponSlotPressed = weaponSlotPressed;

                bool reloadDown = !consoleOpen && !player.dead && glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
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


                        const bool usedMapEntity = entManager.activateUse(player.pos, viewDir, 128.0f, player, &mapParser);
                        if (!usedMapEntity) {
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
                                PropPhysics* candidate = entManager.findPickupProp(player.pos, viewDir, 192.0f, player.pos);
                                if (candidate && candidate->pickup()) heldProp = candidate;
                            }
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
                if (player.dead) {
                    wishDir = Vec3{0, 0, 0};
                    player.velocity.x = 0.0f;
                    player.velocity.z = 0.0f;
                }

                std::string currentFloorTex;

                if (noclip && !player.dead) {


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
                    bool crouchDown = !player.dead && ((glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                                       (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS));
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
                    if (!player.dead && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && player.onGround) {
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

                    float dFloor=999999.0f;
                    Vec3 nFloor{0,1,0};
                    bool floorHit=false;
                    if(mapParser.traceRay(player.pos,{0,-1,0},dFloor,nFloor)) floorHit=true;
                    float dProp=999999.0f; Vec3 nProp;
                    if(propManager.traceRay(player.pos,{0,-1,0},dProp,nProp) && dProp<dFloor){dFloor=dProp;nFloor=nProp;floorHit=true;}
                    float dEnt=999999.0f; Vec3 nEnt;
                    if(entManager.traceRay(player.pos,{0,-1,0},dEnt,nEnt) && dEnt<dFloor){dFloor=dEnt;nFloor=nEnt;floorHit=true;}
                    if(floorHit && dFloor <= player.height + 0.1f) {
                        if(player.velocity.y<=0){
                            player.pos.y += player.height-dFloor;
                            player.velocity.y=0;
                            player.onGround=true;
                            currentFloorTex=mapParser.getHitTextureName(player.pos,{0,-1,0});
                        }
                    } else player.onGround=false;
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

                if (Multiplayer_L::active()) {
                    multiplayerMoveSendTimer -= dt;
                    if (multiplayerMoveSendTimer <= 0.0f) {
                        multiplayerMoveSendTimer = 0.05f;
                        SendMultiplayerPropUpdates(entManager);
                    }
                    Multiplayer_L::setLocalState(player.pos, player.yaw, player.pitch, player.height, false);
                }

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
                if (player.dead) renderCameraPos.y -= (standingHeight - crouchHeight) * 0.9f;

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
                mapParser.renderTransparentWorld();
                mapParser.renderCubemapReflections(player.pos, cubemapManager);
                if(flashlightEnabled)RenderFlashlight(player,renderCameraPos,mapParser,propManager,entManager,true,false);
                propManager.render(&mapParser);
                entManager.renderAll();

                const MPPlayerState& remote = Multiplayer_L::remoteState();
                if (Multiplayer_L::active() && remote.valid) {
                    if (remote.model != remotePlayerModelPath) {
                        remotePlayerModelPath = remote.model;
                        remotePlayerModel.load(remotePlayerModelPath, materialParser);
                    }
                    if (remotePlayerModel.loaded) {
                        remotePlayerModel.setAnimationEventContext(remote.pos, true);
                        remotePlayerModel.updateAnimation(dt);
                    }
                    if (remotePlayerModel.loaded) {
                        glPushMatrix();
                        glTranslatef(remote.pos.x, remote.pos.y - std::max(32.0f, remote.height) + 4.0f, remote.pos.z);
                        glRotatef(remote.yaw, 0.0f, 1.0f, 0.0f);
                        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
                        remotePlayerModel.render();
                        glPopMatrix();
                    }
                }

                if(flashlightEnabled)RenderFlashlight(player,renderCameraPos,mapParser,propManager,entManager,false,true);
                Vec3 viewDirForEffects=GetViewDirection(player);
                Vec3 cameraUp=camRight.cross(viewDirForEffects).normalize();
                Weapon_L::renderImpactEffects(renderCameraPos,camRight,cameraUp);
                for (Entity* ent : entManager.entities) {
                    if (player.dead) break;
                    Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
                    if (weapon && weapon->isActive(player)) {
                        weapon->renderViewModel(player, bobSide, bobUp, bobRoll, cameraBobBlend, cameraBobTime);
                        break;
                    }
                }
                lightManager.disableAll();
                if (postProcessActive) postProcess.endScene();

                hudRenderer.render(windowWidth, windowHeight, player, entManager.getActiveWeapon(player));
                hudRenderer.renderDamageOverlay(windowWidth, windowHeight, player);
                if (Multiplayer_L::active()) {
                    const MPPlayerState& remoteHud = Multiplayer_L::remoteState();
                    if (remoteHud.valid) {
                        const Vec3 toRemote = remoteHud.pos - player.pos;
                        const float distance = toRemote.length();
                        if (distance > 0.01f && distance < 4096.0f) {
                            const float facing = GetViewDirection(player).dot(toRemote.normalize());
                            if (facing > 0.987f) hudRenderer.renderRemotePlayerLabel(windowWidth, windowHeight, remoteHud.nick);
                        }
                    }
                }


                if (openPauseMenu) menu.captureCurrentFrame(windowWidth, windowHeight);

                menu.handleConsoleOverlayInput(window, windowWidth, windowHeight);
                menu.renderConsoleOverlay(windowWidth, windowHeight);

                glfwSwapBuffers(window);

                if (openPauseMenu) {


                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                    inGameplay = false;
                    pauseMenuActive = true;
                    if (Multiplayer_L::active()) Multiplayer_L::setLocalState(player.pos, player.yaw, player.pitch, player.height, true);
                    MenuResult pauseResult = menu.runPauseMenu(window);
                    pauseMenuActive = false;
                    inGameplay = true;
                    if (Multiplayer_L::active()) Multiplayer_L::setLocalState(player.pos, player.yaw, player.pitch, player.height, false);
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
            NPC_L::shutdownRuntime();
            PhysicsWorld::shutdown();
            Sound_L::Get().shutdown();

            if (appQuitRequested) break;
            if (glfwWindowShouldClose(window)) break;

            if (levelChangedViaTrigger) {
                seamlessTransition = true;
                continue;
            }

            seamlessTransition = false;
            if (selectedMap.empty()) {
                Multiplayer_L::shutdown();
                continue;
            }
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

    Multiplayer_L::shutdown();
    menu.shutdown();
    postProcess.shutdown();
    if (logoLoaded) glDeleteTextures(1, &logoTexture);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
