#include "sound_l.h"
#include "bsp_l.h"
#include "texture_l.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>
#include <cctype>
#include <iostream>

static uint16_t slU16(const unsigned char* p) { return uint16_t(p[0]) | uint16_t(p[1] << 8); }
static uint32_t slU32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1] << 8) | uint32_t(p[2] << 16) | uint32_t(p[3] << 24); }

Sound_L& Sound_L::Get() { static Sound_L s; return s; }
Sound_L::~Sound_L() { shutdown(); }

void Sound_L::init(const std::string& gameDir, Texture_L* fileSystem) {
    shutdown();
    gameDirectory = gameDir;
    textureFS = fileSystem;
    reverbL.assign(44100, 0.0f);
    reverbR.assign(44100, 0.0f);
    dampL.assign(44100, 0.0f);
    dampR.assign(44100, 0.0f);
    reverbIndex = 0;
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = 44100;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = 176400;
    if (waveOutOpen(&waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        waveOut = nullptr;
        std::cout << "Error: Audio output is unavailable." << std::endl;
    }
    for (auto& b : outputBuffers) b.assign(size_t(outputFrames) * 2, 0);
    running.store(true);
    audioThread = std::thread(&Sound_L::audioLoop, this);
}

void Sound_L::shutdown() {
    if (!running.exchange(false)) {
        if (waveOut) { waveOutReset(waveOut); waveOutClose(waveOut); waveOut = nullptr; }
        return;
    }
    requestCv.notify_all();
    if (audioThread.joinable()) audioThread.join();
    if (waveOut) {
        waveOutReset(waveOut);
        for (auto& h : headers) if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(waveOut, &h, sizeof(WAVEHDR));
        waveOutClose(waveOut);
        waveOut = nullptr;
    }
    std::lock_guard<std::mutex> lock(requestMutex);
    requests.clear(); voices.clear(); cache.clear(); loopHandles.clear();
}

void Sound_L::setListener(const Vec3& pos, const Vec3& forward, const Vec3& right, const Vec3& up, BSP_L* map) {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        listener.pos = pos;
        listener.forward = forward;
        listener.right = right;
        listener.up = up;
        listener.map = map;
    }
    updateRoom();
}

void Sound_L::update(float) { processRequests(); }

void Sound_L::enqueue(const Request& req) {
    { std::lock_guard<std::mutex> lock(requestMutex); requests.push_back(req); }
    requestCv.notify_one();
}

bool Sound_L::readWavMemory(const std::vector<char>& bytes, WavData& out) {
    std::vector<unsigned char> data(bytes.begin(), bytes.end());
    if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data()+8, "WAVE", 4) != 0) return false;
    uint16_t tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmSize = 0;
    size_t p = 12;
    while (p + 8 <= data.size()) {
        const unsigned char* c = data.data() + p;
        uint32_t n = slU32(c + 4);
        size_t end = p + 8ull + n;
        if (end > data.size()) break;
        if (std::memcmp(c, "fmt ", 4) == 0 && n >= 16) {
            tag = slU16(c+8); channels = slU16(c+10); rate = slU32(c+12); bits = slU16(c+22);
        } else if (std::memcmp(c, "data", 4) == 0) { pcm = c + 8; pcmSize = n; }
        p = end + (n & 1u);
    }
    if (!pcm || tag != 1 || (channels != 1 && channels != 2) || (bits != 8 && bits != 16) || rate == 0) return false;
    size_t srcFrames = pcmSize / ((bits / 8u) * channels);
    if (!srcFrames) return false;
    std::vector<float> src(srcFrames * channels);
    if (bits == 16) {
        const int16_t* s = reinterpret_cast<const int16_t*>(pcm);
        for (size_t i = 0; i < src.size(); ++i) src[i] = float(s[i]) / 32768.0f;
    } else {
        for (size_t i = 0; i < src.size(); ++i) src[i] = (float(pcm[i]) - 128.0f) / 128.0f;
    }
    double ratio = double(rate) / 44100.0;
    size_t dstFrames = std::max<size_t>(1, size_t(std::ceil(double(srcFrames) / ratio)));
    out.sampleRate = 44100;
    out.channels = channels;
    out.samples.resize(dstFrames * channels);
    for (size_t i = 0; i < dstFrames; ++i) {
        double sp = double(i) * ratio;
        size_t a = std::min(srcFrames - 1, size_t(std::floor(sp)));
        size_t b = std::min(srcFrames - 1, a + 1);
        float frac = float(sp - std::floor(sp));
        for (int ch = 0; ch < channels; ++ch) out.samples[i*channels+ch] = src[a*channels+ch] + (src[b*channels+ch] - src[a*channels+ch]) * frac;
    }
    return true;
}

std::shared_ptr<Sound_L::WavData> Sound_L::loadWav(const std::string& relative) {
    std::string key = relative;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    std::replace(key.begin(), key.end(), '\\', '/');
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    auto w = std::make_shared<WavData>();
    bool ok = false;
    if (textureFS) {
        std::vector<char> raw;
        if (textureFS->getFileRawData(key, raw)) ok = readWavMemory(raw, *w);
    }
    if (!ok) {
        std::string path = gameDirectory + "/" + key;
        std::replace(path.begin(), path.end(), '/', '\\');
        ok = readWavFile(path, *w);
    }
    if (!ok) return {};
    cache[key] = w;
    return w;
}

bool Sound_L::readWavFile(const std::string& path, WavData& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return readWavMemory(data, out);
}

bool Sound_L::occluded(const Vec3& from, const Vec3& to, BSP_L* map) const {
    if (!map) return false;
    Vec3 d = to - from;
    float dist = d.length();
    if (dist <= 8.0f) return false;
    d = d * (1.0f / dist);
    float hit = 0.0f; Vec3 n;
    return map->traceRay(from, d, hit, n) && hit > 0.0f && hit < dist - 8.0f;
}

float Sound_L::gain(const Voice& v, const ListenerState& s) const {
    if (!v.spatial) return v.volume;
    Vec3 d = v.position - s.pos;
    float dist = d.length();
    float t = std::clamp((dist - 24.0f) / 1600.0f, 0.0f, 1.0f);
    float g = std::pow(1.0f - t, 2.35f);
    if (dist >= 1624.0f) g = 0.0f;
    if (s.wet > 0.22f) g *= 0.92f;
    return v.volume * g;
}

float Sound_L::pan(const Voice& v, const ListenerState& s) const {
    if (!v.spatial) return 0.0f;
    Vec3 d = v.position - s.pos;
    float len = d.length();
    if (len < 0.001f) return 0.0f;
    return std::clamp((d * (1.0f / len)).dot(s.right), -1.0f, 1.0f);
}

float Sound_L::sampleAt(const WavData& w, double frame, int ch) const {
    size_t frames = w.samples.size() / w.channels;
    if (!frames || frame < 0.0 || frame >= double(frames)) return 0.0f;
    size_t a = size_t(std::floor(frame));
    size_t b = std::min(frames - 1, a + 1);
    float t = float(frame - std::floor(frame));
    int c = std::min(ch, w.channels - 1);
    float x = w.samples[a*w.channels+c], y = w.samples[b*w.channels+c];
    return x + (y - x) * t;
}

void Sound_L::processRequests() {
    std::deque<Request> local;
    { std::lock_guard<std::mutex> lock(requestMutex); local.swap(requests); }
    for (const auto& req : local) {
        if (req.type == Request::LoopStop) {
            auto it = loopHandles.find(req.handle);
            if (it != loopHandles.end()) { voices.erase(it->second); loopHandles.erase(it); }
            continue;
        }
        auto wav = loadWav(req.path);
        if (!wav) {
            std::cout << "Error: Missing or unreadable WAV: " << req.path << std::endl;
            continue;
        }
        Voice v;
        v.id = nextId.fetch_add(1);
        v.data = wav;
        v.position = req.position;
        v.volume = std::clamp(req.volume, 0.0f, 1.0f);
        v.spatial = req.spatial;
        v.reverb = req.reverb;
        v.loop = req.type == Request::LoopStart;
        voices[v.id] = v;
        if (v.loop) loopHandles[req.handle] = v.id;
    }
}

void Sound_L::mixBlock(std::vector<int16_t>& dst) {
    std::fill(dst.begin(), dst.end(), 0);
    ListenerState s;
    { std::lock_guard<std::mutex> lock(stateMutex); s = listener; }
    std::vector<float> dryL(outputFrames, 0.0f), dryR(outputFrames, 0.0f), send(outputFrames, 0.0f);
    for (auto it = voices.begin(); it != voices.end();) {
        Voice& v = it->second;
        if (!v.data) { it = voices.erase(it); continue; }
        float g = gain(v, s);
        float p = pan(v, s);
        float gl = std::sqrt(std::max(0.0f, 0.5f * (1.0f - p)));
        float gr = std::sqrt(std::max(0.0f, 0.5f * (1.0f + p)));
        size_t frames = v.data->samples.size() / v.data->channels;
        for (int i=0; i<outputFrames; ++i) {
            float l = sampleAt(*v.data, v.cursor, 0);
            float r = v.data->channels == 2 ? sampleAt(*v.data, v.cursor, 1) : l;
            dryL[i] += l * g * gl;
            dryR[i] += r * g * gr;
            if (v.reverb) send[i] += 0.5f * (l * gl + r * gr) * g;
            v.cursor += 1.0;
        }
        if (v.cursor >= double(frames)) {
            if (v.loop && frames) v.cursor = std::fmod(v.cursor, double(frames));
            else it = voices.erase(it); 
            if (v.loop) ++it;
        } else ++it;
    }
    int taps[4] = { 23, 41, 67, 89 };
    float tapGain[4] = { 0.24f, 0.17f, 0.11f, 0.07f };
    int fbA = 137, fbB = 173;
    for (int i=0; i<outputFrames; ++i) {
        size_t w = reverbIndex;
        float input = send[i] * s.wet;
        float earlyL = 0.0f, earlyR = 0.0f;
        for (int k=0;k<4;++k) {
            size_t idx = (w + reverbL.size() - size_t(taps[k] * 44.1f)) % reverbL.size();
            earlyL += reverbL[idx] * tapGain[k];
            earlyR += reverbR[idx] * tapGain[k];
        }
        size_t ia = (w + reverbL.size() - size_t(fbA * 44.1f)) % reverbL.size();
        size_t ib = (w + reverbL.size() - size_t(fbB * 44.1f)) % reverbL.size();
        float diffuseL = (reverbL[ia] + reverbL[ib]) * 0.16f;
        float diffuseR = (reverbR[ia] + reverbR[ib]) * 0.16f;
        float feedbackL = 0.5f * (earlyL + diffuseL) * s.roomDecay;
        float feedbackR = 0.5f * (earlyR + diffuseR) * s.roomDecay;
        float nextL = input + feedbackL;
        float nextR = input + feedbackR;
        dampL[w] = dampL[(w + dampL.size() - 1) % dampL.size()] * s.damping + nextL * (1.0f - s.damping);
        dampR[w] = dampR[(w + dampR.size() - 1) % dampR.size()] * s.damping + nextR * (1.0f - s.damping);
        reverbL[w] = std::clamp(nextL + dampL[w] * 0.28f, -1.0f, 1.0f);
        reverbR[w] = std::clamp(nextR + dampR[w] * 0.28f, -1.0f, 1.0f);
        float outL = dryL[i] * s.dry + (earlyL + diffuseL) * 0.85f;
        float outR = dryR[i] * s.dry + (earlyR + diffuseR) * 0.85f;
        outL = std::tanh(outL * 1.08f);
        outR = std::tanh(outR * 1.08f);
        dst[i*2] = (int16_t)std::lround(std::clamp(outL,-1.0f,1.0f)*32767.0f);
        dst[i*2+1] = (int16_t)std::lround(std::clamp(outR,-1.0f,1.0f)*32767.0f);
        reverbIndex = (reverbIndex + 1) % reverbL.size();
    }
}

void Sound_L::audioLoop() {
    while (running.load()) {
        processRequests();
        if (!waveOut) { Sleep(10); continue; }
        WAVEHDR& h = headers[currentBuffer];
        if (h.dwFlags & WHDR_PREPARED) {
            while (running.load() && !(h.dwFlags & WHDR_DONE)) Sleep(1);
            waveOutUnprepareHeader(waveOut, &h, sizeof(WAVEHDR));
        }
        mixBlock(outputBuffers[currentBuffer]);
        h.lpData = reinterpret_cast<LPSTR>(outputBuffers[currentBuffer].data());
        h.dwBufferLength = DWORD(outputBuffers[currentBuffer].size() * sizeof(int16_t));
        h.dwFlags = 0;
        waveOutPrepareHeader(waveOut, &h, sizeof(WAVEHDR));
        waveOutWrite(waveOut, &h, sizeof(WAVEHDR));
        currentBuffer = (currentBuffer + 1) % 3;
    }
}

void Sound_L::updateRoom() {
    ListenerState s;
    { std::lock_guard<std::mutex> lock(stateMutex); s = listener; }
    if (!s.map) return;
    const Vec3 dirs[8] = {{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0.7071f,0,0.7071f},{-0.7071f,0,0.7071f},{0.7071f,0,-0.7071f},{-0.7071f,0,-0.7071f}};
    float sum = 0.0f; int hits = 0;
    for (const Vec3& d : dirs) {
        float dist = 0.0f; Vec3 n;
        if (s.map->traceRay(s.pos, d, dist, n) && dist > 0.0f) { sum += std::min(dist, 2400.0f); ++hits; }
        else sum += 2400.0f;
    }
    float avg = sum / 8.0f;
    float compact = 1.0f - std::clamp(avg / 1800.0f, 0.0f, 1.0f);
    s.wet = std::clamp(0.035f + compact * 0.25f, 0.035f, 0.285f);
    s.dry = 1.0f - s.wet * 0.32f;
    s.roomDecay = std::clamp(0.36f + compact * 0.52f, 0.36f, 0.88f);
    s.damping = std::clamp(0.70f + (1.0f-compact)*0.16f, 0.70f, 0.86f);
    std::lock_guard<std::mutex> lock(stateMutex);
    listener = s;
}

void Sound_L::play(const std::string& path, const Vec3& pos, float volume, bool spatial, bool allowReverb) {
    if (path.empty()) return;
    Request r; r.type=Request::OneShot; r.path=path; r.position=pos; r.volume=volume; r.spatial=spatial; r.reverb=allowReverb; enqueue(r);
}
void Sound_L::playRandom(const std::vector<std::string>& paths, const Vec3& pos, float volume, bool spatial, bool allowReverb) {
    if (paths.empty()) return;
    play(paths[(size_t)(std::rand()%paths.size())], pos, volume, spatial, allowReverb);
}
unsigned int Sound_L::playLoop(const std::string& path, const Vec3& pos, float volume, bool spatial) {
    unsigned int h = nextLoop.fetch_add(1);
    Request r; r.type=Request::LoopStart; r.path=path; r.position=pos; r.volume=volume; r.spatial=spatial; r.reverb=true; r.handle=h; enqueue(r); return h;
}
void Sound_L::stopLoop(unsigned int h) { if (!h) return; Request r; r.type=Request::LoopStop; r.handle=h; enqueue(r); }
