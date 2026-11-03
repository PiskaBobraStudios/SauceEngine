#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <cstdint>
#include "math.h"

class Camera;
class BSP_L;
class Entity;
class EntityManager;
class Texture_L;

struct SceneEvent_L {
    int type = 0;
    std::string name;
    std::string params;
    std::string params2;
    std::string params3;
    float startTime = 0.0f;
    float endTime = -1.0f;
    float ramp = 0.0f;
    float distance = 0.0f;
    int flags = 0;
    int loopCount = -1;
    float gestureDuration = -1.0f;
    int channel = 0;
};

struct SceneActor_L {
    std::string name;
    bool active = true;
    std::vector<SceneEvent_L> events;
};

struct SceneData_L {
    std::string path;
    std::vector<SceneEvent_L> globalEvents;
    std::vector<SceneActor_L> actors;
    float duration = 0.0f;
    bool valid = false;
    bool binary = false;
};

class SceneSystem_L {
public:
    static SceneSystem_L& Get();

    void init(const std::string& gameDirectory, Texture_L* textureSystem);
    void shutdown();
    void clear();
    void update(float dt, Camera& player, BSP_L* map, EntityManager& entities);

    bool startScene(Entity* sceneEntity, const std::string& file, EntityManager& entities,
                    Camera& player, BSP_L* map, const std::string& actorFilter = "");
    void stopScene(Entity* sceneEntity, EntityManager& entities, Camera& player, BSP_L* map);
    void pauseScene(Entity* sceneEntity, bool paused);
    bool isScenePlaying(Entity* sceneEntity) const;
    bool triggerScene(Entity* sceneEntity, EntityManager& entities, Camera& player, BSP_L* map);

    const std::string& lastError() const { return error; }

private:
    struct Instance {
        unsigned long long ownerId = 0;
        std::shared_ptr<SceneData_L> data;
        std::vector<Entity*> actorEntities;
        std::vector<std::vector<unsigned char>> actorStarted;
        std::vector<std::vector<unsigned char>> actorEnded;
        std::string actorFilter;
        std::vector<unsigned char> globalStarted;
        std::vector<unsigned char> globalEnded;
        float currentTime = 0.0f;
        float loopStart = -1.0f;
        int loopRemaining = 0;
        bool loop = false;
        bool paused = false;
        bool started = false;
        bool awaitingActors = false;
        bool onStartFired = false;
        bool completionFired = false;
    };

    SceneSystem_L() = default;
    SceneSystem_L(const SceneSystem_L&) = delete;
    SceneSystem_L& operator=(const SceneSystem_L&) = delete;

    std::string gameDir;
    Texture_L* textureSystem = nullptr;
    std::map<std::string, std::shared_ptr<SceneData_L>> cache;
    std::vector<Instance> instances;
    std::shared_ptr<SceneData_L> imagePoolSource;
    std::vector<char> imageData;
    std::vector<uint32_t> imageStringOffsets;
    std::vector<std::string> stringPool;
    bool imagePoolAttempted = false;
    bool imagePoolReady = false;
    std::string error;

    std::shared_ptr<SceneData_L> loadScene(const std::string& file, bool& ok);
    bool loadImagePool();
    std::string imageString(int id) const;
    bool parseTextScene(const std::string& text, SceneData_L& out);
    bool parseBinaryScene(const std::vector<char>& bytes, SceneData_L& out);
    void bindActors(Instance& instance, Entity* owner, EntityManager& entities, const std::string& actorFilter);
    void startEvent(Instance& instance, int actorIndex, size_t eventIndex, EntityManager& entities, Camera& player, BSP_L* map);
    void endEvent(Instance& instance, int actorIndex, size_t eventIndex, EntityManager& entities, Camera& player, BSP_L* map);
    void finishInstance(Instance& instance, EntityManager& entities, Camera& player, BSP_L* map, bool canceled);
    Entity* findEntity(EntityManager& entities, const std::string& name) const;
    Entity* findNamedEntity(EntityManager& entities, Entity* owner, const std::string& name) const;
    Entity* findActorEntity(EntityManager& entities, Entity* owner, const std::string& name, const std::string& actorFilter) const;
    Entity* ownerEntity(EntityManager& entities, unsigned long long id) const;
    bool entityAlive(EntityManager& entities, Entity* entity) const;
    static std::string normalizeScenePath(const std::string& file);
};
