#pragma once
#include "entity_l.h"
#include "mdl_l.h"
#include "sauce_compat_l.h"
#include <string>
#include <memory>
#include <unordered_map>

class BSP_L;

struct NPCVisualCache_L;

class NPC_L : public Entity {
public:
    std::string className;
    std::string gameDirectory;
    std::string modelPath;
    MDL_L* model = nullptr;
    std::shared_ptr<NPCVisualCache_L> visualCache;
    Vec3 angles{0,0,0};
    float scale = 1.0f;
    int health = 100;
    int maxHealth = 100;
    Vec3 collisionMin{-16,-72,-16};
    Vec3 collisionMax{16,0,16};

    NPC_L(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL);
    ~NPC_L() override;

    void update(float dt, Camera& player, BSP_L* map) override;
    void updateAI(float dt, Camera& player, BSP_L* map, EntityManager& entities);
    void render() override;
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) override;

    void takeDamage(float damage, const Vec3& hitDirection, const Vec3& hitPoint = Vec3{0,0,0});
    bool isAlive() const { return health > 0; }

    static Entity* Create(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL) {
        return new NPC_L(block, pos, gameDir, texL);
    }
    static void shutdownRuntime();

private:
    Entity* lookTarget = nullptr;
    bool lookAtPlayer = false;
    float lookReaction = 0.0f;
    float idleLookTimer = 1.8f;
    float idleLookYaw = 0.0f;
    bool lookAtSound = false;
    Vec3 soundLookPos{0,0,0};
    float soundFocusTimer = 0.0f;
    double lastHeardSoundTime = -1.0;
    std::unordered_map<Entity*, Vec3> lastObservedPositions;
    std::unordered_map<Entity*, float> activityScores;
};
