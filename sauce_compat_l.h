#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#endif

                                                     
typedef void* (*SauceCreateInterfaceFn)(const char* name, int* returnCode);

struct SauceInterfaceInfo {
    std::string modulePath;
    std::string interfaceName;
    void* pointer = nullptr;
};

                                                        
namespace SauceInterfaceName {
    static constexpr const char* EngineServer023 = "VEngineServer023";
    static constexpr const char* EngineServer022 = "VEngineServer022";
    static constexpr const char* EngineServer021 = "VEngineServer021";
    static constexpr const char* ServerGameDLL012 = "ServerGameDLL012";
    static constexpr const char* ServerGameDLL011 = "ServerGameDLL011";
    static constexpr const char* ServerGameDLL010 = "ServerGameDLL010";
    static constexpr const char* ServerGameDLL009 = "ServerGameDLL009";
    static constexpr const char* ServerGameDLL008 = "ServerGameDLL008";
}

                                                                           
class SauceModule_L {
public:
    SauceModule_L() = default;
    ~SauceModule_L();

    bool load(const std::string& path);
    void unload();
    bool loaded() const;
    const std::string& path() const { return m_path; }
    const std::string& error() const { return m_error; }

    void* query(const std::string& interfaceName, int* rc = nullptr) const;
    std::vector<SauceInterfaceInfo> probe(const std::string& prefix,
                                           int firstVersion = 1,
                                           int lastVersion = 999) const;
    std::vector<SauceInterfaceInfo> probeAll(const std::vector<std::string>& prefixes,
                                             int firstVersion = 1,
                                             int lastVersion = 999) const;
    SauceCreateInterfaceFn factory() const { return m_factory; }

private:
#ifdef _WIN32
    HMODULE m_handle = nullptr;
#else
    void* m_handle = nullptr;
#endif
    SauceCreateInterfaceFn m_factory = nullptr;
    std::string m_path;
    std::string m_error;
};

                                                                               
                                                                                
struct SauceServerGameDLLView {
    void** vtable = nullptr;
    bool valid() const { return vtable != nullptr; }
};

using SauceGameDLLInitFn = bool (*)(void*, SauceCreateInterfaceFn,
                                    SauceCreateInterfaceFn, SauceCreateInterfaceFn, void*);
using SauceGameDLLVoidFn = void (*)(void*);
using SauceGameDLLBoolFn = bool (*)(void*);
using SauceGameDLLGameFrameFn = void (*)(void*, bool);
using SauceGameDLLLevelInitFn = bool (*)(void*, const char*, const char*, const char*,
                                         const char*, bool, bool);

                                                                                
                                                                                
                                                                
class SauceInterfaceBroker_L {
public:
    void clear();
    void publish(const std::string& name, void* object);
    void unpublish(const std::string& name);
    void* query(const char* name, int* rc = nullptr) const;
    bool has(const std::string& name) const;
    size_t size() const { return m_objects.size(); }

    SauceCreateInterfaceFn factory() const;
    static SauceInterfaceBroker_L* active();
    static void setActive(SauceInterfaceBroker_L* broker);

private:
    static void* factoryThunk(const char* name, int* rc);
    std::map<std::string, void*> m_objects;
    static SauceInterfaceBroker_L* s_active;
};

                                                                          
                                                               
class SauceGameDLLBridge_L {
public:
    bool attach(SauceModule_L& module);
    bool initialize(SauceCreateInterfaceFn engineFactory,
                    SauceCreateInterfaceFn physicsFactory,
                    SauceCreateInterfaceFn fileSystemFactory,
                    void* globals);
    bool gameInit();
    bool levelInit(const std::string& mapName,
                   const std::string& entities = {},
                   const std::string& oldLevel = {},
                   const std::string& landmark = {},
                   bool loadGame = false,
                   bool background = false);
    void gameFrame(bool simulating);
    void levelShutdown();
    void gameShutdown();
    void dllShutdown();
    void detach();

    bool attached() const { return m_iface.valid(); }
    bool initialized() const { return m_initialized; }
    const std::string& interfaceName() const { return m_interfaceName; }
    const std::string& error() const { return m_error; }

private:
    template <typename T>
    T method(size_t index) const {
        if (!m_iface.vtable) return nullptr;
        return reinterpret_cast<T>(m_iface.vtable[index]);
    }

    SauceServerGameDLLView m_iface;
    std::string m_interfaceName;
    std::string m_error;
    bool m_initialized = false;
    bool m_gameInitialized = false;
    bool m_levelInitialized = false;
};

class ISauceWeaponAdapter {
public:
    virtual ~ISauceWeaponAdapter() = default;
    virtual bool bind(SauceModule_L& server, SauceModule_L& client,
                      const std::string& className) = 0;
    virtual bool ownsClass() const = 0;
    virtual bool isAutomatic() const = 0;
    virtual int clipSize() const = 0;
    virtual int clip1() const = 0;
    virtual int reserve1() const = 0;
    virtual bool primaryAttack(float curTime) = 0;
    virtual bool secondaryAttack(float curTime) = 0;
    virtual bool reload(float curTime) = 0;
    virtual void itemPostFrame(float dt) = 0;
    virtual void frame(float dt) = 0;
};

class SauceWeaponRuntime_L {
public:
    bool initialize(const std::string& gameDir,
                    const std::string& serverDll,
                    const std::string& clientDll);
    void shutdown();
    bool bindClass(const std::string& className);
    bool bindExternalAdapter(std::unique_ptr<ISauceWeaponAdapter> adapter);

                                                                           
                                                                                       
    bool initializeGameDLL(void* globals = nullptr);
    void shutdownGameDLL();
    SauceInterfaceBroker_L& hostInterfaces() { return m_host; }
    const SauceGameDLLBridge_L& gameDLL() const { return m_gameDll; }
    bool nativeGameDLLReady() const { return m_gameDll.initialized(); }

    bool hasFactoryPath() const { return !m_serverInterfaces.empty() || !m_clientInterfaces.empty(); }
    bool hasExecutableAdapter() const { return m_adapter != nullptr; }
    bool primaryAttack(float curTime);
    bool secondaryAttack(float curTime);
    bool reload(float curTime);
    void itemPostFrame(float dt);
    void frame(float dt);

    const std::vector<SauceInterfaceInfo>& serverInterfaces() const { return m_serverInterfaces; }
    const std::vector<SauceInterfaceInfo>& clientInterfaces() const { return m_clientInterfaces; }

private:
    std::string m_gameDir;
    std::string m_className;
    SauceModule_L m_server;
    SauceModule_L m_client;
    std::vector<SauceInterfaceInfo> m_serverInterfaces;
    std::vector<SauceInterfaceInfo> m_clientInterfaces;
    std::unique_ptr<ISauceWeaponAdapter> m_adapter;
    SauceInterfaceBroker_L m_host;
    SauceGameDLLBridge_L m_gameDll;
};

class SauceEngineInterfaceHub_L {
public:
    bool initialize(const std::string& gameDir);
    void shutdown();
    SauceInterfaceBroker_L& broker() { return m_broker; }
    const SauceModule_L& serverModule() const { return m_server; }
    const SauceModule_L& clientModule() const { return m_client; }

private:
    SauceModule_L m_server;
    SauceModule_L m_client;
    SauceInterfaceBroker_L m_broker;
};
