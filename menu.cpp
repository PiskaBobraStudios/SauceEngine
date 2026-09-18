#include "menu.h"
#include "menu_kv.h"
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

#ifdef _MSC_VER
#pragma comment(lib, "gdi32.lib")
#endif

#include "stb_image.h"

extern bool GameConsole_IsOpen();
extern std::vector<std::pair<std::string, bool>> GameConsole_GetLines();
extern std::string GameConsole_GetInput();
extern bool GameConsole_TakeMapCommand(std::string& outMap);

namespace {
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
}

Menu_L::~Menu_L() {
    if (resourcesActive && glfwGetCurrentContext()) shutdown();
}

void Menu_L::shutdown() {
    if (!resourcesActive || !glfwGetCurrentContext()) return;
    destroyFonts();
    destroyOwnedTextures();
    if (pauseFrameTexture) glDeleteTextures(1, &pauseFrameTexture);
    pauseFrameTexture = 0;
    pauseFrameWidth = pauseFrameHeight = 0;
    ownerWindow = nullptr;
    fileSystem = nullptr;
    resourcesActive = false;
}

bool Menu_L::init(GLFWwindow* window, Texture_L* fs,
                  const std::string& gDir, const std::string& gameTitle) {
    ownerWindow = window;
    fileSystem = fs;
    gameDirectory = gDir;
    title = gameTitle.empty() || gameTitle == "Sauce name" ? "Sauce" : gameTitle;
    selectedMapName.clear();
    selectedChapter = 0;
    chapterPageStart = 0;
    loadContent();
    fontsReady = initFonts();
    resourcesActive = true;
    return fontsReady;
}

bool Menu_L::fileExists(const std::string& relativePath) const {
    const std::string normalized = lowerText(relativePath);
    if (fileSystem) {
        std::vector<char> raw;
        if (fileSystem->getFileRawData(normalized, raw)) return true;
    }
    const std::filesystem::path path = std::filesystem::path(gameDirectory) / relativePath;
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

Menu_L::TextureRef Menu_L::createWhiteTexture() {
    TextureRef tex;
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    const unsigned char pixel[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    tex.width = tex.height = 1;
    tex.owned = true;
    return tex;
}

Menu_L::TextureRef Menu_L::loadRawImage(const std::string& relativePath) {
    TextureRef out;
    std::vector<char> raw;
    const std::string path = lowerText(relativePath);
    if (fileSystem) {
        fileSystem->getFileRawData(path, raw);
    } else {
        const std::filesystem::path disk = std::filesystem::path(gameDirectory) / path;
        std::ifstream f(disk, std::ios::binary);
        if (f) raw.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }
    if (raw.empty()) return out;

    stbi_set_flip_vertically_on_load(false);
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(raw.data()),
                                                   static_cast<int>(raw.size()), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return out;
    }

    glGenTextures(1, &out.id);
    glBindTexture(GL_TEXTURE_2D, out.id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    stbi_image_free(pixels);

    out.width = w;
    out.height = h;
    out.owned = true;
    return out;
}

Menu_L::TextureRef Menu_L::loadMaterialTexture(const std::string& materialBase) {
    TextureRef out;
    std::string base = withoutMaterials(materialBase);
    base = stripExtension(base);
    if (base.empty() || !fileSystem) return out;

                                                                             
                                                                            
                                                                               
                                                                              
    const unsigned int id = fileSystem->getMaterial(base);
    if (!id) return out;
    out.id = id;
    out.width = out.height = 1;
    fileSystem->getTextureSize(id, out.width, out.height);
    out.owned = false;
    return out;
}

Menu_L::TextureRef Menu_L::loadTextureCandidates(const std::vector<std::string>& candidates) {
    for (const std::string& rawCandidate : candidates) {
        std::string candidate = lowerText(rawCandidate);
        if (candidate.empty()) continue;

        if (hasExtension(candidate, ".png") || hasExtension(candidate, ".jpg") ||
            hasExtension(candidate, ".jpeg") || hasExtension(candidate, ".tga")) {
            TextureRef tex = loadRawImage(candidate);
            if (tex.id) return tex;
            if (candidate.rfind("materials/", 0) == 0) {
                tex = loadRawImage(candidate.substr(10));
                if (tex.id) return tex;
            }
            continue;
        }

        if (hasExtension(candidate, ".vmt") || hasExtension(candidate, ".vtf")) {
            TextureRef tex = loadMaterialTexture(candidate);
            if (tex.id) return tex;
            continue;
        }

        TextureRef tex = loadMaterialTexture(candidate);
        if (tex.id) return tex;

        for (const char* ext : {".png", ".jpg", ".jpeg", ".tga"}) {
            tex = loadRawImage(candidate + ext);
            if (tex.id) return tex;
            if (candidate.rfind("materials/", 0) == 0) {
                tex = loadRawImage(candidate.substr(10) + ext);
                if (tex.id) return tex;
            }
        }
    }
    return {};
}

void Menu_L::scanTextureFiles(const std::string& keyword, std::vector<std::string>& outCandidates) const {
    if (!fileSystem) return;
    const std::string want = lowerText(keyword);
    std::set<std::string> seen;
    for (const auto& kv : fileSystem->vpkFileSystem) {
        const std::string path = lowerText(kv.first);
        if (path.find(want) == std::string::npos || !looksLikeTextureEntry(path)) continue;
        if (seen.insert(path).second) outCandidates.push_back(path);
    }
}

void Menu_L::scanDiskTextureFiles(const std::string& keyword, std::vector<std::string>& outCandidates) const {
    const std::filesystem::path root = std::filesystem::path(gameDirectory) / "materials";
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return;
    const std::string want = lowerText(keyword);
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const std::string path = lowerText(std::filesystem::relative(it->path(), gameDirectory, ec).generic_string());
        if (path.find(want) == std::string::npos || !looksLikeTextureEntry(path)) continue;
        outCandidates.push_back(path);
    }
}

void Menu_L::loadBackgrounds() {
    backgroundTextures.clear();
    backgroundIndex = 0;

    std::vector<std::string> candidates;
    std::set<std::string> seen;

    int fbW = 0, fbH = 0;
    if (ownerWindow) glfwGetFramebufferSize(ownerWindow, &fbW, &fbH);
    const bool widescreen = fbW > 0 && fbH > 0 &&
                            (static_cast<float>(fbW) / static_cast<float>(fbH)) >= 1.6f;

                                                                               
                                                                               
    std::string chapterBackgrounds;
    if (readTextResource(fileSystem, gameDirectory, "scripts/ChapterBackgrounds.txt", chapterBackgrounds)) {
        const auto roots = MenuKV::parse(chapterBackgrounds);
        for (const auto& root : roots) {
            if (lowerText(root.name) != "chapters") continue;
            for (const auto& entry : root.children) {
                if (!entry.values.empty()) {
                    addUnique(candidates, seen, "materials/console/" + entry.values.front() +
                                                 (widescreen ? "_widescreen" : ""));
                    addUnique(candidates, seen, "materials/console/" + entry.values.front());
                }
            }
        }
    }

                                                                                
                                                                         
    for (int i = 1; i <= 12; ++i) {
        char n2[8] = {};
        char n1[8] = {};
        std::snprintf(n2, sizeof(n2), "%02d", i);
        std::snprintf(n1, sizeof(n1), "%d", i);
        for (const std::string& number : {std::string(n2), std::string(n1)}) {
            const std::string base = "materials/console/background" + number;
            if (widescreen) {
                addUnique(candidates, seen, base + "_widescreen");
                addUnique(candidates, seen, base);
            } else {
                addUnique(candidates, seen, base);
                addUnique(candidates, seen, base + "_widescreen");
            }
        }
    }

    for (const char* path : {
        "materials/console/background",
        "materials/console/background_menu",
        "materials/console/background_menu_widescreen",
        "materials/vgui/background",
        "materials/menu/background",
        "materials/menu/menu_background"
    }) addUnique(candidates, seen, path);

    std::string menuText;
    if (readTextResource(fileSystem, gameDirectory, "cfg/menu.txt", menuText)) {
        const auto roots = MenuKV::parse(menuText);
        for (const auto& root : roots) {
            if (lowerText(root.name) != "menu") continue;
            for (const std::string& value : {
                MenuKV::value(root, "background"),
                MenuKV::value(root, "menu_background")
            }) {
                if (value.empty()) continue;
                addUnique(candidates, seen, value);
                                                                                
                                                                                
                const std::string normalized = lowerText(value);
                if (!hasExtension(normalized, ".vmt") && !hasExtension(normalized, ".vtf")) {
                    const std::string base = normalized.rfind("materials/", 0) == 0
                        ? normalized.substr(10) : normalized;
                    addUnique(candidates, seen, "materials/console/" + base +
                                                 (widescreen ? "_widescreen" : ""));
                    addUnique(candidates, seen, "materials/console/" + base);
                }
            }
        }
    }

    for (const std::string& candidate : candidates) {
        TextureRef tex = loadTextureCandidates({candidate});
        if (tex.id) {
            backgroundTextures.push_back(tex);
            return;
        }
    }

                                                                                  
                                                                                    
    std::vector<std::string> scanned;
    for (const std::string& keyword : {
        std::string("console/background"),
        std::string("vgui/background"),
        std::string("menu/background"),
        std::string("background")
    }) scanTextureFiles(keyword, scanned);
    for (const std::string& keyword : {
        std::string("console/background"),
        std::string("vgui/background"),
        std::string("menu/background"),
        std::string("background")
    }) scanDiskTextureFiles(keyword, scanned);
    for (const std::string& value : scanned) addUnique(candidates, seen, value);

    for (const std::string& value : scanned) {
        TextureRef tex = loadTextureCandidates({value});
        if (tex.id) {
            backgroundTextures.push_back(tex);
            return;
        }
    }
}

void Menu_L::loadContent() {
    destroyOwnedTextures();
    backgroundTextures.clear();
    chapterTextures.clear();
    chapters.clear();
    whiteTexture = createWhiteTexture();
    loadBackgrounds();
    loadChapters();
}

void Menu_L::loadChapters() {
    std::string menuText;
    if (readTextResource(fileSystem, gameDirectory, "cfg/menu.txt", menuText)) {
        const auto roots = MenuKV::parse(menuText);
        for (const auto& root : roots) {
            if (lowerText(root.name) != "menu") continue;
            const std::string configuredTitle = MenuKV::value(root, "title");
            if (!configuredTitle.empty()) title = configuredTitle;

            auto readChapterNodes = [&](const std::vector<MenuKVNode>& nodes) {
                for (const auto& c : nodes) {
                    int number = 0;
                    try { number = std::stoi(c.name); } catch (...) { continue; }
                    MenuChapter chapter;
                    chapter.number = number;
                    chapter.name = MenuKV::value(c, "name", "Chapter " + std::to_string(number));
                    chapter.mapName = MenuKV::value(c, "map");
                    chapter.image = MenuKV::value(c, "image");
                    if (!chapter.mapName.empty()) chapters.push_back(chapter);
                }
            };
            readChapterNodes(root.children);
            if (const MenuKVNode* list = MenuKV::child(root, "chapters")) readChapterNodes(list->children);
        }
    }

    if (readTextResource(fileSystem, gameDirectory, "cfg/menu.txt", menuText)) {
        const auto roots = MenuKV::parse(menuText);
        const std::string rootlessTitle = [&]() -> std::string {
            for (const auto& node : roots) {
                const std::string key = lowerText(node.name);
                if (node.children.empty() && !node.values.empty() && (key == "title" || key == "game"))
                    return node.values.front();
                const std::string nested = MenuKV::value(node, "title");
                if (!nested.empty()) return nested;
            }
            return {};
        }();
        if (!rootlessTitle.empty()) title = rootlessTitle;
    }

    if (chapters.empty()) {
        if (readTextResource(fileSystem, gameDirectory, "cfg/menu.txt", menuText)) {
            const auto roots = MenuKV::parse(menuText);
            for (const auto& root : roots) {
                int number = 0;
                try { number = std::stoi(root.name); } catch (...) { continue; }
                MenuChapter chapter;
                chapter.number = number;
                chapter.name = MenuKV::value(root, "name", "Chapter " + std::to_string(number));
                chapter.mapName = MenuKV::value(root, "map");
                chapter.image = MenuKV::value(root, "image");
                if (!chapter.mapName.empty()) chapters.push_back(chapter);
            }
        }
    }

    if (chapters.empty()) {
        for (int i = 1; i < 200; ++i) {
            std::string text;
            if (!readTextResource(fileSystem, gameDirectory, "cfg/chapter" + std::to_string(i) + ".cfg", text)) continue;

            MenuChapter chapter;
            chapter.number = i;
            const auto nodes = MenuKV::parse(text);
            for (const auto& node : nodes) {
                if (node.values.empty()) continue;
                const std::string key = lowerText(node.name);
                if (key == "name" || key == "title") chapter.name = node.values.front();
                else if (key == "map") chapter.mapName = node.values.front();
                else if (key == "image" || key == "texture" || key == "background") chapter.image = node.values.front();
            }
            if (chapter.mapName.empty()) chapter.mapName = firstMapFromLegacyCfg(text);
            if (chapter.mapName.empty()) continue;
            if (chapter.name.empty()) chapter.name = "Chapter " + std::to_string(i);
            chapters.push_back(chapter);
        }
    }

    std::sort(chapters.begin(), chapters.end(), [](const MenuChapter& a, const MenuChapter& b) { return a.number < b.number; });
    chapterTextures.assign(chapters.size(), TextureRef{});
    if (!chapters.empty()) {
        selectedChapter = std::min(selectedChapter, chapters.size() - 1);
        const size_t pageSize = chapters.size() <= 1 ? 1 : (chapters.size() == 2 ? 2 : 3);
        chapterPageStart = (selectedChapter / pageSize) * pageSize;
    }
}

bool Menu_L::initFonts() {
    if (!ownerWindow) return false;
    HWND hwnd = reinterpret_cast<HWND>(glfwGetWin32Window(ownerWindow));
    HDC dc = GetDC(hwnd);
    if (!dc) return false;

    struct FontSpec { Font* dst; int size; int weight; };
    const FontSpec specs[] = {{&fontTitle,56,FW_NORMAL},{&fontMenu,16,FW_NORMAL},{&fontSmall,12,FW_NORMAL},{&fontTiny,10,FW_NORMAL}};
    bool ok = true;
    for (const auto& spec : specs) {
        HFONT font = makeSystemFont(spec.size, spec.weight);
        if (!font) { ok = false; continue; }
        HGDIOBJ old = SelectObject(dc, font);
        spec.dst->listBase = glGenLists(96);
        spec.dst->hfont = font;
        spec.dst->pixelHeight = spec.size;
        if (!spec.dst->listBase || !wglUseFontBitmapsA(dc, 32, 96, spec.dst->listBase)) {
            if (spec.dst->listBase) glDeleteLists(spec.dst->listBase, 96);
            spec.dst->listBase = 0;
            DeleteObject(font);
            spec.dst->hfont = nullptr;
            ok = false;
        } else spec.dst->valid = true;
        SelectObject(dc, old);
    }
    ReleaseDC(hwnd, dc);
    return ok;
}

void Menu_L::destroyFonts() {
    for (Font* font : {&fontTitle, &fontMenu, &fontSmall, &fontTiny}) {
        if (font->listBase) glDeleteLists(font->listBase, 96);
        if (font->hfont) DeleteObject(static_cast<HFONT>(font->hfont));
        *font = Font{};
    }
    fontsReady = false;
}

void Menu_L::destroyOwnedTextures() {
    for (TextureRef& tex : backgroundTextures) {
        if (tex.owned && tex.id) glDeleteTextures(1, &tex.id);
        tex = TextureRef{};
    }
    for (TextureRef& tex : chapterTextures) {
        if (tex.owned && tex.id) glDeleteTextures(1, &tex.id);
        tex = TextureRef{};
    }
    if (whiteTexture.owned && whiteTexture.id) glDeleteTextures(1, &whiteTexture.id);
    whiteTexture = TextureRef{};
}

unsigned int Menu_L::resolveChapterTexture(size_t index) {
    if (index >= chapters.size()) return whiteTexture.id;
    if (chapterTextures[index].id) return chapterTextures[index].id;

    const MenuChapter& c = chapters[index];
    std::vector<std::string> candidates;
    std::set<std::string> seen;
    if (!c.image.empty()) addUnique(candidates, seen, c.image);

    const std::string n = std::to_string(c.number);
    char n2[8] = {};
    std::snprintf(n2, sizeof(n2), "%02d", c.number);
    const std::string map = lowerText(c.mapName);

                                                                               
                                                                             
                                                                        
    for (const std::string& dir : {
        "materials/vgui/chapters/",
        "materials/vgui/hl2/chapters/",
        "materials/vgui/episodic/chapters/",
        "materials/vgui/ep2/chapters/",
        "materials/vgui/lostcoast/chapters/"
    }) {
        addUnique(candidates, seen, dir + "chapter" + n);
        addUnique(candidates, seen, dir + "chapter" + std::string(n2));
    }

                                                                                 
                                                           
    for (const std::string& base : {
        "materials/console/chapter" + n,
        "materials/console/chapter" + std::string(n2),
        "materials/menu/chapter" + n,
        "materials/menu/chapter" + std::string(n2),
        "materials/console/chapters/chapter" + n,
        "materials/console/chapters/chapter" + std::string(n2),
        "materials/vgui/chapter" + n,
        "materials/vgui/chapter" + std::string(n2),
        "materials/console/" + map,
        "materials/vgui/" + map
    }) addUnique(candidates, seen, base);

    TextureRef tex = loadTextureCandidates(candidates);
    if (!tex.id) {
        std::vector<std::string> scanned;
        scanTextureFiles("vgui/chapters/chapter" + n, scanned);
        scanTextureFiles("vgui/chapters/chapter" + std::string(n2), scanned);
        if (!map.empty()) scanTextureFiles(map, scanned);
        scanDiskTextureFiles("vgui/chapters/chapter" + n, scanned);
        scanDiskTextureFiles("vgui/chapters/chapter" + std::string(n2), scanned);
        if (!map.empty()) scanDiskTextureFiles(map, scanned);
        for (const std::string& value : scanned) addUnique(candidates, seen, value);
        tex = loadTextureCandidates(scanned);
    }

    if (!tex.id) {
        chapterTextures[index] = whiteTexture;
        chapterTextures[index].owned = false;
    } else {
        chapterTextures[index] = tex;
    }
    return chapterTextures[index].id;
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

void Menu_L::renderMainMenu(int width, int height) {
    if (pauseMode) {
        renderPauseFrame(width, height);
    } else if (!backgroundTextures.empty()) {
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
        : std::vector<std::string>{"New Game", "Load Game", "Options", "Quit"};

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
    dialogW = 790.0f * s;
    dialogH = 392.0f * s;
    dialogX = (width - dialogW) * 0.5f + 70.0f * s;
    dialogY = (height - dialogH) * 0.5f;
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

void Menu_L::closeDialog() {
    dialog = DialogKind::None;
    draggingDialog = false;
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
        : std::vector<std::string>{"New Game", "Load Game", "Options", "Quit"};
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
    }
    return -1;
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
            case 1: openEmptyDialog("Load Game", width, height); break;
            case 2: openEmptyDialog("Options", width, height); break;
            case 3: result = MenuResult::Quit; break;
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

bool Menu_L::handleDialogInput(GLFWwindow* window, int width, int height, MenuResult& result) {
    if (GameConsole_IsOpen()) return false;
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    const bool mouseDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    const int hit = hitDialogControl(mx, my, width, height);
    const float s = std::max(0.55f, std::min(1.35f, std::min(width / 1920.0f, height / 1080.0f)));
    const bool titleHit = pointIn(static_cast<float>(mx), static_cast<float>(my), dialogX, dialogY, dialogW, 27.0f * s);

    if (mouseDown && !mouseWasDown) {
        if (hit == 900 || hit == 901) pressedControl = hit;
        else if (dialog == DialogKind::Chapters) {
            const float bodyX = dialogX + 18.0f * s;
            const float innerW = dialogW - 36.0f * s;
            const float gap = 14.0f * s;
            const float cardW = std::min(218.0f * s, (innerW - gap * 2.0f) / 3.0f);
            const float imageH = cardW * 0.56f;
            const float cardTop = dialogY + 47.0f * s;
            for (int slot = 0; slot < 3; ++slot) {
                const float x = bodyX + slot * (cardW + gap);
                if (pointIn(static_cast<float>(mx), static_cast<float>(my), x, cardTop + 34.0f * s, cardW, imageH)) {
                    pressedControl = 100 + slot;
                    break;
                }
            }
            if (pressedControl == -1) {
                if (pointIn((float)mx, (float)my, bodyX, cardTop + imageH + 66.0f * s, 30.0f * s, 28.0f * s))
                    pressedControl = 110;
                else if (pointIn((float)mx, (float)my, bodyX + innerW - 30.0f * s,
                                 cardTop + imageH + 66.0f * s, 30.0f * s, 28.0f * s))
                    pressedControl = 111;
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
            if (releaseControl == 900 || releaseControl == 901 || releaseControl == 100 ||
                releaseControl == 101 || releaseControl == 102 || releaseControl == 103 ||
                releaseControl == 104 || releaseControl == 110 || releaseControl == 111)
                activateDialogControl(releaseControl, width, height, result);
        }
        pressedControl = -1;
        draggingDialog = false;
    }

    const bool left = isKeyPressed(window, GLFW_KEY_LEFT);
    const bool right = isKeyPressed(window, GLFW_KEY_RIGHT);
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
    }

    if (escape && !escapeWasDown) closeDialog();
    mouseWasDown = mouseDown;
    leftWasDown = left;
    rightWasDown = right;
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
    const int itemCount = pauseMode ? 6 : 4;
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
    if (down && !consoleMouseWasDown) {
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
    const float contentTop = consoleY + 34.0f;
    const float contentBottom = consoleY + consoleH - inputH - 12.0f;
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

    drawRoundedRect(consoleX + pad - 2.0f, consoleY + consoleH - inputH - 9.0f,
                    consoleW - pad * 2.0f + 4.0f, inputH, 4.0f,
                    0.18f, 0.20f, 0.21f, 0.88f);
    drawText("> " + GameConsole_GetInput(), consoleX + pad + 4.0f,
             consoleY + consoleH - inputH - 4.0f, fontTiny,
             0.96f, 0.96f, 0.96f, 1.0f);

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
        begin2D(w, h, true);
        renderMainMenu(w, h);
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

        end2D();
        glfwSwapBuffers(window);

        if (result != MenuResult::None)
            return result;
    }
    return MenuResult::MainMenu;
}
