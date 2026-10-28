#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <array>
#include <memory>
#include "math.h"

class Texture_L;

struct MDLVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 texCoord;
    unsigned char bone[3] = {0, 0, 0};
    float boneWeight[3] = {1.0f, 0.0f, 0.0f};
    unsigned char boneCount = 0;
};

struct MDLBoneInfo {
    std::string name;
    int parent=-1;
    Vec3 pos{0,0,0};
    float quat[4]={0,0,0,1};
    Vec3 rotEuler{0,0,0};
    Vec3 posScale{1,1,1};
    Vec3 rotScale{1,1,1};
    float poseToBone[12]={1,0,0,0,0,1,0,0,0,0,1,0};
};
struct MDLSequenceEvent {
    float cycle = 0.0f;
    int event = 0;
    int type = 0;
    std::string options;
};
struct MDLSequenceInfo {
    std::string label;
    std::string activity;
    int flags=0;
    int numFrames=1;
    float fps=30.0f;
    int animIndex=-1;
    int animFlags=0;
    int animDescOffset=-1;
    int animBlock=0;
    int animDataIndex=0;
    int sectionIndex=0;
    int sectionFrames=0;
    std::vector<MDLSequenceEvent> events;

    int sourceModelIndex = 0;
};

struct MDLRenderMesh {
    std::vector<MDLVertex> vertices;
    std::vector<unsigned int> indices;
    unsigned int glTexture = 0;
};

class MDL_L {
public:
    bool load(const std::string& mdlPath, Texture_L& texLoader, bool enableBindPoseSkinning = false, int bodyValue = 0, int skinValue = 0);
    void render();
    void updateAnimation(float dt, bool npcSkeletalIdle = false);
    void setAnimationEventContext(const Vec3& position, bool spatial = true) { animationEventPosition=position; animationEventSpatial=spatial; }
    void renderSkinned(const std::vector<std::array<float,12>>& matrices) const;
    void releaseCPUData();
    void buildBindPoseMatrices(std::vector<std::array<float,12>>& out) const;
    bool playSequence(const std::string& name);
    bool playActivity(const std::string& activity);
    bool playAnimationHint(const std::string& hint);
    bool traceRayLocal(const Vec3& origin, const Vec3& direction, float& outDist, Vec3& outNormal) const;
    const std::string& currentSequenceName() const { return m_sequenceName; }
    float currentSequenceDuration() const;
    bool currentSequenceLoops() const;

    std::string modelName;
    Vec3 hullMin, hullMax;
    std::vector<MDLRenderMesh> meshes;
    bool loaded = false;
    Vec3 renderBoundsMin{0,0,0};
    Vec3 renderBoundsMax{0,0,0};
    bool hasRenderBounds = false;


    int32_t checksum = 0;


    float physicsMass = 0.0f;
    bool animated=false;
    std::vector<MDLBoneInfo> bones;
    std::vector<MDLSequenceInfo> sequences;

private:
    unsigned int displayList = 0;
    std::vector<std::vector<MDLVertex>> bindMeshVertices;
    std::vector<std::array<float,12>> boneMatrices;
    std::vector<std::array<float,12>> m_prevBoneMatrices;
    float m_poseBlend=1.0f;
    int m_poseSequence=-1;
    int m_sequenceIndex=-1;
    float m_sequenceCycle=0.0f;
    float m_sequenceTime=0.0f;
    std::string m_sequenceName;
    bool m_sequenceLoop=true;
    bool m_sequenceFinished=false;
    int m_bodyValue=0;
    int m_skinValue=0;

    void buildBindSkeletonPose();
    void applyProceduralViewmodelPose();
    void skinDynamicVertices();

    bool parseMDL(const std::vector<char>& data, Texture_L& texLoader);
    void skinVerticesToBindPose(const std::vector<char>& mdlData, std::vector<MDLVertex>& verts);
    bool parseVVD(const std::vector<char>& data, std::vector<MDLVertex>& outVerts, int maxVerts);
    bool parseVTX(const std::vector<char>& vtxData, const std::vector<char>& mdlData, const std::vector<MDLVertex>& vvdVerts, Texture_L& texLoader);

    std::vector<std::string> textureDirs;
    std::vector<std::string> textureNames;
    std::vector<char> mdlSourceData;
    std::vector<char> aniSourceData;


    std::vector<std::shared_ptr<MDL_L>> animationSources;
    std::string mdlBasePath;
    Vec3 animationEventPosition{0,0,0};
    bool animationEventSpatial=true;

    bool decodeAnimationFrame(const MDLSequenceInfo& seq, int frame,
                              std::vector<Vec3>& positions,
                              std::vector<std::array<float,4>>& rotations) const;

    template<typename T>
    static T readAt(const char* buf, size_t bufSize, size_t offset) {
        T val{};
        if (offset + sizeof(T) <= bufSize) memcpy(&val, buf + offset, sizeof(T));
        return val;
    }
    static int32_t ri(const char* b, size_t s, size_t o) { return readAt<int32_t>(b, s, o); }
    static uint16_t rs(const char* b, size_t s, size_t o) { return readAt<uint16_t>(b, s, o); }
    static float rf(const char* b, size_t s, size_t o) { return readAt<float>(b, s, o); }
    static Vec3 rv(const char* b, size_t s, size_t o) {
        Vec3 v{};
        if (o + 12 <= s) memcpy(&v, b + o, 12);
        return v;
    }
    static std::string readStr(const char* b, size_t s, size_t offset, size_t maxLen = 256);
};
