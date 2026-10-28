#include "texture_l.h"
#include "fs_compat.h"
#include "stb_image.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <limits>
#include <windows.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#define GL_BGR_EXT 0x80E0
#define GL_BGRA_EXT 0x80E1
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3

typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
static PFNGLCOMPRESSEDTEXIMAGE2DPROC glCompressedTexImage2D_Ext = nullptr;

#pragma pack(push, 1)
struct VTFResource { char tag[3]; unsigned char flags; unsigned int offset; };
struct VTFHeader {
    char magic[4]; unsigned int version[2]; unsigned int headerSize;
    unsigned short width, height; unsigned int flags;
    unsigned short frames, firstFrame; unsigned char pad0[4];
    float reflectivity[3]; unsigned char pad1[4]; float bumpScale;
    unsigned int highResFormat; unsigned char mipCount;
    unsigned int lowResFormat; unsigned char lowResWidth, lowResHeight;
    unsigned short depth; unsigned char pad2[3]; unsigned int numResources;
};
struct ZipLocalHeader {
    unsigned int sig; unsigned short ver, flags, comp, mtime, mdate;
    unsigned int crc, csize, usize; unsigned short fname_len, extra_len;
};
struct VPKEntryHeader {
    unsigned int crc;
    unsigned short preloadBytes;
    unsigned short archiveIndex;
    unsigned int entryOffset;
    unsigned int entryLength;
    unsigned short terminator;
};
#pragma pack(pop)

namespace {
static std::string normalizePath(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::replace(value.begin(), value.end(), '\\', '/');
    while (value.rfind("./", 0) == 0) value.erase(0, 2);
    while (!value.empty() && value.front() == '/') value.erase(value.begin());
    return value;
}

static std::string normalizeRoot(const std::filesystem::path& path) {
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec).lexically_normal();
    std::string s = ec ? FsCompat::toUtf8(path) : FsCompat::toUtf8(absolute);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static void addUniqueRoot(std::vector<std::filesystem::path>& roots,
                          const std::filesystem::path& candidate) {
    std::error_code ec;
    if (!std::filesystem::is_directory(candidate, ec)) return;
    const std::string key = normalizeRoot(candidate);
    for (const auto& root : roots) {
        if (normalizeRoot(root) == key) return;
    }
    roots.push_back(std::filesystem::absolute(candidate, ec).lexically_normal());
}

static std::string readCString(std::ifstream& f) {
    std::string s;
    char c = 0;
    while (f.read(&c, 1) && c != '\0') s += c;
    return s;
}

static bool readVPKCString(std::ifstream& f, std::uint64_t end, std::string& out) {
    out.clear();
    while (true) {
        const std::streamoff pos = f.tellg();
        if (pos < 0 || static_cast<std::uint64_t>(pos) >= end) return false;
        char c = 0;
        if (!f.read(&c, 1)) return false;
        if (c == '\0') return true;
        out.push_back(c);
    }
}
}

void Texture_L::init(const std::string& gDir) {
    gameDir = gDir;
    vpkFiles.clear();
    searchRoots.clear();
    vpkFileSystem.clear();
    embeddedFiles.clear();
    cache.clear();
    alphaByTexture.clear();
    transparentByTexture.clear();
    alphaTestByTexture.clear();
    envmapByTexture.clear();
    envmapMaskByTexture.clear();
    envmapBaseAlphaMaskByTexture.clear();
    envmapNormalAlphaMaskByTexture.clear();
    envmapTintByTexture.clear();
    sizeByTexture.clear();
    surfacePropByTexture.clear();

    SetConsoleOutputCP(CP_UTF8);
    glCompressedTexImage2D_Ext = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(
        glfwGetProcAddress("glCompressedTexImage2D"));
    if (!glCompressedTexImage2D_Ext) {
        std::cout << "Warning: Compressed texture upload is unavailable." << std::endl;
    }


    std::vector<std::filesystem::path> roots;
    const std::filesystem::path root = FsCompat::fromUtf8(gameDir).lexically_normal();
    addUniqueRoot(roots, root);
    const auto parent = root.parent_path();
    std::vector<char> gameInfoData;
    const std::filesystem::path gameInfoPath = root / "gameinfo.txt";
    if (FsCompat::isRegularFile(gameInfoPath)) {
        std::ifstream gi(gameInfoPath, std::ios::binary);
        if (gi) gameInfoData.assign(std::istreambuf_iterator<char>(gi), std::istreambuf_iterator<char>());
    }
    std::filesystem::path sourceEngineRoot = parent;
    std::string parentName = FsCompat::toUtf8(parent.filename());
    std::transform(parentName.begin(), parentName.end(), parentName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (parentName == "sourcemods") {
        const auto commonRoot = parent.parent_path() / "common";
        std::error_code commonEc;
        if (std::filesystem::is_directory(commonRoot, commonEc)) {
            std::vector<std::filesystem::path> commonGames;
            std::filesystem::directory_iterator commonIt(commonRoot, std::filesystem::directory_options::skip_permission_denied, commonEc), commonEnd;
            while (commonIt != commonEnd && !commonEc) {
                std::error_code gameEc;
                if (commonIt->is_directory(gameEc) && std::filesystem::is_directory(commonIt->path() / "hl2", gameEc)) commonGames.push_back(commonIt->path());
                commonIt.increment(commonEc);
            }
            std::sort(commonGames.begin(), commonGames.end(), [](const auto& a, const auto& b) { return normalizeRoot(a) < normalizeRoot(b); });
            for (const auto& game : commonGames) {
                std::string name = FsCompat::toUtf8(game.filename());
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name == "half-life 2" || name == "half-life 2 update") { sourceEngineRoot = game; break; }
            }
        }
    }
    const std::string selectedRootKey = normalizeRoot(root);
    const std::string sourceEngineRootKey = normalizeRoot(sourceEngineRoot);
    const std::string platformRootKey = normalizeRoot(sourceEngineRoot / "platform");
    if (sourceEngineRoot != parent) {
        addUniqueRoot(roots, sourceEngineRoot / "hl2");
        addUniqueRoot(roots, sourceEngineRoot / "platform");
    }
    auto isAllowedSearchRoot = [&](const std::filesystem::path& candidate) {
        const std::string key = normalizeRoot(candidate.lexically_normal());
        if (key == selectedRootKey || key.rfind(selectedRootKey + "/", 0) == 0) return true;
        if (key == sourceEngineRootKey || key.rfind(sourceEngineRootKey + "/", 0) == 0) return true;
        return key == platformRootKey;
    };
    auto addSearchCandidate = [&](const std::filesystem::path& candidate) {
        if (!isAllowedSearchRoot(candidate)) return;
        const auto filename = candidate.filename();
        const std::string pattern = FsCompat::toUtf8(filename);
        if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
            addUniqueRoot(roots, candidate);
            return;
        }
        std::error_code ec;
        const auto directory = candidate.parent_path();
        if (!std::filesystem::is_directory(directory, ec)) return;
        std::vector<std::filesystem::path> matches;
        std::filesystem::directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec), finish;
        while (it != finish && !ec) {
            const std::string name = FsCompat::toUtf8(it->path().filename());
            bool matched = pattern == "*";
            const size_t star = pattern.find('*');
            if (!matched && star != std::string::npos) {
                const std::string prefix = pattern.substr(0, star);
                const std::string suffix = pattern.substr(star + 1);
                matched = name.size() >= prefix.size() + suffix.size() &&
                    name.compare(0, prefix.size(), prefix) == 0 &&
                    name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
            }
            if (pattern.find('?') != std::string::npos && star == std::string::npos && name.size() == pattern.size()) {
                matched = true;
                for (size_t i = 0; i < pattern.size(); ++i) if (pattern[i] != '?' && pattern[i] != name[i]) { matched = false; break; }
            }
            std::error_code itemEc;
            if (matched && it->is_directory(itemEc) && isAllowedSearchRoot(it->path())) matches.push_back(it->path());
            it.increment(ec);
        }
        std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) { return normalizeRoot(a) < normalizeRoot(b); });
        for (const auto& match : matches) addUniqueRoot(roots, match);
    };
    if (!gameInfoData.empty()) {
        std::string gi(gameInfoData.begin(), gameInfoData.end());
        std::string low = gi;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        const size_t searchPos = low.find("\"searchpaths\"");
        if (searchPos != std::string::npos) {
            const size_t open = low.find('{', searchPos);
            if (open != std::string::npos) {
                int depth = 1;
                size_t close = open + 1;
                for (; close < low.size() && depth; ++close) {
                    if (low[close] == '{') ++depth;
                    else if (low[close] == '}') --depth;
                }
                const std::string block = gi.substr(open + 1, close > open ? close - open - 2 : std::string::npos);
                std::string blockLow = low.substr(open + 1, close > open ? close - open - 2 : std::string::npos);
                size_t pos = 0;
                while ((pos = blockLow.find('"', pos)) != std::string::npos) {
                    const size_t ke = blockLow.find('"', pos + 1);
                    if (ke == std::string::npos) break;
                    const size_t vq = blockLow.find('"', ke + 1);
                    if (vq == std::string::npos) break;
                    const size_t ve = blockLow.find('"', vq + 1);
                    if (ve == std::string::npos) break;
                    std::string value = block.substr(vq + 1, ve - vq - 1);
                    std::replace(value.begin(), value.end(), '\\', '/');
                    std::string valueLow = value;
                    std::transform(valueLow.begin(), valueLow.end(), valueLow.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    std::filesystem::path candidate;
                    const std::string gameInfoToken = "|gameinfo_path|";
                    const std::string allSourceToken = "|all_source_engine_paths|";
                    size_t marker = valueLow.find(gameInfoToken);
                    if (marker != std::string::npos) {
                        std::string tail = value.substr(marker + gameInfoToken.size());
                        while (!tail.empty() && (tail.front() == '/' || tail.front() == '.')) tail.erase(tail.begin());
                        candidate = root / FsCompat::fromUtf8(tail);
                    } else if ((marker = valueLow.find(allSourceToken)) != std::string::npos) {
                        std::string tail = value.substr(marker + allSourceToken.size());
                        while (!tail.empty() && (tail.front() == '/' || tail.front() == '.')) tail.erase(tail.begin());
                        candidate = sourceEngineRoot / FsCompat::fromUtf8(tail);
                    } else {
                        const std::filesystem::path rawValue = FsCompat::fromUtf8(value);
                        if (rawValue.is_absolute()) candidate = rawValue;
                        else {
                            const auto rootCandidate = (root / rawValue).lexically_normal();
                            std::error_code rootEc;
                            const auto parentCandidate = (parent / rawValue).lexically_normal();
                            std::error_code parentEc;
                            if (std::filesystem::exists(rootCandidate, rootEc)) candidate = rootCandidate;
                            else if (std::filesystem::exists(parentCandidate, parentEc)) candidate = parentCandidate;
                            else candidate = rootCandidate;
                        }
                    }
                    addSearchCandidate(candidate.lexically_normal());
                    pos = ve + 1;
                }
            }
        }
    }
    for (const auto& r : roots) searchRoots.push_back(FsCompat::toUtf8(r));


    std::vector<std::filesystem::path> allVpks;
    for (const auto& scanRoot : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(scanRoot, ec)) continue;
        std::vector<std::filesystem::path> rootVpks;
        std::filesystem::directory_iterator it(scanRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
        while (it != end && !ec) {
            std::error_code fileEc;
            if (it->is_regular_file(fileEc)) {
                std::string ext = FsCompat::toUtf8(it->path().extension());
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".vpk") rootVpks.push_back(it->path());
            }
            it.increment(ec);
        }
        std::sort(rootVpks.begin(), rootVpks.end(), [](const auto& a, const auto& b) { return normalizeRoot(a) < normalizeRoot(b); });
        for (const auto& path : rootVpks) {
            const std::string key = normalizeRoot(path);
            const bool duplicate = std::any_of(allVpks.begin(), allVpks.end(), [&](const auto& existing) { return normalizeRoot(existing) == key; });
            if (!duplicate) allVpks.push_back(path);
        }
    }

    for (const auto& vpkPathFs : allVpks) {
        const std::string vpkPath = FsCompat::toUtf8(vpkPathFs);
        std::ifstream vpk(vpkPathFs, std::ios::binary);
        if (!vpk.is_open()) continue;

        std::uint32_t sig = 0, version = 0, treeSize = 0;
        if (!vpk.read(reinterpret_cast<char*>(&sig), 4) || sig != 0x55AA1234u) continue;
        if (!vpk.read(reinterpret_cast<char*>(&version), 4) || (version != 1u && version != 2u)) continue;
        if (!vpk.read(reinterpret_cast<char*>(&treeSize), 4)) continue;

        const std::uint64_t headerLen = version == 1u ? 12ull : 28ull;
        if (version == 2u) {
            std::uint32_t dummy[4] = {};
            if (!vpk.read(reinterpret_cast<char*>(dummy), 16)) continue;
        }
        if (treeSize == 0) continue;

        const std::uint64_t treeStart = headerLen;
        const std::uint64_t treeEnd = treeStart + static_cast<std::uint64_t>(treeSize);
        if (treeEnd < treeStart) continue;
        vpk.seekg(static_cast<std::streamoff>(treeStart), std::ios::beg);
        if (!vpk.good()) continue;

        bool treeOk = true;
        while (treeOk) {
            std::string ext;
            if (!readVPKCString(vpk, treeEnd, ext)) { treeOk = false; break; }
            if (ext.empty()) break;

            while (treeOk) {
                std::string path;
                if (!readVPKCString(vpk, treeEnd, path)) { treeOk = false; break; }
                if (path.empty()) break;

                while (treeOk) {
                    std::string name;
                    if (!readVPKCString(vpk, treeEnd, name)) { treeOk = false; break; }
                    if (name.empty()) break;

                    const std::streamoff headerPos = vpk.tellg();
                    if (headerPos < 0 || static_cast<std::uint64_t>(headerPos) > treeEnd ||
                        treeEnd - static_cast<std::uint64_t>(headerPos) < sizeof(VPKEntryHeader)) {
                        treeOk = false;
                        break;
                    }

                    VPKEntryHeader entry{};
                    if (!vpk.read(reinterpret_cast<char*>(&entry), sizeof(entry))) {
                        treeOk = false;
                        break;
                    }

                    std::string fullPath = (path == " " || path.empty())
                        ? name + "." + ext
                        : path + "/" + name + "." + ext;
                    fullPath = normalizePath(fullPath);

                    const std::streamoff preloadPos = vpk.tellg();
                    if (preloadPos < 0 || static_cast<std::uint64_t>(preloadPos) > treeEnd ||
                        static_cast<std::uint64_t>(entry.preloadBytes) >
                            treeEnd - static_cast<std::uint64_t>(preloadPos)) {
                        treeOk = false;
                        break;
                    }

                    VPKFileEntry fEntry{};
                    fEntry.preloadBytes = entry.preloadBytes;
                    fEntry.archiveIndex = entry.archiveIndex;
                    fEntry.entryLength = entry.entryLength;
                    fEntry.dirFilePreloadOffset = static_cast<size_t>(preloadPos);
                    fEntry.absoluteDataOffset = entry.archiveIndex == 0x7FFF
                        ? static_cast<size_t>(headerLen + static_cast<std::uint64_t>(treeSize) + entry.entryOffset)
                        : static_cast<size_t>(entry.entryOffset);
                    fEntry.dirFilePath = vpkPath;
                    vpkFileSystem[fullPath].push_back(fEntry);

                    if (entry.preloadBytes > 0) {
                        vpk.seekg(entry.preloadBytes, std::ios::cur);
                        if (!vpk.good()) {
                            treeOk = false;
                            break;
                        }
                    }
                }
            }
        }

        if (treeOk) vpkFiles.push_back(vpkPath);
    }
}

void Texture_L::initPakFile(const std::vector<char>& pakData) {
    embeddedFiles.clear();
    if (pakData.empty()) return;

    const char* data = pakData.data();
    const size_t size = pakData.size();
    size_t i = 0;
    while (i + 30 <= size) {
        if (static_cast<unsigned char>(data[i]) == 0x50 &&
            static_cast<unsigned char>(data[i + 1]) == 0x4B &&
            static_cast<unsigned char>(data[i + 2]) == 0x03 &&
            static_cast<unsigned char>(data[i + 3]) == 0x04) {
            const ZipLocalHeader* h = reinterpret_cast<const ZipLocalHeader*>(data + i);
            if (i + 30 + h->fname_len > size) break;

            std::string name(data + i + 30, h->fname_len);
            name = normalizePath(name);
            const size_t fileStart = i + 30 + h->fname_len + h->extra_len;
            size_t dataSize = h->csize;
            if (h->comp == 0 && dataSize == 0) dataSize = h->usize;

            if (dataSize > 0 && fileStart <= size && dataSize <= size - fileStart) {
                if (h->comp == 0) {
                    embeddedFiles[name] = std::vector<char>(data + fileStart, data + fileStart + dataSize);
                } else if (h->comp == 8 && h->usize > 0) {
                    int decodedSize = 0;
                    char* decoded = stbi_zlib_decode_noheader_malloc(data + fileStart, static_cast<int>(dataSize), &decodedSize);
                    if (decoded && decodedSize > 0) {
                        embeddedFiles[name] = std::vector<char>(decoded, decoded + decodedSize);
                        std::free(decoded);
                    }
                }
            }
            const size_t jump = 30 + h->fname_len + h->extra_len + dataSize;
            if (jump == 0 || jump > size - i) break;
            i += jump;
        } else if (static_cast<unsigned char>(data[i]) == 0x50 &&
                   static_cast<unsigned char>(data[i + 1]) == 0x4B &&
                   (static_cast<unsigned char>(data[i + 2]) == 0x01 ||
                    static_cast<unsigned char>(data[i + 2]) == 0x05)) {
            break;
        } else {
            ++i;
        }
    }
}

bool Texture_L::getFileRawData(std::string fileName, std::vector<char>& out) {
    out.clear();
    fileName = normalizePath(std::move(fileName));
    if (fileName.empty()) return false;

    const bool doLog = fileName.find(".mdl") != std::string::npos ||
                       fileName.find(".vvd") != std::string::npos ||
                       fileName.find(".vtx") != std::string::npos;


    for (const std::string& root : searchRoots) {
        const std::filesystem::path path = FsCompat::fromUtf8(root) / FsCompat::fromUtf8(fileName);
        std::ifstream f(path, std::ios::binary);
        if (f.is_open()) {
            out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!out.empty()) return true;
        }
    }


    auto emIt = embeddedFiles.find(fileName);
    if (emIt != embeddedFiles.end()) {
        out = emIt->second;
        return !out.empty();
    }

    auto it = vpkFileSystem.find(fileName);
    if (it != vpkFileSystem.end()) {
        constexpr std::uint64_t maxEntryBytes = 100ull * 1024ull * 1024ull;
        for (const VPKFileEntry& fEntry : it->second) {
            const std::uint64_t total = static_cast<std::uint64_t>(fEntry.preloadBytes) +
                                        static_cast<std::uint64_t>(fEntry.entryLength);
            if (total == 0 || total > maxEntryBytes) continue;

            std::vector<char> candidate(static_cast<size_t>(total));
            bool ok = true;

            if (fEntry.preloadBytes > 0) {
                std::ifstream dirVpk(FsCompat::fromUtf8(fEntry.dirFilePath), std::ios::binary);
                if (!dirVpk.is_open()) ok = false;
                else {
                    dirVpk.seekg(static_cast<std::streamoff>(fEntry.dirFilePreloadOffset), std::ios::beg);
                    if (!dirVpk.read(candidate.data(), fEntry.preloadBytes)) ok = false;
                }
            }
            if (!ok) continue;

            if (fEntry.entryLength > 0) {
                std::filesystem::path dirPath = FsCompat::fromUtf8(fEntry.dirFilePath);
                std::filesystem::path archivePath = dirPath;
                if (fEntry.archiveIndex != 0x7FFFu) {
                    std::string stem = FsCompat::toUtf8(dirPath.stem());
                    const std::string suffix = "_dir";
                    if (stem.size() >= suffix.size() &&
                        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        stem.resize(stem.size() - suffix.size());
                    }
                    char num[16] = {};
                    std::snprintf(num, sizeof(num), "_%03u.vpk",
                                  static_cast<unsigned>(fEntry.archiveIndex));
                    archivePath = dirPath.parent_path() / FsCompat::fromUtf8(stem + num);
                }

                std::ifstream arc(archivePath, std::ios::binary);
                if (!arc.is_open()) {
                    if (doLog) std::cout << "Warning: VPK archive not found: '"
                                          << FsCompat::toUtf8(archivePath) << "'" << std::endl;
                    continue;
                }

                arc.seekg(0, std::ios::end);
                const std::streamoff fileSize = arc.tellg();
                if (fileSize < 0) continue;
                const std::uint64_t endOffset = static_cast<std::uint64_t>(fEntry.absoluteDataOffset) +
                                                static_cast<std::uint64_t>(fEntry.entryLength);
                if (endOffset > static_cast<std::uint64_t>(fileSize)) continue;
                arc.seekg(static_cast<std::streamoff>(fEntry.absoluteDataOffset), std::ios::beg);
                if (!arc.read(candidate.data() + fEntry.preloadBytes,
                              static_cast<std::streamsize>(fEntry.entryLength))) continue;
            }

            out.swap(candidate);
            return true;
        }
    }

    if (doLog) std::cout << "Error: File not found: " << fileName << std::endl;
    return false;
}

int Texture_L::getVTFMipSize(int w, int h, int fmt) {
    w = std::max(1, w);
    h = std::max(1, h);
    switch (fmt) {
        case 13:
        case 20:
            return ((w + 3) / 4) * ((h + 3) / 4) * 8;
        case 14:
        case 15:
            return ((w + 3) / 4) * ((h + 3) / 4) * 16;
        case 0:
        case 1:
        case 11:
        case 12:
        case 16:
            return w * h * 4;
        case 2:
        case 3:
        case 9:
        case 10:
            return w * h * 3;
        case 4:
        case 6:
        case 17:
        case 18:
        case 21:
        case 22:
            return w * h * 2;
        case 5:
        case 7:
        case 8:
            return w * h;
        case 23:
            return w * h * 4;
        case 24:
        case 25:
            return w * h * 8;
        case 26:
            return w * h * 4;
        case 27:
            return w * h * 4;
        case 28:
            return w * h * 12;
        case 29:
            return w * h * 16;
        default:
            return w * h * 4;
    }
}

std::string Texture_L::parseVMT(const std::string& path) {
    std::vector<char> buffer;
    if (!getFileRawData(path, buffer) || buffer.empty()) return "";

    std::string content(buffer.begin(), buffer.end());
    std::string low = content;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    size_t p = 0;
    while ((p = low.find("$basetexture", p)) != std::string::npos) {
        const size_t endKey = p + 12;
        if (endKey < low.length() &&
            (std::isalnum(static_cast<unsigned char>(low[endKey])) || low[endKey] == '_')) {
            p = endKey;
            continue;
        }
        const size_t s = content.find_first_not_of(" \t\"", endKey);
        if (s == std::string::npos) return "";
        const size_t e = content.find_first_of(" \t\r\n\"", s);
        std::string res = content.substr(s, e == std::string::npos ? e : e - s);
        std::replace(res.begin(), res.end(), '\\', '/');
        return res;
    }
    return "";
}

std::string Texture_L::parseVMTKey(const std::string& path, const std::string& key) {
    std::vector<char> buffer;
    if (!getFileRawData(path, buffer) || buffer.empty()) return {};
    std::string content(buffer.begin(), buffer.end());
    std::string low = content;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    std::string wanted = key;
    std::transform(wanted.begin(), wanted.end(), wanted.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    size_t p = 0;
    while ((p = low.find(wanted, p)) != std::string::npos) {
        const size_t e = p + wanted.size();
        if (e < low.size() && (std::isalnum((unsigned char)low[e]) || low[e] == '_')) { p = e; continue; }
        size_t i = e;
        if (p > 0 && content[p - 1] == '\"' && i < content.size() && content[i] == '\"') ++i;
        while (i < content.size() && (content[i] == ' ' || content[i] == '\t' ||
                                      content[i] == '\r' || content[i] == '\n')) ++i;
        if (i >= content.size()) return {};
        if (content[i] == '\"') {
            const size_t qe = content.find('\"', i + 1);
            if (qe == std::string::npos) return {};
            return content.substr(i + 1, qe - i - 1);
        }
        const size_t s = i;
        while (i < content.size() && !std::isspace((unsigned char)content[i]) &&
               content[i] != '\"' && content[i] != '{' && content[i] != '}') ++i;
        if (i == s) return {};
        return content.substr(s, i - s);
    }
    return {};
}


unsigned int Texture_L::loadVTF(const std::string& path) {
    std::vector<char> buffer;
    if (!getFileRawData(path, buffer)) return 0;
    if (buffer.size() < sizeof(VTFHeader)) return 0;

    VTFHeader* h = reinterpret_cast<VTFHeader*>(buffer.data());
    if (std::memcmp(h->magic, "VTF", 3) != 0) return 0;
    if (h->width > 8192 || h->height > 8192) return 0;
    if (h->width == 0 || h->height == 0) return 0;
    if (h->mipCount == 0 || h->mipCount > 16) return 0;

    unsigned int offset = h->headerSize;
    bool usedResource = false;
    if ((h->version[0] > 7 || (h->version[0] == 7 && h->version[1] >= 3)) &&
        h->numResources > 0 && h->numResources < 64) {
        const size_t resStart = 80;
        const size_t resEnd = resStart + h->numResources * sizeof(VTFResource);
        if (resEnd <= buffer.size()) {
            VTFResource* resBase = reinterpret_cast<VTFResource*>(buffer.data() + resStart);
            for (unsigned int i = 0; i < h->numResources; ++i) {
                if (resBase[i].tag[0] == 0x30 && resBase[i].tag[1] == 0 && resBase[i].tag[2] == 0) {
                    offset = resBase[i].offset;
                    usedResource = true;
                    break;
                }
            }
        }
    }

    if (!usedResource) offset += getVTFMipSize(h->lowResWidth, h->lowResHeight, h->lowResFormat);

    const int numFrames = std::max(static_cast<int>(h->frames), 1);
    for (int i = h->mipCount - 1; i > 0; --i) {
        const int mipW = std::max(1, static_cast<int>(h->width) >> i);
        const int mipH = std::max(1, static_cast<int>(h->height) >> i);
        offset += static_cast<unsigned int>(getVTFMipSize(mipW, mipH, h->highResFormat) * numFrames);
    }

    const int sz = getVTFMipSize(h->width, h->height, h->highResFormat);
    if (static_cast<size_t>(offset) + static_cast<size_t>(sz) > buffer.size()) {
        std::cout << "Error: Texture buffer overflow prevented: " << path << std::endl;
        return 0;
    }

    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLenum iF = GL_RGBA;
    GLenum fF = GL_RGBA;
    bool compressed = false;

    switch (h->highResFormat) {
        case 13:
        case 20:
            iF = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT; compressed = true; break;
        case 14:
            iF = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; compressed = true; break;
        case 15:
            iF = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; compressed = true; break;
        case 2:
            iF = GL_RGB; fF = GL_RGB; break;
        case 3:
        case 10:
            iF = GL_RGB; fF = GL_BGR_EXT; break;
        case 0:
        case 11:
            iF = GL_RGBA; fF = GL_RGBA; break;
        case 1:
        case 12:
            iF = GL_RGBA; fF = GL_BGRA_EXT; break;
        case 8:
            iF = GL_ALPHA; fF = GL_ALPHA; break;
        case 6:
            iF = GL_LUMINANCE_ALPHA; fF = GL_LUMINANCE_ALPHA; break;
        default:
            glDeleteTextures(1, &tex);
            return 0;
    }

    bool hasAlpha = false;
    switch (h->highResFormat) {
        case 0:
        case 1:
        case 6:
        case 8:
        case 11:
        case 12:
        case 14:
        case 15:
        case 19:
        case 20:
        case 21:
        case 23:
        case 24:
        case 25:
        case 26:
        case 29:
            hasAlpha = true;
            break;
        default:
            hasAlpha = false;
            break;
    }
    if ((h->flags & 0x00001000u) != 0 || (h->flags & 0x00002000u) != 0) hasAlpha = true;

    std::vector<size_t> mipOffsets(static_cast<size_t>(h->mipCount), 0);
    mipOffsets[0] = static_cast<size_t>(offset);
    size_t lowerOffset = static_cast<size_t>(offset);
    for (int mip = 1; mip < h->mipCount; ++mip) {
        const int mipW = std::max(1, static_cast<int>(h->width) >> mip);
        const int mipH = std::max(1, static_cast<int>(h->height) >> mip);
        const size_t bytes = static_cast<size_t>(getVTFMipSize(mipW, mipH, h->highResFormat)) *
                             static_cast<size_t>(numFrames);
        if (bytes > lowerOffset) {
            glDeleteTextures(1, &tex);
            return 0;
        }
        lowerOffset -= bytes;
        mipOffsets[static_cast<size_t>(mip)] = lowerOffset;
    }

    if (compressed && !glCompressedTexImage2D_Ext) {
        glDeleteTextures(1, &tex);
        return 0;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int mip = 0; mip < h->mipCount; ++mip) {
        const int mipW = std::max(1, static_cast<int>(h->width) >> mip);
        const int mipH = std::max(1, static_cast<int>(h->height) >> mip);
        const size_t mipSize = static_cast<size_t>(getVTFMipSize(mipW, mipH, h->highResFormat));
        const size_t dataOffset = mipOffsets[static_cast<size_t>(mip)];
        if (dataOffset > buffer.size() || mipSize > buffer.size() - dataOffset) {
            glDeleteTextures(1, &tex);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            return 0;
        }

        if (compressed) {
            glCompressedTexImage2D_Ext(GL_TEXTURE_2D, mip, iF, mipW, mipH, 0,
                                       static_cast<GLsizei>(mipSize), buffer.data() + dataOffset);
        } else {
            glTexImage2D(GL_TEXTURE_2D, mip, iF, mipW, mipH, 0, fF,
                         GL_UNSIGNED_BYTE, buffer.data() + dataOffset);
        }
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    h->mipCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    alphaByTexture[tex] = hasAlpha;
    sizeByTexture[tex] = {static_cast<int>(h->width), static_cast<int>(h->height)};
    return tex;
}

bool Texture_L::textureHasAlpha(unsigned int textureId) const {
    auto it = alphaByTexture.find(textureId);
    return it != alphaByTexture.end() && it->second;
}

bool Texture_L::materialUsesTransparency(unsigned int textureId) const {
    auto it = transparentByTexture.find(textureId);
    return it != transparentByTexture.end() && it->second;
}

bool Texture_L::materialUsesAlphaTest(unsigned int textureId) const {
    auto it = alphaTestByTexture.find(textureId);
    return it != alphaTestByTexture.end() && it->second;
}

bool Texture_L::materialUsesEnvmap(unsigned int textureId) const {
    auto it = envmapByTexture.find(textureId);
    return it != envmapByTexture.end() && it->second;
}

unsigned int Texture_L::getEnvmapMaskTexture(unsigned int textureId) const {
    auto it = envmapMaskByTexture.find(textureId);
    return it == envmapMaskByTexture.end() ? 0u : it->second;
}

bool Texture_L::materialUsesBaseAlphaEnvmapMask(unsigned int textureId) const {
    auto it = envmapBaseAlphaMaskByTexture.find(textureId);
    return it != envmapBaseAlphaMaskByTexture.end() && it->second;
}

unsigned int Texture_L::getEnvmapNormalAlphaMaskTexture(unsigned int textureId) const {
    auto it = envmapNormalAlphaMaskByTexture.find(textureId);
    return it == envmapNormalAlphaMaskByTexture.end() ? 0u : it->second;
}

std::array<float, 3> Texture_L::getEnvmapTint(unsigned int textureId) const {
    auto it = envmapTintByTexture.find(textureId);
    if (it != envmapTintByTexture.end()) return it->second;
    return {1.0f, 1.0f, 1.0f};
}

bool Texture_L::getTextureSize(unsigned int textureId, int& width, int& height) const {
    auto it = sizeByTexture.find(textureId);
    if (it == sizeByTexture.end()) return false;
    width = it->second.first;
    height = it->second.second;
    return true;
}

bool Texture_L::materialUsesEnvmap(const std::string& name) {
    std::string n = normalizePath(name);
    if (n.rfind("materials/", 0) == 0) n.erase(0, 10);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".vmt") == 0) n.resize(n.size() - 4);
    if (n.empty()) return false;

    std::vector<std::string> paths;
    paths.push_back("materials/" + n + ".vmt");

    for (const std::string& path : paths) {
        std::string envmap = parseVMTKey(path, "$envmap");
        std::transform(envmap.begin(), envmap.end(), envmap.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!envmap.empty() && envmap != "none" && envmap != "0") return true;
    }
    return false;
}

unsigned int Texture_L::createFallbackTexture() {
    unsigned int t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    const int size = 16;
    unsigned char pixels[size * size * 3];
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool isPink = ((x / 8) + (y / 8)) % 2 == 0;
            const int idx = (y * size + x) * 3;
            pixels[idx] = isPink ? 255 : 0;
            pixels[idx + 1] = 0;
            pixels[idx + 2] = isPink ? 255 : 0;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    sizeByTexture[t] = {size, size};
    alphaByTexture[t] = false;
    transparentByTexture[t] = false;
    alphaTestByTexture[t] = false;
    envmapByTexture[t] = false;
    envmapMaskByTexture[t] = 0;
    envmapBaseAlphaMaskByTexture[t] = false;
    envmapNormalAlphaMaskByTexture[t] = 0;
    envmapTintByTexture[t] = {1.0f, 1.0f, 1.0f};
    return t;
}

unsigned int Texture_L::getMaterial(const std::string& name) {
    if (disableTextures) {
        if (!cache.count("fallback")) cache["fallback"] = createFallbackTexture();
        return cache["fallback"];
    }

    std::string n = normalizePath(name);
    if (n.rfind("materials/", 0) == 0) n.erase(0, 10);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".vmt") == 0) n.resize(n.size() - 4);
    if (n.size() > 4 && n.compare(n.size() - 4, 4, ".vtf") == 0) n.resize(n.size() - 4);

    std::string sourceVmtPath = "materials/" + n + ".vmt";
    std::string vtfName;
    if (n.length() > 5 && n.substr(0, 5) == "maps/") {
        vtfName = parseVMT(sourceVmtPath);
        const size_t s = n.find('/', 5);
        if (s != std::string::npos) n = n.substr(s + 1);

        for (int i = 0; i < 3; ++i) {
            const size_t u = n.find_last_of('_');
            if (u == std::string::npos || u + 1 >= n.length()) break;
            const std::string suffix = n.substr(u + 1);
            bool isNumeric = !suffix.empty();
            size_t j0 = (suffix[0] == '-') ? 1 : 0;
            if (j0 >= suffix.length()) isNumeric = false;
            for (size_t j = j0; j < suffix.length() && isNumeric; ++j) {
                if (!std::isdigit(static_cast<unsigned char>(suffix[j]))) isNumeric = false;
            }
            if (isNumeric) n = n.substr(0, u);
            else break;
        }
    }

    if (cache.count(n)) return cache[n];
    const std::string vmtPath = sourceVmtPath;
    if (vtfName.empty()) vtfName = parseVMT(vmtPath);
    if (vtfName.empty()) vtfName = n;
    if (vtfName.rfind("materials/", 0) == 0) vtfName.erase(0, 10);
    if (vtfName.size() > 4 && vtfName.compare(vtfName.size() - 4, 4, ".vtf") == 0)
        vtfName.resize(vtfName.size() - 4);

    unsigned int t = loadVTF("materials/" + vtfName + ".vtf");
    if (!t && vtfName != n) t = loadVTF("materials/" + n + ".vtf");
    if (!t) {
        std::cout << "Error: Missing texture: " << n << std::endl;
        t = createFallbackTexture();
    }
    const std::string actualVmtPath = "materials/" + vtfName + ".vmt";
    std::string surface = parseVMTKey(vmtPath, "$surfaceprop");
    if (surface.empty() && actualVmtPath != vmtPath) surface = parseVMTKey(actualVmtPath, "$surfaceprop");
    std::string envmapName = parseVMTKey(vmtPath, "$envmap");
    if (envmapName.empty() && actualVmtPath != vmtPath) envmapName = parseVMTKey(actualVmtPath, "$envmap");
    std::transform(envmapName.begin(), envmapName.end(), envmapName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool useEnvmap = !envmapName.empty() && envmapName != "none" && envmapName != "0";

    std::string envmapMaskName = parseVMTKey(vmtPath, "$envmapmask");
    if (envmapMaskName.empty() && actualVmtPath != vmtPath) envmapMaskName = parseVMTKey(actualVmtPath, "$envmapmask");
    std::string baseAlphaEnvmapMask = parseVMTKey(vmtPath, "$basealphaenvmapmask");
    if (baseAlphaEnvmapMask.empty() && actualVmtPath != vmtPath) baseAlphaEnvmapMask = parseVMTKey(actualVmtPath, "$basealphaenvmapmask");
    std::string normalMapAlphaEnvmapMask = parseVMTKey(vmtPath, "$normalmapalphaenvmapmask");
    if (normalMapAlphaEnvmapMask.empty() && actualVmtPath != vmtPath) normalMapAlphaEnvmapMask = parseVMTKey(actualVmtPath, "$normalmapalphaenvmapmask");
    std::string bumpMapName = parseVMTKey(vmtPath, "$bumpmap");
    if (bumpMapName.empty() && actualVmtPath != vmtPath) bumpMapName = parseVMTKey(actualVmtPath, "$bumpmap");
    std::string materialFlags = parseVMTKey(vmtPath, "$flags");
    if (materialFlags.empty() && actualVmtPath != vmtPath) materialFlags = parseVMTKey(actualVmtPath, "$flags");
    auto isTrue = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v == "1" || v == "true";
    };
    auto parseFlags = [](const std::string& v) {
        try { return std::stoul(v); } catch (...) { return 0ul; }
    };
    const unsigned long flags = parseFlags(materialFlags);
    const bool flagAdditive = (flags & (1ul << 7)) != 0;
    const bool flagEnvmapSphere = (flags & (1ul << 17)) != 0;
    const bool flagAlphaTest = (flags & (1ul << 8)) != 0;
    const bool flagBaseAlphaEnvmapMask = (flags & (1ul << 20)) != 0;
    const bool flagNormalMapAlphaEnvmapMask = (flags & (1ul << 22)) != 0;
    const bool flagTranslucent = (flags & (1ul << 21)) != 0;

    std::string translucent = parseVMTKey(vmtPath, "$translucent");
    std::string alphatest = parseVMTKey(vmtPath, "$alphatest");
    std::string additive = parseVMTKey(vmtPath, "$additive");
    std::string alphaValue = parseVMTKey(vmtPath, "$alpha");
    if (actualVmtPath != vmtPath) {
        const std::string actualTranslucent = parseVMTKey(actualVmtPath, "$translucent");
        const std::string actualAlphaTest = parseVMTKey(actualVmtPath, "$alphatest");
        const std::string actualAdditive = parseVMTKey(actualVmtPath, "$additive");
        const std::string actualAlphaValue = parseVMTKey(actualVmtPath, "$alpha");
        if (translucent.empty()) translucent = actualTranslucent;
        if (alphatest.empty()) alphatest = actualAlphaTest;
        if (additive.empty()) additive = actualAdditive;
        if (alphaValue.empty()) alphaValue = actualAlphaValue;
    }

    const bool baseAlphaMask = useEnvmap && textureHasAlpha(t) && (isTrue(baseAlphaEnvmapMask) || flagBaseAlphaEnvmapMask);
    bool explicitTransparency = isTrue(translucent) || isTrue(additive) || flagAdditive || flagTranslucent;
    if (!explicitTransparency && !alphaValue.empty()) {
        try { explicitTransparency = std::stof(alphaValue) < 0.999f; } catch (...) {}
    }
    const bool alphaTest = isTrue(alphatest) || flagAlphaTest;
    const bool needsTransparency = explicitTransparency;

    unsigned int envmapMaskTexture = 0;
    if (useEnvmap && !envmapMaskName.empty()) {
        std::string maskName = normalizePath(envmapMaskName);
        if (maskName.rfind("materials/", 0) == 0) maskName.erase(0, 10);
        if (maskName.size() > 4 && maskName.compare(maskName.size() - 4, 4, ".vmt") == 0) maskName.resize(maskName.size() - 4);
        if (!maskName.empty()) envmapMaskTexture = getMaterial(maskName);
    }

    unsigned int normalMapAlphaMaskTexture = 0;
    if (useEnvmap && (isTrue(normalMapAlphaEnvmapMask) || flagNormalMapAlphaEnvmapMask) && !bumpMapName.empty()) {
        std::string bumpName = normalizePath(bumpMapName);
        if (bumpName.rfind("materials/", 0) == 0) bumpName.erase(0, 10);
        if (bumpName.size() > 4 && bumpName.compare(bumpName.size() - 4, 4, ".vmt") == 0) bumpName.resize(bumpName.size() - 4);
        if (!bumpName.empty()) normalMapAlphaMaskTexture = getMaterial(bumpName);
    }

    std::array<float, 3> envmapTint = {1.0f, 1.0f, 1.0f};
    std::string envmapTintValue = parseVMTKey(vmtPath, "$envmaptint");
    if (envmapTintValue.empty() && actualVmtPath != vmtPath) envmapTintValue = parseVMTKey(actualVmtPath, "$envmaptint");
    if (!envmapTintValue.empty()) {
        std::string tint = envmapTintValue;
        std::replace(tint.begin(), tint.end(), '[', ' ');
        std::replace(tint.begin(), tint.end(), ']', ' ');
        std::stringstream ts(tint);
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (ts >> r >> g >> b) {
            envmapTint = {std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b)};
        }
    }

    envmapByTexture[t] = useEnvmap;
    envmapMaskByTexture[t] = envmapMaskTexture;
    envmapBaseAlphaMaskByTexture[t] = baseAlphaMask;
    envmapNormalAlphaMaskByTexture[t] = normalMapAlphaMaskTexture;
    transparentByTexture[t] = needsTransparency;
    alphaTestByTexture[t] = alphaTest;
    envmapTintByTexture[t] = envmapTint;
    if (!surface.empty()) surfacePropByTexture[t] = surface;
    return cache[n] = t;
}

std::string Texture_L::getSurfaceProp(const std::string& name) {
    std::string n = normalizePath(name);
    if (n.rfind("materials/",0)==0) n.erase(0,10);
    if (n.size()>4 && n.compare(n.size()-4,4,".vmt")==0) n.resize(n.size()-4);
    std::string sourcePath = "materials/" + n + ".vmt";
    if (n.rfind("maps/",0)==0) {
        std::string mapped = n;
        const size_t slash = mapped.find('/',5);
        if (slash != std::string::npos) mapped = mapped.substr(slash + 1);
        sourcePath = "materials/" + n + ".vmt";
        std::string surface = parseVMTKey(sourcePath, "$surfaceprop");
        if (!surface.empty()) return surface;
        if (!mapped.empty()) return parseVMTKey("materials/" + mapped + ".vmt", "$surfaceprop");
        return {};
    }
    return parseVMTKey(sourcePath, "$surfaceprop");
}

std::string Texture_L::getSurfaceProp(unsigned int textureId) const {
    auto it=surfacePropByTexture.find(textureId);
    return it==surfacePropByTexture.end()?std::string():it->second;
}
