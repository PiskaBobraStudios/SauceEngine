#pragma once
#include "entity_l.h"
#include "mdl_l.h"
#include <string>

class BSP_L;
class HammerEntity : public Entity {
public:
    MDL_L model;
    Vec3 angles{0,0,0};
    float scale = 1.0f;
    Vec4 renderColor{1,1,1,1};
    Vec3 boundsMin{0,0,0};
    Vec3 boundsMax{0,0,0};
    bool hasBounds = false;
    bool wasTouching = false;
    bool triggeredOnce = false;
    bool pressed = false;
    bool opened = false;
    bool locked = false;
    float thinkTimer = 0.0f;
    float damageTimer = 0.0f;
    float health = 100.0f;
    float counterValue = 0.0f;
    float nextThink = 0.0f;
    unsigned int loopSoundHandle = 0;
    bool soundPlaying = false;

    HammerEntity(const std::string& block, const Vec3& p, const BSP_L* map, const std::string& gameDir, Texture_L* texL);
    ~HammerEntity() override;
    void update(float dt, Camera& player, BSP_L* map) override;
    void render() override;
    bool traceRay(const Vec3& origin, const Vec3& direction, float& outDist, Vec3& outNormal) override;
    bool containsPlayer(const Camera& player) const;
    bool isTrigger() const;
    bool isUseTarget() const;
    bool isLogicEntity() const;
    void refreshBounds(const std::string& block, const BSP_L* map);
private:
    bool hitBounds(const Vec3& origin, const Vec3& direction, float& outDist, Vec3& outNormal) const;
};
