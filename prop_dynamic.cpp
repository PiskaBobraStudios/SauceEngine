#include "prop_dynamic.h"
#include "bsp_l.h"
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
        model.load(modelPath, *texL);
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
    lightmapUpdateTimer -= dt;
    if(map && lightmapUpdateTimer<=0.0f &&
       (!hasLightSample || (pos-lastLightSamplePos).length()>12.0f)) {
        float b=1.0f;
        lightmapColor=map->sampleLightmapLighting(pos,&b);
        if(!std::isfinite(b)) b=1.0f;
        lightmapBrightness=std::max(0.30f,std::min(1.18f,b));
        lastLightSamplePos=pos;
        hasLightSample=true;
        lightmapUpdateTimer=0.12f;
    }
}

void PropDynamic::render() {
    if (!model.loaded) return;
    glEnable(GL_LIGHTING);
    glPushMatrix();

    glTranslatef(pos.x, pos.y, pos.z);
    if (std::abs(scale - 1.0f) > 0.001f) glScalef(scale, scale, scale);

    glRotatef(angles.y, 0.0f, 1.0f, 0.0f);         
    glRotatef(-angles.x, 1.0f, 0.0f, 0.0f);          
    glRotatef(angles.z, 0.0f, 0.0f, 1.0f);          
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);                                

    const Vec3 lc = Vec3{0.24f,0.24f,0.24f} + lightmapColor*0.76f;
    const float rb = std::max(0.30f,std::min(1.12f,lightmapBrightness));
    glColor4f(std::clamp(renderColor.x*rb*lc.x,0.0f,1.0f),
              std::clamp(renderColor.y*rb*lc.y,0.0f,1.0f),
              std::clamp(renderColor.z*rb*lc.z,0.0f,1.0f), renderColor.w);
    model.render();

    glPopMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

bool PropDynamic::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    if (!model.loaded || solid == 0) return false;

                                                                                                                                    
    Vec3 localOrig = orig - pos;
    Vec3 localDir = dir;

    const float DEG2RAD = 3.14159265f / 180.0f;

                                                                                                                                                                   
                                              
                                                       

                          
    {
        float y = localOrig.y * 0.0f - localOrig.z * 1.0f;
        float z = localOrig.y * 1.0f + localOrig.z * 0.0f;
        localOrig.y = y; localOrig.z = z;
        y = localDir.y * 0.0f - localDir.z * 1.0f;
        z = localDir.y * 1.0f + localDir.z * 0.0f;
        localDir.y = y; localDir.z = z;
    }
                       
    {
        float c = std::cos(-angles.z * DEG2RAD), s = std::sin(-angles.z * DEG2RAD);
        float x = localOrig.x * c - localOrig.y * s;
        float y = localOrig.x * s + localOrig.y * c;
        localOrig.x = x; localOrig.y = y;
        x = localDir.x * c - localDir.y * s;
        y = localDir.x * s + localDir.y * c;
        localDir.x = x; localDir.y = y;
    }
                        
    {
        float c = std::cos(angles.x * DEG2RAD), s = std::sin(angles.x * DEG2RAD);
        float y = localOrig.y * c - localOrig.z * s;
        float z = localOrig.y * s + localOrig.z * c;
        localOrig.y = y; localOrig.z = z;
        y = localDir.y * c - localDir.z * s;
        z = localDir.y * s + localDir.z * c;
        localDir.y = y; localDir.z = z;
    }
                      
    {
        float c = std::cos(-angles.y * DEG2RAD), s = std::sin(-angles.y * DEG2RAD);
        float x = localOrig.x * c - localOrig.z * s;
        float z = localOrig.x * s + localOrig.z * c;
        localOrig.x = x; localOrig.z = z;
        x = localDir.x * c - localDir.z * s;
        z = localDir.x * s + localDir.z * c;
        localDir.x = x; localDir.z = z;
    }

                                                                                        
    float invScale = 1.0f / scale;
    localOrig = localOrig * invScale;
    localDir = localDir * invScale;

                                                                                                                                                                                 
    Vec3 bMin = model.hullMin;
    Vec3 bMax = model.hullMax;
    if (bMin.x > bMax.x) std::swap(bMin.x, bMax.x);
    if (bMin.y > bMax.y) std::swap(bMin.y, bMax.y);
    if (bMin.z > bMax.z) std::swap(bMin.z, bMax.z);

                     
    float tMin = -1e9f, tMax = 1e9f;
    int hitAxis = 0;
    bool hitSign = false;

    float invD[3] = {
        std::abs(localDir.x) > 1e-8f ? 1.0f / localDir.x : 1e9f,
        std::abs(localDir.y) > 1e-8f ? 1.0f / localDir.y : 1e9f,
        std::abs(localDir.z) > 1e-8f ? 1.0f / localDir.z : 1e9f
    };

    float bounds[2][3] = { {bMin.x,bMin.y,bMin.z},{bMax.x,bMax.y,bMax.z} };
    float o[3] = { localOrig.x, localOrig.y, localOrig.z };

    for (int i = 0; i < 3; ++i) {
        float t1 = (bounds[0][i] - o[i]) * invD[i];
        float t2 = (bounds[1][i] - o[i]) * invD[i];
        bool sw = false;
        if (t1 > t2) { std::swap(t1, t2); sw = true; }
        if (t1 > tMin) { tMin = t1; hitAxis = i; hitSign = !sw; }
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return false;
    }

    if (tMax < 0.0f) return false;
    outDist = (tMin > 0.0001f ? tMin : tMax) * scale;

                                                                                                 
    Vec3 ln = { 0,0,0 };
    if (hitAxis == 0) ln.x = hitSign ? -1 : 1;
    else if (hitAxis == 1) ln.y = hitSign ? -1 : 1;
    else ln.z = hitSign ? -1 : 1;

                                                                                                                                             
    Vec3 n = ln;
    {
        float c = std::cos(angles.y * DEG2RAD), s = std::sin(angles.y * DEG2RAD);
        float x = n.x * c - n.z * s, z = n.x * s + n.z * c; n.x = x; n.z = z;
    }
    {
        float c = std::cos(-angles.x * DEG2RAD), s = std::sin(-angles.x * DEG2RAD);
        float y = n.y * c - n.z * s, z = n.y * s + n.z * c; n.y = y; n.z = z;
    }
    {
        float c = std::cos(angles.z * DEG2RAD), s = std::sin(angles.z * DEG2RAD);
        float x = n.x * c - n.y * s, y = n.x * s + n.y * c; n.x = x; n.y = y;
    }
    { float y = n.y * 0.0f - n.z * (-1.0f), z = n.y * (-1.0f) + n.z * 0.0f; n.y = y; n.z = z; }

    outNormal = n;
    return true;
}