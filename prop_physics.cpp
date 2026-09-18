#include "prop_physics.h"
#include "bsp_l.h"
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
    float sx = std::abs(model.hullMax.x - model.hullMin.x) * scale;
    float sy = std::abs(model.hullMax.y - model.hullMin.y) * scale;
    float sz = std::abs(model.hullMax.z - model.hullMin.z) * scale;
    float largest = std::max(sx, std::max(sy, sz));
    if (!std::isfinite(largest) || largest > pickupMaxSize) return false;
                                                                               
                                                                                      
    (void)playerPos;
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
    Vec3 half = (model.hullMax - model.hullMin) * (0.5f * scale);
    float radius = std::max(12.0f, std::sqrt(half.x*half.x + half.y*half.y + half.z*half.z));
    radius = std::min(radius, 96.0f);

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
        float assist = std::min(34.0f, 10.0f + radius*0.28f + centerT*0.04f);
        if (centerT <= maxDistance && centerPerp <= assist && centerPerp < bestPerp) { bestPerp=centerPerp; bestT=centerT; }
    }

    if (bestPerp > std::min(34.0f, 10.0f + radius*0.28f + bestT*0.04f)) return 1e30f;
    outAlong = bestT;
    return bestPerp * 3.0f + bestT * 0.02f;
}

void PropPhysics::updateHeld(const Vec3& targetPosition, float dt) {
    if (!held || !body.internalBodyID) return;
    body.holdAt(targetPosition, dt, 1200.0f);
    pos = body.getPosition();
}

void PropPhysics::update(float dt, Camera& player, BSP_L* map) {
    (void)player;
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
    glEnable(GL_LIGHTING);
    glPushMatrix();

    Vec3 renderPos = body.getPosition();
    Vec3 renderAngles = body.getAngles();

    glTranslatef(renderPos.x, renderPos.y, renderPos.z);
    if (std::abs(scale - 1.0f) > 0.001f) glScalef(scale, scale, scale);

    glRotatef(renderAngles.y, 0.0f, 1.0f, 0.0f);
    glRotatef(-renderAngles.x, 1.0f, 0.0f, 0.0f);
    glRotatef(renderAngles.z, 0.0f, 0.0f, 1.0f);
    glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);

    const Vec3 lc=Vec3{0.22f,0.22f,0.22f}+lightmapColor*0.78f;
    const float rb=std::max(0.30f,std::min(1.12f,lightmapBrightness));
    glColor4f(std::max(0.0f,std::min(1.0f,renderColor.x*rb*lc.x)),
              std::max(0.0f,std::min(1.0f,renderColor.y*rb*lc.y)),
              std::max(0.0f,std::min(1.0f,renderColor.z*rb*lc.z)),
              renderColor.w);
    model.render();

    glPopMatrix();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

bool PropPhysics::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    if (!model.loaded || solid == 0 || !body.internalBodyID) return false;

    const Vec3 renderPos = body.getPosition();
    const Vec3 renderAngles = body.getAngles();
    const float DEG2RAD = 3.14159265358979323846f / 180.0f;
    const float safeScale = std::max(scale, 0.001f);

                                                                         
                                                                                 
                                                                
    Vec3 localOrig = orig - renderPos;
    Vec3 localDir = dir.normalize();

    {
        float c = std::cos(-renderAngles.y * DEG2RAD), s = std::sin(-renderAngles.y * DEG2RAD);
        float x = localOrig.x * c - localOrig.z * s;
        float z = localOrig.x * s + localOrig.z * c;
        localOrig.x = x; localOrig.z = z;
        x = localDir.x * c - localDir.z * s;
        z = localDir.x * s + localDir.z * c;
        localDir.x = x; localDir.z = z;
    }
    {
        float c = std::cos(renderAngles.x * DEG2RAD), s = std::sin(renderAngles.x * DEG2RAD);
        float y = localOrig.y * c - localOrig.z * s;
        float z = localOrig.y * s + localOrig.z * c;
        localOrig.y = y; localOrig.z = z;
        y = localDir.y * c - localDir.z * s;
        z = localDir.y * s + localDir.z * c;
        localDir.y = y; localDir.z = z;
    }
    {
        float c = std::cos(-renderAngles.z * DEG2RAD), s = std::sin(-renderAngles.z * DEG2RAD);
        float x = localOrig.x * c - localOrig.y * s;
        float y = localOrig.x * s + localOrig.y * c;
        localOrig.x = x; localOrig.y = y;
        x = localDir.x * c - localDir.y * s;
        y = localDir.x * s + localDir.y * c;
        localDir.x = x; localDir.y = y;
    }
    {
        float y = -localOrig.z, z = localOrig.y;
        localOrig.y = y; localOrig.z = z;
        y = -localDir.z; z = localDir.y;
        localDir.y = y; localDir.z = z;
    }

                                                                                  
                                                                             
                                                                         
    localOrig = localOrig * (1.0f / safeScale);

    Vec3 broadMin = model.hullMin;
    Vec3 broadMax = model.hullMax;
    if (model.hasRenderBounds) {
        broadMin = model.renderBoundsMin;
        broadMax = model.renderBoundsMax;
    }
    if (broadMin.x > broadMax.x) std::swap(broadMin.x, broadMax.x);
    if (broadMin.y > broadMax.y) std::swap(broadMin.y, broadMax.y);
    if (broadMin.z > broadMax.z) std::swap(broadMin.z, broadMax.z);

                                                                  
    float boxMinT = 0.0f, boxMaxT = std::numeric_limits<float>::max();
    const float bo[3] = { localOrig.x, localOrig.y, localOrig.z };
    const float bd[3] = { localDir.x, localDir.y, localDir.z };
    const float bmn[3] = { broadMin.x, broadMin.y, broadMin.z };
    const float bmx[3] = { broadMax.x, broadMax.y, broadMax.z };
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(bd[axis]) < 1e-8f) {
            if (bo[axis] < bmn[axis] || bo[axis] > bmx[axis]) return false;
            continue;
        }
        float a = (bmn[axis] - bo[axis]) / bd[axis];
        float b = (bmx[axis] - bo[axis]) / bd[axis];
        if (a > b) std::swap(a, b);
        boxMinT = std::max(boxMinT, a);
        boxMaxT = std::min(boxMaxT, b);
        if (boxMinT > boxMaxT) return false;
    }

    float bestT = std::numeric_limits<float>::max();
    Vec3 bestNormal{0, 0, 0};
    bool hit = false;

    auto considerTriangle = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        const Vec3 edge1 = b - a;
        const Vec3 edge2 = c - a;
        const Vec3 pvec = localDir.cross(edge2);
        const float det = edge1.dot(pvec);
        if (std::abs(det) < 1e-7f) return;
        const float invDet = 1.0f / det;
        const Vec3 tvec = localOrig - a;
        const float u = tvec.dot(pvec) * invDet;
        if (u < 0.0f || u > 1.0f) return;
        const Vec3 qvec = tvec.cross(edge1);
        const float v = localDir.dot(qvec) * invDet;
        if (v < 0.0f || u + v > 1.0f) return;
        const float t = edge2.dot(qvec) * invDet;
        if (t < 0.0f || t >= bestT) return;

        Vec3 n = edge1.cross(edge2);
        if (n.length() < 1e-7f) return;
        n = n.normalize();
                                                                            
        if (n.dot(localDir) > 0.0f) n = n * -1.0f;
        bestT = t;
        bestNormal = n;
        hit = true;
    };

                                                                                 
                                                                        
    for (const auto& mesh : model.meshes) {
        const auto& verts = mesh.vertices;
        if (verts.size() < 3) continue;
        if (!mesh.indices.empty()) {
            for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                const unsigned int ia = mesh.indices[i];
                const unsigned int ib = mesh.indices[i + 1];
                const unsigned int ic = mesh.indices[i + 2];
                if (ia >= verts.size() || ib >= verts.size() || ic >= verts.size()) continue;
                considerTriangle(verts[ia].position, verts[ib].position, verts[ic].position);
            }
        } else {
            for (size_t i = 0; i + 2 < verts.size(); i += 3)
                considerTriangle(verts[i].position, verts[i + 1].position, verts[i + 2].position);
        }
    }

                                                                                 
    if (!hit) {
        const Vec3 bMin = broadMin;
        const Vec3 bMax = broadMax;

        float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
        int hitAxis = -1;
        float hitSign = 1.0f;
        const float o[3] = { localOrig.x, localOrig.y, localOrig.z };
        const float d[3] = { localDir.x, localDir.y, localDir.z };
        const float mn[3] = { bMin.x, bMin.y, bMin.z };
        const float mx[3] = { bMax.x, bMax.y, bMax.z };
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(d[axis]) < 1e-8f) {
                if (o[axis] < mn[axis] || o[axis] > mx[axis]) return false;
                continue;
            }
            float a = (mn[axis] - o[axis]) / d[axis];
            float b = (mx[axis] - o[axis]) / d[axis];
            float sign = -1.0f;
            if (a > b) { std::swap(a, b); sign = 1.0f; }
            if (a > tMin) { tMin = a; hitAxis = axis; hitSign = sign; }
            tMax = std::min(tMax, b);
            if (tMin > tMax) return false;
        }
        if (hitAxis < 0 || tMax < 0.0f) return false;
        bestT = std::max(0.0f, tMin);
        bestNormal = {0, 0, 0};
        if (hitAxis == 0) bestNormal.x = hitSign;
        else if (hitAxis == 1) bestNormal.y = hitSign;
        else bestNormal.z = hitSign;
        hit = true;
    }

    if (!hit || !std::isfinite(bestT)) return false;
    outDist = bestT * safeScale;

                                                                           
                                                          
    Vec3 n = bestNormal;
    {
        float y = -n.z, z = n.y;
        n.y = y; n.z = z;
    }
    {
        float c = std::cos(renderAngles.z * DEG2RAD), s = std::sin(renderAngles.z * DEG2RAD);
        float x = n.x * c - n.y * s, y = n.x * s + n.y * c;
        n.x = x; n.y = y;
    }
    {
        float c = std::cos(-renderAngles.x * DEG2RAD), s = std::sin(-renderAngles.x * DEG2RAD);
        float y = n.y * c - n.z * s, z = n.y * s + n.z * c;
        n.y = y; n.z = z;
    }
    {
        float c = std::cos(renderAngles.y * DEG2RAD), s = std::sin(renderAngles.y * DEG2RAD);
        float x = n.x * c - n.z * s, z = n.x * s + n.z * c;
        n.x = x; n.z = z;
    }
    outNormal = n.normalize();
    return true;
}
