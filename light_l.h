#pragma once
#include <vector>
#include <string>
#include "math.h"

enum LightType {
    LIGHT_POINT,
    LIGHT_SPOT,
    LIGHT_ENVIRONMENT
};

struct RenderLight {
    LightType type;
    Vec3 pos;
    Vec3 dir;
    Vec3 color;
    float intensity;

    float spotInner;
    float spotOuter;

    float distToCamera;
};

class Light_L {
public:
    std::vector<RenderLight> lights;
    Vec3 globalAmbient;
    bool disableLighting = false;

    void parseLights(const std::string& entityData);
    void applyLights(const Vec3& cameraPos);
    void disableAll();

private:
    std::string extractEntityValue(const std::string& block, const std::string& key);
    Vec3 anglesToDir(float pitch, float yaw, float roll);
    float parseSafeFloat(const std::string& str, float def = 0.0f);
};