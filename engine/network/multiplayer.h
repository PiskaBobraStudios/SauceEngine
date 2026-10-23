#pragma once
#include <string>
#include <vector>
#include "math.h"

struct MPPlayerState {
    bool valid = false;
    Vec3 pos{0,0,0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float height = 64.0f;
    bool paused = false;
    std::string nick;
    std::string model = "models/player.mdl";
    std::string mapName;
};

struct MPPropSpawnEv {
    int netId = 0;
    int owner = 0;
    std::string classname;
    std::string model;
    Vec3 pos{0,0,0};
    Vec3 angles{0,0,0};
    float scale = 1.0f;
};

struct MPPropMoveEv {
    int netId = 0;
    Vec3 pos{0,0,0};
    Vec3 angles{0,0,0};
};

class Multiplayer_L {
public:
    static bool host(int port);
    static bool connectTo(const std::string& ip, int port);
    static bool parseAddress(const std::string& spec, std::string& host, int& port);
    static int defaultPort();
    static bool parsePort(const std::string& digits, int& port);
    static const std::string& lastError();
    static void shutdown();
    static bool active();
    static bool isHost();
    static bool connected();
    static void tick(float dt);
    static void setCurrentMap(const std::string& map);
    static void setLocalIdentity(const std::string& nick, const std::string& model);
    static void setLocalState(const Vec3& pos, float yaw, float pitch, float height, bool paused);
    static const MPPlayerState& remoteState();
    static void noteLocalSpawn(int netId, const std::string& classname, const std::string& model,
                               const Vec3& pos, const Vec3& angles, float scale);
    static void queueMove(int netId, const Vec3& pos, const Vec3& angles);
    static void setPropOwner(int netId, int owner);
    static bool ownsProp(int netId);
    static void resetProps();
    static void setSuppress(bool on);
    static bool suppressed();
    static std::vector<MPPropSpawnEv> takeSpawns();
    static std::vector<MPPropMoveEv> takeMoves();
    static std::string steamNick();
    static int localPort();
    static int localOwnerId();
    static std::string localAddress();
    static std::string statusLine(const std::string& hostname, const std::string& mapName);
    static int allocateNetId();
};
