#include "prop_static.h"
#include "bsp_l.h"
#include "prop_lighting.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <GLFW/glfw3.h>

void PropStatic_L::parseFromBSP(const std::vector<char>& data, int version) {
    modelNames.clear();
    instances.clear();
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
        inst.solid = (pOff + 31 <= sz) ? d[pOff + 30] : 1;
        if (pOff + 56 <= sz) {
            const float lx=*reinterpret_cast<const float*>(d+pOff+44);
            const float ly=*reinterpret_cast<const float*>(d+pOff+48);
            const float lz=*reinterpret_cast<const float*>(d+pOff+52);
            inst.lightingOrigin={lx,lz,-ly};
            inst.hasLightingOrigin=std::isfinite(lx)&&std::isfinite(ly)&&std::isfinite(lz);
        }

        instances.push_back(inst);
    }
}

void PropStatic_L::loadModels(Texture_L& texL) {
    for (auto* m : models) delete m;
    models.clear();
    for (auto& inst : instances) { inst.bakedLightSampled=false; inst.bakedLightColor={1,1,1}; inst.bakedLightBrightness=1.0f; }
    for (const auto& name : modelNames) {
        MDL_L* m = new MDL_L();
        m->load(name, texL);
        models.push_back(m);
    }
}

void PropStatic_L::render(const BSP_L* map) {
    for (auto& inst : instances) {
        if (inst.modelIdx >= models.size() || !models[inst.modelIdx]->loaded) continue;

        glPushMatrix();
        glTranslatef(inst.origin.x, inst.origin.y, inst.origin.z);


        glRotatef(inst.angles.y, 0.0f, 1.0f, 0.0f);
        glRotatef(-inst.angles.x, 1.0f, 0.0f, 0.0f);
        glRotatef(inst.angles.z, 0.0f, 0.0f, 1.0f);
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);

        if(map && !inst.bakedLightSampled) {
            const Vec3& sampleOrigin=inst.hasLightingOrigin?inst.lightingOrigin:inst.origin;
            SamplePropLighting(map,*models[inst.modelIdx],sampleOrigin,inst.angles,inst.scale,inst.bakedLightColor,inst.bakedLightBrightness);
            inst.bakedLightSampled=true;
        }
        glColor4f(gPropFlashlightPass?1.0f:inst.bakedLightColor.x,gPropFlashlightPass?1.0f:inst.bakedLightColor.y,gPropFlashlightPass?1.0f:inst.bakedLightColor.z,1.0f);
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
        const Vec3 localOrig=PropWorldToModelPoint(orig,inst.origin,inst.angles,inst.scale);
        const Vec3 localDir=PropWorldToModelDirection(dir,inst.angles);
        float t=0.0f; Vec3 n;
        if(m->traceRayLocal(localOrig,localDir,t,n)) {
            const float worldT=t*std::max(0.0001f,std::abs(inst.scale));
            if(worldT<outDist){outDist=worldT;outNormal=PropModelNormalToWorld(n,inst.angles);hit=true;}
        }

    }
    return hit;
}

PropStatic_L::~PropStatic_L() {
    for (auto m : models) delete m;
}
