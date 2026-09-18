#pragma once
#include "entity_l.h"
#include "mdl_l.h"

class PropDynamic : public Entity {
public:
    MDL_L model;
    Vec3 angles;
    float scale = 1.0f;
    int solid = 0;
    Vec4 renderColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    float lightmapUpdateTimer = 0.0f;
    float lightmapBrightness = 1.0f;
    Vec3 lightmapColor{1,1,1};
    Vec3 lastLightSamplePos{0,0,0};
    bool hasLightSample = false;

                                                                                                                                           
    Vec3 localBoxMin;
    Vec3 localBoxMax;

    PropDynamic(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL);

    void update(float dt, Camera& player, BSP_L* map) override;                       
    void render() override;
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) override;

    static Entity* Create(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL) {
        return new PropDynamic(block, pos, gameDir, texL);
    }
};