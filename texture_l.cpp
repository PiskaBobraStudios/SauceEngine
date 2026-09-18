#include "texture_l.h"
#include <fstream>
#include <algorithm>
#include <iostream>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <limits>
#include <windows.h>
#include <GLFW/glfw3.h>
#include <cstring>
#include <cctype>
#include <cstdio>

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
    std::string s = ec ? path.generic_string() : absolute.generic_string();
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
    sizeByTexture.clear();

    SetConsoleOutputCP(CP_UTF8);
    glCompressedTexImage2D_Ext = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC>(
        glfwGetProcAddress("glCompressedTexImage2D"));
    if (!glCompressedTexImage2D_Ext) {
        std::cout << "Warning: Compressed texture upload is unavailable." << std::endl;
    }

                                                                                
                                                                       
    std::vector<std::filesystem::path> roots;
    const std::filesystem::path root = std::filesystem::path(gameDir);
    addUniqueRoot(roots, root);
    const auto parent = root.parent_path();
    if (!parent.empty()) {
        addUniqueRoot(roots, parent / "hl2");
        addUniqueRoot(roots, parent / "platform");
        addUniqueRoot(roots, parent / "hl2_mp");
        addUniqueRoot(roots, parent / "episodic");
        addUniqueRoot(roots, parent / "ep2");
    }
    for (const auto& r : roots) searchRoots.push_back(r.string());

                                                                                 
                                                                               
    std::vector<std::filesystem::path> allVpks;
    for (const auto& scanRoot : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(scanRoot, ec)) continue;
        std::filesystem::recursive_directory_iterator it(
            scanRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
        while (it != end && !ec) {
            std::error_code fileEc;
            if (it->is_regular_file(fileEc)) {
                const std::string filename = it->path().filename().string();
                if (filename.size() >= 4) {
                    std::string ext = it->path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
                    if (ext == ".vpk") allVpks.push_back(it->path());
                }
            }
            it.increment(ec);
        }
    }

    std::sort(allVpks.begin(), allVpks.end(), [](const auto& a, const auto& b) {
        return normalizeRoot(a) < normalizeRoot(b);
    });
    allVpks.erase(std::unique(allVpks.begin(), allVpks.end(), [](const auto& a, const auto& b) {
        return normalizeRoot(a) == normalizeRoot(b);
    }), allVpks.end());

    for (const auto& vpkPathFs : allVpks) {
        const std::string vpkPath = vpkPathFs.string();
        std::ifstream vpk(vpkPath, std::ios::binary);
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

            if (h->comp == 0 && dataSize > 0 && fileStart <= size && dataSize <= size - fileStart) {
                embeddedFiles[name] = std::vector<char>(data + fileStart, data + fileStart + dataSize);
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
        const std::filesystem::path path = std::filesystem::path(root) / fileName;
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
                std::ifstream dirVpk(fEntry.dirFilePath, std::ios::binary);
                if (!dirVpk.is_open()) ok = false;
                else {
                    dirVpk.seekg(static_cast<std::streamoff>(fEntry.dirFilePreloadOffset), std::ios::beg);
                    if (!dirVpk.read(candidate.data(), fEntry.preloadBytes)) ok = false;
                }
            }
            if (!ok) continue;

            if (fEntry.entryLength > 0) {
                std::filesystem::path dirPath(fEntry.dirFilePath);
                std::filesystem::path archivePath = dirPath;
                if (fEntry.archiveIndex != 0x7FFFu) {
                    std::string stem = dirPath.stem().string();
                    const std::string suffix = "_dir";
                    if (stem.size() >= suffix.size() &&
                        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        stem.resize(stem.size() - suffix.size());
                    }
                    char num[16] = {};
                    std::snprintf(num, sizeof(num), "_%03u.vpk",
                                  static_cast<unsigned>(fEntry.archiveIndex));
                    archivePath = dirPath.parent_path() / (stem + num);
                }

                std::ifstream arc(archivePath, std::ios::binary);
                if (!arc.is_open()) {
                    if (doLog) std::cout << "Warning: VPK archive not found: '"
                                          << archivePath.string() << "'" << std::endl;
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
        case 18:
        case 22:
        case 25:
            return w * h * 4;
        case 2:
        case 3:
        case 9:
        case 10:
            return w * h * 3;
        case 4:
        case 6:
        case 16:
        case 17:
        case 21:
        case 23:
        case 24:
            return w * h * 2;
        case 5:
        case 7:
        case 8:
            return w * h;
        case 26:
            return w * h * 4;
        case 27:
            return w * h * 12;
        case 28:
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
        case 8:
        case 6:
        case 11:
        case 12:
        case 14:
        case 15:
        case 18:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
        case 25:
        case 26:
            hasAlpha = true;
            break;
        default:
            hasAlpha = false;
            break;
    }

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

bool Texture_L::getTextureSize(unsigned int textureId, int& width, int& height) const {
    auto it = sizeByTexture.find(textureId);
    if (it == sizeByTexture.end()) return false;
    width = it->second.first;
    height = it->second.second;
    return true;
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

    std::string vtfName;
    if (n.length() > 5 && n.substr(0, 5) == "maps/") {
        vtfName = parseVMT("materials/" + n + ".vmt");
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
    if (vtfName.empty()) vtfName = parseVMT("materials/" + n + ".vmt");
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
    return cache[n] = t;
}
