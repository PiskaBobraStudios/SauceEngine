#pragma once
#include "entity_l.h"
#include "mdl_l.h"
#include "phys.h"

class PropPhysics : public Entity {
public:
    MDL_L model;
    float scale = 1.0f;
    int solid = 6;

    RigidBody body;
    Vec4 renderColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    bool held = false;
    float pickupMaxMass = 35.0f;
    float pickupMaxSize = 128.0f;
    float physicsSoundCooldown = 0.0f;
    float lastSpeed = 0.0f;
    float lastVerticalSpeed = 0.0f;
    bool physicsSoundInitialized = false;
    bool physicsScrapePlaying = false;
    unsigned int physicsScrapeHandle = 0;
    float lightmapUpdateTimer = 0.0f;
    float lightmapBrightness = 1.0f;
    Vec3 lightmapColor{1,1,1};
    Vec3 lastLightSamplePos{0,0,0};
    bool hasLightSample = false;
    Vec3 lastVelocity{0,0,0};
    float playerPushCooldown = 0.0f;
    std::string physicsModelPath;
    std::string gameDirectory;

    PropPhysics(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL);
    ~PropPhysics() override;

    void update(float dt, Camera& player, BSP_L* map) override;
    void render() override;
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) override;

    void applyImpulse(const Vec3& force, const Vec3& contactOffset);
    bool canPlayerPickup(const Vec3& playerPos) const;
    bool pickup();
    void drop(const Vec3& throwVelocity = Vec3{0, 0, 0});
    void updateHeld(const Vec3& targetPosition, float dt);
    float pickupAimScore(const Vec3& rayOrigin, const Vec3& rayDir, float maxDistance, float& outAlong) const;

    static Entity* Create(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL) {
        return new PropPhysics(block, pos, gameDir, texL);
    }
};