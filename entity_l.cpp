#include "entity_l.h"
#include "prop_dynamic.h"
#include "prop_physics.h"
#include "weapon_l.h"
#include <iostream>
#include <sstream>
#include <cmath>

void EntityManager::init(const std::string& gameDirectory, Texture_L* textureSystem) {
    gameDir = gameDirectory;
    texL = textureSystem;
    Weapon_L::discoverAndRegister(*this, gameDir, texL);
}

void EntityManager::registerEntity(const std::string& classname, EntityFactory factory) {
    factories[classname] = factory;
}

std::string EntityManager::extractEntityValue(const std::string& block, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = block.find(search);
    if (pos == std::string::npos) return "";

    size_t valStart = block.find('\"', pos + search.length());
    if (valStart == std::string::npos) return "";
    valStart++;

    size_t valEnd = block.find('\"', valStart);
    if (valEnd == std::string::npos) return "";

    return block.substr(valStart, valEnd - valStart);
}

void EntityManager::parseMapEntities(const std::string& entityData) {
    size_t pos = 0;

    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        size_t endPos = entityData.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string block = entityData.substr(pos, endPos - pos);
        std::string classname = extractEntityValue(block, "classname");

        if (classname == "worldspawn" || classname == "light" || classname == "light_spot" ||
            classname == "light_environment" || classname == "info_player_start" ||
            classname == "trigger_changelevel" || classname == "info_landmark" || classname.empty()) {
            pos = endPos + 1;
            continue;
        }

        std::string originStr = extractEntityValue(block, "origin");
        Vec3 entPos{ 0, 0, 0 };
        if (!originStr.empty()) {
            std::istringstream oss(originStr);
            float x, y, z;
            if (oss >> x >> y >> z) entPos = { x, z, -y };
        }

        if (factories.count(classname)) {
            Entity* newEnt = factories[classname](block, entPos, gameDir, texL);
            entities.push_back(newEnt);
        } else {
            std::cout << "[Entity] No factory for classname: " << classname << std::endl;
        }
        pos = endPos + 1;
    }
}

void EntityManager::spawn(const std::string& classname, const Vec3& position, bool inventorySpawn) {
    if (factories.count(classname)) {
        std::string block = "\"classname\" \"" + classname +
            "\"\n\"model\" \"models/items/hevsuit.mdl\"\n\"solid\" \"6\"";
        if (inventorySpawn) block += "\n\"__inventory\" \"1\"";
        Entity* newEnt = factories[classname](block, position, gameDir, texL);
        entities.push_back(newEnt);
    } else {
        std::cout << "[Entity] No factory for classname: " << classname << std::endl;
    }
}

void EntityManager::updateAll(float dt, Camera& player, BSP_L* map) {
    for (auto it = entities.begin(); it != entities.end(); ) {
        Entity* ent = *it;
        ent->update(dt, player, map);
        if (ent->shouldDestroy) {
            delete ent;
            it = entities.erase(it);
        }
        else {
            ++it;
        }
    }
}

void EntityManager::renderAll() {
    for (Entity* ent : entities) ent->render();
}

bool EntityManager::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    bool hitAnything = false;
    float closestDist = 999999.0f;
    Vec3 closestNormal;

    float tempDist;
    Vec3 tempNormal;

    for (Entity* ent : entities) {
        if (ent->traceRay(orig, dir, tempDist, tempNormal)) {
            if (tempDist < closestDist) {
                closestDist = tempDist;
                closestNormal = tempNormal;
                hitAnything = true;
            }
        }
    }

    if (hitAnything) {
        outDist = closestDist;
        outNormal = closestNormal;
    }
    return hitAnything;
}

Weapon_L* EntityManager::getActiveWeapon(const Camera& player) const {
    for (Entity* ent : entities) {
        Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
        if (weapon && weapon->isActive(player)) return weapon;
    }
    return nullptr;
}

void EntityManager::selectWeaponSlot(int slot, Camera& player) {
    if (slot <= 0) return;
    std::vector<Weapon_L*> choices;
    for (const std::string& owned : player.weaponInventory) {
        for (Entity* ent : entities) {
            Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
            if (!weapon || weapon->className() != owned || weapon->slot() != slot) continue;
            choices.push_back(weapon);
            break;
        }
    }
    if (choices.empty()) return;
    std::sort(choices.begin(), choices.end(), [](Weapon_L* a, Weapon_L* b) {
        if (a->definition.bucketPosition != b->definition.bucketPosition)
            return a->definition.bucketPosition < b->definition.bucketPosition;
        return a->className() < b->className();
    });
    int nextIndex = 0;
    if (player.activeWeaponSlot == slot) {
        int current = -1;
        for (size_t i=0; i<choices.size(); ++i) {
            if (choices[i]->isActive(player)) { current=(int)i; break; }
        }
        if (current >= 0) nextIndex=(current+1)%(int)choices.size();
    }
    choices[(size_t)nextIndex]->equip(player);
    player.weaponCycleCursor[slot]=nextIndex;
}

PropPhysics* EntityManager::findPickupProp(const Vec3& orig, const Vec3& dir, float maxDistance, const Vec3& playerPos) {
    PropPhysics* best = nullptr;
    float bestScore = 999999.0f;
    float bestAlong = 999999.0f;

    for (Entity* ent : entities) {
        PropPhysics* prop = dynamic_cast<PropPhysics*>(ent);
        if (!prop) continue;
        if (!prop->canPlayerPickup(playerPos)) continue;

        float along = 0.0f;
        float score = prop->pickupAimScore(orig, dir, maxDistance, along);
        if (score < bestScore || (std::fabs(score - bestScore) < 0.001f && along < bestAlong)) {
            best = prop;
            bestScore = score;
            bestAlong = along;
        }
    }

    return best;
}

void EntityManager::clear() {
    for (Entity* ent : entities) delete ent;
    entities.clear();
}