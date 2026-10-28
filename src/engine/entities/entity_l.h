#pragma once
#include <string>
#include <vector>
#include <map>
#include <deque>
#include "math.h"
#include "camera.h"
#include "texture_l.h"

class BSP_L;
class Weapon_L;
class PropPhysics;
class NPC_L;
class HammerEntity;

struct HammerOutput {
    std::string event;
    std::string target;
    std::string input;
    std::string parameter;
    float delay = 0.0f;
    int maxFires = -1;
    int fireCount = 0;
};

class Entity {
public:
    Vec3 pos;
    std::string classname;
    std::string targetname;
    unsigned long long ioId = 0;
    std::map<std::string, std::string> keyValues;
    std::vector<HammerOutput> hammerOutputs;
    bool enabled = true;
    bool shouldDestroy = false;
    bool destroyOutputsFired = false;
    int multiplayerNetId = 0;
    int multiplayerOwner = 0;
    bool multiplayerRemote = false;

    virtual ~Entity() = default;
    virtual void update(float dt, Camera& player, BSP_L* map) = 0;
    virtual void render() = 0;
    virtual bool traceRay(const Vec3&, const Vec3&, float&, Vec3&) { return false; }
};

typedef Entity* (*EntityFactory)(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL);

class EntityManager {
public:
    std::vector<Entity*> entities;

    void init(const std::string& gameDirectory, Texture_L* textureSystem);
    void registerEntity(const std::string& classname, EntityFactory factory);
    void parseMapEntities(const std::string& entityData, const BSP_L* map = nullptr);
    bool activateUse(const Vec3& origin, const Vec3& direction, float maxDistance, Camera& player, BSP_L* map);
    Entity* spawn(const std::string& classname, const Vec3& position, bool inventorySpawn = true);
    Entity* spawnNetwork(const std::string& classname, const Vec3& position, const std::string& modelPath,
                         const Vec3& angles, float scale, int netId, int owner);
    Entity* findMultiplayerEntity(int netId) const;

    void updateAll(float dt, Camera& player, BSP_L* map);
    void renderAll();


    void fireEntityOutput(Entity* source, const std::string& output, Camera& player, BSP_L* map);
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal);
    void clear();
    void selectWeaponSlot(int slot, Camera& player);
    Weapon_L* getActiveWeapon(const Camera& player) const;
    PropPhysics* findPickupProp(const Vec3& orig, const Vec3& dir, float maxDistance, const Vec3& playerPos);

    static std::string extractEntityValue(const std::string& block, const std::string& key);

private:
    std::string gameDir;
    Texture_L* texL;
    std::map<std::string, EntityFactory> factories;
    struct PendingHammerInput {
        float remaining = 0.0f;
        std::string target;
        std::string input;
        std::string parameter;
        unsigned long long directTargetId = 0;
    };
    std::vector<PendingHammerInput> pendingInputs;
    bool mapSpawnQueued = false;
    unsigned long long nextIoId = 1;
    void fireOutput(Entity* source, const std::string& output, Camera& player, BSP_L* map);
    void dispatchInput(const PendingHammerInput& command, Camera& player, BSP_L* map);
    void processPendingInputs(float dt, Camera& player, BSP_L* map);
    void processHammerEntity(HammerEntity& ent, float dt, Camera& player, BSP_L* map);
    void parseBlockMetadata(Entity* ent, const std::string& block);
};
