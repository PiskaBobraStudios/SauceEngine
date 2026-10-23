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
    loadMenuPreview();
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

    if (chapters.empty()) {
        try {
            const std::filesystem::path mapsDir = std::filesystem::path(gameDirectory) / "maps";
            if (std::filesystem::exists(mapsDir)) {
                int number = 1;
                for (const auto& entry : std::filesystem::directory_iterator(mapsDir)) {
                    if (!entry.is_regular_file() || lowerText(entry.path().extension().string()) != ".bsp") continue;
                    MenuChapter chapter;
                    chapter.number = number++;
                    chapter.mapName = entry.path().stem().string();
                    chapter.name = chapter.mapName;
                    chapters.push_back(chapter);
                }
            }
        } catch (...) {
        }
    }

    try {
        const std::filesystem::path mapsDir = std::filesystem::path(gameDirectory) / "maps";
        if (std::filesystem::exists(mapsDir)) {
            std::set<std::string> known;
            for (const auto& chapter : chapters) known.insert(lowerText(chapter.mapName));
            int nextNumber = chapters.empty() ? 1 : chapters.back().number + 1;
            for (const auto& entry : std::filesystem::directory_iterator(mapsDir)) {
                if (!entry.is_regular_file() || lowerText(entry.path().extension().string()) != ".bsp") continue;
                const std::string mapName = lowerText(entry.path().stem().string());
                if (mapName.empty() || !known.insert(mapName).second) continue;
                MenuChapter chapter;
                chapter.number = nextNumber++;
                chapter.mapName = mapName;
                chapter.name = mapName;
                chapters.push_back(std::move(chapter));
            }
        }
    } catch (...) {
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

