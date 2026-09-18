#include "step_sound_l.h"
#include "sound_l.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

static std::string ssLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return s;
}

void StepSound_L::init(const std::string& gameDir, Texture_L* texSystem) { gameDirectory=gameDir; texL=texSystem; }

std::string StepSound_L::getSurfaceProp(const std::string& texName) {
    std::string key=ssLower(texName);
    std::replace(key.begin(), key.end(), '\\', '/');
    if (key.empty()) return "concrete";
    auto it=textureToSurfaceMap.find(key);
    if (it!=textureToSurfaceMap.end()) return it->second;
    std::string surface="concrete";
    std::vector<char> data;
    if (texL && texL->getFileRawData("materials/"+key+".vmt", data) && !data.empty()) {
        std::string text(data.begin(), data.end());
        std::string low=ssLower(text);
        size_t p=low.find("$surfaceprop");
        if (p!=std::string::npos) {
            size_t q=low.find_first_not_of(" \t\r\n\"", p+12);
            if (q!=std::string::npos) {
                if (low[q]=='\"') ++q;
                size_t e=low.find_first_of("\" \t\r\n", q);
                if (e!=std::string::npos && e>q) surface=low.substr(q,e-q);
            }
        }
    }
    textureToSurfaceMap[key]=surface;
    return surface;
}

std::string StepSound_L::soundFamily(const std::string& surface) const {
    std::string s=ssLower(surface);
    if (s.find("chainlink")!=std::string::npos) return "chainlink";
    if (s.find("metalgrate")!=std::string::npos || s.find("metal_grate")!=std::string::npos) return "metalgrate";
    if (s.find("metal")!=std::string::npos) return "metal";
    if (s.find("duct")!=std::string::npos) return "duct";
    if (s.find("grass")!=std::string::npos || s.find("foliage")!=std::string::npos) return "grass";
    if (s.find("mud")!=std::string::npos || s.find("slime")!=std::string::npos) return "mud";
    if (s.find("gravel")!=std::string::npos) return "gravel";
    if (s.find("sand")!=std::string::npos) return "sand";
    if (s.find("snow")!=std::string::npos) return "snow";
    if (s.find("wood")!=std::string::npos) return "wood";
    if (s.find("tile")!=std::string::npos || s.find("porcelain")!=std::string::npos || s.find("glass")!=std::string::npos) return "tile";
    if (s.find("rubber")!=std::string::npos) return "rubber";
    return "concrete";
}

void StepSound_L::update(float dt, bool onGround, const Vec3& velocity, const std::string& currentTexture) {
    if (!onGround) return;
    float speed=std::sqrt(velocity.x*velocity.x+velocity.z*velocity.z);
    if (speed<50.0f) { walkTimer=0.0f; return; }
    walkTimer += speed*dt;
    if (walkTimer<120.0f) return;
    walkTimer=0.0f;
    std::string family=soundFamily(getSurfaceProp(currentTexture));
    int count = 4;
    if (family=="snow" || family=="rubber") count=4;
    int n=1+(std::rand()%count);
    Sound_L::Get().play("sound/player/footsteps/"+family+std::to_string(n)+".wav", Vec3{0,0,0}, 1.0f, false, true);
}
