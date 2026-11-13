#include "cubemap_l.h"
#include "fs_compat.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <GLFW/glfw3.h>

#define GL_BGR_EXT 0x80E0
#define GL_BGRA_EXT 0x80E1
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#ifndef GL_RGBA16F_ARB
#define GL_RGBA16F_ARB 0x881A
#endif
#ifndef GL_HALF_FLOAT_ARB
#define GL_HALF_FLOAT_ARB 0x140B
#endif
#ifndef GL_RGBA16
#define GL_RGBA16 0x805B
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_R32F
#define GL_R32F 0x822E
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_FLOAT
#define GL_FLOAT 0x1406
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_RGBA4
#define GL_RGBA4 0x8056
#endif
#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif
#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA 0x190A
#endif

#ifndef GL_TEXTURE_CUBE_MAP
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_BINDING_CUBE_MAP 0x8514
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X 0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y 0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y 0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z 0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z 0x851A
#define GL_PROXY_TEXTURE_CUBE_MAP 0x851B
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE 0x851C
#define GL_REFLECTION_MAP 0x8512
#endif
#ifndef GL_TEXTURE_WRAP_R
#define GL_TEXTURE_WRAP_R 0x8072
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC_CUBEMAP)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*);
static PFNGLCOMPRESSEDTEXIMAGE2DPROC_CUBEMAP gCompressedCube = nullptr;

#pragma pack(push, 1)
struct CubemapVTFResource { char tag[3]; unsigned char flags; unsigned int offset; };
struct CubemapVTFHeader {
    char magic[4];
    unsigned int version[2];
    unsigned int headerSize;
    unsigned short width, height;
    unsigned int flags;
    unsigned short frames, firstFrame;
    unsigned char pad0[4];
    float reflectivity[3];
    unsigned char pad1[4];
    float bumpScale;
    unsigned int highResFormat;
    unsigned char mipCount;
    unsigned int lowResFormat;
    unsigned char lowResWidth, lowResHeight;
    unsigned short depth;
    unsigned char pad2[3];
    unsigned int numResources;
};
#pragma pack(pop)

Cubemap_L::~Cubemap_L() {
    clear();
}

void Cubemap_L::clear() {
    std::vector<unsigned int> ids;
    for (const auto& probe : probes) {
        if (probe.texture) ids.push_back(probe.texture);
    }
    if (!ids.empty()) glDeleteTextures(static_cast<GLsizei>(ids.size()), ids.data());
    probes.clear();
}

std::string Cubemap_L::lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string Cubemap_L::entityValue(const std::string& block, const std::string& key) {
    std::string wanted = "\"" + key + "\"";
    size_t p = block.find(wanted);
    if (p == std::string::npos) {
        const std::string blockLow = lower(block);
        const std::string wantedLow = lower(wanted);
        p = blockLow.find(wantedLow);
        if (p == std::string::npos) return {};
    }
    const size_t q = block.find('"', p + wanted.size());
    if (q == std::string::npos) return {};
    const size_t e = block.find('"', q + 1);
    if (e == std::string::npos) return {};
    return block.substr(q + 1, e - q - 1);
}

bool Cubemap_L::parseOrigin(const std::string& value, Vec3& out) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    char tail = 0;
    std::stringstream ss(value);
    if (!(ss >> x >> y >> z)) return false;
    if (ss >> tail) return false;
    out = {x, z, -y};
    return true;
}

int Cubemap_L::mipSize(int width, int height, int format) {
    width = std::max(1, width);
    height = std::max(1, height);
    switch (format) {
        case 13:
        case 20:
            return ((width + 3) / 4) * ((height + 3) / 4) * 8;
        case 14:
        case 15:
            return ((width + 3) / 4) * ((height + 3) / 4) * 16;
        case 0:
        case 1:
        case 11:
        case 12:
        case 16:
            return width * height * 4;
        case 2:
        case 3:
        case 9:
        case 10:
            return width * height * 3;
        case 4:
        case 6:
        case 17:
        case 18:
        case 21:
        case 22:
            return width * height * 2;
        case 5:
        case 7:
        case 8:
            return width * height;
        case 23:
            return width * height * 4;
        case 24:
        case 25:
            return width * height * 8;
        case 26:
            return width * height * 4;
        case 27:
            return width * height * 4;
        case 28:
            return width * height * 12;
        case 29:
            return width * height * 16;
        default:
            return 0;
    }
}

bool Cubemap_L::loadVTF(Texture_L& textureSystem, const std::string& path, unsigned int& outTexture) {
    outTexture = 0;
    std::vector<char> data;
    if (!textureSystem.getFileRawData(path, data)) return false;
    return uploadVTF(textureSystem, data, outTexture);
}

bool Cubemap_L::uploadVTF(Texture_L&, const std::vector<char>& buffer, unsigned int& outTexture) {
    outTexture = 0;
    if (buffer.size() < sizeof(CubemapVTFHeader)) return false;
    const CubemapVTFHeader* h = reinterpret_cast<const CubemapVTFHeader*>(buffer.data());
    if (std::memcmp(h->magic, "VTF", 3) != 0) return false;
    if ((h->flags & 0x00004000u) == 0) return false;
    if (h->width == 0 || h->height == 0 || h->width > 4096 || h->height > 4096) return false;
    if (h->mipCount == 0 || h->mipCount > 16) return false;

    const int frames = std::max(1, static_cast<int>(h->frames));
    const int faces = (h->version[0] == 7 && h->version[1] < 1) ? 6 : 7;
    if (faces < 6) return false;

    unsigned int dataOffset = h->headerSize;
    bool usedResource = false;
    if ((h->version[0] > 7 || (h->version[0] == 7 && h->version[1] >= 3)) && h->numResources > 0 && h->numResources < 64) {
        const size_t start = 80;
        const size_t end = start + static_cast<size_t>(h->numResources) * sizeof(CubemapVTFResource);
        if (end <= buffer.size()) {
            const auto* resources = reinterpret_cast<const CubemapVTFResource*>(buffer.data() + start);
            for (unsigned int i = 0; i < h->numResources; ++i) {
                if (resources[i].tag[0] == 0x30 && resources[i].tag[1] == 0 && resources[i].tag[2] == 0) {
                    dataOffset = resources[i].offset;
                    usedResource = true;
                    break;
                }
            }
        }
    }
    if (!usedResource) {
        const int lowSize = mipSize(h->lowResWidth, h->lowResHeight, h->lowResFormat);
        if (lowSize < 0) return false;
        if (static_cast<size_t>(dataOffset) + static_cast<size_t>(lowSize) > buffer.size()) return false;
        dataOffset += static_cast<unsigned int>(lowSize);
    }

    std::vector<size_t> mipOffsets(static_cast<size_t>(h->mipCount), 0);
    size_t cursor = static_cast<size_t>(dataOffset);
    for (int mip = h->mipCount - 1; mip >= 0; --mip) {
        const int w = std::max(1, static_cast<int>(h->width) >> mip);
        const int hh = std::max(1, static_cast<int>(h->height) >> mip);
        const int faceSize = mipSize(w, hh, h->highResFormat);
        if (faceSize <= 0) return false;
        const size_t bytes = static_cast<size_t>(faceSize) * static_cast<size_t>(faces) * static_cast<size_t>(frames);
        if (cursor + bytes > buffer.size()) return false;
        mipOffsets[static_cast<size_t>(mip)] = cursor;
        cursor += bytes;
    }

    GLenum internalFormat = GL_RGBA;
    GLenum externalFormat = GL_RGBA;
    GLenum dataType = GL_UNSIGNED_BYTE;
    bool compressed = false;
    switch (h->highResFormat) {
        case 13:
        case 20:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
            compressed = true;
            break;
        case 14:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
            compressed = true;
            break;
        case 15:
            internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            compressed = true;
            break;
        case 2:
            internalFormat = GL_RGB;
            externalFormat = GL_RGB;
            break;
        case 3:
        case 10:
            internalFormat = GL_RGB;
            externalFormat = GL_BGR_EXT;
            break;
        case 0:
        case 11:
            internalFormat = GL_RGBA;
            externalFormat = GL_RGBA;
            break;
        case 1:
        case 12:
            internalFormat = GL_RGBA;
            externalFormat = GL_BGRA_EXT;
            break;
        case 17:
            internalFormat = GL_RGB;
            externalFormat = GL_RGB;
            break;
        case 18:
            internalFormat = GL_RGB5_A1;
            externalFormat = GL_BGRA;
            break;
        case 19:
            internalFormat = GL_RGBA4;
            externalFormat = GL_BGRA;
            break;
        case 21:
            internalFormat = GL_RGBA;
            externalFormat = GL_BGRA;
            break;
        case 22:
            internalFormat = GL_LUMINANCE_ALPHA;
            externalFormat = GL_LUMINANCE_ALPHA;
            break;
        case 23:
            internalFormat = GL_RGBA;
            externalFormat = GL_RGBA;
            break;
        case 26:
            internalFormat = GL_RGBA;
            externalFormat = GL_RGBA;
            break;
        case 27:
            internalFormat = GL_R32F;
            externalFormat = GL_RED;
            dataType = GL_FLOAT;
            break;
        case 24:
            internalFormat = GL_RGBA16F_ARB;
            externalFormat = GL_RGBA;
            dataType = GL_HALF_FLOAT_ARB;
            break;
        case 25:
            internalFormat = GL_RGBA16;
            externalFormat = GL_RGBA;
            dataType = GL_UNSIGNED_SHORT;
            break;
        default:
            return false;
    }

    if (compressed) {
        if (!gCompressedCube) gCompressedCube = reinterpret_cast<PFNGLCOMPRESSEDTEXIMAGE2DPROC_CUBEMAP>(glfwGetProcAddress("glCompressedTexImage2D"));
        if (!gCompressedCube) return false;
    }

    glGenTextures(1, &outTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, outTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLenum targetsBySourceFace[6] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y
    };

    for (int mip = 0; mip < h->mipCount; ++mip) {
        const int w = std::max(1, static_cast<int>(h->width) >> mip);
        const int hh = std::max(1, static_cast<int>(h->height) >> mip);
        const size_t faceSize = static_cast<size_t>(mipSize(w, hh, h->highResFormat));
        const size_t frameBase = mipOffsets[static_cast<size_t>(mip)];
        for (int face = 0; face < 6; ++face) {
            const size_t source = frameBase + static_cast<size_t>(face) * faceSize;
            if (source > buffer.size() || faceSize > buffer.size() - source) {
                glDeleteTextures(1, &outTexture);
                outTexture = 0;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                return false;
            }
            if (compressed) {
                gCompressedCube(targetsBySourceFace[face], mip, internalFormat, w, hh, 0, static_cast<GLsizei>(faceSize), buffer.data() + source);
            } else {
                glTexImage2D(targetsBySourceFace[face], mip, internalFormat, w, hh, 0, externalFormat, dataType, buffer.data() + source);
            }
        }
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, h->mipCount > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#ifdef GL_TEXTURE_WRAP_R
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
#endif
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return true;
}

bool Cubemap_L::load(const std::string& mapName, const std::string& entityData, Texture_L& textureSystem) {
    clear();
    std::string mapBase = mapName;
    const size_t slash = mapBase.find_last_of("/\\");
    if (slash != std::string::npos) mapBase.erase(0, slash + 1);
    const size_t dot = mapBase.find_last_of('.');
    if (dot != std::string::npos) mapBase.resize(dot);
    mapBase = lower(mapBase);

    auto addProbe = [&](const std::string& file, int x, int y, int z) {
        for (const auto& existing : probes) {
            if (existing.texture && existing.sourceX == x &&
                existing.sourceY == y && existing.sourceZ == z) return;
        }
        CubemapProbe_L probe;
        probe.sourceX = x;
        probe.sourceY = y;
        probe.sourceZ = z;
        probe.position = {static_cast<float>(x), static_cast<float>(z), static_cast<float>(-y)};
        if (!loadVTF(textureSystem, file, probe.texture)) return;
        probes.push_back(probe);
    };

    size_t pos = 0;
    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        const size_t end = entityData.find('}', pos);
        if (end == std::string::npos) break;
        const std::string block = entityData.substr(pos, end - pos);
        if (lower(entityValue(block, "classname")) == "env_cubemap") {
            Vec3 origin;
            const std::string originValue = entityValue(block, "origin");
            if (parseOrigin(originValue, origin)) {
                CubemapProbe_L probe;
                probe.position = origin;
                std::stringstream ss(originValue);
                float sx = 0.0f, sy = 0.0f, sz = 0.0f;
                ss >> sx >> sy >> sz;
                probe.sourceX = static_cast<int>(std::lround(sx));
                probe.sourceY = static_cast<int>(std::lround(sy));
                probe.sourceZ = static_cast<int>(std::lround(sz));
                const std::string stem = "maps/" + mapBase + "/c" + std::to_string(probe.sourceX) + "_" + std::to_string(probe.sourceY) + "_" + std::to_string(probe.sourceZ);
                const std::string explicitName = entityValue(block, "cubemap");
                std::vector<std::string> candidates;
                if (!explicitName.empty()) {
                    std::string n = lower(explicitName);
                    if (n.find(".vtf") == std::string::npos) n += ".vtf";
                    if (n.rfind("materials/", 0) != 0) n = "materials/" + n;
                    candidates.push_back(n);
                }
                candidates.push_back("materials/" + stem + ".vtf");
                candidates.push_back("materials/" + stem + ".hdr.vtf");
                candidates.push_back("materials/" + stem + "_hdr.vtf");
                candidates.push_back("materials/maps/" + mapBase + "/cubemapdefault.vtf");
                candidates.push_back("materials/maps/" + mapBase + "/cubemapdefault.hdr.vtf");
                for (const std::string& candidate : candidates) {
                    unsigned int texture = 0;
                    if (loadVTF(textureSystem, candidate, texture)) {
                        probe.texture = texture;
                        break;
                    }
                }
                probes.push_back(probe);
            }
        }
        pos = end + 1;
    }

    const std::string prefix = "materials/maps/" + mapBase + "/";
    std::vector<std::string> files;
    for (const auto& kv : textureSystem.embeddedFiles) {
        if (kv.first.compare(0, prefix.size(), prefix) == 0) files.push_back(kv.first);
    }
    for (const auto& kv : textureSystem.vpkFileSystem) {
        if (kv.first.compare(0, prefix.size(), prefix) == 0) files.push_back(kv.first);
    }
    for (const std::string& root : textureSystem.searchRoots) {
        std::error_code ec;
        const std::filesystem::path dir = FsCompat::fromUtf8(root) / FsCompat::fromUtf8(prefix);
        std::filesystem::directory_iterator it(dir, ec), end;
        while (!ec && it != end) {
            std::error_code fileEc;
            if (it->is_regular_file(fileEc)) files.push_back(prefix + lower(FsCompat::toUtf8(it->path().filename())));
            it.increment(ec);
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    for (int pass = 0; pass < 2; ++pass) {
        for (const std::string& file : files) {
            if (file.size() < prefix.size() + 5) continue;
            if (file.compare(file.size() - 4, 4, ".vtf") != 0) continue;
            std::string stem = file.substr(prefix.size(), file.size() - prefix.size() - 4);
            bool hdr = false;
            if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".hdr") == 0) { hdr = true; stem.resize(stem.size() - 4); }
            else if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, "_hdr") == 0) { hdr = true; stem.resize(stem.size() - 4); }
            if (hdr != (pass == 1)) continue;
            if (stem.size() < 4 || stem[0] != 'c') continue;
            int x = 0, y = 0, z = 0;
            char tail = 0;
            std::stringstream ss(stem.substr(1));
            if (!(ss >> x)) continue;
            if (ss.get() != '_') continue;
            if (!(ss >> y)) continue;
            if (ss.get() != '_') continue;
            if (!(ss >> z)) continue;
            if (ss >> tail) continue;
            addProbe(file, x, y, z);
        }
    }

    if (probes.empty()) {
        const char* suffixes[] = { ".vtf", ".hdr.vtf", "_hdr.vtf" };
        for (const char* suffix : suffixes) {
            CubemapProbe_L probe;
            if (loadVTF(textureSystem, prefix + "cubemapdefault" + suffix, probe.texture)) {
                probes.push_back(probe);
                break;
            }
        }
    }
    return hasCubemaps();
}

unsigned int Cubemap_L::nearestTexture(const Vec3& position) const {
    unsigned int result = 0;
    float best = 1e30f;
    for (const auto& probe : probes) {
        if (!probe.texture) continue;
        const float d = (probe.position - position).length();
        if (d < best) {
            best = d;
            result = probe.texture;
        }
    }
    return result;
}

bool Cubemap_L::hasCubemaps() const {
    for (const auto& probe : probes) if (probe.texture) return true;
    return false;
}
