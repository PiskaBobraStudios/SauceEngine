#include "prop_dynamic.h"
#include "bsp_l.h"
#include "prop_lighting.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

PropDynamic::PropDynamic(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL) {
    pos = p;
    angles = { 0.0f, 0.0f, 0.0f };

    std::string modelPath = EntityManager::extractEntityValue(block, "model");
    if (!modelPath.empty()) {
        std::replace(modelPath.begin(), modelPath.end(), '\\', '/');
        int bodyValue = 0, skinValue = 0;
        try { bodyValue = std::max(0, std::stoi(EntityManager::extractEntityValue(block, "body"))); } catch (...) {}
        try { skinValue = std::max(0, std::stoi(EntityManager::extractEntityValue(block, "skin"))); } catch (...) {}
        if (texL) model.load(modelPath, *texL, false, bodyValue, skinValue);
        if (model.loaded && model.animated) model.playAnimationHint("idle");
    }

    std::string anglesStr = EntityManager::extractEntityValue(block, "angles");
    if (!anglesStr.empty()) {
        std::istringstream oss(anglesStr);
        oss >> angles.x >> angles.y >> angles.z;
    }

    std::string scaleStr = EntityManager::extractEntityValue(block, "modelscale");
    if (scaleStr.empty()) scaleStr = EntityManager::extractEntityValue(block, "uniformscale");
    if (!scaleStr.empty()) {
        std::istringstream ss(scaleStr);
        if (!(ss >> scale)) scale = 1.0f;
    }

    std::string colorStr = EntityManager::extractEntityValue(block, "rendercolor");
    if (!colorStr.empty()) {
        std::istringstream css(colorStr);
        float r = 255.0f, g = 255.0f, b = 255.0f;
        if (css >> r >> g >> b) {
            renderColor.x = r / 255.0f;
            renderColor.y = g / 255.0f;
            renderColor.z = b / 255.0f;
        }
    }

    std::string solidStr = EntityManager::extractEntityValue(block, "solid");
    if (!solidStr.empty()) {
        std::istringstream sss(solidStr);
        if (!(sss >> solid)) solid = 0;
    }


}

void PropDynamic::update(float dt, Camera& player, BSP_L* map) {
    (void)player;
    if (model.loaded && model.animated) {
        model.setAnimationEventContext(pos, true);
        model.updateAnimation(dt);
    }
    lightmapUpdateTimer -= dt;
    if(map && lightmapUpdateTimer<=0.0f &&
       (!hasLightSample || (pos-lastLightSamplePos).length()>12.0f)) {
        float b=1.0f;
        SamplePropLighting(map,model,pos,angles,scale,lightmapColor,lightmapBrightness);
        lastLightSamplePos=pos;
        hasLightSample=true;
        lightmapUpdateTimer=0.12f;
    }
}

void PropDynamic::render() {
    if (!model.loaded) return;
    glPushMatrix();

    glTranslatef(pos.x, pos.y, pos.z);
    if (std::abs(scale - 1.0f) > 0.001f) glScalef(scale, scale, scale);

    glRotatef(angles.y, 0.0f, 1.0f, 0.0f);
    glRotatef(-angles.x, 1.0f, 0.0f, 0.0f);
    glRotatef(angles.z, 0.0f, 0.0f, 1.0f);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);

    glColor4f(gPropFlashlightPass?renderColor.x:renderColor.x*lightmapColor.x,gPropFlashlightPass?renderColor.y:renderColor.y*lightmapColor.y,gPropFlashlightPass?renderColor.z:renderColor.z*lightmapColor.z,renderColor.w);
    model.render();

    glPopMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void PropDynamic::setNetworkTransform(const Vec3& p, const Vec3& a) {
    pos = p;
    angles = a;
}

bool PropDynamic::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    if (!model.loaded || solid == 0) return false;
    const Vec3 localOrig=PropWorldToModelPoint(orig,pos,angles,scale);
    const Vec3 localDir=PropWorldToModelDirection(dir,angles);
    float t=0.0f; Vec3 n;
    if(!model.traceRayLocal(localOrig,localDir,t,n)) return false;
    outDist=t*std::max(0.0001f,std::abs(scale));
    outNormal=PropModelNormalToWorld(n,angles);
    return outDist>=0.0f;
}
