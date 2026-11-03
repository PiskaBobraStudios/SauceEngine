#include "sauce_compat_l.h"
#include "fs_compat.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <iterator>

static std::string sclLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

#ifdef _WIN32
static std::string sclWinError(DWORD code) {
    if (!code) return {};
    char* raw = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    FormatMessageA(flags, nullptr, code, 0, reinterpret_cast<LPSTR>(&raw), 0, nullptr);
    std::string out = raw ? raw : "unknown Windows error";
    if (raw) LocalFree(raw);
    while (!out.empty() && (out.back()=='\r' || out.back()=='\n')) out.pop_back();
    return out;
}
#endif

SauceModule_L::~SauceModule_L() { unload(); }

bool SauceModule_L::load(const std::string& path) {
    unload();
    m_path = path;
#ifdef _WIN32


    std::wstring wpath = FsCompat::fromUtf8(path).native();
    m_handle = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m_handle) {
        m_error = "LoadLibrary failed: " + sclWinError(GetLastError());
        return false;
    }
    m_factory = reinterpret_cast<SauceCreateInterfaceFn>(GetProcAddress(m_handle, "CreateInterface"));
    if (!m_factory) {
        m_error = "CreateInterface export not found";
        unload();
        return false;
    }
    return true;
#else
    (void)path;
    m_error = "DLL runtime is Windows-only";
    return false;
#endif
}

void SauceModule_L::unload() {
#ifdef _WIN32
    if (m_handle) FreeLibrary(m_handle);
#endif
    m_handle = nullptr;
    m_factory = nullptr;
    m_error.clear();
}

bool SauceModule_L::loaded() const {
#ifdef _WIN32
    return m_handle != nullptr && m_factory != nullptr;
#else
    return false;
#endif
}

void* SauceModule_L::query(const std::string& interfaceName, int* rc) const {
    if (rc) *rc = -1;
    if (!m_factory) return nullptr;
    return m_factory(interfaceName.c_str(), rc);
}

std::vector<SauceInterfaceInfo> SauceModule_L::probe(const std::string& prefix,
                                                       int firstVersion,
                                                       int lastVersion) const {
    std::vector<SauceInterfaceInfo> result;
    if (!m_factory) return result;
    if (firstVersion < 1) firstVersion = 1;
    if (lastVersion < firstVersion) std::swap(firstVersion, lastVersion);
    for (int v = lastVersion; v >= firstVersion; --v) {
        char name[128];
        std::snprintf(name, sizeof(name), "%s%03d", prefix.c_str(), v);
        int rc = -1;
        void* p = m_factory(name, &rc);
        if (p) result.push_back({m_path, name, p});
    }
    return result;
}

std::vector<SauceInterfaceInfo> SauceModule_L::probeAll(const std::vector<std::string>& prefixes,
                                                          int firstVersion, int lastVersion) const {
    std::vector<SauceInterfaceInfo> out;
    for (const auto& prefix : prefixes) {
        auto v = probe(prefix, firstVersion, lastVersion);
        out.insert(out.end(), v.begin(), v.end());
    }
    return out;
}

SauceInterfaceBroker_L* SauceInterfaceBroker_L::s_active = nullptr;

void SauceInterfaceBroker_L::clear() { m_objects.clear(); }
void SauceInterfaceBroker_L::publish(const std::string& name, void* object) {
    if (!name.empty() && object) m_objects[name] = object;
}
void SauceInterfaceBroker_L::unpublish(const std::string& name) { m_objects.erase(name); }
bool SauceInterfaceBroker_L::has(const std::string& name) const { return m_objects.find(name) != m_objects.end(); }

void* SauceInterfaceBroker_L::query(const char* name, int* rc) const {
    if (rc) *rc = -1;
    if (!name) return nullptr;
    auto it = m_objects.find(name);
    if (it == m_objects.end()) return nullptr;
    if (rc) *rc = 0;
    return it->second;
}

void* SauceInterfaceBroker_L::factoryThunk(const char* name, int* rc) {
    return s_active ? s_active->query(name, rc) : nullptr;
}

SauceCreateInterfaceFn SauceInterfaceBroker_L::factory() const {
    return &SauceInterfaceBroker_L::factoryThunk;
}
SauceInterfaceBroker_L* SauceInterfaceBroker_L::active() { return s_active; }
void SauceInterfaceBroker_L::setActive(SauceInterfaceBroker_L* broker) { s_active = broker; }

bool SauceGameDLLBridge_L::attach(SauceModule_L& module) {
    detach();
    if (!module.loaded()) {
        m_error = "server module is not loaded";
        return false;
    }


    const char* names[] = {
        SauceInterfaceName::ServerGameDLL012,
        SauceInterfaceName::ServerGameDLL011,
        SauceInterfaceName::ServerGameDLL010,
        SauceInterfaceName::ServerGameDLL009,
        SauceInterfaceName::ServerGameDLL008
    };
    for (const char* name : names) {
        int rc = -1;
        void* p = module.query(name, &rc);
        if (p) {
            m_iface.vtable = *reinterpret_cast<void***>(p);
            m_interfaceName = name;
            m_error.clear();
            return true;
        }
    }
    m_error = "ServerGameDLL interface not found";
    return false;
}

bool SauceGameDLLBridge_L::initialize(SauceCreateInterfaceFn engineFactory,
                                        SauceCreateInterfaceFn physicsFactory,
                                        SauceCreateInterfaceFn fileSystemFactory,
                                        void* globals) {
    if (!m_iface.vtable) {
        m_error = "ServerGameDLL is not attached";
        return false;
    }
    auto fn = method<SauceGameDLLInitFn>(0);
    if (!fn) {
        m_error = "ServerGameDLL::DLLInit vtable entry is missing";
        return false;
    }
    if (!fn(m_iface.vtable, engineFactory, physicsFactory, fileSystemFactory, globals)) {
        m_error = "ServerGameDLL::DLLInit returned false";
        return false;
    }
    m_initialized = true;
    m_error.clear();
    return true;
}

bool SauceGameDLLBridge_L::gameInit() {
    if (!m_initialized) return false;
    auto fn = method<SauceGameDLLBoolFn>(2);
    if (!fn || !fn(m_iface.vtable)) return false;
    m_gameInitialized = true;
    return true;
}

bool SauceGameDLLBridge_L::levelInit(const std::string& mapName,
                                      const std::string& entities,
                                      const std::string& oldLevel,
                                      const std::string& landmark,
                                      bool loadGame,
                                      bool background) {
    if (!m_gameInitialized) return false;
    auto fn = method<SauceGameDLLLevelInitFn>(3);
    if (!fn) return false;
    const bool ok = fn(m_iface.vtable, mapName.c_str(), entities.c_str(), oldLevel.c_str(),
                       landmark.c_str(), loadGame, background);
    m_levelInitialized = ok;
    return ok;
}

void SauceGameDLLBridge_L::gameFrame(bool simulating) {
    if (!m_gameInitialized) return;
    if (auto fn = method<SauceGameDLLGameFrameFn>(4)) fn(m_iface.vtable, simulating);
}

void SauceGameDLLBridge_L::levelShutdown() {
    if (!m_levelInitialized) return;
    if (auto fn = method<SauceGameDLLVoidFn>(7)) fn(m_iface.vtable);
    m_levelInitialized = false;
}

void SauceGameDLLBridge_L::gameShutdown() {
    if (!m_gameInitialized) return;
    if (auto fn = method<SauceGameDLLVoidFn>(8)) fn(m_iface.vtable);
    m_gameInitialized = false;
}

void SauceGameDLLBridge_L::dllShutdown() {
    if (!m_initialized) return;
    if (auto fn = method<SauceGameDLLVoidFn>(9)) fn(m_iface.vtable);
    m_initialized = false;
}

void SauceGameDLLBridge_L::detach() {
    m_iface.vtable = nullptr;
    m_interfaceName.clear();
    m_error.clear();
    m_initialized = false;
    m_gameInitialized = false;
    m_levelInitialized = false;
}

bool SauceWeaponRuntime_L::initialize(const std::string& gameDir,
                                      const std::string& serverDll,
                                      const std::string& clientDll) {
    shutdown();
    m_gameDir = gameDir;
    bool ok = false;
    if (!serverDll.empty() && FsCompat::exists(FsCompat::fromUtf8(serverDll))) ok = m_server.load(serverDll) || ok;
    if (!clientDll.empty() && FsCompat::exists(FsCompat::fromUtf8(clientDll))) ok = m_client.load(clientDll) || ok;

    m_serverInterfaces.clear();
    m_clientInterfaces.clear();
    m_serverInterfaces = m_server.probeAll({"ServerGameDLL", "ServerGameEnts", "ServerTools", "VEngineServer"}, 1, 99);
    m_clientInterfaces = m_client.probeAll({"ClientDLL", "VEngineClient", "VClient"}, 1, 99);


    m_gameDll.attach(m_server);

    return ok;
}

void SauceWeaponRuntime_L::shutdownGameDLL() {
    m_gameDll.levelShutdown();
    m_gameDll.gameShutdown();
    m_gameDll.dllShutdown();
    SauceInterfaceBroker_L::setActive(nullptr);
}

void SauceWeaponRuntime_L::shutdown() {
    shutdownGameDLL();
    m_adapter.reset();
    m_server.unload();
    m_client.unload();
    m_serverInterfaces.clear();
    m_clientInterfaces.clear();
    m_host.clear();
    m_className.clear();
    m_gameDir.clear();
}

bool SauceWeaponRuntime_L::initializeGameDLL(void* globals) {
    if (!m_server.loaded() || !m_gameDll.attached()) return false;


    SauceInterfaceBroker_L::setActive(&m_host);
    const bool ok = m_gameDll.initialize(m_host.factory(), m_host.factory(), m_host.factory(), globals);
    if (!ok) {
        SauceInterfaceBroker_L::setActive(nullptr);
        return false;
    }
    return true;
}

bool SauceWeaponRuntime_L::bindClass(const std::string& className) {
    m_className = className;
    if (m_adapter && m_adapter->bind(m_server, m_client, className)) return true;


    return m_gameDll.attached() || hasFactoryPath();
}

bool SauceWeaponRuntime_L::bindExternalAdapter(std::unique_ptr<ISauceWeaponAdapter> adapter) {
    m_adapter = std::move(adapter);
    return m_adapter && m_adapter->bind(m_server, m_client, m_className);
}

bool SauceWeaponRuntime_L::primaryAttack(float curTime) {
    return m_adapter && m_adapter->primaryAttack(curTime);
}
bool SauceWeaponRuntime_L::secondaryAttack(float curTime) {
    return m_adapter && m_adapter->secondaryAttack(curTime);
}
bool SauceWeaponRuntime_L::reload(float curTime) {
    return m_adapter && m_adapter->reload(curTime);
}
void SauceWeaponRuntime_L::itemPostFrame(float dt) {
    if (m_adapter) m_adapter->itemPostFrame(dt);
    if (m_gameDll.initialized()) m_gameDll.gameFrame(true);
}
void SauceWeaponRuntime_L::frame(float dt) {
    if (m_adapter) m_adapter->frame(dt);
    (void)dt;
}


static std::vector<std::string> sclNPCCandidates(const std::string& gameDir) {
    return {
        gameDir+"/client.dll", gameDir+"/server.dll",
        gameDir+"/bin/client.dll", gameDir+"/bin/server.dll",
        gameDir+"/game/bin/client.dll", gameDir+"/game/bin/server.dll"
    };
}

static std::string sclNPCExtractModel(const std::string& path, const std::string& className) {
    std::ifstream f(FsCompat::fromUtf8(path), std::ios::binary);
    if (!f) return {};
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return {};
    std::string low = sclLower(className);
    std::vector<std::string> tokens;
    std::string token;
    for (char c : low) {
        if (std::isalnum((unsigned char)c)) token.push_back(c);
        else if (!token.empty()) { tokens.push_back(token); token.clear(); }
    }
    if (!token.empty()) tokens.push_back(token);
    std::vector<std::string> filteredTokens;
    for (const auto& t : tokens) {
        if (t == "npc" || t == "monster" || t == "ally" || t == "enemy" || t == "creature" || t == "character" || t == "animal" || t == "ai") continue;
        if (t.size() >= 3) filteredTokens.push_back(t);
    }
    tokens.swap(filteredTokens);
    static const std::map<std::string, std::vector<std::string>> aliases = {
        {"npc_citizen", {"citizen", "humans", "male_"}},
        {"npc_metropolice", {"metropolice", "police"}},
        {"npc_combine_s", {"combine", "soldier"}},
        {"npc_zombie", {"zombie", "classic"}},
        {"npc_fastzombie", {"fast", "zombie"}},
        {"npc_headcrab", {"headcrab"}},
        {"npc_headcrab_fast", {"headcrab", "fast"}},
        {"npc_headcrab_black", {"headcrab", "black"}},
        {"npc_antlionguard", {"antlion", "guard"}},
        {"npc_vortigaunt", {"vortigaunt"}},
        {"npc_manhack", {"manhack"}},
        {"npc_rollermine", {"rollermine", "roller"}},
        {"npc_stalker", {"stalker"}},
        {"npc_turret_floor", {"turret", "floor"}},
        {"npc_turret_ceiling", {"turret", "ceiling"}},
        {"npc_turret_ground", {"turret", "floor"}}
    };
    auto aliasIt = aliases.find(sclLower(className));
    if (aliasIt != aliases.end()) tokens.insert(tokens.end(), aliasIt->second.begin(), aliasIt->second.end());
    std::string best; int bestScore = -1;
    for (size_t i=0;i+7<data.size();++i) {
        if (sclLower(data.substr(i,7)) != "models/") continue;
        size_t j=i;
        while (j<data.size()) {
            unsigned char c=(unsigned char)data[j];
            if (!(std::isalnum(c)||c=='_'||c=='-'||c=='.'||c=='/'||c=='\\')) break;
            ++j;
            if (j-i>260) break;
        }
        if (j>=data.size() || j<=i+7) continue;
        std::string model=data.substr(i,j-i);
        std::string ml=sclLower(model);
        if (ml.size()<4 || ml.substr(ml.size()-4)!=".mdl") continue;
        int score=0;
        for (const auto& t:tokens) if (ml.find(t)!=std::string::npos) score+=10;
        if (ml.find("models/")!=std::string::npos) score+=1;
        if (score>bestScore) { bestScore=score; best=model; }
    }
    std::replace(best.begin(),best.end(),'\\','/');
    return bestScore>1 ? best : std::string();
}

bool SauceNPCRuntime_L::initialize(const std::string& gameDir, const std::string& serverDll, const std::string& clientDll) {
    shutdown();
    m_gameDir=gameDir;
    std::vector<std::string> candidates=sclNPCCandidates(gameDir);
    std::string serverPath=serverDll, clientPath=clientDll;
    if (serverPath.empty()) {
        for (const auto& p:candidates) { std::string l=sclLower(p); if (l.size()>=10 && l.rfind("server.dll")==l.size()-10 && FsCompat::exists(FsCompat::fromUtf8(p))) { serverPath=p; break; } }
    }
    if (clientPath.empty()) {
        for (const auto& p:candidates) { std::string l=sclLower(p); if (l.size()>=10 && l.rfind("client.dll")==l.size()-10 && FsCompat::exists(FsCompat::fromUtf8(p))) { clientPath=p; break; } }
    }
    bool ok=false;
    if (!serverPath.empty()) ok=m_server.load(serverPath)||ok;
    if (!clientPath.empty()) ok=m_client.load(clientPath)||ok;
    m_serverInterfaces=m_server.probeAll({"ServerTools","ServerGameEnts","ServerGameDLL"},1,99);
    m_clientInterfaces=m_client.probeAll({"ClientDLL","VClient","VEngineClient"},1,99);
    m_gameDll.attach(m_server);
    if (!serverPath.empty() && FsCompat::exists(FsCompat::fromUtf8(serverPath))) scanModels(serverPath, true);
    if (!clientPath.empty() && FsCompat::exists(FsCompat::fromUtf8(clientPath))) scanModels(clientPath, false);
    return ok || !m_serverInterfaces.empty() || !m_clientInterfaces.empty();
}

void SauceNPCRuntime_L::scanModels(const std::string& path, bool serverModule) {
    std::ifstream f(FsCompat::fromUtf8(path), std::ios::binary);
    if (!f) return;
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.empty()) return;
    std::vector<std::string> classes={
        "npc_citizen","npc_metropolice","npc_combine_s","npc_zombie","npc_zombine","npc_fastzombie","npc_headcrab","npc_headcrab_fast","npc_headcrab_black","npc_antlion","npc_antlionguard","npc_vortigaunt","npc_manhack","npc_rollermine","npc_stalker","npc_turret_floor","npc_turret_ceiling","npc_turret_ground"
    };
    for (const auto& cls:classes) {
        SauceNPCKnowledge& k=m_knowledge[cls];
        k.className=cls;
        if (serverModule) k.serverDllPath=path;
        else k.clientDllPath=path;
        k.serverFactory=!m_serverInterfaces.empty();
        k.hasNativeAI=m_gameDll.attached();
        if (k.modelPath.empty()) k.modelPath=sclNPCExtractModel(path,cls);
    }
}

bool SauceNPCRuntime_L::bindClass(const std::string& className) {
    m_className=sclLower(className);
    SauceNPCKnowledge& k=m_knowledge[m_className];
    k.className=m_className;
    if (k.serverDllPath.empty()) k.serverDllPath=m_server.path();
    k.clientDllPath=m_client.path();
    k.serverFactory=!m_serverInterfaces.empty();
    k.hasNativeAI=m_gameDll.attached();
    if (k.modelPath.empty() && !k.serverDllPath.empty()) k.modelPath=sclNPCExtractModel(k.serverDllPath,m_className);
    if (k.modelPath.empty() && !k.clientDllPath.empty()) k.modelPath=sclNPCExtractModel(k.clientDllPath,m_className);
    if (m_adapter) return m_adapter->bind(m_server,m_client,m_className);
    return k.serverFactory || !k.modelPath.empty();
}

std::string SauceNPCRuntime_L::modelPathForClass(const std::string& className) {
    const std::string cls=sclLower(className);
    auto it=m_knowledge.find(cls);
    if (it==m_knowledge.end()) { bindClass(cls); it=m_knowledge.find(cls); }
    return it==m_knowledge.end()?std::string():it->second.modelPath;
}

const std::string& SauceNPCRuntime_L::modelPath() const {
    static const std::string empty;
    auto it=m_knowledge.find(m_className);
    return it==m_knowledge.end()?empty:it->second.modelPath;
}

bool SauceNPCRuntime_L::bindExternalAdapter(std::unique_ptr<ISauceNPCAdapter> adapter) {
    m_adapter=std::move(adapter);
    return m_adapter && m_adapter->bind(m_server,m_client,m_className);
}

bool SauceNPCRuntime_L::spawn(const Vec3& position) { return m_adapter && m_adapter->spawn(position); }
void SauceNPCRuntime_L::update(float dt) { if (m_adapter) m_adapter->update(dt); }
void SauceNPCRuntime_L::setPosition(const Vec3& position) { if (m_adapter) m_adapter->setPosition(position); }
bool SauceNPCRuntime_L::takeDamage(float damage) { return m_adapter && m_adapter->takeDamage(damage); }
bool SauceNPCRuntime_L::alive() const { return m_adapter ? m_adapter->alive() : false; }

void SauceNPCRuntime_L::shutdown() {
    m_adapter.reset();
    m_gameDll.levelShutdown();
    m_gameDll.gameShutdown();
    m_gameDll.dllShutdown();
    m_gameDll.detach();
    SauceInterfaceBroker_L::setActive(nullptr);
    m_serverInterfaces.clear();
    m_clientInterfaces.clear();
    m_knowledge.clear();
    m_server.unload();
    m_client.unload();
    m_className.clear();
    m_gameDir.clear();
    m_host.clear();
}

bool SauceEngineInterfaceHub_L::initialize(const std::string& gameDir) {
    shutdown();
    return true;
}

void SauceEngineInterfaceHub_L::shutdown() {
    m_broker.clear();
    m_server.unload();
    m_client.unload();
}
