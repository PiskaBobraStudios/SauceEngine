#include "sound_l.h"
#include "fs_compat.h"
#include "bsp_l.h"
#include "texture_l.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>
#include <cctype>
#include <iostream>
#include <chrono>
#include <sstream>
#include <unordered_set>
#include <filesystem>
#include <functional>

static uint16_t slU16(const unsigned char* p) { return uint16_t(p[0]) | uint16_t(p[1] << 8); }
static uint32_t slU32(const unsigned char* p) { return uint32_t(p[0]) | uint32_t(p[1] << 8) | uint32_t(p[2] << 16) | uint32_t(p[3] << 24); }


namespace {
struct SLKVNode {
    std::string key;
    std::string value;
    std::vector<SLKVNode> children;
};
static std::string slLower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return v;
}
static std::vector<std::string> slKVTokens(const std::string& text) {
    std::vector<std::string> out;
    for (size_t i=0; i<text.size();) {
        unsigned char c=(unsigned char)text[i];
        if (std::isspace(c)) { ++i; continue; }
        if (text[i]=='/' && i+1<text.size() && text[i+1]=='/') {
            i+=2; while(i<text.size() && text[i]!='\n') ++i; continue;
        }
        if (text[i]=='/' && i+1<text.size() && text[i+1]=='*') {
            i+=2; while(i+1<text.size() && !(text[i]=='*'&&text[i+1]=='/')) ++i;
            i=std::min(text.size(),i+2); continue;
        }
        if (text[i]=='{' || text[i]=='}') { out.emplace_back(1,text[i++]); continue; }
        std::string t;
        if (text[i]=='"') {
            ++i;
            while(i<text.size()) {
                char ch=text[i++];
                if(ch=='"') break;
                if(ch=='\\' && i<text.size()) {
                    char next=text[i++];
                    if(next=='n') t.push_back('\n'); else if(next=='t') t.push_back('\t'); else t.push_back(next);
                } else t.push_back(ch);
            }
        } else {
            while(i<text.size() && !std::isspace((unsigned char)text[i]) && text[i]!='{' && text[i]!='}') {
                if(text[i]=='/' && i+1<text.size() && (text[i+1]=='/' || text[i+1]=='*')) break;
                t.push_back(text[i++]);
            }
        }
        if(!t.empty()) out.push_back(std::move(t));
    }
    return out;
}
static std::vector<SLKVNode> slParseKVLevel(const std::vector<std::string>& t, size_t& i, bool nested=false) {
    std::vector<SLKVNode> result;
    while(i<t.size()) {
        if(t[i]=="}") { ++i; break; }
        if(t[i]=="{") { ++i; continue; }
        SLKVNode n; n.key=t[i++];
        if(i<t.size() && t[i]=="{") { ++i; n.children=slParseKVLevel(t,i,true); }
        else if(i<t.size() && t[i]!="}") n.value=t[i++];
        else break;
        result.push_back(std::move(n));
    }
    (void)nested;
    return result;
}
static const SLKVNode* slChild(const SLKVNode& node, const std::string& name) {
    const std::string n=slLower(name);
    for(const auto& c:node.children) if(slLower(c.key)==n) return &c;
    return nullptr;
}
static void slCollectWaves(const SLKVNode& node, std::vector<std::string>& out, bool insideWave=false) {
    const std::string k=slLower(node.key);
    if(k=="wave") {
        if(!node.value.empty()) out.push_back(node.value);
        for(const auto& c:node.children) slCollectWaves(c,out,true);
        return;
    }
    if(k=="rndwave") {
        for(const auto& c:node.children) slCollectWaves(c,out,true);
        return;
    }
    if(insideWave && !node.value.empty() && node.children.empty()) out.push_back(node.value);
    for(const auto& c:node.children) slCollectWaves(c,out,insideWave);
}
static void slWalkSoundNodes(const std::vector<SLKVNode>& nodes,
    std::map<std::string,std::vector<std::string>>& waves,
    std::map<std::string,std::string>& bases) {
    for(const auto& n:nodes) {
        const SLKVNode* base=slChild(n,"base");
        const SLKVNode* wave=slChild(n,"wave");
        const SLKVNode* rnd=slChild(n,"rndwave");
        const std::string key=slLower(n.key);
        if((base || wave || rnd) && key!="game_sounds_manifest" && key!="game_sounds") {
            std::vector<std::string> found;
            for(const auto& c:n.children) {
                if(slLower(c.key)=="wave" || slLower(c.key)=="rndwave") slCollectWaves(c,found,false);
            }
            if(!found.empty()) waves[key]=std::move(found);
            if(base && !base->value.empty()) bases[key]=slLower(base->value);
        }
        slWalkSoundNodes(n.children,waves,bases);
    }
}
static std::string slNormalizeSoundPath(std::string p) {
    std::replace(p.begin(),p.end(),'\\','/');
    while(!p.empty() && (p.front()=='/' || p.front()=='\\')) p.erase(p.begin());
    std::string low=slLower(p);
    if(low.rfind("sound/",0)==0) p=p.substr(6);
    return p;
}
}

Sound_L& Sound_L::Get() { static Sound_L s; return s; }
Sound_L::~Sound_L() { shutdown(); }

void Sound_L::init(const std::string& gameDir, Texture_L* fileSystem) {
    shutdown();
    gameDirectory = gameDir;
    textureFS = fileSystem;
    soundScriptWaves.clear(); soundScriptBases.clear(); soundScriptsLoaded=false;
    loadSoundScripts();
    reverbL.assign(44100, 0.0f);
    reverbR.assign(44100, 0.0f);
    dampL.assign(44100, 0.0f);
    dampR.assign(44100, 0.0f);
    reverbIndex = 0;
    { std::lock_guard<std::mutex> lock(stateMutex); recentSounds.clear(); }
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

void Sound_L::setFileSystem(Texture_L* fileSystem) {
    textureFS = fileSystem;
    soundScriptsLoaded = false;
    soundScriptWaves.clear(); soundScriptBases.clear();
    loadSoundScripts();
}


void Sound_L::update(float) {}

void Sound_L::enqueue(const Request& req) {
    { std::lock_guard<std::mutex> lock(requestMutex); requests.push_back(req); }
    requestCv.notify_one();
}

bool Sound_L::readWavMemory(const std::vector<char>& bytes, WavData& out) {
    std::vector<unsigned char> data(bytes.begin(), bytes.end());
    if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0 || std::memcmp(data.data()+8, "WAVE", 4) != 0) return false;
    uint16_t tag = 0, channels = 0, bits = 0, blockAlign = 0, samplesPerBlock = 0;
    uint32_t rate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmSize = 0;
    std::vector<std::pair<int,int>> msAdpcmCoefficients;
    size_t p = 12;
    while (p + 8 <= data.size()) {
        const unsigned char* c = data.data() + p;
        const uint32_t n = slU32(c + 4);
        const size_t payload = p + 8;
        const size_t end = payload + (size_t)n;
        if (end > data.size()) break;
        if (std::memcmp(c, "fmt ", 4) == 0 && n >= 16) {
            tag = slU16(c+8); channels = slU16(c+10); rate = slU32(c+12);
            blockAlign = slU16(c+20); bits = slU16(c+22);
            if (tag == 0xfffe && n >= 40) {

                const uint32_t subtype = slU32(c+32);
                if (subtype <= 0xffffu) tag = (uint16_t)subtype;
            }
            if ((tag == 2 || tag == 0x11) && n >= 20) samplesPerBlock = slU16(c+26);
            if (tag == 2 && n >= 22) {
                const uint16_t coefficientCount = slU16(c+28);
                for (uint16_t ci=0; ci<coefficientCount && ci<32; ++ci) {
                    const size_t co = p + 8 + 22 + (size_t)ci*4;
                    if (co+4 > end) break;
                    const int16_t a = (int16_t)slU16(data.data()+co);
                    const int16_t b = (int16_t)slU16(data.data()+co+2);
                    msAdpcmCoefficients.emplace_back((int)a,(int)b);
                }
            }
        } else if (std::memcmp(c, "data", 4) == 0) {
            pcm = c + 8; pcmSize = n;
        }
        p = end + (n & 1u);
    }
    if (!pcm || (channels != 1 && channels != 2) || rate == 0) return false;

    std::vector<float> src;
    size_t srcFrames = 0;
    if (tag == 1 && (bits == 8 || bits == 16)) {
        srcFrames = pcmSize / ((bits / 8u) * channels);
        if (!srcFrames) return false;
        src.resize(srcFrames * channels);
        if (bits == 16) {
            for (size_t i=0; i<src.size(); ++i) {
                const size_t off=i*2;
                const int16_t sample=(int16_t)((uint16_t)pcm[off] | ((uint16_t)pcm[off+1]<<8));
                src[i]=(float)sample/32768.0f;
            }
        } else {
            for (size_t i=0; i<src.size(); ++i) src[i]=((float)pcm[i]-128.0f)/128.0f;
        }
    } else if (tag == 2 && bits == 4 && blockAlign >= (uint16_t)(7*channels)) {

        static const int adaptation[16]={230,230,230,230,307,409,512,614,768,614,512,409,307,230,230,230};
        if (msAdpcmCoefficients.empty()) msAdpcmCoefficients={{256,0},{512,-256},{0,0},{192,64},{240,0},{460,-208},{392,-232}};
        if (!samplesPerBlock) samplesPerBlock=(uint16_t)(((blockAlign-7*channels)*2/channels)+2);
        const size_t fullBlock=(size_t)blockAlign;
        auto readS16=[&](size_t off)->int { return (int)(int16_t)((uint16_t)pcm[off] | ((uint16_t)pcm[off+1]<<8)); };
        for (size_t bo=0; bo+fullBlock<=pcmSize; bo+=fullBlock) {
            const unsigned char* b=pcm+bo;
            size_t cursor=0;
            int predictor[2]={0,0}, delta[2]={16,16}, sample1[2]={0,0}, sample2[2]={0,0};
            for(int ch=0;ch<channels;++ch) predictor[ch]=b[cursor++];
            for(int ch=0;ch<channels;++ch){delta[ch]=std::max(16,readS16(bo+cursor));cursor+=2;}
            for(int ch=0;ch<channels;++ch){sample1[ch]=readS16(bo+cursor);cursor+=2;}
            for(int ch=0;ch<channels;++ch){sample2[ch]=readS16(bo+cursor);cursor+=2;}
            std::vector<int16_t> decoded[2];
            for(int ch=0;ch<channels;++ch){decoded[ch].reserve(samplesPerBlock);decoded[ch].push_back((int16_t)sample2[ch]);decoded[ch].push_back((int16_t)sample1[ch]);}
            auto decodeNibble=[&](int nib,int ch) {
                nib &= 15;
                const int signedNib=(nib>=8)?nib-16:nib;
                const int pi=std::clamp(predictor[ch],0,(int)msAdpcmCoefficients.size()-1);
                int predicted=(sample1[ch]*msAdpcmCoefficients[(size_t)pi].first+sample2[ch]*msAdpcmCoefficients[(size_t)pi].second)/256 + signedNib*delta[ch];
                predicted=std::clamp(predicted,-32768,32767);
                sample2[ch]=sample1[ch]; sample1[ch]=predicted;
                delta[ch]=std::max(16,adaptation[nib]*delta[ch]/256);
                if(decoded[ch].size()<samplesPerBlock) decoded[ch].push_back((int16_t)predicted);
            };
            while(cursor<fullBlock && decoded[0].size()<samplesPerBlock) {
                const unsigned char byte=b[cursor++];
                if(channels==1){decodeNibble(byte>>4,0);decodeNibble(byte&15,0);}
                else {decodeNibble(byte>>4,0);decodeNibble(byte&15,1);}
            }
            const size_t frames=std::min(decoded[0].size(),decoded[channels-1].size());
            for(size_t f=0;f<frames;++f)for(int ch=0;ch<channels;++ch)src.push_back((float)decoded[ch][f]/32768.0f);
        }
        srcFrames=src.size()/channels;
    } else if (tag == 0x11 && bits == 4 && blockAlign >= (uint16_t)(4*channels)) {

        static const int indexTable[16]={-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
        static const int stepTable[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5360,5897,6487,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
        if (!samplesPerBlock) samplesPerBlock=(uint16_t)(((blockAlign-4*channels)*2/channels)+1);
        auto decodeNibble=[&](int nib,int& predictor,int& index)->int16_t {
            const int step=stepTable[std::clamp(index,0,88)];
            int diff=step>>3;
            if(nib&1) diff+=step>>2;
            if(nib&2) diff+=step>>1;
            if(nib&4) diff+=step;
            predictor+=(nib&8)?-diff:diff; predictor=std::clamp(predictor,-32768,32767);
            index=std::clamp(index+indexTable[nib&15],0,88);
            return (int16_t)predictor;
        };
        const size_t fullBlock=(size_t)blockAlign;
        for(size_t bo=0;bo+fullBlock<=pcmSize;bo+=fullBlock){
            const unsigned char* b=pcm+bo;size_t cursor=0;
            int predictor[2]={0,0},index[2]={0,0};std::vector<int16_t> decoded[2];
            for(int ch=0;ch<channels;++ch){predictor[ch]=(int16_t)((uint16_t)b[cursor]|((uint16_t)b[cursor+1]<<8));cursor+=2;index[ch]=std::clamp((int)b[cursor++],0,88);++cursor;decoded[ch].reserve(samplesPerBlock);decoded[ch].push_back((int16_t)predictor[ch]);}
            if(channels==1){
                while(cursor<fullBlock && decoded[0].size()<samplesPerBlock){unsigned char byte=b[cursor++];decoded[0].push_back(decodeNibble(byte&15,predictor[0],index[0]));if(decoded[0].size()<samplesPerBlock)decoded[0].push_back(decodeNibble(byte>>4,predictor[0],index[0]));}
            } else {
                while(cursor<fullBlock && decoded[0].size()<samplesPerBlock){
                    for(int ch=0;ch<channels;++ch){
                        for(int group=0;group<4 && cursor<fullBlock;++group){unsigned char byte=b[cursor++];decoded[ch].push_back(decodeNibble(byte&15,predictor[ch],index[ch]));decoded[ch].push_back(decodeNibble(byte>>4,predictor[ch],index[ch]));}
                    }
                    const size_t frames=std::min(decoded[0].size(),decoded[1].size());
                    if(frames>=samplesPerBlock)break;
                }
            }
            const size_t frames=std::min(decoded[0].size(),decoded[channels-1].size());
            for(size_t f=0;f<frames;++f)for(int ch=0;ch<channels;++ch)src.push_back((float)decoded[ch][f]/32768.0f);
        }
        srcFrames=src.size()/channels;
    } else {
        return false;
    }
    if (!srcFrames || src.empty()) return false;
    const double ratio = double(rate) / 44100.0;
    const size_t dstFrames = std::max<size_t>(1, size_t(std::ceil(double(srcFrames) / ratio)));
    out.sampleRate = 44100;
    out.channels = channels;
    out.samples.resize(dstFrames * channels);
    for (size_t i = 0; i < dstFrames; ++i) {
        const double sp = double(i) * ratio;
        const size_t a = std::min(srcFrames - 1, size_t(std::floor(sp)));
        const size_t b = std::min(srcFrames - 1, a + 1);
        const float frac = float(sp - std::floor(sp));
        for (int ch = 0; ch < channels; ++ch) out.samples[i*channels+ch] = src[a*channels+ch] + (src[b*channels+ch] - src[a*channels+ch]) * frac;
    }
    return true;
}

void Sound_L::parseSoundScript(const std::string& filename, const std::string& text) {
    if(text.empty()) return;
    const auto tokens=slKVTokens(text);
    size_t at=0;
    const auto nodes=slParseKVLevel(tokens,at);
    slWalkSoundNodes(nodes,soundScriptWaves,soundScriptBases);
    (void)filename;
}

void Sound_L::loadSoundScripts() {
    if(soundScriptsLoaded) return;
    soundScriptsLoaded=true;
    std::vector<std::string> files={
        "scripts/game_sounds_manifest.txt",
        "scripts/game_sounds.txt",
        "scripts/game_sounds_weapons.txt",
        "scripts/game_sounds_weapons2.txt",
        "scripts/game_sounds_player.txt",
        "scripts/game_sounds_items.txt",
        "scripts/game_sounds_physics.txt",
        "scripts/game_sounds_world.txt",
        "scripts/game_sounds_vehicles.txt",
        "scripts/game_sounds_ambient_generic.txt",
        "scripts/game_sounds_npc.txt",
        "scripts/game_sounds_npc_combine.txt",
        "scripts/game_sounds_npc_zombie.txt",
        "scripts/game_sounds_vo.txt",
        "scripts/game_sounds_ui.txt",
        "scripts/game_sounds_misc.txt",
        "scripts/game_sounds_music.txt",
        "scripts/game_sounds_flesh.txt",
        "scripts/game_sounds_player_voice.txt"
    };
    auto readText=[&](const std::string& name)->std::string {
        std::vector<char> raw;
        if(textureFS && textureFS->getFileRawData(name,raw) && !raw.empty()) return std::string(raw.begin(),raw.end());
        std::string disk=gameDirectory;
        if(!disk.empty() && disk.back()!='/' && disk.back()!='\\') disk += '/';
        disk += name;
        std::ifstream f(FsCompat::fromUtf8(disk),std::ios::binary);
        if(!f) {

            const size_t slash=name.find('/');
            if(slash!=std::string::npos) {
                std::string fallback=gameDirectory + "/" + name.substr(slash+1);
                std::ifstream alt(FsCompat::fromUtf8(fallback),std::ios::binary);
                if(alt) return std::string((std::istreambuf_iterator<char>(alt)),std::istreambuf_iterator<char>());
            }
            return {};
        }
        return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    };
    const std::string manifest=readText("scripts/game_sounds_manifest.txt");
    if(!manifest.empty()) {
        auto tok=slKVTokens(manifest); size_t i=0; auto tree=slParseKVLevel(tok,i);
        std::function<void(const std::vector<SLKVNode>&)> collect=[&](const auto& nodes){
            for(const auto& n:nodes) {
                const std::string k=slLower(n.key);
                if((k.find("precache_file")!=std::string::npos || k.find("preload_file")!=std::string::npos || k=="file") && !n.value.empty()) {
                    std::string f=n.value; std::replace(f.begin(),f.end(),'\\','/');
                    if(f.rfind("scripts/",0)!=0) f="scripts/"+f;
                    if(f.size()>4 && slLower(f.substr(f.size()-4))==".txt") files.push_back(f);
                }
                collect(n.children);
            }
        };
        collect(tree);
    }
    std::sort(files.begin(),files.end());
    files.erase(std::unique(files.begin(),files.end()),files.end());
    for(const auto& f:files) parseSoundScript(f,readText(f));
    std::cout << "[Sound] Loaded " << soundScriptWaves.size() << " Source soundscript aliases" << std::endl;
}

std::string Sound_L::resolveSoundName(const std::string& input, int depth) const {
    if(input.empty() || depth>8) return input;
    std::string key=slLower(input);
    std::replace(key.begin(),key.end(),'\\','/');
    while(!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
    while(!key.empty() && std::isspace((unsigned char)key.front())) key.erase(key.begin());
    if(!key.empty() && key.front()=='!') key.erase(key.begin());

    if(key.find('/')!=std::string::npos || key.find('\\')!=std::string::npos ||
       (key.size()>4 && key.substr(key.size()-4)==".wav") ||
       (key.size()>4 && key.substr(key.size()-4)==".mp3") ||
       (key.size()>4 && key.substr(key.size()-4)==".ogg")) return slNormalizeSoundPath(input);
    auto it=soundScriptWaves.find(key);
    if(it!=soundScriptWaves.end() && !it->second.empty()) {
        const size_t n=(size_t)(std::rand()%it->second.size());
        return slNormalizeSoundPath(it->second[n]);
    }
    auto bi=soundScriptBases.find(key);
    if(bi!=soundScriptBases.end()) return resolveSoundName(bi->second,depth+1);
    return slNormalizeSoundPath(input);
}

std::shared_ptr<Sound_L::WavData> Sound_L::loadWav(const std::string& relative) {
    std::string key = relative;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    std::replace(key.begin(), key.end(), '\\', '/');
    key = slNormalizeSoundPath(key);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    auto w = std::make_shared<WavData>();
    bool ok = false;
    std::vector<std::string> candidates;
    candidates.push_back(key);
    if (key.rfind("sound/",0)==0) candidates.push_back(key.substr(6));
    else candidates.push_back("sound/" + key);
    if (key.find('.')==std::string::npos) {
        candidates.push_back(key+".wav");
        candidates.push_back("sound/"+key+".wav");
    }
    for (const std::string& candidate : candidates) {
        if (textureFS) {
            std::vector<char> raw;
            if (textureFS->getFileRawData(candidate, raw) && readWavMemory(raw, *w)) { ok=true; break; }
        }
        std::string path = gameDirectory + "/" + candidate;
        std::replace(path.begin(), path.end(), '/', '\\');
        if (readWavFile(path, *w)) { ok=true; break; }
    }
    if (!ok) return {};
    cache[key] = w;
    return w;
}

bool Sound_L::readWavFile(const std::string& path, WavData& out) {
    std::ifstream f(FsCompat::fromUtf8(path), std::ios::binary);
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
            if (v.loop && frames) { v.cursor = std::fmod(v.cursor, double(frames)); ++it; }
            else it = voices.erase(it);
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
    if (spatial && volume > 0.05f) {
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(stateMutex);
        while (!recentSounds.empty() && now - recentSounds.front().time > 2.0) recentSounds.pop_front();
        recentSounds.push_back(SoundEvent{pos,std::clamp(volume,0.0f,4.0f),now});
    }
    Request r; r.type=Request::OneShot; r.path=resolveSoundName(path); r.position=pos; r.volume=volume; r.spatial=spatial; r.reverb=allowReverb; enqueue(r);
}
void Sound_L::getRecentSounds(std::vector<SoundEvent>& out, double maxAge) const {
    out.clear();
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lock(stateMutex);
    for (const auto& sound : recentSounds) {
        if (now - sound.time <= maxAge) out.push_back(sound);
    }
}
void Sound_L::playRandom(const std::vector<std::string>& paths, const Vec3& pos, float volume, bool spatial, bool allowReverb) {
    if (paths.empty()) return;
    play(paths[(size_t)(std::rand()%paths.size())], pos, volume, spatial, allowReverb);
}
unsigned int Sound_L::playLoop(const std::string& path, const Vec3& pos, float volume, bool spatial) {
    unsigned int h = nextLoop.fetch_add(1);
    Request r; r.type=Request::LoopStart; r.path=resolveSoundName(path); r.position=pos; r.volume=volume; r.spatial=spatial; r.reverb=true; r.handle=h; enqueue(r); return h;
}
void Sound_L::stopLoop(unsigned int h) { if (!h) return; Request r; r.type=Request::LoopStop; r.handle=h; enqueue(r); }
