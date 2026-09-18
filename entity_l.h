#pragma once
#include <string>
#include <vector>
#include <map>
#include "math.h"
#include "camera.h"
#include "texture_l.h"

class BSP_L;
class Weapon_L;
class PropPhysics;

class Entity {
public:
    Vec3 pos;
    bool shouldDestroy = false;

    virtual ~Entity() = default;
    virtual void update(float dt, Camera& player, BSP_L* map) = 0;
    virtual void render() = 0;
    virtual bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) { return false; }
};

typedef Entity* (*EntityFactory)(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL);

class EntityManager {
public:
    std::vector<Entity*> entities;

    void init(const std::string& gameDirectory, Texture_L* textureSystem);
    void registerEntity(const std::string& classname, EntityFactory factory);
    void parseMapEntities(const std::string& entityData);
    void spawn(const std::string& classname, const Vec3& position, bool inventorySpawn = true);

    void updateAll(float dt, Camera& player, BSP_L* map);
    void renderAll();
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
};