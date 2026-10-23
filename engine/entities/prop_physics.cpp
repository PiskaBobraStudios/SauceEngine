#include "prop_physics.h"
#include "bsp_l.h"
#include "prop_lighting.h"
#include "texture_l.h"
#include "sound_l.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>
#include <fstream>
#include <cstdlib>
#include <windows.h>
#include <mmsystem.h>
#include <GLFW/glfw3.h>

static std::string lowerString(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool ppFileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return (bool)f;
}

static void ppPlayRandomFiles(const std::string& gameDir, const std::vector<std::string>& files, const Vec3& pos, float volume) {
    (void)gameDir;
    Sound_L::Get().playRandom(files, pos, volume, true, true);
}

static void ppStartScrape(unsigned int& handle, const std::vector<std::string>& files, const Vec3& pos, float volume) {
    if(handle!=0 || files.empty()) return;
    const std::string& path=files[(size_t)(std::rand()%files.size())];
    handle=Sound_L::Get().playLoop(path,pos,volume,true);
}

static void ppStopScrape(unsigned int& handle) {
    if(handle==0) return;
    Sound_L::Get().stopLoop(handle);
    handle=0;
}

static std::string extractEntityValueCI(const std::string& block, const std::string& wanted) {
    const std::string key = lowerString(wanted);
    size_t p = 0;
    while ((p = block.find('"', p)) != std::string::npos) {
        size_t e = block.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string name = lowerString(block.substr(p + 1, e - p - 1));
        p = e + 1;
        if (name != key) continue;
        size_t q = block.find('"', p);
        if (q == std::string::npos) break;
        size_t qe = block.find('"', q + 1);
        if (qe == std::string::npos) break;
        return block.substr(q + 1, qe - q - 1);
    }
    return {};
}

static bool parseFloatValue(const std::string& s, float& out) {
    if (s.empty()) return false;
    try {
        size_t used = 0;
        out = std::stof(s, &used);
        return used > 0 && std::isfinite(out);
    } catch (...) {
        return false;
    }
}

static void applySurfacePhysics(const std::string& surfaceProp, RigidBody& body) {
    const std::string s = lowerString(surfaceProp);
                                                                                  
    if (s.find("rubber") != std::string::npos) { body.friction = 1.10f; body.bounciness = 0.08f; }
    else if (s.find("metal") != std::string::npos) { body.friction = 0.58f; body.bounciness = 0.02f; }
    else if (s.find("wood") != std::string::npos) { body.friction = 0.72f; body.bounciness = 0.03f; }
    else if (s.find("plastic") != std::string::npos) { body.friction = 0.64f; body.bounciness = 0.05f; }
    else if (s.find("concrete") != std::string::npos || s.find("rock") != std::string::npos) { body.friction = 0.86f; body.bounciness = 0.015f; }
}

static Vec3 SauceModelToEngine(const Vec3& v) {
                                                                          
    return { v.x, v.z, -v.y };
}

static uint64_t mixVertexHash(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static uint64_t quantizedVertexKey(const Vec3& v) {
    const int64_t qx = (int64_t)std::lround(v.x * 100.0f);
    const int64_t qy = (int64_t)std::lround(v.y * 100.0f);
    const int64_t qz = (int64_t)std::lround(v.z * 100.0f);
    return mixVertexHash((uint64_t)qx) ^ (mixVertexHash((uint64_t)qy) << 21) ^ (mixVertexHash((uint64_t)qz) >> 7);
}

static void appendUniqueVertex(std::vector<Vec3>& out, std::unordered_set<uint64_t>& seen, const Vec3& v) {
    if (!seen.insert(quantizedVertexKey(v)).second) return;
    out.push_back(v);
}

static void splitLargePart(const std::vector<Vec3>& input, std::vector<std::vector<Vec3>>& out, int depth = 0) {
    if (input.size() < 24 || depth >= 3 || out.size() >= 24) {
        out.push_back(input);
        return;
    }
    Vec3 mn = input[0], mx = input[0];
    for (const Vec3& v : input) {
        mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
        mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
    }
    const Vec3 extent = mx - mn;
    const float largestExtent = std::max(extent.x, std::max(extent.y, extent.z));
    if (largestExtent < 128.0f) {
        out.push_back(input);
        return;
    }
    int axis = 0;
    if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
    else if (extent.z > extent.x && extent.z > extent.y) axis = 2;
    const float mid = axis == 0 ? (mn.x + mx.x) * 0.5f : axis == 1 ? (mn.y + mx.y) * 0.5f : (mn.z + mx.z) * 0.5f;
    std::vector<Vec3> a, b;
    a.reserve(input.size() / 2 + 1); b.reserve(input.size() / 2 + 1);
    for (const Vec3& v : input) {
        const float value = axis == 0 ? v.x : axis == 1 ? v.y : v.z;
        (value <= mid ? a : b).push_back(v);
    }
    if (a.size() < 8 || b.size() < 8) {
        out.push_back(input);
        return;
    }
    splitLargePart(a, out, depth + 1);
    if (out.size() < 24) splitLargePart(b, out, depth + 1);
}

PropPhysics::PropPhysics(const std::string& block, const Vec3& p,
                         const std::string& gDir, Texture_L* texL) {
    gameDirectory = gDir;
    pos = p;
    body.position = p;
    body.angles = { 0.0f, 0.0f, 0.0f };
    body.velocity = { 0.0f, 0.0f, 0.0f };
    body.angularVelocity = { 0.0f, 0.0f, 0.0f };
    body.mass = 20.0f;

    std::string modelPath = extractEntityValueCI(block, "model");
    physicsModelPath = modelPath;
    std::replace(modelPath.begin(), modelPath.end(), '\\', '/');
    if (!modelPath.empty() && texL) model.load(modelPath, *texL);

    std::string anglesStr = extractEntityValueCI(block, "angles");
    if (!anglesStr.empty()) {
        std::istringstream ss(anglesStr);
        ss >> body.angles.x >> body.angles.y >> body.angles.z;
    }

    std::string scaleStr = extractEntityValueCI(block, "modelscale");
    if (scaleStr.empty()) scaleStr = extractEntityValueCI(block, "uniformscale");
    parseFloatValue(scaleStr, scale);
    if (!std::isfinite(scale) || scale <= 0.001f) scale = 1.0f;

    std::string solidStr = extractEntityValueCI(block, "solid");
    if (!solidStr.empty()) {
        try { solid = std::stoi(solidStr); } catch (...) { solid = 6; }
    }

                                                                               
                                                                 
    float explicitMass = 0.0f;
    bool hasExplicitMass = parseFloatValue(extractEntityValueCI(block, "mass"), explicitMass);

    float massScale = 1.0f;
    parseFloatValue(extractEntityValueCI(block, "massscale"), massScale);
    if (!std::isfinite(massScale) || massScale <= 0.0f) massScale = 1.0f;

    PhyInfo phyInfo;
    bool phyLoaded = false;
    if (model.loaded && texL) {
        std::string base = modelPath;
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base.resize(dot);
        std::replace(base.begin(), base.end(), '\\', '/');

        std::vector<char> phyData;
        if (texL->getFileRawData(base + ".phy", phyData)) {
            phyLoaded = PhysicsWorld::loadPHY(phyData, model.checksum, phyInfo);
            if (!phyLoaded) {
                std::cout << "Error: Invalid collision file: " << base << ".phy" << std::endl;
            }
        } else {
            std::cout << "Error: Missing: " << base << ".phy" << std::endl;
        }
    }

    surfaceProp=phyLoaded?lowerString(phyInfo.surfaceProp):std::string();
    if (phyLoaded) applySurfacePhysics(phyInfo.surfaceProp, body);

    float baseMass = 20.0f;
    if (hasExplicitMass && explicitMass > 0.0f) {
        baseMass = explicitMass;
    } else if (phyLoaded && phyInfo.mass > 0.0f) {
        baseMass = phyInfo.mass;
    } else if (model.physicsMass > 0.0f) {
        baseMass = model.physicsMass;
    }

                                                                                
                                                                                    
                                                                                 
                                                                                   
    float finalMass = baseMass * massScale;
    body.mass = std::max(0.25f, std::min(100000.0f, finalMass));

    if (solid == 0) return;

    body.isStatic = false;

    std::vector<std::vector<Vec3>> collisionParts;
    collisionParts.reserve(24);

    if (model.loaded) {
        for (const auto& mesh : model.meshes) {
            std::vector<Vec3> meshVerts;
            std::unordered_set<uint64_t> seen;
            meshVerts.reserve(std::min<size_t>(mesh.vertices.size(), 4096));
            seen.reserve(std::min<size_t>(mesh.vertices.size() * 2, 8192));
            for (const auto& v : mesh.vertices) {
                Vec3 ev = SauceModelToEngine(v.position) * scale;
                if (std::isfinite(ev.x) && std::isfinite(ev.y) && std::isfinite(ev.z)) {
                    appendUniqueVertex(meshVerts, seen, ev);
                }
                if (meshVerts.size() >= 4096) break;
            }
            if (meshVerts.size() >= 4) splitLargePart(meshVerts, collisionParts);
            if (collisionParts.size() >= 24) break;
        }
    }

    if (collisionParts.size() == 1) {
        body.createConvexHull(collisionParts.front());
    } else if (!collisionParts.empty()) {
        body.createCompoundHull(collisionParts);
    } else {
        Vec3 fallbackMin = model.hullMin;
        Vec3 fallbackMax = model.hullMax;
        if (model.hasRenderBounds) {
            fallbackMin = model.renderBoundsMin;
            fallbackMax = model.renderBoundsMax;
        }
        float sx = std::abs(fallbackMax.x - fallbackMin.x) * scale * 0.5f;
        float sy = std::abs(fallbackMax.y - fallbackMin.y) * scale * 0.5f;
        float sz = std::abs(fallbackMax.z - fallbackMin.z) * scale * 0.5f;
        sx = std::max(sx, 4.0f);
        sy = std::max(sy, 4.0f);
        sz = std::max(sz, 4.0f);
        body.createBox({ sx, sy, sz });
    }
}

PropPhysics::~PropPhysics() {
    ppStopScrape(physicsScrapeHandle);
    body.destroy();
}

void PropPhysics::applyImpulse(const Vec3& force, const Vec3& contactPoint) {
    body.applyImpulse(force, contactPoint);
}

bool PropPhysics::canPlayerPickup(const Vec3& playerPos) const {
    if (solid == 0 || !body.internalBodyID || held || body.isStatic) return false;
    if (body.mass > pickupMaxMass) return false;
    const Vec3 boundsMin=model.hasRenderBounds?model.renderBoundsMin:model.hullMin;
    const Vec3 boundsMax=model.hasRenderBounds?model.renderBoundsMax:model.hullMax;
    float sx = std::abs(boundsMax.x - boundsMin.x) * scale;
    float sy = std::abs(boundsMax.y - boundsMin.y) * scale;
    float sz = std::abs(boundsMax.z - boundsMin.z) * scale;
    float largest = std::max(sx, std::max(sy, sz));
    if (!std::isfinite(largest) || largest > pickupMaxSize) return false;
    const Vec3 center = body.getCenterOfMassPosition();
    const float pickupRadius = std::min(220.0f, std::max(160.0f, 64.0f + largest * 0.55f));
    if ((center - playerPos).length() > pickupRadius) return false;
    return true;
}

bool PropPhysics::pickup() {
    if (held || !body.internalBodyID || body.isStatic) return false;
    if (body.mass > pickupMaxMass) return false;
    held = true;
    body.wakeUp();
    return true;
}

void PropPhysics::drop(const Vec3& throwVelocity) {
    if (!held) return;
    held = false;
    body.releaseFromHold(throwVelocity);
}

float PropPhysics::pickupAimScore(const Vec3& rayOrigin, const Vec3& rayDir, float maxDistance, float& outAlong) const {
    outAlong = maxDistance + 1.0f;
    if (!model.loaded || solid == 0 || !body.internalBodyID || body.isStatic || held) return 1e30f;
    if (body.mass > pickupMaxMass) return 1e30f;

    const float DEG = 3.14159265358979323846f / 180.0f;
    const Vec3 center = body.getPosition();
    const Vec3 boundsMin=model.hasRenderBounds?model.renderBoundsMin:model.hullMin;
    const Vec3 boundsMax=model.hasRenderBounds?model.renderBoundsMax:model.hullMax;
    Vec3 half = (boundsMax - boundsMin) * (0.5f * scale);
    float radius = std::max(16.0f, std::sqrt(half.x*half.x + half.y*half.y + half.z*half.z));
    radius = std::min(radius, 128.0f);

    auto rotate = [&](Vec3 v) {
        float c = std::cos(body.getAngles().y * DEG), ss = std::sin(body.getAngles().y * DEG);
        float x = v.x*c - v.z*ss, z = v.x*ss + v.z*c; v.x=x; v.z=z;
        c = std::cos(-body.getAngles().x * DEG); ss = std::sin(-body.getAngles().x * DEG);
        float y = v.y*c - v.z*ss; z = v.y*ss + v.z*c; v.y=y; v.z=z;
        c = std::cos(body.getAngles().z * DEG); ss = std::sin(body.getAngles().z * DEG);
        x = v.x*c - v.y*ss; y = v.x*ss + v.y*c; v.x=x; v.y=y;
        return v;
    };

    const Vec3 d = rayDir.normalize();
    float bestPerp = 1e30f;
    float bestT = maxDistance + 1.0f;

                                                                               
                                                                                   
    size_t total = 0;
    for (const auto& mesh : model.meshes) total += mesh.vertices.size();
    const size_t stride = total > 512 ? (total / 512) + 1 : 1;
    size_t index = 0;
    for (const auto& mesh : model.meshes) {
        for (const auto& vert : mesh.vertices) {
            if ((index++ % stride) != 0) continue;
            Vec3 local = SauceModelToEngine(vert.position) * scale;
            Vec3 world = center + rotate(local);
            float t = (world - rayOrigin).dot(d);
            if (t < -radius || t > maxDistance + radius) continue;
            t = std::max(0.0f, std::min(maxDistance, t));
            float perp = (world - (rayOrigin + d*t)).length();
            if (perp < bestPerp || (std::abs(perp-bestPerp) < 0.01f && t < bestT)) { bestPerp=perp; bestT=t; }
        }
    }

                                                                                
                                                        
    float sphereT = 0.0f;
    Vec3 sphereN;
    if (raySphereIntersect(rayOrigin, d, center, radius, sphereT, sphereN)) {
        float perp = 0.0f;
        if (perp < bestPerp) { bestPerp=perp; bestT=sphereT; }
    } else {
        float centerT = std::max(0.0f, std::min(maxDistance, (center-rayOrigin).dot(d)));
        float centerPerp = (center - (rayOrigin + d*centerT)).length();
        float assist = std::min(52.0f, 14.0f + radius*0.32f + centerT*0.05f);
        if (centerT <= maxDistance && centerPerp <= assist && centerPerp < bestPerp) { bestPerp=centerPerp; bestT=centerT; }
    }

    if (bestPerp > std::min(52.0f, 14.0f + radius*0.32f + bestT*0.05f)) return 1e30f;
    outAlong = bestT;
    return bestPerp * 3.0f + bestT * 0.02f;
}

void PropPhysics::updateHeld(const Vec3& targetPosition, float dt) {
    if (!held || !body.internalBodyID) return;
    body.holdAt(targetPosition, dt, 1050.0f);
    pos = body.getPosition();
}

void PropPhysics::setNetworkTransform(const Vec3& p, const Vec3& a) {
    body.setNetworkTransform(p, a);
    pos = p;
}

void PropPhysics::update(float dt, Camera& player, BSP_L* map) {
    (void)player;
    if (multiplayerRemote) {
        pos = body.getPosition();
        return;
    }
    if (solid == 0 || !body.internalBodyID) return;
    lightmapUpdateTimer -= dt;
    if (map && lightmapUpdateTimer <= 0.0f && (!hasLightSample || (body.getPosition()-lastLightSamplePos).length() > 24.0f)) {
        const Vec3 lp = body.getPosition();
        float b = map->sampleLightmapBrightness(lp);
        if (!std::isfinite(b)) b = 1.0f;
        lightmapBrightness = std::max(0.42f, std::min(1.18f, b));
        lightmapColor = map->sampleLightmapColor(lp);
        lastLightSamplePos = lp;
        hasLightSample = true;
        lightmapUpdateTimer = 0.45f;
    }

    pos = body.getPosition();
    Vec3 currentVelocity = body.getVelocity();
    float currentSpeed = currentVelocity.length();
    if (physicsSoundCooldown > 0.0f) physicsSoundCooldown = std::max(0.0f, physicsSoundCooldown - dt);

    if (!physicsSoundInitialized) {
        lastSpeed = currentSpeed;
        lastVerticalSpeed = currentVelocity.y;
        lastVelocity = currentVelocity;
        physicsSoundInitialized = true;
    }

    if (!held && physicsSoundCooldown <= 0.0f) {
        const bool hardImpact = (lastVerticalSpeed < -65.0f && currentVelocity.y > -12.0f) ||
                                (lastSpeed > 85.0f && currentSpeed < lastSpeed * 0.50f);
        if (hardImpact) {
                                                                                
                                                                             
            const bool barrel = lowerString(physicsModelPath).find("barrel") != std::string::npos;
            if (barrel) {
                ppPlayRandomFiles(gameDirectory, {
                    "sound/physics/plastic/plastic_barrel_impact_hard1.wav",
                    "sound/physics/plastic/plastic_barrel_impact_hard2.wav",
                    "sound/physics/plastic/plastic_barrel_impact_hard3.wav",
                    "sound/physics/plastic/plastic_barrel_impact_hard4.wav",
                    "sound/physics/plastic/plastic_barrel_impact_soft1.wav",
                    "sound/physics/plastic/plastic_barrel_impact_soft2.wav",
                    "sound/physics/plastic/plastic_barrel_impact_soft3.wav",
                    "sound/physics/plastic/plastic_barrel_impact_soft4.wav"
                }, body.getPosition(), 0.95f);
            } else {
                ppPlayRandomFiles(gameDirectory, {
                    "sound/physics/plastic/plastic_box_impact_hard1.wav",
                    "sound/physics/plastic/plastic_box_impact_hard2.wav",
                    "sound/physics/plastic/plastic_box_impact_hard3.wav",
                    "sound/physics/plastic/plastic_box_impact_hard4.wav",
                    "sound/physics/plastic/plastic_box_impact_soft1.wav",
                    "sound/physics/plastic/plastic_box_impact_soft2.wav",
                    "sound/physics/plastic/plastic_box_impact_soft3.wav",
                    "sound/physics/plastic/plastic_box_impact_soft4.wav"
                }, body.getPosition(), 0.90f);
            }

            physicsSoundCooldown = 0.10f;
        }
    }

                                                                               
                                                                               
                                                                  
    const bool movingOnSurface = !held && currentSpeed > 28.0f &&
                                 std::abs(currentVelocity.y) < 18.0f;
    if (movingOnSurface) {
        if (!physicsScrapePlaying && physicsSoundCooldown <= 0.0f) {
            const int which = std::rand() % 3;
            const char* candidates[3] = {
                "sound/physics/plastic/plastic_barrel_scrape_smooth_loop1.wav",
                "sound/physics/plastic/plastic_barrel_scrape_rough_loop1.wav",
                "sound/physics/plastic/plastic_barrel_roll_loop1.wav"
            };
            ppStartScrape(physicsScrapeHandle, {candidates[which]}, body.getPosition(), 0.38f);
            physicsScrapePlaying = physicsScrapeHandle != 0;
        }
    } else if (physicsScrapePlaying) {
        ppStopScrape(physicsScrapeHandle);
        physicsScrapePlaying = false;
    }

    lastSpeed = currentSpeed;
    lastVerticalSpeed = currentVelocity.y;
    lastVelocity = currentVelocity;

    if (held) return;

                                                                   
                                                                                             

}

void PropPhysics::render() {
    if (!model.loaded) return;
    glPushMatrix();

    Vec3 renderPos = body.getPosition();
    Vec3 renderAngles = body.getAngles();

    glTranslatef(renderPos.x, renderPos.y, renderPos.z);
    if (std::abs(scale - 1.0f) > 0.001f) glScalef(scale, scale, scale);

    glRotatef(renderAngles.y, 0.0f, 1.0f, 0.0f);
    glRotatef(-renderAngles.x, 1.0f, 0.0f, 0.0f);
    glRotatef(renderAngles.z, 0.0f, 0.0f, 1.0f);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);

    glColor4f(gPropFlashlightPass?renderColor.x:renderColor.x*lightmapColor.x,gPropFlashlightPass?renderColor.y:renderColor.y*lightmapColor.y,gPropFlashlightPass?renderColor.z:renderColor.z*lightmapColor.z,renderColor.w);
    model.render();

    glPopMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

bool PropPhysics::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    if (!model.loaded || solid == 0 || !body.internalBodyID) return false;
    const Vec3 renderPos=body.getPosition();
    const Vec3 renderAngles=body.getAngles();
    const Vec3 localOrig=PropWorldToModelPoint(orig,renderPos,renderAngles,scale);
    const Vec3 localDir=PropWorldToModelDirection(dir,renderAngles);
    float t=0.0f; Vec3 n;
    if(!model.traceRayLocal(localOrig,localDir,t,n)) return false;
    outDist=t*std::max(0.0001f,std::abs(scale));
    outNormal=PropModelNormalToWorld(n,renderAngles);
    return outDist>=0.0f;
}
