#pragma once
#include <cstdint>
#include <string>
#include "camera.h"

struct DemoInputState {
    uint32_t buttons = 0;
    uint32_t buttonsPressed = 0;
    uint8_t impulse = 0;
    int weaponSelect = 0;
    bool impulsePressed = false;
    bool valid = false;
};

class DemoPlayer {
public:
    static bool prepare(const std::string& name, const std::string& gameDirectory, std::string& error);
    static bool prepared();
    static bool active();
    static const std::string& mapName();
    static const std::string& fileName();
    static void start();
    static bool update(float dt, Camera& player, DemoInputState& input);
    static void stop();
};
