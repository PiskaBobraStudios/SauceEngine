#pragma once
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <iostream>
#include <cstddef>
#include <cstring>
#include "types.h"
#include "texture_l.h"

struct BSPFaceBounds {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};

struct TriggerResult {
    std::string targetMap;
    std::string landmark;
};

struct MapTrigger {
    BSPFaceBounds bounds;
    std::string targetMap;
    std::string landmark;
};

class BSP_L {
public:
    std::vector<BSPVertex>   vertices;
    std::vector<BSPEdge>     edges;
    std::vector<int>         surfedges;
    std::vector<BSPFace>     faces;
    std::vector<BSPTexInfo>  texinfos;
    std::vector<BSPTexData>  texdatas;
    std::vector<BSPDispInfo> dispinfos;
    std::vector<BSPDispVert> dispverts;
    std::vector<ColorRGBExp32> lightingData;
    std::vector<unsigned int> faceLightmapTextures;
    std::vector<unsigned char> faceBloomEligible;
    std::vector<char>        texDataStringData;
    std::vector<int>         texDataStringTable;
    std::vector<BSPModel>    models;
    std::vector<char>        pakfileData;

    std::string entityData;
    std::map<int, unsigned int> glTextures;
    std::vector<bool> faceRenderable;
    std::vector<GeneratedDispTri> dispTriangles;
    std::vector<BSPFaceBounds> faceBounds;
    std::vector<MapTrigger> changelevelTriggers;
    Texture_L* textureSystem = nullptr;

    std::string mapSkyboxName = "";
    std::vector<char> staticPropData;
    int staticPropVersion = 0;

    bool loadMap(const std::string& filename);
    bool loadMapFromData(const std::vector<char>& data, const std::string& sourceName = "<memory>");
    void setupTextures(Texture_L& texL);
    void buildDisplacements();
    void buildLightmaps();
    void renderWorld();
    void renderLightmaps();
    float sampleLightmapBrightness(const Vec3& worldPos) const;
    Vec3 sampleLightmapColor(const Vec3& worldPos) const;
    Vec3 sampleLightmapLighting(const Vec3& worldPos, float* outBrightness = nullptr) const;
    Vec3 getPlayerSpawn();
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal);
    std::string getHitTextureName(const Vec3& orig, const Vec3& dir);                                                                      
    TriggerResult checkTriggers(const Vec3& pos, float radius, float height);
    bool findLandmark(const std::string& name, Vec3& outPos);
    bool textureHasAlpha(unsigned int texture) const;

private:
                                                                                                                                                                                 
    float parseSafeFloat(const std::string& str, float def = 0.0f);
    int parseSafeInt(const std::string& str, int def = 0);

    template<typename T>
    bool readLumpSafe(std::ifstream& file, const BSPLump& lump,
        std::vector<T>& vec, size_t fileSize, const char* lumpName)
    {
        vec.clear();

        if (lump.length <= 0 || lump.offset <= 0) return true;

        size_t off = static_cast<size_t>(lump.offset);
        size_t len = static_cast<size_t>(lump.length);

        if (off >= fileSize || len > fileSize || off + len > fileSize) {
            std::cout << "[BSP] WARNING: Lump '" << lumpName << "' out of bounds, skipping." << std::endl;
            return true;
        }

        size_t elemSize = sizeof(T);
        size_t count = len / elemSize;

        if (count == 0) return true;

        if (count > 50000000) {                                                                                                           
            std::cout << "[BSP] WARNING: Lump '" << lumpName << "' too large (" << count << " elements), skipping." << std::endl;
            return true;
        }

        try {
            vec.resize(count);
        }
        catch (...) {
            std::cout << "[BSP] ERROR: Out of memory for lump '" << lumpName << "'" << std::endl;
            return false;
        }

        file.seekg(static_cast<std::streamoff>(off), std::ios::beg);
        file.read(reinterpret_cast<char*>(vec.data()), count * elemSize);

        if (!file.good()) {
            std::cout << "[BSP] WARNING: Read error on lump '" << lumpName << "'" << std::endl;
            vec.clear();
        }

        return true;
    }


    template<typename T>
    bool readLumpSafe(const std::vector<char>& data, const BSPLump& lump,
        std::vector<T>& vec, size_t fileSize, const char* lumpName)
    {
        vec.clear();
        if (lump.length <= 0 || lump.offset <= 0) return true;
        const size_t off = static_cast<size_t>(lump.offset);
        const size_t len = static_cast<size_t>(lump.length);
        if (off >= fileSize || len > fileSize || off + len > fileSize) {
            std::cout << "[BSP] WARNING: Lump '" << lumpName << "' out of bounds, skipping." << std::endl;
            return true;
        }
        const size_t elemSize = sizeof(T);
        const size_t count = len / elemSize;
        if (count == 0) return true;
        if (count > 50000000) {
            std::cout << "[BSP] WARNING: Lump '" << lumpName << "' too large (" << count << " elements), skipping." << std::endl;
            return true;
        }
        try { vec.resize(count); }
        catch (...) {
            std::cout << "[BSP] ERROR: Out of memory for lump '" << lumpName << "'" << std::endl;
            return false;
        }
        std::memcpy(vec.data(), data.data() + off, count * elemSize);
        return true;
    }

    void validateFaces();
    void parseEntities();
    std::string extractEntityValue(const std::string& block, const std::string& key);
    void precalculateBounds();
};