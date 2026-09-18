#pragma once
#include "entity_l.h"
#include "mdl_l.h"
#include "sauce_compat_l.h"
#include <string>
#include <vector>
#include <map>

class BSP_L;
class PropPhysics;
class Texture_L;

struct WeaponSDKKnowledge {
    std::string className;
    std::string serverSymbols;
    std::string clientSymbols;
    std::string scriptPath;
    bool scriptLoaded = false;
    std::string serverDllPath;
    std::string clientDllPath;
    bool hasPrimaryAttack = false;
    bool hasSecondaryAttack = false;
    bool hasReload = false;
    bool hasItemPostFrame = false;
    bool hasDeploy = false;
    bool hasHolster = false;
    bool hasFireBullets = false;
    bool hasBasePrimaryAttack = false;
    bool hasProjectileCreation = false;
    bool hasMeleeEvents = false;
    bool hasViewModelActivities = false;
    bool explicitAutomatic = false;
    bool explicitSemiAutomatic = false;
    bool isMelee = false;
    bool hasMuzzleFlash = false;
    bool hasViewPunch = false;
    bool usesClip = false;
    std::string baseClass;
    std::vector<std::string> sourceFiles;
    bool hasMachineGunBase = false;
    bool hasShotgunSignature = false;
    bool hasPump = false;
    bool hasProjectileSignature = false;
    bool hasStartReload = false;
    bool hasGetFireRate = false;
    bool hasGetBulletSpread = false;
    bool hasSendWeaponAnim = false;
    int sourcePellets = 0;
    int sourceMaxClip1 = 0;
    int sourceDefaultClip1 = -1;
    std::vector<std::string> primaryAnimationHints;
    std::vector<std::string> secondaryAnimationHints;
    std::vector<std::string> reloadAnimationHints;
    std::vector<std::string> drawAnimationHints;
    std::vector<std::string> idleAnimationHints;
    std::map<std::string, std::string> sourceSoundAliases;
    float sauceCycleTime = 0.0f;
    float sauceSpreadDegrees = 0.0f;
    int fireBulletsCount = 1;
    int sourceBurstMin = 1;
    int sourceBurstMax = 1;
    int sourceBurstSize = 0;
    bool sourceReloadsSingly = false;
    bool sourceCallsSequenceDuration = false;
    bool sourceUsesPrimaryAmmo = false;
    bool sourceUsesClips = false;
    bool sourcePrimaryUsesClip = false;
    bool sourcePrimaryReserveOnly = false;
    bool sourceSecondaryReserveOnly = false;
    std::vector<std::string> serverMethods;
    std::vector<std::string> clientMethods;
};

struct WeaponDefinition {
    std::string className;
    std::string printName;
    std::string viewModel;
    std::string worldModel;
    std::string animPrefix;
    int bucket = 0;
    int bucketPosition = 0;
    int bucket360 = 0;
    int bucketPosition360 = 0;
    int slot = 1;
    int clipSize = -1;
    int defaultClip = -1;
    int weight = 0;
    int itemFlags = 0;
    float cycleTime = 0.15f;
    int damage = 0;
    int bullets = 1;
    float spread = 0.0f;
    bool hitscan = false;
    bool automatic = false;
    bool usesPrimaryClip = false;
    int fireBulletsCount = 1;
    int shotgunPellets = 1;
    float reloadDuration = 0.75f;
    float viewKickDegrees = 0.6f;
    bool pumpAfterShot = false;
    bool reloadSingleShell = false;
    bool profileMachineGun = false;
    bool profileShotgun = false;
    bool profilePistol = false;
    bool profileRevolver = false;
    bool profileCrossbow = false;
    bool profileRPG = false;
    std::string primaryAmmo;
    std::string secondaryAmmo;
    std::map<std::string, std::string> sounds;
    WeaponSDKKnowledge sdk;
};

class Weapon_L : public Entity {
public:
    WeaponDefinition definition;
    WeaponSDKKnowledge knowledge;
    MDL_L worldModel;
    MDL_L viewModel;
    std::string gameDirectory;
    Vec3 angles{0,0,0};
    float scale = 1.0f;
    bool pickedUp = false;
    bool attackActive = false;
    bool impactDone = false;
    float attackTime = 0.0f;
    float attackCooldown = 0.0f;
    float attackInterval = 0.30f;
    float pickupRadius = 27.0559f;
    float meleeRange = 75.0f;
    std::string scriptPath;
    bool scriptLoaded = false;
    bool inventorySpawn = false;
    bool firePending = false;
    int clipAmmo = 0;
    int reserveAmmo = 0;
    int maxReserveAmmo = 0;
    int secondaryReserveAmmo = 0;
    int maxSecondaryReserveAmmo = 0;
    SauceWeaponRuntime_L sauceRuntime;
    float lightmapUpdateTimer = 0.0f;
    float lightmapBrightness = 1.0f;
    Vec3 lightmapColor{1,1,1};
    Vec3 lastLightSamplePos{0,0,0};
    bool hasLightSample = false;

    Weapon_L(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL);
    ~Weapon_L() override;

    void update(float dt, Camera& player, BSP_L* map) override;
    void render() override;
    bool traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) override;

    bool pickup(Camera& player);
    bool equip(Camera& player);
    void primaryAttack(Camera& player);
    void secondaryAttack(Camera& player);
    void reload(Camera& player);
    void performHitscanAttack(Camera& player, BSP_L* map, EntityManager* entities);
    void updateWeapon(float dt, Camera& player, BSP_L* map, EntityManager& entities);
    void renderViewModel(const Camera& player, float bobSide, float bobUp, float bobRoll, float bobBlend, float bobTime);
    bool playWeaponAnimation(const std::vector<std::string>& hints);
    bool isActive(const Camera& player) const;
    int slot() const { return definition.slot; }
    const std::string& className() const { return definition.className; }
    int clipCount() const { return clipAmmo; }
    int reserveCount() const { return reserveAmmo; }
    int clipCapacity() const { return definition.clipSize; }
    bool isHitscan() const { return definition.hitscan; }
    bool isAutomatic() const { return definition.automatic || knowledge.explicitAutomatic || definition.profileMachineGun; }
    bool allowsHeldPrimary() const { return definition.automatic || knowledge.explicitAutomatic || definition.profileMachineGun || isMeleeWeapon(); }
    bool showsAmmo() const { return definition.clipSize > 0 || !definition.primaryAmmo.empty() || reserveAmmo > 0; }
    bool usesClipAmmo() const { return definition.clipSize > 0; }

    static Entity* Create(const std::string& block, const Vec3& pos, const std::string& gameDir, Texture_L* texL) {
        return new Weapon_L(block, pos, gameDir, texL);
    }

    static void discoverAndRegister(EntityManager& manager, const std::string& gameDir, Texture_L* fileSystem);
    static const std::map<std::string, WeaponSDKKnowledge>& knowledgeBase();
    static const std::vector<std::string>& impulse101Weapons();
    float aimScore(const Vec3& origin, const Vec3& dir, float maxDistance, float& along) const;

private:
    bool loadDllDefinition();
    bool loadScriptAugments();
    bool isMeleeWeapon() const;
    void performMeleeImpact(Camera& player, BSP_L* map, EntityManager& entities);
    void playScriptSound(const std::string& key, const Vec3& pos, float volume = 1.0f);
    void playRandomScriptSound(const std::string& key, const Vec3& pos, float volume = 1.0f);
    void playFallback(const std::string& relative, const Vec3& pos, float volume = 1.0f, bool spatial = true);
    Texture_L* textureSystem = nullptr;
};
