#pragma once
#include <string>
#include <map>
#include "math.h"
#include "texture_l.h"

class StepSound_L {
public:
    void init(const std::string& gameDir, Texture_L* texSystem);
    void update(float dt, bool onGround, const Vec3& velocity, const std::string& currentTexture);
private:
    std::string gameDirectory;
    Texture_L* texL = nullptr;
    float walkTimer = 0.0f;
    std::map<std::string, std::string> textureToSurfaceMap;
    std::string getSurfaceProp(const std::string& texName);
    std::string soundFamily(const std::string& surface) const;
};
