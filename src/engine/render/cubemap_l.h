#pragma once
#include <string>
#include <vector>
#include "math.h"
#include "texture_l.h"

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

struct CubemapProbe_L {
    Vec3 position;
    int sourceX = 0;
    int sourceY = 0;
    int sourceZ = 0;
    unsigned int texture = 0;
};

class Cubemap_L {
public:
    Cubemap_L() = default;
    ~Cubemap_L();

    void clear();
    bool load(const std::string& mapName, const std::string& entityData, Texture_L& textureSystem);
    unsigned int nearestTexture(const Vec3& position) const;
    bool hasCubemaps() const;

private:
    std::vector<CubemapProbe_L> probes;

    static std::string lower(std::string value);
    static std::string entityValue(const std::string& block, const std::string& key);
    static bool parseOrigin(const std::string& value, Vec3& out);
    static bool loadVTF(Texture_L& textureSystem, const std::string& path, unsigned int& outTexture);
    static int mipSize(int width, int height, int format);
    static bool uploadVTF(Texture_L& textureSystem, const std::vector<char>& data, unsigned int& outTexture);
};
