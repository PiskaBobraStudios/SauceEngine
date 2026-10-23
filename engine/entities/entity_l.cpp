#include "entity_l.h"
#include "prop_dynamic.h"
#include "prop_physics.h"
#include "weapon_l.h"
#include "npc_l.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cctype>

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

static bool IsNPCClassName(const std::string& value) {
    std::string classname = value;
    std::transform(classname.begin(), classname.end(), classname.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static const char* prefixes[] = {
        "npc_", "monster_", "ally_", "enemy_", "creature_", "character_", "animal_", "ai_"
    };
    for (const char* prefix : prefixes) {
        if (classname.rfind(prefix, 0) == 0) return true;
    }
    return false;
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

        auto fit = factories.find(classname);
        if (fit != factories.end()) {
            Entity* newEnt = fit->second(block, entPos, gameDir, texL);
            entities.push_back(newEnt);
        } else if (IsNPCClassName(classname)) {
            entities.push_back(NPC_L::Create(block, entPos, gameDir, texL));
        } else {
            std::cout << "[Entity] No factory for classname: " << classname << std::endl;
        }
        pos = endPos + 1;
    }
}

Entity* EntityManager::spawn(const std::string& classname, const Vec3& position, bool inventorySpawn) {
    auto fit = factories.find(classname);
    if (fit != factories.end()) {
        std::string block = "\"classname\" \"" + classname + "\"";
        if (!IsNPCClassName(classname)) block += "\n\"model\" \"models/items/hevsuit.mdl\"\n\"solid\" \"6\"";
        if (inventorySpawn) block += "\n\"__inventory\" \"1\"";
        Entity* newEnt = fit->second(block, position, gameDir, texL);
        entities.push_back(newEnt);
        return newEnt;
    } else if (IsNPCClassName(classname)) {
        std::string block = "\"classname\" \"" + classname + "\"";
        Entity* newEnt = NPC_L::Create(block, position, gameDir, texL);
        entities.push_back(newEnt);
        return newEnt;
    } else {
        std::cout << "[Entity] No factory for classname: " << classname << std::endl;
    }
    return nullptr;
}

Entity* EntityManager::spawnNetwork(const std::string& classname, const Vec3& position, const std::string& modelPath,
                                      const Vec3& angles, float scale, int netId, int owner) {
    auto fit = factories.find(classname);
    if (fit == factories.end()) return nullptr;
    std::string block = "\"classname\" \"" + classname + "\"";
    if (!modelPath.empty()) block += "\n\"model\" \"" + modelPath + "\"";
    block += "\n\"angles\" \"" + std::to_string(angles.x) + " " + std::to_string(angles.y) + " " + std::to_string(angles.z) + "\"";
    block += "\n\"modelscale\" \"" + std::to_string(scale > 0.001f ? scale : 1.0f) + "\"";
    block += "\n\"solid\" \"6\"";
    Entity* ent = fit->second(block, position, gameDir, texL);
    if (!ent) return nullptr;
    ent->multiplayerNetId = netId;
    ent->multiplayerOwner = owner;
    ent->multiplayerRemote = owner != 0 && owner != 1 ? true : owner != (netId >= 0x40000000 ? 1 : 0);
    entities.push_back(ent);
    return ent;
}

Entity* EntityManager::findMultiplayerEntity(int netId) const {
    for (Entity* ent : entities) {
        if (ent && ent->multiplayerNetId == netId) return ent;
    }
    return nullptr;
}

void EntityManager::updateAll(float dt, Camera& player, BSP_L* map) {
    for (auto it = entities.begin(); it != entities.end(); ) {
        Entity* ent = *it;
        ent->update(dt, player, map);
        if (auto* npc = dynamic_cast<NPC_L*>(ent)) npc->updateAI(dt, player, map, *this);
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
        if (prop->multiplayerRemote) continue;
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