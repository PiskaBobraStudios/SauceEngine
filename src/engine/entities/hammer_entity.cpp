#include "hammer_entity.h"
#include "bsp_l.h"
#include "sound_l.h"
#include "prop_lighting.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cctype>

namespace {
std::string lowerHammer(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool parseVec3Hammer(const std::string& text, Vec3& value) {
    std::istringstream ss(text);
    return static_cast<bool>(ss >> value.x >> value.y >> value.z);
}
}

HammerEntity::HammerEntity(const std::string& block, const Vec3& p, const BSP_L* map, const std::string& gameDir, Texture_L* texL) {
    (void)gameDir;
    pos = p;
    classname = lowerHammer(EntityManager::extractEntityValue(block, "classname"));
    targetname = EntityManager::extractEntityValue(block, "targetname");
    std::string modelPath = EntityManager::extractEntityValue(block, "model");
    std::replace(modelPath.begin(), modelPath.end(), '\\', '/');
    int bodyValue = 0, skinValue = 0;
    try { bodyValue = std::max(0, std::stoi(EntityManager::extractEntityValue(block, "body"))); } catch (...) {}
    try { skinValue = std::max(0, std::stoi(EntityManager::extractEntityValue(block, "skin"))); } catch (...) {}
    if (!modelPath.empty() && modelPath[0] != '*' && classname != "prop_static" && texL) model.load(modelPath, *texL, false, bodyValue, skinValue);
    std::istringstream angleStream(EntityManager::extractEntityValue(block, "angles"));
    angleStream >> angles.x >> angles.y >> angles.z;
    std::string scaleValue = EntityManager::extractEntityValue(block, "modelscale");
    if (scaleValue.empty()) scaleValue = EntityManager::extractEntityValue(block, "uniformscale");
    if (!scaleValue.empty()) { try { scale = std::stof(scaleValue); } catch (...) { scale = 1.0f; } }
    std::istringstream colorStream(EntityManager::extractEntityValue(block, "rendercolor"));
    float r=255,g=255,b=255;
    if (colorStream >> r >> g >> b) { renderColor.x=r/255.0f; renderColor.y=g/255.0f; renderColor.z=b/255.0f; }
    const std::string alphaValue = EntityManager::extractEntityValue(block, "renderamt");
    if (!alphaValue.empty()) { try { renderColor.w=std::clamp(std::stof(alphaValue)/255.0f,0.0f,1.0f); } catch (...) {} }
    refreshBounds(block, map);
    health = 100.0f;
    const std::string healthStr = EntityManager::extractEntityValue(block, "health");
    if (!healthStr.empty()) { try { health = std::stof(healthStr); } catch (...) {} }
    const std::string value = EntityManager::extractEntityValue(block, "startvalue");
    if (!value.empty()) { try { counterValue = std::stof(value); } catch (...) {} }
    thinkTimer = 0.0f;
}

void HammerEntity::refreshBounds(const std::string& block, const BSP_L* map) {
    std::string modelValue = EntityManager::extractEntityValue(block, "model");
    if (map && modelValue.size() > 1 && modelValue[0] == '*') {
        int idx = -1;
        try { idx = std::stoi(modelValue.substr(1)); } catch (...) { idx = -1; }
        if (idx >= 0 && idx < static_cast<int>(map->models.size())) {
            const BSPModel& m = map->models[static_cast<size_t>(idx)];
            boundsMin = {m.mins.x, m.mins.z, -m.maxs.y};
            boundsMax = {m.maxs.x, m.maxs.z, -m.mins.y};
            hasBounds = boundsMin.x <= boundsMax.x && boundsMin.y <= boundsMax.y && boundsMin.z <= boundsMax.z;
        }
    }
    if (!hasBounds) {
        Vec3 mn, mx;
        if (parseVec3Hammer(EntityManager::extractEntityValue(block, "mins"), mn) &&
            parseVec3Hammer(EntityManager::extractEntityValue(block, "maxs"), mx)) {
            boundsMin = pos + Vec3{mn.x, mn.z, -mx.y};
            boundsMax = pos + Vec3{mx.x, mx.z, -mn.y};
            hasBounds = true;
        }
    }
    if (!hasBounds) {
        const std::string cls = classname;
        if (cls.rfind("trigger_", 0) == 0 || cls.rfind("func_", 0) == 0) {
            boundsMin = pos - Vec3{24.0f, 32.0f, 24.0f};
            boundsMax = pos + Vec3{24.0f, 32.0f, 24.0f};
            hasBounds = true;
        } else {


            boundsMin = pos - Vec3{8.0f, 8.0f, 8.0f};
            boundsMax = pos + Vec3{8.0f, 8.0f, 8.0f};
            hasBounds = true;
        }
    }
}

HammerEntity::~HammerEntity() {
    if (loopSoundHandle) Sound_L::Get().stopLoop(loopSoundHandle);
}

void HammerEntity::update(float dt, Camera& player, BSP_L* map) {
    (void)player; (void)map;
    if (model.loaded && model.animated) {
        model.setAnimationEventContext(pos, true);
        model.updateAnimation(dt);
    }
}
void HammerEntity::render() {
    if (!model.loaded || renderColor.w <= 0.001f) return;
    glPushMatrix();
    glTranslatef(pos.x, pos.y, pos.z);
    if (std::abs(scale - 1.0f) > 0.001f) glScalef(scale, scale, scale);
    glRotatef(angles.y, 0.0f, 1.0f, 0.0f);
    glRotatef(-angles.x, 1.0f, 0.0f, 0.0f);
    glRotatef(angles.z, 0.0f, 0.0f, 1.0f);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    glColor4f(renderColor.x, renderColor.y, renderColor.z, renderColor.w);
    model.render();
    glPopMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

bool HammerEntity::isTrigger() const {
    return classname.rfind("trigger_", 0) == 0;
}
bool HammerEntity::isUseTarget() const {
    return classname == "func_button" || classname == "func_rot_button" ||
           classname == "momentary_rot_button" || classname == "func_door" ||
           classname == "func_door_rotating" || classname == "func_door_prismatic" ||
           classname == "func_rotating" || classname == "func_breakable" ||
           classname == "func_physbox" || classname == "prop_button" || classname == "prop_door_rotating" ||
           std::any_of(hammerOutputs.begin(), hammerOutputs.end(), [](const HammerOutput& o) { return o.event == "onuse"; });
}
bool HammerEntity::isLogicEntity() const {
    return classname.rfind("logic_", 0) == 0 || classname == "point_servercommand" ||
           classname == "point_template" || classname == "ambient_generic";
}

bool HammerEntity::containsPlayer(const Camera& player) const {
    if (!hasBounds || !enabled) return false;

    const float pMinX = player.pos.x - player.radius, pMaxX = player.pos.x + player.radius;
    const float pMinY = player.pos.y - player.height, pMaxY = player.pos.y;
    const float pMinZ = player.pos.z - player.radius, pMaxZ = player.pos.z + player.radius;
    return pMinX <= boundsMax.x && pMaxX >= boundsMin.x &&
           pMinY <= boundsMax.y && pMaxY >= boundsMin.y &&
           pMinZ <= boundsMax.z && pMaxZ >= boundsMin.z;
}

bool HammerEntity::hitBounds(const Vec3& origin, const Vec3& direction, float& outDist, Vec3& outNormal) const {
    if (!hasBounds) return false;
    float tMin = 0.0f, tMax = 1.0e9f;
    Vec3 normal{0,0,0};
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {direction.x, direction.y, direction.z};
    const float mn[3] = {boundsMin.x, boundsMin.y, boundsMin.z};
    const float mx[3] = {boundsMax.x, boundsMax.y, boundsMax.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(d[axis]) < 1e-7f) {
            if (o[axis] < mn[axis] || o[axis] > mx[axis]) return false;
            continue;
        }
        float a = (mn[axis] - o[axis]) / d[axis];
        float b = (mx[axis] - o[axis]) / d[axis];
        float sign = -1.0f;
        if (a > b) { std::swap(a, b); sign = 1.0f; }
        if (a > tMin) {
            tMin = a; normal = {0,0,0};
            if (axis == 0) normal.x = sign;
            if (axis == 1) normal.y = sign;
            if (axis == 2) normal.z = sign;
        }
        tMax = std::min(tMax, b);
        if (tMin > tMax) return false;
    }
    if (tMax < 0.0f) return false;
    outDist = tMin;
    outNormal = normal;
    return true;
}

bool HammerEntity::traceRay(const Vec3& origin, const Vec3& direction, float& outDist, Vec3& outNormal) {
    if (!enabled || !isUseTarget()) return false;
    if (model.loaded) {
        const Vec3 localOrigin = PropWorldToModelPoint(origin, pos, angles, scale);
        const Vec3 localDirection = PropWorldToModelDirection(direction, angles);
        float distance = 0.0f; Vec3 normal;
        if (!model.traceRayLocal(localOrigin, localDirection, distance, normal)) return false;
        outDist = distance * std::max(0.0001f, std::abs(scale));
        outNormal = PropModelNormalToWorld(normal, angles);
        return outDist >= 0.0f;
    }
    return hitBounds(origin, direction, outDist, outNormal);
}
