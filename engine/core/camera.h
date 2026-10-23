#pragma once
#include "math.h"
#include <string>
#include <vector>
#include <algorithm>
#include <map>

class Camera {
public:
    Vec3 pos;
    Vec3 velocity;
    float yaw, pitch;
    bool onGround;

           
    float radius;
    float height;

                      
    int health;
    float sprintEnergy;                            
    bool hasSuit;                 
    std::vector<std::string> weaponInventory;
    int activeWeaponSlot = 0;
    std::map<int, int> weaponCycleCursor;

    bool hasWeapon(const std::string& className) const {
        return std::find(weaponInventory.begin(), weaponInventory.end(), className) != weaponInventory.end();
    }

    void giveWeapon(const std::string& className, int slot) {
        if (!hasWeapon(className)) weaponInventory.push_back(className);
        activeWeaponSlot = slot;
    }

    Camera() :
        pos{ 0, 64, 0 }, velocity{ 0, 0, 0 }, yaw(0), pitch(0),
        onGround(false), radius(16.0f), height(64.0f),
        health(100), sprintEnergy(100.0f), hasSuit(false) {
    }
};