#pragma once
#include <string>
#include <map>
#include <vector>
#include <utility>
#include <array>

struct VPKFileEntry {
    unsigned short preloadBytes = 0;
    unsigned short archiveIndex = 0;
    unsigned int entryLength = 0;
    size_t absoluteDataOffset = 0;
    size_t dirFilePreloadOffset = 0;
    std::string dirFilePath;
};

class Texture_L {
public:
    std::string gameDir;
    std::vector<std::string> vpkFiles;
    std::vector<std::string> searchRoots;
    std::map<std::string, std::vector<VPKFileEntry>> vpkFileSystem;
    std::map<std::string, std::vector<char>> embeddedFiles;
    std::map<std::string, unsigned int> cache;
    std::map<unsigned int, bool> alphaByTexture;
    std::map<unsigned int, bool> transparentByTexture;
    std::map<unsigned int, bool> alphaTestByTexture;
    std::map<unsigned int, bool> envmapByTexture;
    std::map<unsigned int, unsigned int> envmapMaskByTexture;
    std::map<unsigned int, bool> envmapBaseAlphaMaskByTexture;
    std::map<unsigned int, unsigned int> envmapNormalAlphaMaskByTexture;
    std::map<unsigned int, std::array<float, 3>> envmapTintByTexture;
    std::map<unsigned int, std::pair<int, int>> sizeByTexture;
    std::map<unsigned int, std::string> surfacePropByTexture;
    bool disableTextures = false;

    void init(const std::string& gameDir);
    void initPakFile(const std::vector<char>& pakData);

    unsigned int getMaterial(const std::string& name);
    std::string getSurfaceProp(const std::string& name);
    std::string getSurfaceProp(unsigned int textureId) const;
    bool textureHasAlpha(unsigned int textureId) const;
    bool materialUsesTransparency(unsigned int textureId) const;
    bool materialUsesAlphaTest(unsigned int textureId) const;
    bool materialUsesEnvmap(unsigned int textureId) const;
    unsigned int getEnvmapMaskTexture(unsigned int textureId) const;
    bool materialUsesBaseAlphaEnvmapMask(unsigned int textureId) const;
    unsigned int getEnvmapNormalAlphaMaskTexture(unsigned int textureId) const;
    std::array<float, 3> getEnvmapTint(unsigned int textureId) const;
    bool getTextureSize(unsigned int textureId, int& width, int& height) const;
    bool materialUsesEnvmap(const std::string& name);

    bool getFileRawData(std::string fileName, std::vector<char>& out);

private:
    std::string parseVMT(const std::string& path);
    std::string parseVMTKey(const std::string& path, const std::string& key);
    unsigned int loadVTF(const std::string& path);
    unsigned int createFallbackTexture();
    static int getVTFMipSize(int w, int h, int fmt);
};
