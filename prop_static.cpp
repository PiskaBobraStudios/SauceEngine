#include "prop_static.h"
#include "bsp_l.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <GLFW/glfw3.h>

void PropStatic_L::parseFromBSP(const std::vector<char>& data, int version) {
    if (data.size() < 12) return;
    const char* d = data.data();
    size_t sz = data.size();
    size_t offset = 0;

    int32_t dictCount = *reinterpret_cast<const int32_t*>(d + offset);
    offset += 4;

    if (offset + dictCount * 128 > sz) return;
    for (int i = 0; i < dictCount; ++i) {
        char name[128];
        memcpy(name, d + offset, 128);
        offset += 128;
        modelNames.push_back(std::string(name));
    }

    if (offset + 4 > sz) return;
    int32_t leafCount = *reinterpret_cast<const int32_t*>(d + offset);
    offset += 4 + leafCount * 2;

    if (offset + 4 > sz) return;
    int32_t propCount = *reinterpret_cast<const int32_t*>(d + offset);
    offset += 4;

    size_t propSize = (version < 4) ? 56 : (version >= 9 ? 72 : 60);

    for (int i = 0; i < propCount; ++i) {
        size_t pOff = offset + i * propSize;
        if (pOff + 36 > sz) break;

        StaticPropInstance inst;
        float px = *reinterpret_cast<const float*>(d + pOff);
        float py = *reinterpret_cast<const float*>(d + pOff + 4);
        float pz = *reinterpret_cast<const float*>(d + pOff + 8);
        inst.origin = { px, pz, -py };                

        float ax = *reinterpret_cast<const float*>(d + pOff + 12);
        float ay = *reinterpret_cast<const float*>(d + pOff + 16);
        float az = *reinterpret_cast<const float*>(d + pOff + 20);
        inst.angles = { ax, ay, az };

        inst.modelIdx = *reinterpret_cast<const uint16_t*>(d + pOff + 24);
        inst.scale = 1.0f;
        inst.solid = 1;

        instances.push_back(inst);
    }
}

void PropStatic_L::loadModels(Texture_L& texL) {
    for (const auto& name : modelNames) {
        MDL_L* m = new MDL_L();
        m->load(name, texL);
        models.push_back(m);
    }
}

void PropStatic_L::render(const BSP_L* map) {
    glEnable(GL_LIGHTING);
    for (const auto& inst : instances) {
        if (inst.modelIdx >= models.size() || !models[inst.modelIdx]->loaded) continue;

        glPushMatrix();
        glTranslatef(inst.origin.x, inst.origin.y, inst.origin.z);

                                                                             
        glRotatef(inst.angles.y, 0.0f, 1.0f, 0.0f);                                
        glRotatef(-inst.angles.x, 1.0f, 0.0f, 0.0f);                                 
        glRotatef(inst.angles.z, 0.0f, 0.0f, 1.0f);                                 
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);                                                

        Vec3 lightColor{1,1,1};
        float brightness=1.0f;
        if(map) lightColor=map->sampleLightmapLighting(inst.origin,&brightness);
        const Vec3 lc=Vec3{0.24f,0.24f,0.24f}+lightColor*0.76f;
        const float rb=std::max(0.30f,std::min(1.12f,brightness));
        glColor4f(std::clamp(rb*lc.x,0.0f,1.0f),
                  std::clamp(rb*lc.y,0.0f,1.0f),
                  std::clamp(rb*lc.z,0.0f,1.0f),1.0f);
        models[inst.modelIdx]->render();
        glPopMatrix();
    }
    glColor4f(1,1,1,1);
}

bool PropStatic_L::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    bool hit = false;
    outDist = 999999.0f;
    for (const auto& inst : instances) {
        if (inst.solid == 0) continue;
        if (inst.modelIdx >= models.size() || !models[inst.modelIdx]->loaded) continue;

        MDL_L* m = models[inst.modelIdx];

        Vec3 hullMinGL = { m->hullMin.x, -m->hullMin.z, m->hullMin.y };
        Vec3 hullMaxGL = { m->hullMax.x, -m->hullMax.z, m->hullMax.y };

        float radius = std::max({ std::abs(hullMaxGL.x - hullMinGL.x), std::abs(hullMaxGL.y - hullMinGL.y), std::abs(hullMaxGL.z - hullMinGL.z) }) * inst.scale * 0.5f;
        if (radius < 5.0f) radius = 15.0f;

        Vec3 center = inst.origin;
        center.y += radius;

        float t; Vec3 n;
        if (raySphereIntersect(orig, dir, center, radius, t, n)) {
            if (t < outDist) {
                outDist = t;
                outNormal = n;
                hit = true;
            }
        }
    }
    return hit;
}

PropStatic_L::~PropStatic_L() {
    for (auto m : models) delete m;
}