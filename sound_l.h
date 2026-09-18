#pragma once
#include <windows.h>

class Texture_L;
#include <mmsystem.h>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>
#include "math.h"

class BSP_L;

class Sound_L {
public:
    static Sound_L& Get();
    void init(const std::string& gameDir, Texture_L* fileSystem = nullptr);
    void setFileSystem(Texture_L* fileSystem) { textureFS = fileSystem; }
    void shutdown();
    void setListener(const Vec3&, const Vec3&, const Vec3&, const Vec3&, BSP_L*);
    void update(float dt);
    void play(const std::string&, const Vec3& = Vec3{0,0,0}, float = 1.0f, bool = true, bool = true);
    void playRandom(const std::vector<std::string>&, const Vec3& = Vec3{0,0,0}, float = 1.0f, bool = true, bool = true);
    unsigned int playLoop(const std::string&, const Vec3&, float = 1.0f, bool = true);
    void stopLoop(unsigned int);
private:
    struct ListenerState {
        Vec3 pos{0,0,0};
        Vec3 forward{0,0,-1};
        Vec3 right{1,0,0};
        Vec3 up{0,1,0};
        BSP_L* map = nullptr;
        float wet = 0.06f;
        float dry = 0.94f;
        float roomDecay = 0.42f;
        float damping = 0.72f;
    };
    struct WavData { int sampleRate = 44100; int channels = 1; std::vector<float> samples; };
    bool readWavMemory(const std::vector<char>& bytes, WavData& out);
    struct Voice {
        unsigned int id = 0;
        std::shared_ptr<WavData> data;
        double cursor = 0.0;
        float volume = 1.0f;
        float pan = 0.0f;
        bool spatial = true;
        bool reverb = true;
        bool loop = false;
        Vec3 position{0,0,0};
    };
    struct Request {
        enum Type { OneShot, LoopStart, LoopStop } type = OneShot;
        std::string path;
        Vec3 position{0,0,0};
        float volume = 1.0f;
        bool spatial = true;
        bool reverb = true;
        unsigned int handle = 0;
    };
    std::string gameDirectory;
    Texture_L* textureFS = nullptr;
    mutable std::mutex stateMutex;
    std::mutex requestMutex;
    std::condition_variable requestCv;
    ListenerState listener;
    std::deque<Request> requests;
    std::map<unsigned int, Voice> voices;
    std::map<std::string, std::shared_ptr<WavData>> cache;
    std::map<unsigned int, unsigned int> loopHandles;
    std::thread audioThread;
    std::atomic<bool> running{false};
    std::atomic<unsigned int> nextId{1};
    std::atomic<unsigned int> nextLoop{1};
    HWAVEOUT waveOut = nullptr;
    std::vector<int16_t> outputBuffers[3];
    WAVEHDR headers[3]{};
    int currentBuffer = 0;
    int outputFrames = 2048;
    std::vector<float> reverbL;
    std::vector<float> reverbR;
    std::vector<float> dampL;
    std::vector<float> dampR;
    size_t reverbIndex = 0;
    Sound_L() = default;
    ~Sound_L();
    Sound_L(const Sound_L&) = delete;
    Sound_L& operator=(const Sound_L&) = delete;
    void audioLoop();
    void processRequests();
    void enqueue(const Request&);
    std::shared_ptr<WavData> loadWav(const std::string&);
    bool readWavFile(const std::string&, WavData&);
    float sampleAt(const WavData&, double, int) const;
    bool occluded(const Vec3&, const Vec3&, BSP_L*) const;
    float gain(const Voice&, const ListenerState&) const;
    float pan(const Voice&, const ListenerState&) const;
    void updateRoom();
    void mixBlock(std::vector<int16_t>&);
};
