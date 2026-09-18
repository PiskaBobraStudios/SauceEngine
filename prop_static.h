#pragma once

class BSP_L;
#include <string>
#include <vector>
#include "math.h"
#include "mdl_l.h"
#include "texture_l.h"

                                                                                               
struct StaticPropInstance {
    Vec3 origin;
    Vec3 angles;
    uint16_t modelIdx;
    float scale;
    uint8_t solid;
};

                                                                                                                               
class PropStatic_L {
public:
    std::vector<std::string> modelNames;
    std::vector<MDL_L*> models;
    std::vector<StaticPropInstance> instances;

    void parseFromBSP(const std::vector<char>& staticPropData, int version);
    void loadModels(Texture_L& texL);
    void render(const BSP_L* map);
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal);
    ~PropStatic_L();
};