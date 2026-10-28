#pragma once
#include "entity_l.h"
#include "mdl_l.h"
#include "sauce_compat_l.h"
#include <string>
#include <memory>
#include <unordered_map>

class BSP_L;

struct NPCVisualCache_L;

class NPC_L : public Entity {
public:
    std::string className;
    std::string gameDirectory;
    std::string modelPath;
    std::string weaponClass;
    MDL_L* model = nullptr;
    std::shared_ptr<NPCVisualCache_L> visualCache;


    std::shared_ptr<MDL_L> instanceModel;
    unsigned int instanceRenderList = 0;
    Texture_L* textureSystem = nullptr;
    Vec3 angles{0,0,0};
    float scale = 1.0f;
    int health = 100;
    int maxHealth = 100;
    Vec3 collisionMin{-16,-72,-16};
    Vec3 collisionMax{16,0,16};

    NPC_L(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL);
    ~NPC_L() override;

    void update(float dt, Camera& player, BSP_L* map) override;
    void updateAI(float dt, Camera& player, BSP_L* map, EntityManager& entities);
    void render() override;
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) override;

    void takeDamage(float damage, const Vec3& hitDirection, const Vec3& hitPoint = Vec3{0,0,0});
    bool playNPCSequence(const std::string& name);
    void beginScriptedMove(const std::string& sequence, const std::string& goalName, float speed = 100.0f, const std::string& controllerName = "", const std::string& entrySequence = "", const std::string& idleSequence = "", const std::string& postIdleSequence = "", const std::string& customMoveSequence = "", int moveMode = 0, const Vec3& markPosition = Vec3{0,0,0}, float markYaw = 0.0f, bool waitForBegin = false, bool loopPostIdle = false);
    void cancelScriptedMove();
    void beginSceneControl();
    void endSceneControl();
    bool sceneControlActive() const { return sceneControlled; }
    void sceneSetMoveTarget(const Vec3& target, float speed = 100.0f, float stopDistance = 16.0f);
    void sceneClearMoveTarget();
    void sceneSetLookTarget(const Vec3& target);
    void sceneClearLookTarget();
    void sceneSetFaceTarget(const Vec3& target);
    void sceneClearFaceTarget();
    void scenePlaySequence(const std::string& name, bool loop = false);
    void sceneStopSequence(const std::string& name = {});
    void wakeAI();
    void sleepAI(int state = 3);
    void setAIEnabled(bool value) { aiEnabled = value; }
    bool scriptedSequenceStarted() const { return scriptedSequenceHasStarted; }
    bool isAlive() const { return health > 0; }

    static Entity* Create(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL) {
        return new NPC_L(block, pos, gameDir, texL);
    }
    static void shutdownRuntime();

private:
    Entity* lookTarget = nullptr;
    bool lookAtPlayer = false;
    float lookReaction = 0.0f;
    float idleLookTimer = 1.8f;
    float idleLookYaw = 0.0f;
    bool lookAtSound = false;
    Vec3 soundLookPos{0,0,0};
    float soundFocusTimer = 0.0f;
    double lastHeardSoundTime = -1.0;
    std::unordered_map<Entity*, Vec3> lastObservedPositions;
    std::unordered_map<Entity*, float> activityScores;
    std::string activePathTarget;
    std::string activeScriptGoal;
    std::string activeScriptController;
    float pathWaitRemaining = 0.0f;
    std::string waitingPathNode;
    float scriptedMoveSpeed = 100.0f;
    float attackCooldown = 0.0f;
    double lastAnimTime = -1.0;
    bool hostileToPlayer = false;
    bool alertedToPlayer = false;
    int spawnFlags = 0;
    int sleepState = 0;
    bool aiEnabled = true;
    bool waitForScriptFlag = false;
    bool wasDamaged = false;
    bool waitTillSeenSatisfied = false;
    bool scriptedControlled = false;
    bool scriptedSequenceHasStarted = false;
    float scriptedSequenceTimer = -1.0f;
    std::string scriptedSequenceName;
    std::string scriptedEntryName;
    std::string scriptedIdleName;
    std::string scriptedPostIdleName;
    std::string scriptedCustomMoveName;
    Vec3 scriptedMarkPos{0,0,0};
    float scriptedMarkYaw = 0.0f;
    bool scriptedHasMark = false;
    bool scriptedWaitForBegin = false;
    bool scriptedLoopPostIdle = false;
    int scriptedMoveMode = 0;
    int scriptedPhase = 0;
    bool sceneControlled = false;
    bool sceneHasMoveTarget = false;
    Vec3 sceneMoveTarget{0,0,0};
    float sceneMoveSpeed = 100.0f;
    float sceneStopDistance = 16.0f;
    bool sceneHasLookTarget = false;
    Vec3 sceneLookTarget{0,0,0};
    bool sceneHasFaceTarget = false;
    Vec3 sceneFaceTarget{0,0,0};
    std::string sceneActiveSequence;
    bool aiMoving = false;
    bool aiAttacking = false;
    float verticalVelocity = 0.0f;
    bool npcOnGround = false;
    float idleSoundTimer = 8.0f;
    std::string fallbackIdleSequence;
    float pathSearchCooldown = 0.0f;
    bool startScriptedPerformance();
    void updateSceneControl(float dt, BSP_L* map);
    void updateGravity(float dt, BSP_L* map);
    void ensureIdleAnimation();
    bool playStateSequence(const std::string& state);
    void playNPCSound(const std::string& event);
};
