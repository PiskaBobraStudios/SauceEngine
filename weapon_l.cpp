#include "weapon_l.h"
#include "bsp_l.h"
#include "prop_physics.h"
#include "sound_l.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <set>
#include <filesystem>
#include <vector>
#include <iterator>
#include <cstdlib>
#include <GLFW/glfw3.h>

static void wlShowEngineError(const char* message) {
    MessageBoxA(nullptr, message, "Engine Error", MB_ICONERROR | MB_OK);
}

static std::string wlLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

static std::string wlNormalize(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static bool wlFileExists(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return (bool)f;
}

static std::string wlValueCI(const std::string& text, const std::string& wanted) {
    const std::string key = wlLower(wanted);
    size_t p = 0;
    while ((p = text.find('"', p)) != std::string::npos) {
        size_t e = text.find('"', p + 1);
        if (e == std::string::npos) break;
        std::string name = wlLower(text.substr(p + 1, e - p - 1));
        p = e + 1;
        if (name != key) continue;
        size_t q = text.find('"', p);
        if (q == std::string::npos) break;
        size_t qe = text.find('"', q + 1);
        if (qe == std::string::npos) break;
        return text.substr(q + 1, qe - q - 1);
    }
    return {};
}

static int wlInt(const std::string& text, const std::string& key, int def) {
    std::string v = wlValueCI(text, key);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

static float wlFloat(const std::string& text, const std::string& key, float def) {
    std::string v = wlValueCI(text, key);
    if (v.empty()) return def;
    try { return std::stof(v); } catch (...) { return def; }
}

static void wlRenderRotation(const Vec3& a) {
    glRotatef(a.y, 0, 1, 0);
    glRotatef(-a.x, 1, 0, 0);
    glRotatef(a.z, 0, 0, 1);
    glRotatef(-90.0f, 1, 0, 0);
}

static bool wlRayAABB(const Vec3& o, const Vec3& d, const Vec3& mn, const Vec3& mx, float& t) {
    float tMin = 0.0f;
    float tMax = 1e30f;
    const float O[3] = {o.x,o.y,o.z};
    const float D[3] = {d.x,d.y,d.z};
    const float MN[3] = {mn.x,mn.y,mn.z};
    const float MX[3] = {mx.x,mx.y,mx.z};
    for (int i=0;i<3;++i) {
        if (std::abs(D[i]) < 1e-8f) {
            if (O[i] < MN[i] || O[i] > MX[i]) return false;
            continue;
        }
        float a=(MN[i]-O[i])/D[i], b=(MX[i]-O[i])/D[i];
        if (a>b) std::swap(a,b);
        tMin=std::max(tMin,a);
        tMax=std::min(tMax,b);
        if (tMin>tMax) return false;
    }
    if (tMax < 0.0f) return false;
    t = std::max(0.0f, tMin);
    return true;
}

static Vec3 wlCross(const Vec3& a, const Vec3& b) {
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}

static Vec3 wlNormalize(const Vec3& v) {
    float l=v.length();
    return l>0.0001f ? v*(1.0f/l) : Vec3{0,1,0};
}

static Vec3 wlInverseModelPoint(const Vec3& world, const Vec3& origin, const Vec3& angles, float scale) {
    Vec3 v=(world-origin)*(1.0f/std::max(scale,0.001f));
    const float r=3.14159265358979323846f/180.0f;
    float c,s,x,y,z;
    y=-v.z; z=v.y; v.y=y; v.z=z;
    c=std::cos(-angles.z*r); s=std::sin(-angles.z*r);
    x=v.x*c-v.y*s; y=v.x*s+v.y*c; v.x=x; v.y=y;
    c=std::cos(angles.x*r); s=std::sin(angles.x*r);
    y=v.y*c-v.z*s; z=v.y*s+v.z*c; v.y=y; v.z=z;
    c=std::cos(-angles.y*r); s=std::sin(-angles.y*r);
    x=v.x*c-v.z*s; z=v.x*s+v.z*c; v.x=x; v.z=z;
    return v;
}



static std::vector<Weapon_L*> gWeaponInstances;
static std::map<int,std::string> gSelectedWeaponBySlot;
static std::map<int,bool> gSlotKeyWasDown;
static std::map<int,int> gSlotCycleCursor;

static int wlKeyForSlot(int slot){
    switch(slot){ case 1:return GLFW_KEY_1; case 2:return GLFW_KEY_2; case 3:return GLFW_KEY_3; case 4:return GLFW_KEY_4; case 5:return GLFW_KEY_5; case 6:return GLFW_KEY_6; case 7:return GLFW_KEY_7; case 8:return GLFW_KEY_8; case 9:return GLFW_KEY_9; default:return -1; }
}

static bool wlIsKeyDown(int key){
    if(key<0) return false;
    GLFWwindow* w=glfwGetCurrentContext();
    return w && glfwGetKey(w,key)==GLFW_PRESS;
}

static void wlCycleSlot(Camera& player,int slot){
    std::vector<Weapon_L*> list;
    std::set<std::string> seen;
    for(Weapon_L* w:gWeaponInstances){
        if(!w) continue;
        if(w->definition.slot!=slot) continue;
        if(!player.hasWeapon(w->definition.className)) continue;
        if(!seen.insert(w->definition.className).second) continue;
        list.push_back(w);
    }
    std::sort(list.begin(),list.end(),[](const Weapon_L* a,const Weapon_L* b){
        if(a->definition.bucketPosition!=b->definition.bucketPosition) return a->definition.bucketPosition<b->definition.bucketPosition;
        return a->definition.className<b->definition.className;
    });
    if(list.empty()) return;
    int cursor=0;
    const auto previous=gSelectedWeaponBySlot.find(slot);
    if(player.activeWeaponSlot==slot && previous!=gSelectedWeaponBySlot.end() && !previous->second.empty()){
        for(size_t i=0;i<list.size();++i){
            if(list[i]->definition.className==previous->second){ cursor=(int)((i+1)%list.size()); break; }
        }
    }
    gSlotCycleCursor[slot]=cursor;
    gSelectedWeaponBySlot[slot]=list[(size_t)cursor]->definition.className;
    player.activeWeaponSlot=slot;
}

static void wlHandleSlotKeys(Camera& player){
    if(glfwGetCurrentContext()==nullptr) return;
    for(int slot=1;slot<=9;++slot){
        const int key=wlKeyForSlot(slot);
        if(key<0) continue;
        const bool down=wlIsKeyDown(key);
        const bool was=gSlotKeyWasDown[slot];
        if(down && !was){
            bool any=false;
            for(Weapon_L* w:gWeaponInstances) if(w && w->pickedUp && w->definition.slot==slot && player.hasWeapon(w->definition.className)){ any=true; break; }
            if(any) wlCycleSlot(player,slot);
        }
        gSlotKeyWasDown[slot]=down;
    }
}

struct WLKVNode {
    std::string name;
    std::map<std::string, std::vector<std::string>> values;
    std::vector<WLKVNode> children;
};

static std::vector<std::string> wlTokens(const std::string& text) {
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (std::isspace(c)) { ++i; continue; }
        if (text[i]=='/' && i+1<text.size() && text[i+1]=='/') {
            i += 2; while (i < text.size() && text[i] != '\n') ++i; continue;
        }
        if (text[i]=='{') { out.emplace_back("{"); ++i; continue; }
        if (text[i]=='}') { out.emplace_back("}"); ++i; continue; }
        if (text[i]=='"') {
            ++i; std::string v;
            while (i < text.size()) {
                if (text[i]=='\\' && i+1<text.size()) { v.push_back(text[i+1]); i += 2; continue; }
                if (text[i]=='"') { ++i; break; }
                v.push_back(text[i++]);
            }
            out.push_back(std::move(v));
            continue;
        }
        size_t j=i;
        while (j<text.size() && !std::isspace((unsigned char)text[j]) && text[j]!='{' && text[j]!='}') ++j;
        out.push_back(text.substr(i,j-i));
        i=j;
    }
    return out;
}

static WLKVNode wlParseNode(const std::vector<std::string>& t, size_t& i, const std::string& name) {
    WLKVNode n; n.name=wlLower(name);
    if (i < t.size() && t[i] == "{") ++i;
    while (i < t.size()) {
        if (t[i] == "}") { ++i; break; }
        std::string key=t[i++];
        if (i >= t.size()) break;
        if (t[i] == "{") {
            n.children.push_back(wlParseNode(t, i, key));
        } else if (t[i] != "}") {
            n.values[wlLower(key)].push_back(t[i++]);
        }
    }
    return n;
}

static std::vector<WLKVNode> wlParseTopBlocks(const std::string& text) {
    const auto t=wlTokens(text);
    std::vector<WLKVNode> out;
    size_t i=0;
    while (i<t.size()) {
        if (t[i].empty() || t[i]=="{") { ++i; continue; }
        std::string name=t[i++];
        if (i<t.size() && t[i]=="{") out.push_back(wlParseNode(t,i,name));
        else if (i<t.size()) ++i;
    }
    return out;
}

static std::string wlNodeValue(const WLKVNode& n, const char* key) {
    auto it=n.values.find(wlLower(key));
    return (it==n.values.end() || it->second.empty()) ? std::string() : it->second.front();
}

static void wlReadWeaponSoundData(const WLKVNode& weaponNode, std::map<std::string,std::string>& out) {
    for (const auto& child : weaponNode.children) {
        if (wlLower(child.name) != "sounddata" && wlLower(child.name) != "sound_data") continue;
        for (const auto& kv : child.values) {
            if (kv.second.empty()) continue;
            out[kv.first] = kv.second.front();
        }
    }
}

static void wlCollectSoundWaves(const WLKVNode& n, std::vector<std::string>& out) {
    auto add=[&](const std::string& s){
        if(s.empty()) return;
        std::string v=wlNormalize(s);
        if(v.rfind("sound/",0)==0) v=v.substr(6);
        if(!v.empty()) out.push_back(v);
    };
    auto w=n.values.find("wave");
    if(w!=n.values.end()) for(const auto& x:w->second) add(x);
    for(const auto& c:n.children) wlCollectSoundWaves(c,out);
}

static std::map<std::string,std::vector<std::string>> gSoundEvents;
static std::string gSoundEventsGameDir;

static void wlLoadSoundScripts(const std::string& gameDir, Texture_L* fileSystem) {
    if(!gSoundEvents.empty() && wlLower(gSoundEventsGameDir)==wlLower(gameDir)) return;
    gSoundEventsGameDir=gameDir;
    gSoundEvents.clear();
    auto ingest=[&](const std::string& raw){
        if(raw.empty()) return;
        for(const auto& n:wlParseTopBlocks(raw)){
            std::vector<std::string> waves;
            wlCollectSoundWaves(n,waves);
            if(waves.empty()) continue;
            auto &dst=gSoundEvents[wlLower(n.name)];
            for(const auto& w:waves){
                if(std::find(dst.begin(),dst.end(),w)==dst.end()) dst.push_back(w);
            }
        }
    };
    const char* files[]={
        "scripts/game_sounds_weapons.txt",
        "scripts/game_sounds.txt",
        "scripts/game_sounds_player.txt",
        "scripts/game_sounds_items.txt"
    };
    for(const char* rel:files){
        std::vector<char> raw;
        if(fileSystem && fileSystem->getFileRawData(rel,raw) && !raw.empty() && raw.size()<=8u*1024u*1024u){
            ingest(std::string(raw.begin(),raw.end()));
            continue;
        }
        std::ifstream f(std::filesystem::path(gameDir)/rel,std::ios::binary);
        if(!f) continue;
        f.seekg(0,std::ios::end); std::streamoff sz=f.tellg(); f.seekg(0,std::ios::beg);
        if(sz<=0 || sz>8*1024*1024) continue;
        std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
        ingest(text);
    }
}

static std::string wlCanonicalWeaponClass(std::string value) {
    value=wlLower(value);
    if(value.rfind("weapon_",0)==0) return value;
    if(value.rfind("cweapon",0)==0) {
        value=value.substr(7);
        if(value.rfind("base",0)==0) return {};
        return "weapon_"+value;
    }
    if(value.rfind("c_weapon_",0)==0) return "weapon_"+value.substr(9);
    return {};
}

static void wlAbsorbDllText(const std::string& text, const std::string& path, bool server, std::map<std::string,WeaponSDKKnowledge>& out) {
    auto inspect=[&](const std::string& rawKey,size_t pos){
        const std::string key=wlCanonicalWeaponClass(rawKey); if(key.empty()) return;
        WeaponSDKKnowledge& k=out[key]; k.className=key;
        if(server) k.serverDllPath=path; else k.clientDllPath=path;
        const size_t a=(pos>3072)?pos-3072:0, b=std::min(text.size(),pos+3072);
        const std::string ctx=wlLower(text.substr(a,b-a));
        const std::string shortName=key.substr(7);
        k.hasPrimaryAttack=true;
        if(ctx.find("secondaryattack")!=std::string::npos) k.hasSecondaryAttack=true;
        if(ctx.find("reload")!=std::string::npos) k.hasReload=true;
        if(ctx.find("firebullets")!=std::string::npos) k.hasFireBullets=true;
        if(ctx.find("domuzzleflash")!=std::string::npos || ctx.find("muzzleflash")!=std::string::npos) k.hasMuzzleFlash=true;
        if(ctx.find("viewpunch")!=std::string::npos || ctx.find("addviewkick")!=std::string::npos) k.hasViewPunch=true;
        if(ctx.find("createprojectile")!=std::string::npos || ctx.find("createentitybyname")!=std::string::npos || ctx.find("grenade")!=std::string::npos) k.hasProjectileCreation=true;
        if(ctx.find("act_vm_")!=std::string::npos) k.hasViewModelActivities=true;
        if(ctx.find("m_iclip1")!=std::string::npos || ctx.find("m_iclip1--")!=std::string::npos) k.usesClip=true;
        if(ctx.find("getmaxclip1")!=std::string::npos || ctx.find("getdefaultclip1")!=std::string::npos) k.usesClip=true;
        if(ctx.find("chlmachinegun")!=std::string::npos || ctx.find("chlselectfiremachinegun")!=std::string::npos || ctx.find("m_ishotsfired")!=std::string::npos) k.explicitAutomatic=true;
        if(ctx.find("baseclass::primaryattack")!=std::string::npos) k.hasBasePrimaryAttack=true;
    };
    auto scanNames=[&](const std::string& raw){
        for(size_t i=0;i<raw.size();) {
            size_t p=raw.find("weapon_",i);
            if(p==std::string::npos) break;
            size_t e=p+7;
            while(e<raw.size() && ((raw[e]>='a'&&raw[e]<='z')||(raw[e]>='A'&&raw[e]<='Z')||(raw[e]>='0'&&raw[e]<='9')||raw[e]=='_')) ++e;
            if(e>p+7) inspect(raw.substr(p,e-p),p);
            i=e;
        }
        for(size_t i=0;i<raw.size();) {
            size_t p=raw.find("cweapon",i);
            if(p==std::string::npos) break;
            size_t e=p+7;
            while(e<raw.size() && ((raw[e]>='a'&&raw[e]<='z')||(raw[e]>='A'&&raw[e]<='Z')||(raw[e]>='0'&&raw[e]<='9')||raw[e]=='_')) ++e;
            if(e>p+7) inspect(raw.substr(p,e-p),p);
            i=e;
        }
    };
    scanNames(text);
}

static std::string wlExtractPrintable(const std::vector<unsigned char>& bytes, size_t start, size_t maxLen) {
    std::string s;
    const size_t end=std::min(bytes.size(),start+maxLen);
    for(size_t i=start;i<end;++i) {
        unsigned char c=bytes[i];
        if(c>=32 && c<=126) s.push_back((char)c); else if(!s.empty()) s.push_back(' ');
    }
    return s;
}

static void wlScanDll(const std::string& path,bool server,std::map<std::string,WeaponSDKKnowledge>& out) {
    std::error_code ec;
    if(!std::filesystem::exists(path,ec)) return;
    const uintmax_t size=std::filesystem::file_size(path,ec);
    if(ec || size==0 || size>128u*1024u*1024u) return;
    std::ifstream f(path,std::ios::binary); if(!f) return;
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    if(bytes.empty()) return;
    std::string all((const char*)bytes.data(),bytes.size());
    wlAbsorbDllText(all,path,server,out);
    std::string utf8like;
    utf8like.reserve(bytes.size()/2);
    for(size_t i=0;i+1<bytes.size();i+=2) {
        const unsigned char c=bytes[i];
        utf8like.push_back((c>=32 && c<=126 && bytes[i+1]==0)?(char)c:' ');
    }
    wlAbsorbDllText(utf8like,path,server,out);
}

static std::vector<std::filesystem::path> wlSourceRoots(const std::string& gameDir) {
    std::filesystem::path g(gameDir);
    return {
        g/"src/game/server/hl2",
        g/"src/game/client/hl2",
        g/"src/game/shared",
        g/"../src/game/server/hl2",
        g/"../src/game/client/hl2",
        g/"../src/game/shared",
        g/"../../src/game/server/hl2",
        g/"../../src/game/client/hl2",
        g/"../../src/game/shared",
        std::filesystem::current_path()/"src/game/server/hl2",
        std::filesystem::current_path()/"src/game/client/hl2",
        std::filesystem::current_path()/"src/game/shared"
    };
}

static float wlReturnFloat(const std::string& text, const char* fn, float def) {
    std::string low=wlLower(text);
    size_t p=0;
    while((p=low.find(fn,p))!=std::string::npos){
        size_t brace=low.find('{',p);
        if(brace==std::string::npos) break;
        size_t semi=low.find('}',brace);
        if(semi==std::string::npos) semi=std::min(low.size(),brace+600);
        std::string body=low.substr(brace,semi-brace);
        size_t r=body.find("return");
        if(r!=std::string::npos){
            r+=6;
            while(r<body.size() && std::isspace((unsigned char)body[r])) ++r;
            size_t e=r; while(e<body.size() && (std::isdigit((unsigned char)body[e])||body[e]=='-'||body[e]=='+'||body[e]=='.')) ++e;
            if(e>r){ try { return std::stof(body.substr(r,e-r)); } catch(...) {} }
        }
        p=brace+1;
    }
    return def;
}

static int wlReturnInt(const std::string& text, const char* fn, int def) {
    float f=wlReturnFloat(text,fn,(float)def);
    return (f==(float)def)?def:(int)f;
}

static void wlAddHint(std::vector<std::string>& v,const std::string& x){
    if(x.empty()) return;
    std::string q=wlLower(x);
    if(std::find(v.begin(),v.end(),q)==v.end()) v.push_back(q);
}


static void wlExtractActivitiesFromFunction(const std::string& text,const std::string& functionName,std::vector<std::string>& out){
    const std::string low=wlLower(text);
    size_t p=0;
    const std::string needle=wlLower(functionName);
    while((p=low.find(needle,p))!=std::string::npos){
        size_t b=low.find('{',p);
        if(b==std::string::npos) break;
        int depth=0;
        size_t e=b;
        for(;e<low.size();++e){
            if(low[e]=='{') ++depth;
            else if(low[e]=='}'){
                --depth;
                if(depth==0){ ++e; break; }
            }
        }
        if(e<=b) break;
        const std::string body=low.substr(b,e-b);
        const char* acts[]={
            "act_vm_primaryattack","act_vm_primaryattack2","act_vm_recoil1","act_vm_recoil2","act_vm_recoil3",
            "act_vm_secondaryattack","act_vm_reload","act_vm_draw","act_vm_idle","act_vm_dryfire",
            "act_shotgun_reload_start","act_shotgun_reload_finish","act_shotgun_pump",
            "act_vm_pullback_high","act_vm_throw","act_vm_throw2","act_vm_throw3"
        };
        for(const char* a:acts){
            if(body.find(a)!=std::string::npos) wlAddHint(out,a);
        }
        static const char* returnNeedles[]={"return ","sendweaponanim("};
        for(const char* rn:returnNeedles){
            size_t q=0;
            while((q=body.find(rn,q))!=std::string::npos){
                size_t r=q+(std::string(rn).size());
                size_t end=body.find_first_of(";\n",r);
                if(end==std::string::npos) end=body.size();
                std::string expr=body.substr(r,end-r);
                const std::string vm="act_vm_";
                size_t a=expr.find(vm);
                if(a!=std::string::npos){
                    size_t z=a;
                    while(z<expr.size() && (std::isalnum((unsigned char)expr[z])||expr[z]=='_')) ++z;
                    wlAddHint(out,expr.substr(a,z-a));
                }
                q=end;
            }
        }
        p=e;
    }
}

static void wlExtractWeaponSoundAliases(const std::string& text,WeaponSDKKnowledge& k){
    const std::string low=wlLower(text);
    const std::pair<const char*,const char*> pairs[]={
        {"single_npc","single_npc"},
        {"single","single_shot"},
        {"reload","reload"},
        {"empty","empty"},
        {"special1","special1"},
        {"special2","special2"},
        {"special3","special3"},
        {"melee_hit","melee_hit"},
        {"melee_miss","melee_miss"},
        {"wpn_double","double_shot"}
    };
    size_t p=0;
    while((p=low.find("weaponsound",p))!=std::string::npos){
        size_t l=low.find('(',p);
        size_t r=low.find(')',l==std::string::npos? p:l);
        if(l==std::string::npos || r==std::string::npos){ p+=11; continue; }
        std::string arg=low.substr(l+1,r-l-1);
        for(const auto& pr:pairs){
            if(arg.find(pr.first)!=std::string::npos){
                k.sourceSoundAliases[pr.first]=pr.second;
                break;
            }
        }
        p=r+1;
    }
    size_t q=0;
    while((q=low.find("precachescriptsound",q))!=std::string::npos){
        size_t a=low.find('(',q), b=low.find(')',a==std::string::npos?q:a);
        if(a!=std::string::npos && b!=std::string::npos){
            std::string raw=text.substr(a+1,b-a-1);
            size_t s=raw.find_first_of("\"'");
            if(s!=std::string::npos){
                size_t e=raw.find_first_of("\"'",s+1);
                if(e!=std::string::npos){
                    std::string ev=wlLower(raw.substr(s+1,e-s-1));
                    if(ev.rfind("weapon_",0)==0 || ev.find("weapon")!=std::string::npos){
                        size_t dot=ev.find('.');
                        if(dot!=std::string::npos){
                            std::string key=ev.substr(dot+1);
                            if(key=="single"||key=="single_npc") k.sourceSoundAliases["single_shot"]=ev;
                            else if(key=="reload") k.sourceSoundAliases["reload"]=ev;
                            else if(key=="empty") k.sourceSoundAliases["empty"]=ev;
                            else if(key=="special"||key=="special1") k.sourceSoundAliases["special1"]=ev;
                        }
                    }
                }
            }
        }
        q=b==std::string::npos?q+19:b+1;
    }
}

static void wlApplySuppliedWeaponProfile(WeaponSDKKnowledge& k);

static void wlAnalyzeSourceText(const std::string& text,const std::string& path,WeaponSDKKnowledge& k){
    const std::string low=wlLower(text);
    if(std::find(k.sourceFiles.begin(),k.sourceFiles.end(),path)==k.sourceFiles.end()) k.sourceFiles.push_back(path);

    std::string detectedBase;
    size_t dc=0;
    while((dc=low.find("declare_class",dc))!=std::string::npos){
        size_t lb=low.find('(',dc), comma=low.find(',',lb==std::string::npos?dc:lb);
        if(lb!=std::string::npos && comma!=std::string::npos){
            size_t q=comma+1; while(q<low.size() && std::isspace((unsigned char)low[q])) ++q;
            size_t e=q; while(e<low.size() && (std::isalnum((unsigned char)low[e])||low[e]=='_')) ++e;
            if(e>q) { detectedBase=text.substr(q,e-q); break; }
        }
        dc+=13;
    }
    size_t cpos=0;
    while((cpos=low.find("class ",cpos))!=std::string::npos){
        size_t nameBegin=cpos+6; while(nameBegin<low.size() && std::isspace((unsigned char)low[nameBegin])) ++nameBegin;
        size_t nameEnd=nameBegin; while(nameEnd<low.size() && (std::isalnum((unsigned char)low[nameEnd])||low[nameEnd]=='_')) ++nameEnd;
        size_t colon=low.find(':',nameEnd);
        if(colon!=std::string::npos && colon<nameEnd+220){
            size_t pub=low.find("public",colon);
            if(pub!=std::string::npos && pub<colon+120){
                std::string cls=low.substr(nameBegin,nameEnd-nameBegin);
                if(cls.find("weapon")!=std::string::npos || cls.find("crowbar")!=std::string::npos || cls.find("crossbow")!=std::string::npos){
                    size_t q=pub+6; while(q<low.size() && (std::isspace((unsigned char)low[q])||low[q]==',')) ++q;
                    size_t e=q; while(e<low.size() && (std::isalnum((unsigned char)low[e])||low[e]=='_')) ++e;
                    if(e>q) detectedBase=text.substr(q,e-q);
                }
            }
        }
        cpos=nameEnd;
    }
    if(!detectedBase.empty()) k.baseClass=detectedBase;

    const std::string bc=wlLower(k.baseClass);
    k.hasMachineGunBase |= bc.find("chlmachinegun")!=std::string::npos || bc.find("chlselectfiremachinegun")!=std::string::npos || bc.find("cweaponmachinegun")!=std::string::npos;
    k.hasShotgunSignature |= low.find("cweaponshotgun")!=std::string::npos || low.find("act_shotgun_reload")!=std::string::npos || low.find("act_shotgun_pump")!=std::string::npos || low.find("m_bneedpump")!=std::string::npos || (low.find("startreload")!=std::string::npos && low.find("pump")!=std::string::npos);
    k.hasPump |= low.find("pump(")!=std::string::npos || low.find("act_shotgun_pump")!=std::string::npos || low.find("needpump")!=std::string::npos;
    k.hasProjectileSignature |= low.find("createprojectile")!=std::string::npos || low.find("createcombineball")!=std::string::npos || low.find("cmissile::create")!=std::string::npos || low.find("createentitybyname")!=std::string::npos || low.find("fireprojectile")!=std::string::npos || low.find("throwgrenade")!=std::string::npos || low.find("firebolt")!=std::string::npos || low.find("launchmissile")!=std::string::npos;
    k.hasProjectileCreation |= k.hasProjectileSignature;
    k.hasFireBullets |= low.find("firebullets")!=std::string::npos;
    k.hasPrimaryAttack |= low.find("primaryattack")!=std::string::npos;
    k.hasSecondaryAttack |= low.find("secondaryattack")!=std::string::npos;
    k.hasReload |= low.find("reload")!=std::string::npos;
    k.hasStartReload |= low.find("startreload")!=std::string::npos;
    k.hasGetFireRate |= low.find("getfirerate")!=std::string::npos;
    k.hasGetBulletSpread |= low.find("getbulletspread")!=std::string::npos;
    k.hasSendWeaponAnim |= low.find("sendweaponanim")!=std::string::npos || low.find("sendviewmodelanim")!=std::string::npos;
    k.hasViewModelActivities |= low.find("act_vm_")!=std::string::npos || k.hasSendWeaponAnim;
    k.hasMuzzleFlash |= low.find("domuzzleflash")!=std::string::npos || low.find("muzzleflash")!=std::string::npos;
    k.hasViewPunch |= low.find("viewpunch")!=std::string::npos || low.find("addviewkick")!=std::string::npos || low.find("domachinegunkick")!=std::string::npos;
    k.usesClip |= low.find("m_iclip1")!=std::string::npos || low.find("getmaxclip1")!=std::string::npos || low.find("getdefaultclip1")!=std::string::npos || low.find("usesclipsforammo1")!=std::string::npos;
    k.sourceUsesPrimaryAmmo |= low.find("usesprimaryammo")!=std::string::npos || low.find("getprimaryammotype")!=std::string::npos;
    k.sourceUsesClips |= low.find("usesclipsforammo1")!=std::string::npos || low.find("defaultreload")!=std::string::npos;
    k.sourceReloadsSingly |= low.find("reloadssingly")!=std::string::npos || k.hasStartReload;
    k.sourceCallsSequenceDuration |= low.find("sequenceduration")!=std::string::npos;
    if(low.find("m_nshotsfired")!=std::string::npos || low.find("firemode_fullauto")!=std::string::npos || k.hasMachineGunBase || low.find("autofire")!=std::string::npos) k.explicitAutomatic=true;
    if(low.find("firemode_semi")!=std::string::npos || low.find("firemode_3rndburst")!=std::string::npos && low.find("firemode_fullauto")==std::string::npos) k.explicitSemiAutomatic=true;
    if(bc.find("basehlbludgeon")!=std::string::npos || bc.find("bludgeon")!=std::string::npos || bc.find("cbasebludgeonweapon")!=std::string::npos || low.find("handleaniveentmelee")!=std::string::npos || low.find("melee_hit")!=std::string::npos) k.isMelee=true;

    float rate=wlReturnFloat(text,"GetFireRate",0.0f);
    if(rate>0.01f && rate<10.0f) k.sauceCycleTime=rate;
    if(k.sauceCycleTime<=0.0f){
        size_t np=low.find("m_flnextprimaryattack");
        while(np!=std::string::npos){
            size_t plus=low.find('+',np);
            if(plus!=std::string::npos && plus<np+220){
                size_t e=plus+1; while(e<low.size() && std::isspace((unsigned char)low[e])) ++e;
                size_t z=e; while(z<low.size() && (std::isdigit((unsigned char)low[z])||low[z]=='.')) ++z;
                if(z>e){ try { float v=std::stof(low.substr(e,z-e)); if(v>0.01f&&v<10.0f) k.sauceCycleTime=v; } catch(...) {} }
            }
            np=low.find("m_flnextprimaryattack",np+1);
        }
    }
    if(low.find("sequenceduration")!=std::string::npos) k.sourceCallsSequenceDuration=true;

    int maxclip=wlReturnInt(text,"GetMaxClip1",-1);
    if(maxclip>0 && maxclip<1000) k.sourceMaxClip1=maxclip;
    int defclip=wlReturnInt(text,"GetDefaultClip1",-1);
    if(defclip>=0 && defclip<1000) k.sourceDefaultClip1=defclip;
    int minBurst=wlReturnInt(text,"GetMinBurst",-1);
    if(minBurst>0 && minBurst<128) k.sourceBurstMin=minBurst;
    int maxBurst=wlReturnInt(text,"GetMaxBurst",-1);
    if(maxBurst>0 && maxBurst<128) k.sourceBurstMax=maxBurst;
    int burstSize=wlReturnInt(text,"GetBurstSize",-1);
    if(burstSize>0 && burstSize<128) k.sourceBurstSize=burstSize;
    int pellets=wlReturnInt(text,"GetNumShots",-1);
    if(pellets>0 && pellets<128) k.sourcePellets=pellets;

    wlExtractActivitiesFromFunction(text,"PrimaryAttack",k.primaryAnimationHints);
    wlExtractActivitiesFromFunction(text,"SecondaryAttack",k.secondaryAnimationHints);
    wlExtractActivitiesFromFunction(text,"Reload",k.reloadAnimationHints);
    wlExtractActivitiesFromFunction(text,"StartReload",k.reloadAnimationHints);
    if(low.find("getdrawactivity")!=std::string::npos) wlAddHint(k.drawAnimationHints,"draw");
    if(low.find("weaponidle")!=std::string::npos) wlAddHint(k.idleAnimationHints,"idle");

    if(k.hasFireBullets){
        std::vector<int> fireCounts;
        size_t fp=0;
        while((fp=low.find("firebullets(",fp))!=std::string::npos){
            size_t q=fp+12;
            while(q<low.size() && std::isspace((unsigned char)low[q])) ++q;
            size_t e=q; while(e<low.size() && std::isdigit((unsigned char)low[e])) ++e;
            if(e>q){ try { int v=std::stoi(low.substr(q,e-q)); if(v>0&&v<128) fireCounts.push_back(v);}catch(...){} }
            fp=e;
        }
        if(!fireCounts.empty()) k.fireBulletsCount=*std::max_element(fireCounts.begin(),fireCounts.end());
    }

    const std::string coneNames[]={"vector_cone_0","vector_cone_1","vector_cone_2","vector_cone_3","vector_cone_4","vector_cone_5","vector_cone_6","vector_cone_7","vector_cone_8","vector_cone_9","vector_cone_10","vector_cone_15","vector_cone_20"};
    for(const std::string& cn:coneNames){
        if(low.find(cn)!=std::string::npos){
            size_t u=cn.find('_',cn.find('_')+1);
            if(u!=std::string::npos){
                try { float deg=std::stof(cn.substr(u+1)); if(deg>=0.0f && deg<=30.0f) k.sauceSpreadDegrees=std::max(k.sauceSpreadDegrees,deg); } catch(...) {}
            }
        }
    }

    wlExtractActivitiesFromFunction(text,"getprimaryattackactivity",k.primaryAnimationHints);
    wlExtractActivitiesFromFunction(text,"primaryattack",k.primaryAnimationHints);
    wlExtractActivitiesFromFunction(text,"getsecondaryattackactivity",k.secondaryAnimationHints);
    wlExtractActivitiesFromFunction(text,"secondaryattack",k.secondaryAnimationHints);
    wlExtractActivitiesFromFunction(text,"getdrawactivity",k.drawAnimationHints);
    wlExtractActivitiesFromFunction(text,"weaponidle",k.idleAnimationHints);
    wlExtractActivitiesFromFunction(text,"reload",k.reloadAnimationHints);
    wlExtractActivitiesFromFunction(text,"startreload",k.reloadAnimationHints);
    wlExtractActivitiesFromFunction(text,"finishreload",k.reloadAnimationHints);
    wlExtractWeaponSoundAliases(text,k);

    if(k.primaryAnimationHints.empty()){
        if(low.find("act_vm_primaryattack")!=std::string::npos) wlAddHint(k.primaryAnimationHints,"act_vm_primaryattack");
        if(low.find("act_vm_recoil1")!=std::string::npos) wlAddHint(k.primaryAnimationHints,"act_vm_recoil1");
        if(low.find("act_vm_recoil2")!=std::string::npos) wlAddHint(k.primaryAnimationHints,"act_vm_recoil2");
        if(low.find("act_vm_recoil3")!=std::string::npos) wlAddHint(k.primaryAnimationHints,"act_vm_recoil3");
    }
    if(low.find("act_vm_secondaryattack")!=std::string::npos) wlAddHint(k.secondaryAnimationHints,"act_vm_secondaryattack");
    if(low.find("act_vm_reload")!=std::string::npos) wlAddHint(k.reloadAnimationHints,"act_vm_reload");
    if(low.find("act_shotgun_pump")!=std::string::npos) { wlAddHint(k.reloadAnimationHints,"act_shotgun_pump"); wlAddHint(k.reloadAnimationHints,"pump"); }
    if(low.find("act_shotgun_reload_start")!=std::string::npos) wlAddHint(k.reloadAnimationHints,"act_shotgun_reload_start");
    if(low.find("act_shotgun_reload_finish")!=std::string::npos) wlAddHint(k.reloadAnimationHints,"act_shotgun_reload_finish");

    wlApplySuppliedWeaponProfile(k);
    if(k.hasMachineGunBase && !k.explicitSemiAutomatic) k.explicitAutomatic=true;
    if(k.sourceBurstMax>1 && k.explicitAutomatic==false && k.sourceBurstMin==1) k.explicitAutomatic=false;
}


static void wlApplySuppliedWeaponProfile(WeaponSDKKnowledge& k){
    const std::string c=wlLower(k.className);
    static const char* clipWeapons[]={
        "weapon_357","weapon_alyxgun","weapon_annabelle","weapon_ar1","weapon_ar2",
        "weapon_cguard","weapon_crossbow","weapon_flaregun","weapon_irifle","weapon_pistol",
        "weapon_shotgun","weapon_smg1","weapon_smg2","weapon_sniperrifle"
    };
    static const char* reserveWeapons[]={
        "weapon_brickbat","weapon_extinguisher","weapon_frag","weapon_hopwire","weapon_immolator",
        "weapon_manhack","weapon_molotov","weapon_oldmanharpoon","weapon_proto1","weapon_rpg",
        "weapon_slam","weapon_striderbuster"
    };
    static const char* noAmmoWeapons[]={
        "weapon_bugbait","weapon_citizenpackage","weapon_crowbar","weapon_cubemap",
        "weapon_physcannon","weapon_stunstick"
    };
    for(const char* x:clipWeapons) if(c==x){ k.sourcePrimaryUsesClip=true; k.sourcePrimaryReserveOnly=false; break; }
    for(const char* x:reserveWeapons) if(c==x){ k.sourcePrimaryReserveOnly=true; k.sourcePrimaryUsesClip=false; break; }
    for(const char* x:noAmmoWeapons) if(c==x){ k.sourcePrimaryReserveOnly=false; k.sourcePrimaryUsesClip=false; k.usesClip=false; break; }
    if(c=="weapon_crossbow") k.hasProjectileCreation=true;
    if(c=="weapon_smg1" || c=="weapon_slam") k.sourceSecondaryReserveOnly=true;
}




static void wlAnalyzeSourceTree(const std::filesystem::path& path,const std::string& text,WeaponSDKKnowledge& k,int depth,std::set<std::string>& visited){
    if(depth>4) return;
    const std::string key=path.lexically_normal().string();
    if(!visited.insert(key).second) return;
    wlAnalyzeSourceText(text,path.string(),k);

    size_t p=0;
    while((p=text.find("#include",p))!=std::string::npos){
        size_t q=text.find_first_of("\"<",p+8);
        if(q==std::string::npos){p+=8;continue;}
        char open=text[q];
        char close=(open=='\"')?'\"':'>';
        size_t e=text.find(close,q+1);
        if(e==std::string::npos){p=q+1;continue;}
        if(open!='\"'){p=e+1;continue;}
        std::string inc=text.substr(q+1,e-q-1);
        std::string incl=wlLower(inc);
        bool relevant=incl.find("weapon")!=std::string::npos || incl.find("basehlcombatweapon")!=std::string::npos || incl.find("basecombatweapon")!=std::string::npos || incl.find("basebludgeonweapon")!=std::string::npos;
        if(inc.empty() || !relevant || inc.size()>160){p=e+1;continue;}
        std::filesystem::path candidate=path.parent_path()/inc;
        std::error_code ec;
        if(std::filesystem::exists(candidate,ec) && std::filesystem::is_regular_file(candidate,ec)){
            uintmax_t sz=std::filesystem::file_size(candidate,ec);
            if(!ec && sz<=512u*1024u){
                std::ifstream f(candidate,std::ios::binary);
                if(f){
                    std::string child((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
                    if(!child.empty()) wlAnalyzeSourceTree(candidate,child,k,depth+1,visited);
                }
            }
        }
        p=e+1;
    }
}

static void wlBuildSourceIndex(const std::string& gameDir, const std::set<std::string>& wanted, std::map<std::string,std::vector<std::filesystem::path>>& out) {
    out.clear();
    std::set<std::string> rootsSeen;
    size_t visited=0;
    const size_t maxVisited=6000;
    for(const auto& root:wlSourceRoots(gameDir)){
        std::error_code ec;
        if(!std::filesystem::exists(root,ec)) continue;
        std::string rk=root.lexically_normal().string();
        if(!rootsSeen.insert(rk).second) continue;
        for(auto it=std::filesystem::recursive_directory_iterator(root,std::filesystem::directory_options::skip_permission_denied,ec), end=std::filesystem::recursive_directory_iterator(); it!=end; it.increment(ec)){
            if(ec){ec.clear();continue;}
            if(++visited>maxVisited) break;
            if(!it->is_regular_file()) continue;
            std::string fn=wlLower(it->path().filename().string());
            std::string cls;
            if(fn.rfind("c_weapon_",0)==0){
                size_t dot=fn.rfind('.');
                if(dot!=std::string::npos) cls="weapon_"+fn.substr(9,dot-9);
            } else if(fn.rfind("weapon_",0)==0){
                size_t dot=fn.rfind('.');
                if(dot!=std::string::npos) cls=fn.substr(0,dot);
            }
            if(cls.empty() || wanted.find(cls)==wanted.end()) continue;
            std::string ext=wlLower(it->path().extension().string());
            if(ext==".cpp") out[cls].push_back(it->path());
        }
    }
}

static bool wlMergePresentationScript(const std::string& gameDir,const std::string& className,Texture_L* fileSystem,WeaponSDKKnowledge& k) {
    const std::string rel="scripts/"+className+".txt";
    std::vector<char> raw;
    std::string text;
    if(fileSystem && fileSystem->getFileRawData(rel,raw) && !raw.empty()) text.assign(raw.begin(),raw.end());
    if(text.empty()){
        std::ifstream f(std::filesystem::path(gameDir)/rel,std::ios::binary);
        if(f) text.assign(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    }
    if(text.empty()) return false;
    for(const auto& n:wlParseTopBlocks(text)){
        const std::string nn=wlLower(n.name);
        if(nn=="weapondata" || nn=="weapon_data" || wlCanonicalWeaponClass(n.name)==className){ k.scriptPath=rel; k.scriptLoaded=true; return true; }
    }
    return false;
}

static std::vector<std::string> wlDllCandidates(const std::string& gameDir) {
    return {
        gameDir+"/client.dll",
        gameDir+"/server.dll",
        gameDir+"/bin/client.dll",
        gameDir+"/bin/server.dll",
        gameDir+"/game/bin/client.dll",
        gameDir+"/game/bin/server.dll"
    };
}

static std::map<std::string,WeaponSDKKnowledge> gWeaponKnowledge;
static std::vector<std::string> gImpulse101Weapons;
const std::map<std::string,WeaponSDKKnowledge>& Weapon_L::knowledgeBase(){ return gWeaponKnowledge; }
const std::vector<std::string>& Weapon_L::impulse101Weapons(){ return gImpulse101Weapons; }

static void wlCollectImpulseWhitelist(const std::string& gameDir, Texture_L* fileSystem) {
    std::set<std::string> allowed;
    auto consider=[&](std::string path){
        path=wlLower(path);
        std::replace(path.begin(), path.end(), '\\', '/');
        const std::string prefix="scripts/weapon_";
        const size_t pos=path.find(prefix);
        if(pos==std::string::npos) return;
        const size_t start=pos+prefix.size();
        const size_t dot=path.find_last_of('.');
        if(dot==std::string::npos || dot<=start || path.substr(dot)!=".txt") return;
        const std::string shortName=path.substr(start,dot-start);
        if(!shortName.empty()) allowed.insert("weapon_"+shortName);
    };
    std::error_code ec;
    const std::filesystem::path root=std::filesystem::path(gameDir)/"scripts";
    if(std::filesystem::exists(root,ec)) {
        for(std::filesystem::recursive_directory_iterator it(root,std::filesystem::directory_options::skip_permission_denied,ec),end; it!=end && !ec; it.increment(ec))
            if(it->is_regular_file(ec)) consider(it->path().generic_string());
    }
    if(fileSystem) for(const auto& entry:fileSystem->vpkFileSystem) consider(entry.first);
    gImpulse101Weapons.clear();
    for(const auto& cls:allowed) if(gWeaponKnowledge.find(cls)!=gWeaponKnowledge.end()) gImpulse101Weapons.push_back(cls);
}


static void wlSeedSuppliedWeaponClasses(){
    static const char* classes[]={
        "weapon_357","weapon_alyxgun","weapon_annabelle","weapon_ar1","weapon_ar2",
        "weapon_brickbat","weapon_bugbait","weapon_cguard","weapon_citizenpackage",
        "weapon_crossbow","weapon_crowbar","weapon_cubemap","weapon_extinguisher",
        "weapon_flaregun","weapon_frag","weapon_hopwire","weapon_immolator","weapon_irifle",
        "weapon_manhack","weapon_molotov","weapon_oldmanharpoon","weapon_physcannon",
        "weapon_pistol","weapon_proto1","weapon_rpg","weapon_shotgun","weapon_slam",
        "weapon_smg1","weapon_smg2","weapon_sniperrifle","weapon_striderbuster","weapon_stunstick"
    };
    for(const char* cls:classes){
        auto& k=gWeaponKnowledge[cls];
        k.className=cls;
        wlApplySuppliedWeaponProfile(k);
    }
}

void Weapon_L::discoverAndRegister(EntityManager& manager,const std::string& gameDir, Texture_L* fileSystem) {
    gWeaponKnowledge.clear();
    wlSeedSuppliedWeaponClasses();

    bool clientDllFound = false;
    for (const std::string& p : wlDllCandidates(gameDir)) {
        const std::string lower = wlLower(p);
        if (lower.size() >= 10 && lower.rfind("client.dll") == lower.size() - 10 &&
            wlFileExists(p)) {
            clientDllFound = true;
            break;
        }
    }

    static bool reportedMissingClientDll = false;
    if (!clientDllFound && !reportedMissingClientDll) {
        reportedMissingClientDll = true;
        std::cout << "[Engine] missing: client.dll" << std::endl;
        wlShowEngineError("Could not load library client. Script errors or missing entities are possible.");
    } else if (clientDllFound) {
        reportedMissingClientDll = false;
    }

    for(const std::string& p:wlDllCandidates(gameDir)){
        const std::string l=wlLower(p);
        if(l.size()>=10 && l.rfind("client.dll")==l.size()-10) wlScanDll(p,false,gWeaponKnowledge);
        else if(l.size()>=10 && l.rfind("server.dll")==l.size()-10) wlScanDll(p,true,gWeaponKnowledge);
    }
    if(gWeaponKnowledge.empty()){
        return;
    }
    std::set<std::string> wanted;
    for(const auto& kv:gWeaponKnowledge) wanted.insert(kv.first);
    std::map<std::string,std::vector<std::filesystem::path>> sourceIndex;
    wlBuildSourceIndex(gameDir,wanted,sourceIndex);
    for(auto& kv:gWeaponKnowledge){
        auto sit=sourceIndex.find(kv.first);
        if(sit!=sourceIndex.end()){
            std::set<std::string> visitedSourceFiles;
            for(const auto& path:sit->second){
                std::error_code ec;
                uintmax_t sz=std::filesystem::file_size(path,ec);
                if(ec || sz>2u*1024u*1024u) continue;
                std::ifstream f(path,std::ios::binary);
                if(!f) continue;
                std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
                if(!text.empty()) wlAnalyzeSourceTree(path,text,kv.second,0,visitedSourceFiles);
                std::filesystem::path sibling=path;
                sibling.replace_extension(".h");
                if(std::filesystem::exists(sibling,ec) && std::filesystem::is_regular_file(sibling,ec)){
                    uintmax_t hsz=std::filesystem::file_size(sibling,ec);
                    if(!ec && hsz<=512u*1024u){
                        std::ifstream hf(sibling,std::ios::binary);
                        if(hf){
                            std::string ht((std::istreambuf_iterator<char>(hf)),std::istreambuf_iterator<char>());
                            if(!ht.empty()) wlAnalyzeSourceTree(sibling,ht,kv.second,0,visitedSourceFiles);
                        }
                    }
                }
            }
        }
        if(kv.second.sourceFiles.empty()) {
            for(const auto& root:wlSourceRoots(gameDir)){
                std::error_code ec;
                if(!std::filesystem::exists(root,ec)) continue;
                std::filesystem::path h=root/(kv.first+".h");
                if(!std::filesystem::exists(h,ec)) continue;
                std::ifstream f(h,std::ios::binary);
                if(!f) continue;
                std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
                if(!text.empty()){
                    std::set<std::string> visitedSourceFiles;
                    wlAnalyzeSourceTree(h,text,kv.second,0,visitedSourceFiles);
                }
                break;
            }
        }
        wlMergePresentationScript(gameDir,kv.first,fileSystem,kv.second);
    }
                                                                           
                                                                               
    wlCollectImpulseWhitelist(gameDir, fileSystem);
    for(const auto& kv:gWeaponKnowledge){
        manager.registerEntity(kv.first,&Weapon_L::Create);
    }
}

Weapon_L::Weapon_L(const std::string& block, const Vec3& p, const std::string& gDir, Texture_L* texL) {
    gWeaponInstances.push_back(this);
    pos=p;
    gameDirectory=gDir;
    textureSystem=texL;
    definition.className=wlLower(wlValueCI(block,"classname"));
    if(definition.className.empty()) definition.className="weapon_unknown";
    auto it=gWeaponKnowledge.find(definition.className);
    if(it!=gWeaponKnowledge.end()) knowledge=it->second;
    loadDllDefinition();
    loadScriptAugments();

    std::string blockWorld=wlValueCI(block,"model");
    if(!blockWorld.empty()) definition.worldModel=wlNormalize(blockWorld);
    std::string a=wlValueCI(block,"angles");
    if(!a.empty()){ std::istringstream ss(a); ss>>angles.x>>angles.y>>angles.z; }
    scale=wlFloat(block,"modelscale",1.0f); if(scale<=0.001f) scale=1.0f;
    inventorySpawn=wlInt(block,"__inventory",0)!=0;

    if(texL) {
        if(definition.worldModel.empty()) definition.worldModel="models/weapons/w_"+definition.className.substr(7)+".mdl";
        worldModel.load(definition.worldModel,*texL,false);

                                                                                                   
                                                                                      
        if(!definition.viewModel.empty()) viewModel.load(definition.viewModel,*texL,false);
        if(!viewModel.loaded) {
            const std::string shortName=definition.className.rfind("weapon_",0)==0?definition.className.substr(7):definition.className;
            const std::string fallback="models/weapons/v_"+shortName+".mdl";
            if(definition.viewModel!=fallback){ definition.viewModel=fallback; viewModel.load(definition.viewModel,*texL,false); }
        }
        if(viewModel.loaded) playWeaponAnimation({"draw","deploy","idle","act_vm_draw","act_vm_deploy"});
    }

    if(definition.usesPrimaryClip && definition.clipSize>0){
        clipAmmo=std::max(0,definition.defaultClip<0?definition.clipSize:definition.defaultClip);
        reserveAmmo=definition.reloadSingleShell ? std::max(definition.clipSize*5,clipAmmo) : std::max(definition.clipSize*3,clipAmmo);
        maxReserveAmmo=definition.clipSize*8;
    } else if(!definition.primaryAmmo.empty()){
        clipAmmo=0;
        const int seed=definition.defaultClip>0?definition.defaultClip:1;
        reserveAmmo=std::max(1,seed);
        maxReserveAmmo=std::max(8,seed*8);
    } else {
        clipAmmo=0; reserveAmmo=0; maxReserveAmmo=0;
    }
    if(!definition.secondaryAmmo.empty()){
        secondaryReserveAmmo=definition.secondaryAmmo=="SMG1_Grenade" ? 3 : 1;
        maxSecondaryReserveAmmo=secondaryReserveAmmo*4;
    } else {
        secondaryReserveAmmo=0; maxSecondaryReserveAmmo=0;
    }
    if(inventorySpawn) pickedUp=true;
}

Weapon_L::~Weapon_L() {
    auto it=std::find(gWeaponInstances.begin(),gWeaponInstances.end(),this);
    if(it!=gWeaponInstances.end()) gWeaponInstances.erase(it);
}

static bool wlReadTextResource(Texture_L* fs, const std::string& gameDir, const std::string& rel, std::string& out) {
    out.clear();
    if(fs){
        std::vector<char> raw;
        if(fs->getFileRawData(rel,raw) && !raw.empty()) { out.assign(raw.begin(),raw.end()); return true; }
    }
    std::ifstream f(std::filesystem::path(gameDir)/rel,std::ios::binary);
    if(!f) return false;
    std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    if(text.empty()) return false;
    out.swap(text);
    return true;
}

static int wlDefaultPresentationSlot(const std::string& cls) {
    const std::string s = wlLower(cls);
    if (s.find("crowbar") != std::string::npos || s.find("gravity") != std::string::npos) return 1;
    if (s.find("pistol") != std::string::npos || s.find("357") != std::string::npos || s.find("revolver") != std::string::npos) return 2;
    if (s.find("shotgun") != std::string::npos || s.find("smg") != std::string::npos || s.find("ar2") != std::string::npos) return 3;
    if (s.find("crossbow") != std::string::npos || s.find("rpg") != std::string::npos || s.find("grenade") != std::string::npos) return 4;
    return 1;
}

static const WLKVNode* wlFindWeaponDataNode(const std::vector<WLKVNode>& nodes, const std::string& className) {
    for (const auto& n : nodes) {
        const std::string nn=wlLower(n.name);
        if (nn=="weapondata" || nn=="weapon_data") return &n;
        if (wlCanonicalWeaponClass(n.name)==className) return &n;
    }
    return nullptr;
}

bool Weapon_L::loadScriptAugments() {
    const std::string shortName = definition.className.rfind("weapon_",0)==0 ? definition.className.substr(7) : definition.className;
    std::string raw;
    scriptPath.clear();
    const std::string rel = "scripts/weapon_" + shortName + ".txt";
    if (wlReadTextResource(textureSystem, gameDirectory, rel, raw)) scriptPath = rel;
    if (raw.empty() && !knowledge.scriptPath.empty()) {
        std::ifstream f(knowledge.scriptPath, std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); raw = ss.str(); scriptPath = knowledge.scriptPath; }
    }
    scriptLoaded = !raw.empty();
    if(!scriptLoaded) return false;

    const auto nodes=wlParseTopBlocks(raw);
    const WLKVNode* weaponNode=wlFindWeaponDataNode(nodes, definition.className);
    if(!weaponNode) return false;

    auto val=[&](const char* key){ return wlNodeValue(*weaponNode,key); };
    auto iv=[&](const char* key,int def){ try { const std::string v=val(key); return v.empty()?def:std::stoi(v); } catch(...) { return def; } };

    if(!val("printname").empty()) definition.printName=val("printname");
    if(!val("viewmodel").empty()) definition.viewModel=wlNormalize(val("viewmodel"));
    if(!val("worldmodel").empty()) definition.worldModel=wlNormalize(val("worldmodel"));
    if(!val("playermodel").empty() && val("worldmodel").empty()) definition.worldModel=wlNormalize(val("playermodel"));
    if(!val("anim_prefix").empty()) definition.animPrefix=val("anim_prefix");

    definition.bucket=iv("bucket",definition.bucket);
    definition.bucketPosition=iv("bucket_position",definition.bucketPosition);
    definition.bucket360=iv("bucket_360",definition.bucket360);
    definition.bucketPosition360=iv("bucket_position_360",definition.bucketPosition360);
    definition.slot=std::max(1,definition.bucket+1);

    const std::string scriptClip=val("clip_size");
    const std::string scriptDefault=val("default_clip");
    if(!scriptClip.empty()) definition.clipSize=iv("clip_size",definition.clipSize);
    else if(knowledge.sourceMaxClip1>0) definition.clipSize=knowledge.sourceMaxClip1;
    if(!scriptDefault.empty()) definition.defaultClip=iv("default_clip",definition.defaultClip);
    else if(knowledge.sourceDefaultClip1>=0) definition.defaultClip=knowledge.sourceDefaultClip1;
    definition.weight=iv("weight",definition.weight);
    definition.itemFlags=iv("item_flags",definition.itemFlags);

    if(!val("primary_ammo").empty()) definition.primaryAmmo=val("primary_ammo");
    if(!val("secondary_ammo").empty()) definition.secondaryAmmo=val("secondary_ammo");

    wlReadWeaponSoundData(*weaponNode, definition.sounds);
    const char* soundKeys[]={"single_shot","single_shot_npc","empty","reload","reload_npc","melee_miss","melee_hit","melee_hit_world","special1","special2","special3","deploy","secondary_attack","double_shot"};
    for(const char* k:soundKeys){ const std::string v=val(k); if(!v.empty()) definition.sounds[wlLower(k)]=v; }

    for(const auto& c:weaponNode->children){
        const std::string cn=wlLower(c.name);
        auto sit=c.values.find("sound");
        if(sit!=c.values.end() && !sit->second.empty()){
            const std::string ev=sit->second.front();
            if(cn=="primary_attack"||cn=="primaryattack") definition.sounds["single_shot"]=ev;
            else if(cn=="secondary_attack"||cn=="secondaryattack") definition.sounds["secondary_attack"]=ev;
            else if(cn=="reload") definition.sounds["reload"]=ev;
            else if(cn=="deploy"||cn=="draw") definition.sounds["deploy"]=ev;
            else if(cn=="empty") definition.sounds["empty"]=ev;
            else definition.sounds[cn]=ev;
        }
    }

    if(knowledge.sourcePrimaryUsesClip) definition.usesPrimaryClip=true;
    else if(knowledge.sourcePrimaryReserveOnly || knowledge.isMelee || knowledge.hasProjectileSignature)
        definition.usesPrimaryClip=false;
    else if(!scriptClip.empty())
        definition.usesPrimaryClip=definition.clipSize>0;
    else
        definition.usesPrimaryClip=definition.clipSize>0 && (knowledge.usesClip || knowledge.hasPrimaryAttack);

    return true;
}

bool Weapon_L::loadDllDefinition() {
    std::string shortName=definition.className;
    if(shortName.rfind("weapon_",0)==0 && shortName.size()>7) shortName=shortName.substr(7);
    definition.viewModel="models/weapons/v_"+shortName+".mdl";
    definition.worldModel="models/weapons/w_"+shortName+".mdl";
    definition.slot=std::max(1,definition.bucket+1);
    definition.clipSize=-1;
    definition.defaultClip=-1;
    definition.weight=0;
    definition.itemFlags=0;
    definition.damage=0;
    definition.bullets=1;
    definition.primaryAmmo.clear();
    definition.secondaryAmmo.clear();
    definition.sounds.clear();

    definition.fireBulletsCount=std::max(1,knowledge.sourcePellets>0?knowledge.sourcePellets:knowledge.fireBulletsCount);
    if(definition.fireBulletsCount<=1 && knowledge.sourceBurstSize>1 && knowledge.hasFireBullets) definition.fireBulletsCount=1;
    definition.spread=knowledge.sauceSpreadDegrees>0.0f?knowledge.sauceSpreadDegrees*0.0174532925199433f:0.0f;
    definition.cycleTime=knowledge.sauceCycleTime>0.0f?knowledge.sauceCycleTime:0.20f;
    definition.sdk=knowledge;
    definition.profileMachineGun=knowledge.hasMachineGunBase;
    const std::string clsLower=wlLower(definition.className);
    definition.profileShotgun=knowledge.hasShotgunSignature || clsLower.find("shotgun")!=std::string::npos;
    definition.profileRevolver=(clsLower.find("357")!=std::string::npos || clsLower.find("revolver")!=std::string::npos);
    definition.profileCrossbow=clsLower.find("crossbow")!=std::string::npos;
    definition.profileRPG=clsLower.find("rpg")!=std::string::npos;
    definition.profilePistol=knowledge.usesClip && !knowledge.hasMachineGunBase && !knowledge.hasShotgunSignature && !knowledge.hasProjectileSignature && !knowledge.isMelee && !definition.profileRevolver && !definition.profileCrossbow && !definition.profileRPG;
    definition.pumpAfterShot=knowledge.hasPump;
    definition.reloadSingleShell=definition.profileShotgun || knowledge.sourceReloadsSingly || clsLower.find("shotgun")!=std::string::npos;
    definition.automatic=!knowledge.isMelee && knowledge.explicitAutomatic && !knowledge.explicitSemiAutomatic;
    definition.hitscan=!knowledge.isMelee && knowledge.hasFireBullets;
    if(knowledge.sourcePrimaryUsesClip) definition.usesPrimaryClip=true;
    else if(knowledge.sourcePrimaryReserveOnly || knowledge.isMelee || knowledge.hasProjectileSignature) definition.usesPrimaryClip=false;
    else definition.usesPrimaryClip=(knowledge.usesClip || knowledge.sourceUsesClips || knowledge.sourceMaxClip1>0);

    if(knowledge.sourceMaxClip1>0) definition.clipSize=knowledge.sourceMaxClip1;
    if(knowledge.sourceDefaultClip1>=0) definition.defaultClip=knowledge.sourceDefaultClip1;

    if(definition.profileShotgun && knowledge.sourcePellets>1) definition.shotgunPellets=knowledge.sourcePellets;
    else definition.shotgunPellets=std::max(1,knowledge.fireBulletsCount);
    if(definition.profileShotgun && definition.shotgunPellets>1) definition.fireBulletsCount=definition.shotgunPellets;

    if(definition.clipSize<0 && definition.usesPrimaryClip) definition.clipSize=12;
    if(definition.defaultClip<0 && definition.clipSize>0) definition.defaultClip=definition.clipSize;
    if(definition.damage<=0) definition.damage=10;

    definition.reloadDuration=knowledge.sourceCallsSequenceDuration?0.80f:0.75f;
    if(definition.profileShotgun) definition.reloadDuration=0.55f;
    if(knowledge.sauceCycleTime>0.01f) definition.cycleTime=knowledge.sauceCycleTime;

    if(knowledge.hasViewPunch) definition.viewKickDegrees=definition.profileMachineGun?1.0f:(definition.profileShotgun?1.5f:0.8f);

    if(knowledge.sourceBurstSize>0 && knowledge.sourceBurstSize<definition.fireBulletsCount) definition.fireBulletsCount=std::max(1,knowledge.fireBulletsCount);

    if(knowledge.sourceSoundAliases.count("single_shot")) definition.sounds["single_shot"]=knowledge.sourceSoundAliases.at("single_shot");
    if(knowledge.sourceSoundAliases.count("single_npc")) definition.sounds["single_shot_npc"]=knowledge.sourceSoundAliases.at("single_npc");
    if(knowledge.sourceSoundAliases.count("reload")) definition.sounds["reload"]=knowledge.sourceSoundAliases.at("reload");
    if(knowledge.sourceSoundAliases.count("empty")) definition.sounds["empty"]=knowledge.sourceSoundAliases.at("empty");
    if(knowledge.sourceSoundAliases.count("special1")) definition.sounds["special1"]=knowledge.sourceSoundAliases.at("special1");
    if(knowledge.sourceSoundAliases.count("special2")) definition.sounds["special2"]=knowledge.sourceSoundAliases.at("special2");

    return true;
}

bool Weapon_L::isMeleeWeapon() const {
    bool meleeSound = definition.sounds.find("melee_hit") != definition.sounds.end() || definition.sounds.find("melee_hit_world") != definition.sounds.end();
    return definition.sdk.isMelee || meleeSound;
}

bool Weapon_L::isActive(const Camera& player) const {
    if(!pickedUp || !player.hasWeapon(definition.className) || player.activeWeaponSlot!=definition.slot) return false;
    auto it=gSelectedWeaponBySlot.find(definition.slot);
    return it==gSelectedWeaponBySlot.end() || it->second.empty() || it->second==definition.className;
}

bool Weapon_L::pickup(Camera& player) {
    if (pickedUp || player.hasWeapon(definition.className)) return false;
    pickedUp=true;
    if(!knowledge.serverDllPath.empty() || !knowledge.clientDllPath.empty()) {
        sauceRuntime.initialize(gameDirectory, knowledge.serverDllPath, knowledge.clientDllPath);
        sauceRuntime.bindClass(definition.className);
    }
    player.giveWeapon(definition.className, definition.slot);
    if(gSelectedWeaponBySlot[definition.slot].empty()) gSelectedWeaponBySlot[definition.slot]=definition.className;
    if(clipAmmo<=0) clipAmmo=std::max(0,definition.defaultClip<0?definition.clipSize:definition.defaultClip);
    if(reserveAmmo<0) reserveAmmo=0;
    if(maxReserveAmmo<reserveAmmo) maxReserveAmmo=reserveAmmo;
    if (definition.sounds.count("deploy")) playScriptSound("deploy", player.pos, 1.0f);
    return true;
}

bool Weapon_L::equip(Camera& player) {
    if (!player.hasWeapon(definition.className)) return false;
    player.activeWeaponSlot=definition.slot;
    gSelectedWeaponBySlot[definition.slot]=definition.className;
    pickedUp=true;
    return true;
}

void Weapon_L::update(float dt, Camera& player, BSP_L* map) {
    lightmapUpdateTimer-=dt;
    if(map && lightmapUpdateTimer<=0.0f &&
       (!hasLightSample || (pos-lastLightSamplePos).length()>12.0f)) {
        float b=1.0f;
        lightmapColor=map->sampleLightmapLighting(pos,&b);
        if(!std::isfinite(b)) b=1.0f;
        lightmapBrightness=std::max(0.30f,std::min(1.18f,b));
        lastLightSamplePos=pos;
        hasLightSample=true;
        lightmapUpdateTimer=0.12f;
    }
    if (inventorySpawn) return;
    if (pickedUp) return;
    if (player.hasWeapon(definition.className)) { shouldDestroy=true; return; }
    float dx=player.pos.x-pos.x;
    float dz=player.pos.z-pos.z;
    float d2=std::sqrt(dx*dx+dz*dz);
    float h=std::abs(player.pos.y-pos.y);
    if (d2<=pickupRadius && h<=80.0f) pickup(player);
}

void Weapon_L::render() {
    if (pickedUp || !worldModel.loaded) return;
    glEnable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(pos.x,pos.y,pos.z);
    if (std::abs(scale-1.0f)>0.001f) glScalef(scale,scale,scale);
    wlRenderRotation(angles);
    const Vec3 lc=Vec3{0.24f,0.24f,0.24f}+lightmapColor*0.76f;
    const float rb=std::max(0.30f,std::min(1.12f,lightmapBrightness));
    glColor4f(std::clamp(rb*lc.x,0.0f,1.0f),
              std::clamp(rb*lc.y,0.0f,1.0f),
              std::clamp(rb*lc.z,0.0f,1.0f),1.0f);
    worldModel.render();
    glPopMatrix();
    glColor4f(1,1,1,1);
}

bool Weapon_L::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    if (pickedUp || !worldModel.loaded) return false;
    Vec3 localO=wlInverseModelPoint(orig,pos,angles,scale);
    Vec3 localP=wlInverseModelPoint(orig+dir,pos,angles,scale);
    Vec3 localD=(localP-localO).normalize();
    float t=0.0f;
    if (!wlRayAABB(localO,localD,worldModel.hullMin,worldModel.hullMax,t)) return false;
    outDist=t*scale;
    outNormal={0,1,0};
    return true;
}

float Weapon_L::aimScore(const Vec3& origin, const Vec3& dir, float maxDistance, float& along) const {
    along=maxDistance+1.0f;
    if (pickedUp || !worldModel.loaded) return 1e30f;
    Vec3 lo=wlInverseModelPoint(origin,pos,angles,scale);
    Vec3 lp=wlInverseModelPoint(origin+dir*maxDistance,pos,angles,scale);
    Vec3 ld=(lp-lo).normalize();
    float t=0.0f;
    if (wlRayAABB(lo,ld,worldModel.hullMin,worldModel.hullMax,t)) {
        along=t*scale;
        if (along<=maxDistance) return along;
    }
    Vec3 center=pos+Vec3{0,(worldModel.hullMin.y+worldModel.hullMax.y)*0.5f*scale,0};
    Vec3 to=center-origin;
    float proj=to.dot(dir);
    if (proj<0 || proj>maxDistance) return 1e30f;
    float rad=std::max(8.0f,std::max({std::abs(worldModel.hullMax.x-worldModel.hullMin.x),std::abs(worldModel.hullMax.y-worldModel.hullMin.y),std::abs(worldModel.hullMax.z-worldModel.hullMin.z)})*scale*0.25f);
    float lateral=(to-dir*proj).length();
    if (lateral<=rad) { along=proj; return proj+lateral*0.35f; }
    return 1e30f;
}

void Weapon_L::playFallback(const std::string& relative, const Vec3& posWorld, float volume, bool spatial) {
    std::string path=wlNormalize(relative);
    Sound_L::Get().play(path,posWorld,volume,spatial,true);
}

void Weapon_L::playScriptSound(const std::string& key, const Vec3& posWorld, float volume) {
    wlLoadSoundScripts(gameDirectory,textureSystem);
    const std::string logicalKey = wlLower(key);
    auto it=definition.sounds.find(logicalKey);
    if(it==definition.sounds.end() && logicalKey=="single_shot") it=definition.sounds.find("double_shot");
    if(it==definition.sounds.end() && logicalKey=="secondary_attack") it=definition.sounds.find("special1");
    std::string eventName = (it == definition.sounds.end()) ? ("Weapon_" + definition.className.substr(7) + "." + logicalKey) : it->second;
    eventName=wlNormalize(eventName);
    std::vector<std::string> waves;
    auto se=gSoundEvents.find(wlLower(eventName));
    if (se==gSoundEvents.end()) {
                                                                                                            
        std::string alt=wlLower(eventName);
        size_t dot=alt.find('.');
        if(dot!=std::string::npos && alt.substr(0,dot).rfind("weapon_",0)==0) {
            alt=alt.substr(0,dot)+alt.substr(dot);
        }
        se=gSoundEvents.find(alt);
    }
    if (se!=gSoundEvents.end()) waves=se->second;
    if (waves.empty()) {
        const std::string direct = wlNormalize(eventName);
        const std::string directLower = wlLower(direct);
        if (directLower.rfind("sound/",0)==0 && directLower.size()>4 && directLower.rfind(".wav") == directLower.size()-4) {
            waves.push_back(direct);
        }
    }
    if (waves.empty()) {
        std::string stem=wlLower(definition.className.substr(7));
        std::string file;
        if(key=="reload") file="sound/weapons/"+stem+"_reload.wav";
        else if(key=="single_shot" || key=="single") file="sound/weapons/"+stem+"_fire1.wav";
        else if(key=="empty") file="sound/weapons/"+stem+"_empty.wav";
        else if(key=="deploy") file="sound/weapons/"+stem+"_draw.wav";
        else if(key=="melee_hit") file="sound/weapons/"+stem+"_hit.wav";
        else if(key=="melee_hit_world") file="sound/weapons/"+stem+"_hit1.wav";
        if(!file.empty()) waves.push_back(file);
    }
    if(waves.empty()) return;
    std::string chosen=waves[(size_t)(std::rand()%waves.size())];
    std::replace(chosen.begin(), chosen.end(), '\\', '/');
    std::string low=wlLower(chosen);
    if(low.rfind("sound/",0)!=0) chosen="sound/"+chosen;
                                                                            
                                                                                  
    Sound_L::Get().play(chosen,posWorld,volume,true,true);
}

void Weapon_L::playRandomScriptSound(const std::string& key, const Vec3& posWorld, float volume) {
    playScriptSound(key,posWorld,volume);
}

bool Weapon_L::playWeaponAnimation(const std::vector<std::string>& hints) {
    if(!viewModel.loaded) return false;
    for(const std::string& h:hints) {
        if(viewModel.playAnimationHint(h)) return true;
        if(viewModel.playSequence(h)) return true;
        if(viewModel.playActivity(h)) return true;
    }
    return false;
}

void Weapon_L::primaryAttack(Camera& player) {
    if(!isActive(player) || attackCooldown>0.0f) return;
    if (sauceRuntime.hasExecutableAdapter() && sauceRuntime.primaryAttack(attackTime)) {
        attackCooldown = std::max(0.05f, definition.cycleTime);
        attackActive = true;
        attackTime = 0.0f;
        if(viewModel.loaded) playWeaponAnimation(knowledge.primaryAnimationHints.empty()?std::vector<std::string>{"primaryattack","act_vm_primaryattack","fire","shoot"}:knowledge.primaryAnimationHints);
        return;
    }

    const bool hasClip = definition.usesPrimaryClip && definition.clipSize > 0;
    const bool hasReserveOnly = !hasClip && !definition.primaryAmmo.empty();
    const bool dry = (hasClip && clipAmmo<=0) || (hasReserveOnly && reserveAmmo<=0);
    if(dry) {
        if(viewModel.loaded) playWeaponAnimation({"dryfire","act_vm_dryfire","empty"});
        playScriptSound("empty",player.pos,0.9f);
        attackCooldown=std::max(0.12f,definition.cycleTime);
        return;
    }

    if(isMeleeWeapon()) {
        attackActive=true; impactDone=false; attackTime=0.0f;
        attackCooldown=knowledge.sauceCycleTime>0.01f?knowledge.sauceCycleTime:0.30f;
        if(viewModel.loaded) playWeaponAnimation(knowledge.primaryAnimationHints.empty()?std::vector<std::string>{"act_vm_hitcenter","primaryattack","act_vm_primaryattack","fire","shoot"}:knowledge.primaryAnimationHints);
        playScriptSound("single_shot",player.pos,0.9f);
        return;
    }

    if(hasClip) --clipAmmo; else if(hasReserveOnly) --reserveAmmo;
    attackActive=true; firePending=true; attackTime=0.0f;
    attackCooldown=std::max(0.05f, definition.cycleTime);
    if(viewModel.loaded) {
        if (definition.profileShotgun) playWeaponAnimation(knowledge.primaryAnimationHints.empty()?std::vector<std::string>{"primaryattack","act_vm_primaryattack","fire","shoot"}:knowledge.primaryAnimationHints);
        else if (definition.profileRevolver) playWeaponAnimation(knowledge.primaryAnimationHints.empty()?std::vector<std::string>{"primaryattack","act_vm_primaryattack","fire","shoot"}:knowledge.primaryAnimationHints);
        else playWeaponAnimation(knowledge.primaryAnimationHints.empty()?std::vector<std::string>{"primaryattack","act_vm_primaryattack","fire","shoot"}:knowledge.primaryAnimationHints);
    }
    playScriptSound(definition.profileShotgun ? "single_shot" : "single_shot", player.pos, 1.0f);
    if (knowledge.hasViewPunch || definition.profileRevolver || definition.profileShotgun || definition.profileMachineGun) {
        const float kick=definition.viewKickDegrees;
        player.pitch -= kick;
        player.yaw += ((std::rand()%2001)/1000.0f-1.0f)*(definition.profileRevolver?1.0f:0.35f);
    }
}

void Weapon_L::secondaryAttack(Camera& player) {
    if (!isActive(player) || !knowledge.hasSecondaryAttack || attackCooldown>0.0f || isMeleeWeapon()) return;
    if(!definition.secondaryAmmo.empty() && secondaryReserveAmmo<=0){
        if(viewModel.loaded) playWeaponAnimation({"dryfire","act_vm_dryfire","empty"});
        playScriptSound("empty",player.pos,0.85f);
        attackCooldown=0.2f;
        return;
    }
    if(!definition.secondaryAmmo.empty()) --secondaryReserveAmmo;
    if (viewModel.loaded) playWeaponAnimation(knowledge.secondaryAnimationHints.empty()?std::vector<std::string>{"secondaryattack","act_vm_secondaryattack","attack2"}:knowledge.secondaryAnimationHints);
    playScriptSound("special1", player.pos, 1.0f);
    attackActive = true;
    attackTime = 0.0f;
    attackCooldown = definition.profileRPG ? 0.9f : 0.5f;
    player.pitch -= definition.profileRPG ? 3.0f : 0.5f;
}

void Weapon_L::reload(Camera& player) {
    if(!isActive(player)) return;
    if (sauceRuntime.hasExecutableAdapter() && sauceRuntime.reload(attackTime)) return;
    if(!definition.usesPrimaryClip || definition.clipSize<=0 || reserveAmmo<=0 || attackActive) return;

    if (definition.reloadSingleShell) {
        if (clipAmmo>=definition.clipSize) return;
        ++clipAmmo;
        --reserveAmmo;
        attackCooldown=definition.reloadDuration;
        if(viewModel.loaded) playWeaponAnimation(knowledge.reloadAnimationHints.empty()?std::vector<std::string>{"reload","act_vm_reload"}:knowledge.reloadAnimationHints);
        playScriptSound("reload",player.pos,0.85f);
        return;
    }

    if(clipAmmo>=definition.clipSize) return;
    const int need=definition.clipSize-clipAmmo;
    const int take=std::min(need,reserveAmmo);
    if(take<=0) return;
    clipAmmo+=take; reserveAmmo-=take;
    attackCooldown=definition.reloadDuration;
    if(viewModel.loaded) playWeaponAnimation(knowledge.reloadAnimationHints.empty()?std::vector<std::string>{"reload","act_vm_reload"}:knowledge.reloadAnimationHints);
    playScriptSound("reload",player.pos,0.85f);
}

void Weapon_L::performHitscanAttack(Camera& player, BSP_L* map, EntityManager* entities) {
    float deg=3.14159265358979323846f/180.0f;
    float yaw=player.yaw*deg,pitch=player.pitch*deg,cp=std::cos(pitch);
    int shotCount=std::max(1,definition.fireBulletsCount);
    for(int shot=0; shot<shotCount; ++shot){
        Vec3 dir{std::sin(yaw)*cp,std::sin(pitch),-std::cos(yaw)*cp};
        if(definition.spread>0.0f){
            float a=((std::rand()%20001)/10000.0f-1.0f)*definition.spread;
            float b=((std::rand()%20001)/10000.0f-1.0f)*definition.spread;
            Vec3 right{std::cos(yaw),0,std::sin(yaw)};
            Vec3 up=right.cross(dir).normalize();
            dir=(dir+right*a+up*b).normalize();
        } else {
            dir=dir.normalize();
        }
        float worldDist=30000.0f;
        Vec3 worldNormal;
        bool hitWorld=map && map->traceRay(player.pos,dir,worldDist,worldNormal);
        PropPhysics* best=nullptr;
        float bestDist=30000.0f;
        if(entities){
            for(Entity* e: entities->entities){
                PropPhysics* p=dynamic_cast<PropPhysics*>(e);
                if(!p || p->held || p->solid==0) continue;
                float d=0.0f; Vec3 n;
                if(p->traceRay(player.pos,dir,d,n) && d<bestDist){ best=p; bestDist=d; }
            }
        }
        bool hitProp=best && (!hitWorld || bestDist<worldDist);
        if(hitProp){
            Vec3 hitPoint=player.pos+dir*bestDist;
            float impulse=std::max(160.0f,definition.damage*10.0f);
            best->applyImpulse(dir*impulse,hitPoint);
            playFallback("sound/weapons/bullet_hit1.wav",hitPoint,0.5f,true);
        } else if(hitWorld){
            Vec3 hitPoint=player.pos+dir*worldDist;
            playFallback("sound/weapons/ric1.wav",hitPoint,0.45f,true);
        }
    }
}

void Weapon_L::performMeleeImpact(Camera& player, BSP_L* map, EntityManager& entities) {
    if (impactDone) return;
    impactDone=true;
    float deg=3.14159265358979323846f/180.0f;
    float yaw=player.yaw*deg, pitch=player.pitch*deg;
    float cp=std::cos(pitch);
    Vec3 dir{std::sin(yaw)*cp,std::sin(pitch),-std::cos(yaw)*cp};
    dir=dir.normalize();
    Vec3 origin=player.pos;
    float worldDist=meleeRange+1.0f;
    Vec3 worldNormal;
    bool hitWorld=map && map->traceRay(origin,dir,worldDist,worldNormal) && worldDist<=meleeRange;
    PropPhysics* best=nullptr;
    float bestDist=meleeRange+1.0f;
    for (Entity* e : entities.entities) {
        PropPhysics* p=dynamic_cast<PropPhysics*>(e);
        if (!p || p->held || p->solid==0) continue;
        float d=0.0f; Vec3 n;
        if (p->traceRay(origin,dir,d,n) && d<bestDist && d<=meleeRange) { best=p; bestDist=d; }
    }
    if (best && (!hitWorld || bestDist<worldDist)) {
        Vec3 hitPoint=origin+dir*bestDist;
        best->applyImpulse(dir*420.0f,hitPoint);
        playRandomScriptSound("melee_hit",hitPoint,1.0f);
    } else if (hitWorld) {
        playRandomScriptSound("melee_hit_world",origin+dir*worldDist,1.0f);
    }
}

void Weapon_L::updateWeapon(float dt, Camera& player, BSP_L* map, EntityManager& entities) {
    if(!gWeaponInstances.empty() && gWeaponInstances.front()==this) wlHandleSlotKeys(player);
    sauceRuntime.frame(dt);
    sauceRuntime.itemPostFrame(dt);
    if(viewModel.loaded) viewModel.updateAnimation(dt);
    if (attackCooldown>0.0f) { attackCooldown-=dt; if (attackCooldown<0) attackCooldown=0; }
    if (!isActive(player)) { attackActive=false; attackTime=0; impactDone=false; firePending=false; return; }
    if (firePending) {
        firePending=false;
        if (definition.hitscan) performHitscanAttack(player,map,&entities);
    }
    if (!isMeleeWeapon()) {
        if(attackActive){
            attackTime+=dt;
            const float activeDuration = std::max(0.08f, std::min(0.42f, definition.cycleTime * 0.65f));
            if(attackTime>=activeDuration){
                attackActive=false; attackTime=0.0f;
                if (definition.pumpAfterShot && clipAmmo>0 && viewModel.loaded) {
                    if(!knowledge.reloadAnimationHints.empty()) playWeaponAnimation(knowledge.reloadAnimationHints);
                    else playWeaponAnimation({"act_shotgun_pump","pump","shotgun_pump","reload_finish","finishreload"});
                }
            }
        }
        return;
    }
    if (!attackActive) return;
    attackTime+=dt;
    if (!impactDone && attackTime>=0.18f) performMeleeImpact(player,map,entities);
    if (attackTime>=0.46f) { attackActive=false; attackTime=0; impactDone=false; }
}

void Weapon_L::renderViewModel(const Camera& player, float bobSide, float bobUp, float bobRoll, float bobBlend, float bobTime) {
    if(!isActive(player) || !viewModel.loaded) return;

    GLint vp[4]={0,0,1280,720};
    glGetIntegerv(GL_VIEWPORT,vp);
    const float aspect=(vp[3]>0)?(float)vp[2]/(float)vp[3]:16.0f/9.0f;
    const float fov=75.0f;
    const float halfTan=std::tan(fov*3.14159265358979323846f/360.0f);
    const float right=halfTan*aspect;

                                                                                              
                                                                                   
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glFrustum(-right,right,-halfTan,halfTan,0.25f,4096.0f);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glPushAttrib(GL_ENABLE_BIT|GL_DEPTH_BUFFER_BIT|GL_LIGHTING_BIT|GL_CURRENT_BIT|GL_CULL_FACE|GL_TEXTURE_BIT|GL_COLOR_BUFFER_BIT);

    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glDepthMask(GL_FALSE);
    glColor4f(1,1,1,1);

    const float s=std::sin(bobTime);
    const float weaponSide=bobSide*0.90f;
    const float weaponUp=bobUp*0.85f;
    float recoil=0.0f;
    if(attackActive) {
        const float t=std::clamp(attackTime/std::max(attackInterval,0.05f),0.0f,1.0f);
        recoil=std::sin(t*3.14159265358979323846f);
    }

    float radius=24.0f;
    if(viewModel.hasRenderBounds) {
        const Vec3 ext=(viewModel.renderBoundsMax-viewModel.renderBoundsMin)*0.5f;
        radius=std::max(1.0f,ext.length());
    }

                                                                                                     
    float fitScale=1.0f;
    if(radius>55.0f) fitScale=55.0f/radius;
    else if(radius<7.0f) fitScale=7.0f/radius;
    fitScale=std::clamp(fitScale,0.08f,2.5f);

                                                                                                   
                                                                                            
                                                                 
    glTranslatef(3.0f+weaponSide,
                 -5.0f+weaponUp,
                 -30.0f-recoil*0.65f);
    glRotatef(1.8f*s*bobBlend+bobRoll*1.5f,0,0,1);
    glRotatef(-6.0f-recoil*10.0f,0,1,0);
    glRotatef(-90.0f,1,0,0);
    glScalef(fitScale,fitScale,fitScale);

    viewModel.render();

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glPopAttrib();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}
