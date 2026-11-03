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
    bool dead = false;
    float damageFlash = 0.0f;
    float deathTime = 0.0f;
    int respawnHealth = 100;

    void takeDamage(int amount) {
        if (dead || amount <= 0) return;
        health -= amount;
        damageFlash = 1.0f;
        if (health <= 0) {
            health = 0;
            dead = true;
            deathTime = 0.0f;
        }
    }

    void setHealth(int value) {
        if (dead && value > 0) {
            dead = false;
            deathTime = 0.0f;
            damageFlash = 0.0f;
        }
        health = std::clamp(value, 0, 1000);
        if (health <= 0 && !dead) {
            dead = true;
            deathTime = 0.0f;
        }
    }

    void addHealth(int amount) {
        if (amount < 0) takeDamage(-amount);
        else setHealth(health + amount);
    }

    void kill() {
        if (!dead) takeDamage(std::max(1, health));
    }

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
