#pragma once
#include "math.h"
#include <string>
#include <vector>
#include <cstdint>

namespace JPH {
    class PhysicsSystem;
    class TempAllocatorImpl;
    class JobSystemThreadPool;
    class BodyInterface;
}

class BSP_L;

                                                                           
                                                                       
                                                                              
                                                                              
                                                                              
                                                                 
struct PhyInfo {
    bool loaded = false;
    bool checksumMatches = true;
    int32_t id = 0;
    int32_t checksum = 0;
    int32_t solidCount = 0;
    float mass = 0.0f;
    std::string surfaceProp;
};

class PhysicsWorld {
public:
    static void init();
    static void step(float dt);
    static void shutdown();

                                                                         
                                                                           
                                                                           
                                                                       
    static void syncPlayer(const Vec3& playerPos, float playerRadius, float playerHeight, float dt);
    static void holdBody(void* internalBodyID, const Vec3& targetPosition, float dt, float maxSpeed = 650.0f);
    static void releaseBody(void* internalBodyID, const Vec3& throwVelocity = Vec3{0, 0, 0});

    static void setWorldCollision(const std::vector<Vec3>& vertices,
                                  const std::vector<unsigned int>& indices);

    static bool loadPHY(const std::string& path, int32_t expectedChecksum, PhyInfo& outInfo);
    static bool loadPHY(const std::vector<char>& data, int32_t expectedChecksum, PhyInfo& outInfo);

    static JPH::PhysicsSystem* getSystem() { return s_PhysicsSystem; }
    static JPH::BodyInterface& getBodyInterface();

private:
    static JPH::PhysicsSystem* s_PhysicsSystem;
    static JPH::TempAllocatorImpl* s_TempAllocator;
    static JPH::JobSystemThreadPool* s_JobSystem;
};

struct RigidBody {
    void* internalBodyID = nullptr;

    Vec3 position;
    Vec3 velocity;
    Vec3 angles;
    Vec3 angularVelocity;

    float mass = 20.0f;
    float radius = 15.0f;
    float friction = 0.8f;
    float bounciness = 0.05f;
    bool isStatic = false;

    void createBox(const Vec3& halfSize);
    void createSphere(float r);
    void createConvexHull(const std::vector<Vec3>& vertices);
    void createCompoundHull(const std::vector<std::vector<Vec3>>& parts);

    void destroy();

    Vec3 getPosition() const;
    Vec3 getAngles() const;
    Vec3 getVelocity() const;
    Vec3 getCenterOfMassPosition() const;
    float getMass() const { return mass; }
    void applyImpulse(const Vec3& impulse, const Vec3& worldPoint);
    void setVelocity(const Vec3& vel);
    void wakeUp();
    void holdAt(const Vec3& targetPosition, float dt, float maxSpeed = 650.0f);
    void releaseFromHold(const Vec3& throwVelocity = Vec3{0, 0, 0});
};
