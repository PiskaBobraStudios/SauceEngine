#pragma once
#include <string>
#include <map>
#include <vector>
#include <utility>

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
    std::map<unsigned int, std::pair<int, int>> sizeByTexture;
    bool disableTextures = false;

    void init(const std::string& gameDir);
    void initPakFile(const std::vector<char>& pakData);

    unsigned int getMaterial(const std::string& name);
    bool textureHasAlpha(unsigned int textureId) const;
    bool getTextureSize(unsigned int textureId, int& width, int& height) const;

    bool getFileRawData(std::string fileName, std::vector<char>& out);

private:
    std::string parseVMT(const std::string& path);
    unsigned int loadVTF(const std::string& path);
    unsigned int createFallbackTexture();
    static int getVTFMipSize(int w, int h, int fmt);
};
