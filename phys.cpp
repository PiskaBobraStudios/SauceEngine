#include "phys.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::ObjectLayer NO_COLLISION = 2;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 3;
}

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        mObjectToBroadPhase[Layers::NO_COLLISION] = BroadPhaseLayers::MOVING;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        return mObjectToBroadPhase[inLayer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::uint8)inLayer) {
        case 0: return "NON_MOVING";
        case 1: return "MOVING";
        case 2: return "NO_COLLISION";
        default: return "UNKNOWN";
        }
    }
#endif
private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::NO_COLLISION || b == Layers::NO_COLLISION) return false;
        if (a == Layers::NON_MOVING) return b == Layers::MOVING;
        if (a == Layers::MOVING) return true;
        return false;
    }
};

class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        if (layer == Layers::NO_COLLISION) return false;
        if (layer == Layers::NON_MOVING) return broad == BroadPhaseLayers::MOVING;
        if (layer == Layers::MOVING) return true;
        return false;
    }
};

JPH::PhysicsSystem* PhysicsWorld::s_PhysicsSystem = nullptr;
JPH::TempAllocatorImpl* PhysicsWorld::s_TempAllocator = nullptr;
JPH::JobSystemThreadPool* PhysicsWorld::s_JobSystem = nullptr;

static BPLayerInterfaceImpl g_BPLayerInterface;
static ObjectLayerPairFilterImpl g_ObjectLayerPairFilter;
static ObjectVsBroadPhaseLayerFilterImpl g_ObjectVsBroadPhaseLayerFilter;
static JPH::BodyID s_WorldBodyID;
static JPH::BodyID s_PlayerBodyID;
static bool s_PlayerBodyCreated = false;
static double s_PhysicsAccumulator = 0.0;
static constexpr double PHYSICS_FIXED_STEP = 1.0 / 60.0;

static std::string LowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static std::string ExtractQuotedValueCI(const std::string& text, const std::string& wantedKey) {
    const std::string key = LowerCopy(wantedKey);
    size_t p = 0;
    while ((p = text.find('"', p)) != std::string::npos) {
        size_t e = text.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string name = LowerCopy(text.substr(p + 1, e - p - 1));
        p = e + 1;
        if (name != key) continue;

        size_t q = text.find('"', p);
        if (q == std::string::npos) break;
        size_t qe = text.find('"', q + 1);
        if (qe == std::string::npos) break;
        return text.substr(q + 1, qe - q - 1);
    }
    return {};
}

void PhysicsWorld::init() {
    if (s_PhysicsSystem) return;

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    s_TempAllocator = new JPH::TempAllocatorImpl(32 * 1024 * 1024);
    unsigned int hw = std::thread::hardware_concurrency();
    unsigned int workers = hw > 1 ? hw - 1 : 1;
    s_JobSystem = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        workers
    );

    s_PhysicsSystem = new JPH::PhysicsSystem();
    s_PhysicsSystem->Init(
        4096, 0, 4096, 4096,
        g_BPLayerInterface,
        g_ObjectVsBroadPhaseLayerFilter,
        g_ObjectLayerPairFilter
    );
    s_PhysicsSystem->SetGravity(JPH::Vec3(0.0f, -800.0f, 0.0f));
    s_PhysicsAccumulator = 0.0;

}

bool PhysicsWorld::loadPHY(const std::vector<char>& rawData, int32_t expectedChecksum, PhyInfo& outInfo) {
    outInfo = {};
    if (rawData.size() < 16 || rawData.size() > (size_t)256 * 1024 * 1024) return false;

    const unsigned char* data = reinterpret_cast<const unsigned char*>(rawData.data());
    const size_t dataSize = rawData.size();
    auto readI32 = [&](size_t off, int32_t& value) -> bool {
        if (off + sizeof(int32_t) > dataSize) return false;
        std::memcpy(&value, data + off, sizeof(value));
        return true;
    };

    int32_t headerSize = 0;
    if (!readI32(0, headerSize) || headerSize < 16 || headerSize > 256 || (size_t)headerSize > dataSize)
        return false;
    if (!readI32(4, outInfo.id) || !readI32(8, outInfo.solidCount) || !readI32(12, outInfo.checksum))
        return false;
    if (outInfo.solidCount <= 0 || outInfo.solidCount > 4096) return false;

    outInfo.checksumMatches = (expectedChecksum == 0 || outInfo.checksum == expectedChecksum);

    size_t cursor = (size_t)headerSize;
    for (int i = 0; i < outInfo.solidCount; ++i) {
        int32_t solidSize = 0;
        if (!readI32(cursor, solidSize)) return false;
        cursor += 4;
        if (solidSize <= 0 || solidSize > (int32_t)dataSize || cursor + (size_t)solidSize > dataSize)
            return false;
        cursor += (size_t)solidSize;
    }

    if (cursor < dataSize) {
        size_t end = cursor;
        while (end < dataSize && data[end] != 0) ++end;
        std::string kv(reinterpret_cast<const char*>(data + cursor), end - cursor);

        std::string mass = ExtractQuotedValueCI(kv, "mass");
        if (!mass.empty()) {
            try { outInfo.mass = std::stof(mass); } catch (...) { outInfo.mass = 0.0f; }
        }
        outInfo.surfaceProp = ExtractQuotedValueCI(kv, "surfaceprop");
    }

    if (!std::isfinite(outInfo.mass) || outInfo.mass <= 0.0f || outInfo.mass > 1000000.0f)
        outInfo.mass = 0.0f;

    outInfo.loaded = true;
    return true;
}

bool PhysicsWorld::loadPHY(const std::string& path, int32_t expectedChecksum, PhyInfo& outInfo) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    file.seekg(0, std::ios::beg);
    if (size < 16 || size > (std::streamoff)256 * 1024 * 1024) return false;
    std::vector<char> data((size_t)size);
    file.read(data.data(), size);
    if (!file) return false;

    bool ok = loadPHY(data, expectedChecksum, outInfo);
    return ok;
}

void PhysicsWorld::syncPlayer(const Vec3& playerPos, float playerRadius, float playerHeight, float dt) {
    if (!s_PhysicsSystem) init();
    if (!s_PhysicsSystem || !s_TempAllocator || !s_JobSystem) return;
    if (!std::isfinite(dt) || dt <= 0.0f) dt = (float)PHYSICS_FIXED_STEP;
    dt = std::max(dt, (float)PHYSICS_FIXED_STEP);

    playerRadius = std::max(2.0f, playerRadius);
    playerHeight = std::max(playerHeight, playerRadius * 2.0f + 2.0f);

    JPH::BodyInterface& bi = s_PhysicsSystem->GetBodyInterface();
    const JPH::RVec3 center(
        playerPos.x,
        playerPos.y - playerHeight * 0.5f,
        playerPos.z);

    if (!s_PlayerBodyCreated || s_PlayerBodyID.IsInvalid()) {
        float halfCylinder = std::max(0.0f, playerHeight * 0.5f - playerRadius);
        JPH::CapsuleShapeSettings shapeSettings(halfCylinder, playerRadius);
        JPH::Shape::ShapeResult result = shapeSettings.Create();
        if (result.HasError()) {
            std::cout << "Error: Player capsule: " << result.GetError().c_str() << std::endl;
            return;
        }

        JPH::BodyCreationSettings settings(
            result.Get(), center, JPH::Quat::sIdentity(),
            JPH::EMotionType::Kinematic, Layers::MOVING);
        settings.mFriction = 0.85f;
        settings.mRestitution = 0.0f;
        settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
        settings.mAllowSleeping = false;
        settings.mMaxLinearVelocity = 2000.0f;
        settings.mMaxAngularVelocity = 0.0f;

        JPH::Body* body = bi.CreateBody(settings);
        if (!body) {
            std::cout << "Error: Could not create kinematic player body.\n";
            return;
        }
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        s_PlayerBodyID = body->GetID();
        s_PlayerBodyCreated = true;
    } else {
        bi.MoveKinematic(s_PlayerBodyID, center, JPH::Quat::sIdentity(), dt);
    }
}

void PhysicsWorld::holdBody(void* internalBodyID, const Vec3& targetPosition, float dt, float maxSpeed) {
    if (!internalBodyID || !s_PhysicsSystem) return;
    if (!std::isfinite(dt) || dt <= 0.0f) dt = (float)PHYSICS_FIXED_STEP;
    JPH::BodyID* id = static_cast<JPH::BodyID*>(internalBodyID);
    JPH::BodyInterface& bi = s_PhysicsSystem->GetBodyInterface();

                                                                            
                                                                                  
    if (bi.GetObjectLayer(*id) != Layers::NO_COLLISION)
        bi.SetObjectLayer(*id, Layers::NO_COLLISION);

    if (bi.GetMotionType(*id) != JPH::EMotionType::Kinematic)
        bi.SetMotionType(*id, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);

    const JPH::RVec3 current = bi.GetPosition(*id);
    JPH::RVec3 target(targetPosition.x, targetPosition.y, targetPosition.z);
    const JPH::RVec3 delta = target - current;
    const float distance = delta.Length();
    const float speed = std::min(std::max(300.0f, maxSpeed), 1400.0f);

    if (distance > 0.01f) {
        const float maxStep = speed * dt;
        const float fraction = std::min(1.0f, maxStep / distance);
        const JPH::RVec3 next = current + delta * fraction;
        bi.MoveKinematic(*id, next, bi.GetRotation(*id), dt);
    } else {
        bi.SetPositionAndRotation(*id, target, bi.GetRotation(*id), JPH::EActivation::Activate);
    }

    bi.SetLinearVelocity(*id, JPH::Vec3::sZero());
    bi.SetAngularVelocity(*id, JPH::Vec3::sZero());
    bi.ActivateBody(*id);
}

void PhysicsWorld::releaseBody(void* internalBodyID, const Vec3& throwVelocity) {
    if (!internalBodyID || !s_PhysicsSystem) return;
    JPH::BodyID* id = static_cast<JPH::BodyID*>(internalBodyID);
    JPH::BodyInterface& bi = s_PhysicsSystem->GetBodyInterface();
    bi.SetObjectLayer(*id, Layers::MOVING);
    bi.SetMotionType(*id, JPH::EMotionType::Dynamic, JPH::EActivation::Activate);
    bi.SetLinearVelocity(*id, JPH::Vec3(throwVelocity.x, throwVelocity.y, throwVelocity.z));
    bi.SetAngularVelocity(*id, JPH::Vec3::sZero());
    bi.ActivateBody(*id);
}

void PhysicsWorld::setWorldCollision(const std::vector<Vec3>& vertices,
                                     const std::vector<unsigned int>& indices) {
    if (!s_PhysicsSystem) init();
    if (vertices.empty() || indices.size() < 3) return;

    JPH::TriangleList triangles;
    triangles.reserve(indices.size() / 3);

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size()) {
            continue;
        }

        const Vec3& a = vertices[indices[i]];
        const Vec3& b = vertices[indices[i + 1]];
        const Vec3& c = vertices[indices[i + 2]];
        Vec3 ab = b - a;
        Vec3 ac = c - a;
        Vec3 n = ab.cross(ac);
        if (n.length() < 0.0001f) {
            continue;
        }

                                                                          
                                                                                
        triangles.push_back(JPH::Triangle(
            JPH::Float3(a.x, a.y, a.z),
            JPH::Float3(b.x, b.y, b.z),
            JPH::Float3(c.x, c.y, c.z)
        ));
        triangles.push_back(JPH::Triangle(
            JPH::Float3(a.x, a.y, a.z),
            JPH::Float3(c.x, c.y, c.z),
            JPH::Float3(b.x, b.y, b.z)
        ));
    }

    if (triangles.empty()) {
        std::cout << "Error: BSP collision contained no valid triangles.\n";
        return;
    }

    JPH::MeshShapeSettings meshSettings(triangles);
    JPH::Shape::ShapeResult shapeResult = meshSettings.Create();
    if (shapeResult.HasError()) {
        std::cout << "Error: BSP collision build failed: "
                  << shapeResult.GetError().c_str() << std::endl;
        return;
    }

    JPH::BodyInterface& bodyInterface = s_PhysicsSystem->GetBodyInterface();
    if (!s_WorldBodyID.IsInvalid()) {
        bodyInterface.RemoveBody(s_WorldBodyID);
        bodyInterface.DestroyBody(s_WorldBodyID);
        s_WorldBodyID = JPH::BodyID();
    }

    JPH::BodyCreationSettings settings(
        shapeResult.Get(),
        JPH::RVec3(0, 0, 0),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING
    );
    settings.mFriction = 0.9f;

    JPH::Body* body = bodyInterface.CreateBody(settings);
    if (!body) {
        std::cout << "Error: Could not create BSP collision body.\n";
        return;
    }

    bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    s_WorldBodyID = body->GetID();

}

void PhysicsWorld::step(float dt) {
    if (!s_PhysicsSystem || !s_TempAllocator || !s_JobSystem) return;

    if (!std::isfinite(dt) || dt <= 0.0f) return;
    dt = std::min(dt, 0.1f);
    s_PhysicsAccumulator = std::min(s_PhysicsAccumulator + (double)dt, 0.25);

    int steps = 0;
    while (s_PhysicsAccumulator >= PHYSICS_FIXED_STEP && steps < 8) {
                                                                           
                                                                               
        s_PhysicsSystem->Update((float)PHYSICS_FIXED_STEP, 2, s_TempAllocator, s_JobSystem);
        s_PhysicsAccumulator -= PHYSICS_FIXED_STEP;
        ++steps;
    }
}

void PhysicsWorld::shutdown() {
    if (s_PhysicsSystem) {
        JPH::BodyInterface& bodyInterface = s_PhysicsSystem->GetBodyInterface();
        if (!s_PlayerBodyID.IsInvalid()) {
            bodyInterface.RemoveBody(s_PlayerBodyID);
            bodyInterface.DestroyBody(s_PlayerBodyID);
            s_PlayerBodyID = JPH::BodyID();
            s_PlayerBodyCreated = false;
        }
        if (!s_WorldBodyID.IsInvalid()) {
            bodyInterface.RemoveBody(s_WorldBodyID);
            bodyInterface.DestroyBody(s_WorldBodyID);
            s_WorldBodyID = JPH::BodyID();
        }
    }

    delete s_PhysicsSystem; s_PhysicsSystem = nullptr;
    delete s_TempAllocator; s_TempAllocator = nullptr;
    delete s_JobSystem; s_JobSystem = nullptr;
    delete JPH::Factory::sInstance; JPH::Factory::sInstance = nullptr;
    s_PhysicsAccumulator = 0.0;
}

JPH::BodyInterface& PhysicsWorld::getBodyInterface() {
    return s_PhysicsSystem->GetBodyInterface();
}

static JPH::Quat EulerToQuat(const Vec3& eulerDeg) {
    const float r = 3.14159265358979323846f / 180.0f;
                                                                  
                                                                          
                                                                                 
    JPH::Quat qYaw = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), eulerDeg.y * r);
    JPH::Quat qPitch = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), -eulerDeg.x * r);
    JPH::Quat qRoll = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), eulerDeg.z * r);
    return (qYaw * qPitch * qRoll).Normalized();
}

static void ApplyCommonBodySettings(JPH::BodyCreationSettings& settings, const RigidBody& body) {
    settings.mRestitution = std::max(0.0f, std::min(1.0f, body.bounciness));
    settings.mFriction = std::max(0.0f, body.friction);
    settings.mLinearDamping = 0.025f;
    settings.mAngularDamping = 0.08f;
    settings.mAllowSleeping = true;
    settings.mMaxLinearVelocity = 1200.0f;
    settings.mMaxAngularVelocity = 18.0f;
    settings.mInertiaMultiplier = 1.0f;
    settings.mNumVelocityStepsOverride = 8;
    settings.mNumPositionStepsOverride = 4;
    settings.mApplyGyroscopicForce = true;
    settings.mEnhancedInternalEdgeRemoval = true;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(0.05f, body.mass);
    if (!body.isStatic) settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
}

void RigidBody::createBox(const Vec3& halfSize) {
    PhysicsWorld::init();
    JPH::BodyInterface& bi = PhysicsWorld::getBodyInterface();

    JPH::BoxShapeSettings shapeSettings(JPH::Vec3(
        std::max(0.01f, halfSize.x),
        std::max(0.01f, halfSize.y),
        std::max(0.01f, halfSize.z)));
    JPH::ShapeSettings::ShapeResult result = shapeSettings.Create();
    if (result.HasError()) {
        std::cout << "Error: Box collision build failed: " << result.GetError().c_str() << std::endl;
        return;
    }

    JPH::ObjectLayer layer = isStatic ? Layers::NON_MOVING : Layers::MOVING;
    JPH::EMotionType motion = isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3(position.x, position.y, position.z),
                                       EulerToQuat(angles), motion, layer);
    ApplyCommonBodySettings(settings, *this);

    JPH::Body* b = bi.CreateBody(settings);
    if (!b) return;
    bi.AddBody(b->GetID(), isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    internalBodyID = new JPH::BodyID(b->GetID());
    radius = std::sqrt(halfSize.x * halfSize.x + halfSize.y * halfSize.y + halfSize.z * halfSize.z);
}

void RigidBody::createSphere(float r) {
    PhysicsWorld::init();
    JPH::BodyInterface& bi = PhysicsWorld::getBodyInterface();

    float radiusValue = std::max(0.05f, r);
    JPH::SphereShapeSettings shapeSettings(radiusValue);
    JPH::ShapeSettings::ShapeResult result = shapeSettings.Create();
    if (result.HasError()) return;

    JPH::ObjectLayer layer = isStatic ? Layers::NON_MOVING : Layers::MOVING;
    JPH::EMotionType motion = isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings settings(result.Get(), JPH::RVec3(position.x, position.y, position.z),
                                       EulerToQuat(angles), motion, layer);
    ApplyCommonBodySettings(settings, *this);

    JPH::Body* b = bi.CreateBody(settings);
    if (!b) return;
    bi.AddBody(b->GetID(), isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    internalBodyID = new JPH::BodyID(b->GetID());
    radius = radiusValue;
}

static bool MakeConvexShape(const std::vector<Vec3>& vertices, JPH::RefConst<JPH::Shape>& outShape) {
    if (vertices.size() < 4) return false;
    JPH::Array<JPH::Vec3> joltVerts;
    joltVerts.reserve(vertices.size());
    for (const Vec3& v : vertices) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) continue;
        joltVerts.push_back(JPH::Vec3(v.x, v.y, v.z));
    }
    if (joltVerts.size() < 4) return false;

    JPH::ConvexHullShapeSettings shapeSettings(joltVerts);
    shapeSettings.mMaxConvexRadius = 0.02f;
    JPH::Shape::ShapeResult result = shapeSettings.Create();
    if (result.HasError()) return false;
    outShape = result.Get();
    return true;
}

static void CreateRigidBodyFromShape(RigidBody& body, const JPH::Shape* shape) {
    JPH::ObjectLayer layer = body.isStatic ? Layers::NON_MOVING : Layers::MOVING;
    JPH::EMotionType motion = body.isStatic ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic;
    JPH::BodyCreationSettings settings(shape, JPH::RVec3(body.position.x, body.position.y, body.position.z),
                                       EulerToQuat(body.angles), motion, layer);
    ApplyCommonBodySettings(settings, body);

    JPH::Body* b = PhysicsWorld::getBodyInterface().CreateBody(settings);
    if (!b) return;
    PhysicsWorld::getBodyInterface().AddBody(
        b->GetID(), body.isStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
    body.internalBodyID = new JPH::BodyID(b->GetID());

    JPH::AABox bounds = shape->GetLocalBounds();
    body.radius = std::max(1.0f, bounds.GetExtent().Length() * 0.5f);
}

void RigidBody::createConvexHull(const std::vector<Vec3>& vertices) {
    PhysicsWorld::init();
    JPH::RefConst<JPH::Shape> shape;
    if (!MakeConvexShape(vertices, shape)) {
        createBox({ 8.0f, 8.0f, 8.0f });
        return;
    }
    CreateRigidBodyFromShape(*this, shape.GetPtr());
}

void RigidBody::createCompoundHull(const std::vector<std::vector<Vec3>>& parts) {
    PhysicsWorld::init();
    if (parts.empty()) {
        createBox({ 8.0f, 8.0f, 8.0f });
        return;
    }

    JPH::StaticCompoundShapeSettings compound;
    size_t validParts = 0;
    for (const auto& part : parts) {
        JPH::RefConst<JPH::Shape> shape;
        if (!MakeConvexShape(part, shape)) continue;
        compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), shape.GetPtr());
        ++validParts;
    }

    if (validParts == 0) {
        createBox({ 8.0f, 8.0f, 8.0f });
        return;
    }

    JPH::Shape::ShapeResult result = compound.Create();
    if (result.HasError()) {
        std::cout << "Error: Compound collision build failed: " << result.GetError().c_str() << std::endl;
        for (const auto& part : parts) {
            JPH::RefConst<JPH::Shape> shape;
            if (MakeConvexShape(part, shape)) {
                CreateRigidBodyFromShape(*this, shape.GetPtr());
                return;
            }
        }
        createBox({ 8.0f, 8.0f, 8.0f });
        return;
    }

    CreateRigidBodyFromShape(*this, result.Get().GetPtr());
}

void RigidBody::destroy() {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return;
    JPH::BodyID* id = static_cast<JPH::BodyID*>(internalBodyID);
    JPH::BodyInterface& bi = PhysicsWorld::getBodyInterface();
    bi.RemoveBody(*id);
    bi.DestroyBody(*id);
    delete id;
    internalBodyID = nullptr;
}

Vec3 RigidBody::getPosition() const {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return position;
    const JPH::BodyID* id = static_cast<const JPH::BodyID*>(internalBodyID);
    JPH::RVec3 p = PhysicsWorld::getBodyInterface().GetPosition(*id);
    return { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
}

Vec3 RigidBody::getVelocity() const {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return velocity;
    const JPH::BodyID* id = static_cast<const JPH::BodyID*>(internalBodyID);
    JPH::Vec3 v = PhysicsWorld::getBodyInterface().GetLinearVelocity(*id);
    return { v.GetX(), v.GetY(), v.GetZ() };
}

Vec3 RigidBody::getCenterOfMassPosition() const {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return position;
    const JPH::BodyID* id = static_cast<const JPH::BodyID*>(internalBodyID);
    JPH::RVec3 p = PhysicsWorld::getBodyInterface().GetCenterOfMassPosition(*id);
    return { (float)p.GetX(), (float)p.GetY(), (float)p.GetZ() };
}

Vec3 RigidBody::getAngles() const {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return angles;
    const JPH::BodyID* id = static_cast<const JPH::BodyID*>(internalBodyID);
    JPH::Quat q = PhysicsWorld::getBodyInterface().GetRotation(*id);

    const float rad2deg = 180.0f / 3.14159265358979323846f;
    float sinr = 2.0f * (q.GetW() * q.GetX() + q.GetY() * q.GetZ());
    float cosr = 1.0f - 2.0f * (q.GetX() * q.GetX() + q.GetY() * q.GetY());
    float roll = std::atan2(sinr, cosr) * rad2deg;

    float sinp = 2.0f * (q.GetW() * q.GetY() - q.GetZ() * q.GetX());
    float pitch = std::abs(sinp) >= 1.0f
        ? std::copysign(90.0f, sinp)
        : std::asin(sinp) * rad2deg;

    float siny = 2.0f * (q.GetW() * q.GetZ() + q.GetX() * q.GetY());
    float cosy = 1.0f - 2.0f * (q.GetY() * q.GetY() + q.GetZ() * q.GetZ());
    float yaw = std::atan2(siny, cosy) * rad2deg;

    return { pitch, yaw, roll };
}

void RigidBody::applyImpulse(const Vec3& impulse, const Vec3& contactOffset) {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return;
    JPH::BodyID* id = static_cast<JPH::BodyID*>(internalBodyID);
    PhysicsWorld::getBodyInterface().AddImpulse(
        *id,
        JPH::Vec3(impulse.x, impulse.y, impulse.z),
        JPH::Vec3(contactOffset.x, contactOffset.y, contactOffset.z));
}

void RigidBody::setVelocity(const Vec3& vel) {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return;
    JPH::BodyID* id = static_cast<JPH::BodyID*>(internalBodyID);
    PhysicsWorld::getBodyInterface().SetLinearVelocity(*id, JPH::Vec3(vel.x, vel.y, vel.z));
}

void RigidBody::holdAt(const Vec3& targetPosition, float dt, float maxSpeed) {
    PhysicsWorld::holdBody(internalBodyID, targetPosition, dt, maxSpeed);
}

void RigidBody::releaseFromHold(const Vec3& throwVelocity) {
    PhysicsWorld::releaseBody(internalBodyID, throwVelocity);
}

void RigidBody::wakeUp() {
    if (!internalBodyID || !PhysicsWorld::getSystem()) return;
    JPH::BodyID* id = static_cast<JPH::BodyID*>(internalBodyID);
    PhysicsWorld::getBodyInterface().ActivateBody(*id);
}
