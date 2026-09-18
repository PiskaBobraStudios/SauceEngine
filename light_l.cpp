#include "light_l.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

std::string Light_L::extractEntityValue(const std::string& block, const std::string& key) {
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

Vec3 Light_L::anglesToDir(float pitch, float yaw, float roll) {
    float yawRad = yaw * 3.14159f / 180.0f;
    float pitchRad = pitch * 3.14159f / 180.0f;

    Vec3 srcDir;
    srcDir.x = std::cos(yawRad) * std::cos(pitchRad);
    srcDir.y = std::sin(yawRad) * std::cos(pitchRad);
    srcDir.z = -std::sin(pitchRad);

    return { srcDir.x, srcDir.z, -srcDir.y };
}

void Light_L::parseLights(const std::string& entityData) {
    lights.clear();
    globalAmbient = { 0.06f, 0.06f, 0.06f };

    int cntPoint = 0, cntSpot = 0, cntEnv = 0;

    size_t pos = 0;
    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        size_t endPos = entityData.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string block = entityData.substr(pos, endPos - pos);
        std::string classname = extractEntityValue(block, "classname");

        if (classname == "light" || classname == "light_spot" || classname == "light_environment") {
            RenderLight rl;
            rl.intensity = 200.0f;
            rl.spotOuter = 45.0f;
            rl.dir = { 0, -1, 0 };

            if (classname == "light") rl.type = LIGHT_POINT;
            else if (classname == "light_spot") rl.type = LIGHT_SPOT;
            else if (classname == "light_environment") rl.type = LIGHT_ENVIRONMENT;

            std::string originStr = extractEntityValue(block, "origin");
            if (!originStr.empty()) {
                std::istringstream oss(originStr);
                float x, y, z;
                if (oss >> x >> y >> z) rl.pos = { x, z, -y };
            }

            std::string lightStr = extractEntityValue(block, "_light");
            if (!lightStr.empty()) {
                std::istringstream lss(lightStr);
                float r, g, b, i = 200.0f;
                if (lss >> r >> g >> b) {
                    if (lss >> i) rl.intensity = i;
                    rl.color = { r / 255.0f, g / 255.0f, b / 255.0f };
                }
            }

            if (rl.type == LIGHT_ENVIRONMENT) {
                std::string ambStr = extractEntityValue(block, "_ambient");
                if (!ambStr.empty()) {
                    std::istringstream ass(ambStr);
                    float r, g, b, i = 200.0f;
                    if (ass >> r >> g >> b) {
                        ass >> i;
                        float brightness = (i / 255.0f);
                        globalAmbient = { (r / 255.0f) * brightness, (g / 255.0f) * brightness, (b / 255.0f) * brightness };
                    }
                }
            }

            std::string anglesStr = extractEntityValue(block, "angles");
            std::string pitchStr = extractEntityValue(block, "pitch");
            float pitch = 0, yaw = 0, roll = 0;

            if (!anglesStr.empty()) {
                std::istringstream ass(anglesStr);
                ass >> pitch >> yaw >> roll;
            }
            if (!pitchStr.empty()) {
                pitch = parseSafeFloat(pitchStr, pitch);
            }
            rl.dir = anglesToDir(pitch, yaw, roll);

            if (rl.type == LIGHT_SPOT) {
                std::string coneStr = extractEntityValue(block, "_cone");
                rl.spotOuter = parseSafeFloat(coneStr, 45.0f);
            }

            lights.push_back(rl);
            if (rl.type == LIGHT_POINT) cntPoint++;
            if (rl.type == LIGHT_SPOT) cntSpot++;
            if (rl.type == LIGHT_ENVIRONMENT) cntEnv++;
        }
        pos = endPos + 1;
    }

    globalAmbient.x = std::max(globalAmbient.x, 0.06f);
    globalAmbient.y = std::max(globalAmbient.y, 0.06f);
    globalAmbient.z = std::max(globalAmbient.z, 0.06f);
}

void Light_L::applyLights(const Vec3& cameraPos) {
    if (disableLighting || lights.empty()) {
        disableAll();
        return;
    }

    glEnable(GL_LIGHTING);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat ambientGL[] = { globalAmbient.x, globalAmbient.y, globalAmbient.z, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambientGL);

    for (auto& l : lights) {
        l.distToCamera = (l.pos - cameraPos).length();
    }

    RenderLight* envLight = nullptr;
    std::vector<RenderLight*> localLights;

    for (auto& l : lights) {
        if (l.type == LIGHT_ENVIRONMENT) {
            if (!envLight || l.intensity > envLight->intensity) envLight = &l;
        }
        else {
            localLights.push_back(&l);
        }
    }

    std::sort(localLights.begin(), localLights.end(), [](RenderLight* a, RenderLight* b) {
        return a->distToCamera < b->distToCamera;
        });

    int hardwareSlot = 0;

    if (envLight && hardwareSlot < 8) {
        int lightID = GL_LIGHT0 + hardwareSlot;

        GLfloat posDir[] = { envLight->dir.x, envLight->dir.y, envLight->dir.z, 0.0f };
        float b = (envLight->intensity / 255.0f) * 2.10f;
        GLfloat diffuse[] = { envLight->color.x * b, envLight->color.y * b, envLight->color.z * b, 1.0f };
        GLfloat none[] = { 0.0f, 0.0f, 0.0f, 1.0f };

        glLightfv(lightID, GL_POSITION, posDir);
        glLightfv(lightID, GL_DIFFUSE, diffuse);
        glLightfv(lightID, GL_SPECULAR, diffuse);
        glLightfv(lightID, GL_AMBIENT, none);

        glLightf(lightID, GL_CONSTANT_ATTENUATION, 1.0f);
        glLightf(lightID, GL_LINEAR_ATTENUATION, 0.0f);
        glLightf(lightID, GL_QUADRATIC_ATTENUATION, 0.0f);
        glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);

        glEnable(lightID);
        hardwareSlot++;
    }

    for (size_t i = 0; i < localLights.size() && hardwareSlot < 8; ++i) {
        int lightID = GL_LIGHT0 + hardwareSlot;
        RenderLight* l = localLights[i];

        GLfloat pos[] = { l->pos.x, l->pos.y, l->pos.z, 1.0f };
        float b = (l->intensity / 255.0f) * 2.00f;
        GLfloat diffuse[] = { l->color.x * b, l->color.y * b, l->color.z * b, 1.0f };
        GLfloat none[] = { 0.0f, 0.0f, 0.0f, 1.0f };

        glLightfv(lightID, GL_POSITION, pos);
        glLightfv(lightID, GL_DIFFUSE, diffuse);
        glLightfv(lightID, GL_AMBIENT, none);

        glLightf(lightID, GL_CONSTANT_ATTENUATION, 0.0f);
        glLightf(lightID, GL_LINEAR_ATTENUATION, 0.001f);
        glLightf(lightID, GL_QUADRATIC_ATTENUATION, 0.00005f);

        if (l->type == LIGHT_SPOT) {
            GLfloat spotDir[] = { l->dir.x, l->dir.y, l->dir.z };
            glLightfv(lightID, GL_SPOT_DIRECTION, spotDir);
            glLightf(lightID, GL_SPOT_CUTOFF, l->spotOuter / 2.0f);
            glLightf(lightID, GL_SPOT_EXPONENT, 15.0f);
        }
        else {
            glLightf(lightID, GL_SPOT_CUTOFF, 180.0f);
        }

        glEnable(lightID);
        hardwareSlot++;
    }

    for (int i = hardwareSlot; i < 8; ++i) {
        glDisable(GL_LIGHT0 + i);
    }
}

void Light_L::disableAll() {
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    for (int i = 0; i < 8; ++i) {
        glDisable(GL_LIGHT0 + i);
    }
}

float Light_L::parseSafeFloat(const std::string& str, float def) {
    if (str.empty()) return def;
    std::istringstream ss(str);
    float val;
    if (ss >> val) return val;
    return def;
}