#include "npc_l.h"
#include "bsp_l.h"
#include "sound_l.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <GLFW/glfw3.h>

struct NPCVisualCache_L {
    std::shared_ptr<MDL_L> model;
    Vec3 sourceMin{0,0,0};
    Vec3 sourceMax{0,0,0};
    unsigned int renderList=0;
    Texture_L* textureSystem=nullptr;
    double lastAnimTime=-1.0;
};

static SauceNPCRuntime_L gNpcRuntime;
static bool gNpcRuntimeReady=false;
static bool gNpcRuntimeAttempted=false;
static std::unordered_set<std::string> gNpcBoundClasses;
static std::unordered_map<std::string,std::shared_ptr<NPCVisualCache_L>> gNpcVisualCache;

static std::string npcLower(std::string s){std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return (char)std::tolower(c);});return s;}
static std::string npcNormalize(std::string s){std::replace(s.begin(),s.end(),'\\','/');return s;}
static float npcFloat(const std::string& value,float fallback){if(value.empty())return fallback;try{return std::stof(value);}catch(...){return fallback;}}
static int npcInt(const std::string& value,int fallback){if(value.empty())return fallback;try{return std::stoi(value);}catch(...){return fallback;}}
static Vec3 npcParseVec3(const std::string& value,const Vec3& fallback){if(value.empty())return fallback;std::istringstream ss(value);Vec3 out{};if(!(ss>>out.x>>out.y>>out.z))return fallback;return out;}
static std::string npcDefaultModel(const std::string& rawClass){const std::string cls=npcLower(rawClass);static const std::map<std::string,std::string> defaults={{"npc_citizen","models/humans/group01/male_07.mdl"},{"npc_metropolice","models/police.mdl"},{"npc_combine_s","models/combine_soldier.mdl"},{"npc_zombie","models/zombie/classic.mdl"},{"npc_zombine","models/zombie/zombine/zombie_soldier.mdl"},{"npc_fastzombie","models/zombie/fast.mdl"},{"npc_headcrab","models/headcrabclassic.mdl"},{"npc_headcrab_fast","models/headcrabfast.mdl"},{"npc_headcrab_black","models/headcrabblack.mdl"},{"npc_antlion","models/antlion.mdl"},{"npc_antlionguard","models/antlion_guard.mdl"},{"npc_vortigaunt","models/vortigaunt.mdl"},{"npc_manhack","models/manhack.mdl"},{"npc_rollermine","models/roller.mdl"},{"npc_stalker","models/stalker.mdl"},{"npc_turret_floor","models/combine_turrets/floor_turret.mdl"},{"npc_turret_ceiling","models/combine_turrets/ceiling_turret.mdl"},{"npc_turret_ground","models/combine_turrets/floor_turret.mdl"},{"npc_l","models/humans/group01/male_07.mdl"}};auto it=defaults.find(cls);return it!=defaults.end()?it->second:std::string();}
static std::string npcClassName(const std::string& block){return EntityManager::extractEntityValue(block,"classname");}
static bool npcValidBounds(const Vec3& mn,const Vec3& mx){return std::isfinite(mn.x)&&std::isfinite(mn.y)&&std::isfinite(mn.z)&&std::isfinite(mx.x)&&std::isfinite(mx.y)&&std::isfinite(mx.z)&&(mx.x-mn.x)>0.01f&&(mx.y-mn.y)>0.01f&&(mx.z-mn.z)>0.01f;}
static void npcNormalizeBounds(Vec3& mn,Vec3& mx){if(mn.x>mx.x)std::swap(mn.x,mx.x);if(mn.y>mx.y)std::swap(mn.y,mx.y);if(mn.z>mx.z)std::swap(mn.z,mx.z);}
static Vec3 npcRotateZ(const Vec3& v,float degrees){const float r=degrees*3.14159265358979323846f/180.0f;const float c=std::cos(r),s=std::sin(r);return {v.x*c-v.y*s,v.x*s+v.y*c,v.z};}
static Vec3 npcRotateX(const Vec3& v,float degrees){const float r=degrees*3.14159265358979323846f/180.0f;const float c=std::cos(r),s=std::sin(r);return {v.x,v.y*c-v.z*s,v.y*s+v.z*c};}
static Vec3 npcRotateY(const Vec3& v,float degrees){const float r=degrees*3.14159265358979323846f/180.0f;const float c=std::cos(r),s=std::sin(r);return {v.x*c+v.z*s,v.y,-v.x*s+v.z*c};}
static Vec3 npcSourceToEngine(const Vec3& v){return {v.x,v.z,-v.y};}
static Vec3 npcModelVectorToWorld(const Vec3& source,const Vec3& angles,float scale){Vec3 v=npcSourceToEngine(source)*scale;v=npcRotateZ(v,angles.z);v=npcRotateX(v,-angles.x);v=npcRotateY(v,angles.y);return v;}
static void npcBuildCollisionBox(const Vec3& sourceMin,const Vec3& sourceMax,const Vec3& angles,float scale,Vec3& outMin,Vec3& outMax){outMin={1e30f,1e30f,1e30f};outMax={-1e30f,-1e30f,-1e30f};for(int ix=0;ix<2;++ix)for(int iy=0;iy<2;++iy)for(int iz=0;iz<2;++iz){Vec3 p{ix?sourceMax.x:sourceMin.x,iy?sourceMax.y:sourceMin.y,iz?sourceMax.z:sourceMin.z};Vec3 w=npcModelVectorToWorld(p,angles,scale);outMin.x=std::min(outMin.x,w.x);outMin.y=std::min(outMin.y,w.y);outMin.z=std::min(outMin.z,w.z);outMax.x=std::max(outMax.x,w.x);outMax.y=std::max(outMax.y,w.y);outMax.z=std::max(outMax.z,w.z);}}
static bool npcRayAabb(const Vec3& origin,const Vec3& direction,const Vec3& mn,const Vec3& mx,float& outT,int& outAxis,bool& outPositive){float tMin=-1e30f,tMax=1e30f;outAxis=0;outPositive=false;const float bounds[2][3]={{mn.x,mn.y,mn.z},{mx.x,mx.y,mx.z}};const float o[3]={origin.x,origin.y,origin.z};const float d[3]={direction.x,direction.y,direction.z};for(int i=0;i<3;++i){if(std::fabs(d[i])<1e-8f){if(o[i]<bounds[0][i]||o[i]>bounds[1][i])return false;continue;}float t1=(bounds[0][i]-o[i])/d[i],t2=(bounds[1][i]-o[i])/d[i];bool swapped=false;if(t1>t2){std::swap(t1,t2);swapped=true;}if(t1>tMin){tMin=t1;outAxis=i;outPositive=!swapped;}tMax=std::min(tMax,t2);if(tMin>tMax)return false;}if(tMax<0)return false;outT=tMin>0.0001f?tMin:tMax;return outT>=0.0001f;}
static Vec3 npcBoxNormal(int axis,bool positive){if(axis==0)return positive?Vec3{-1,0,0}:Vec3{1,0,0};if(axis==1)return positive?Vec3{0,-1,0}:Vec3{0,1,0};return positive?Vec3{0,0,-1}:Vec3{0,0,1};}
static unsigned int npcBuildDisplayList(const MDL_L& mdl,Texture_L* texL){if(mdl.meshes.empty())return 0;GLuint list=glGenLists(1);if(!list)return 0;glNewList(list,GL_COMPILE);for(const auto& mesh:mdl.meshes){const bool alphaMesh=texL&&mesh.glTexture!=0&&texL->textureHasAlpha(mesh.glTexture);if(mesh.glTexture)glBindTexture(GL_TEXTURE_2D,mesh.glTexture);if(alphaMesh){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_ALPHA_TEST);glAlphaFunc(GL_GREATER,0.01f);}else{glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);}glBegin(GL_TRIANGLES);for(size_t i=0;i+2<mesh.indices.size();i+=3){for(int t=0;t<3;++t){const auto&v=mesh.vertices[mesh.indices[i+(size_t)t]];glTexCoord2f(v.texCoord.x,v.texCoord.y);glNormal3f(v.normal.x,v.normal.y,v.normal.z);glVertex3f(v.position.x,v.position.y,v.position.z);}}glEnd();}glDisable(GL_BLEND);glDisable(GL_ALPHA_TEST);glEndList();return list;}

NPC_L::NPC_L(const std::string& block,const Vec3& p,const std::string& gDir,Texture_L* texL){
    pos=p;
    gameDirectory=gDir;
    className=npcLower(npcClassName(block));
    if(className.empty())className="npc_l";
    if(!gNpcRuntimeAttempted){
        gNpcRuntimeReady=gNpcRuntime.initialize(gameDirectory,{},{});
        gNpcRuntimeAttempted=true;
    }
    if(gNpcBoundClasses.insert(className).second&&gNpcRuntimeReady)gNpcRuntime.bindClass(className);
    modelPath=npcNormalize(EntityManager::extractEntityValue(block,"model"));
    if(modelPath.empty()&&gNpcRuntimeReady)modelPath=npcNormalize(gNpcRuntime.modelPathForClass(className));
    if(modelPath.empty())modelPath=npcDefaultModel(className);
    angles=npcParseVec3(EntityManager::extractEntityValue(block,"angles"),{0,0,0});
    idleLookYaw=angles.y;
    scale=std::clamp(npcFloat(EntityManager::extractEntityValue(block,"modelscale"),1.0f),0.05f,32.0f);
    maxHealth=std::max(1,npcInt(EntityManager::extractEntityValue(block,"health"),100));
    health=maxHealth;
    auto it=gNpcVisualCache.find(modelPath);
    if(it==gNpcVisualCache.end()){
        auto cache=std::make_shared<NPCVisualCache_L>();
        cache->model=std::make_shared<MDL_L>();
        cache->textureSystem=texL;
        Vec3 sourceMin{-16,-16,0},sourceMax{16,16,72};
        if(!modelPath.empty()&&texL&&cache->model->load(modelPath,*texL,true)){
            sourceMin=cache->model->hullMin;
            sourceMax=cache->model->hullMax;
            if(!npcValidBounds(sourceMin,sourceMax)&&cache->model->hasRenderBounds){sourceMin=cache->model->renderBoundsMin;sourceMax=cache->model->renderBoundsMax;}
            if(!npcValidBounds(sourceMin,sourceMax)){sourceMin={-16,-16,0};sourceMax={16,16,72};}
            npcNormalizeBounds(sourceMin,sourceMax);
            if(cache->model->animated){cache->model->playAnimationHint("idle");cache->model->updateAnimation(0.0f,true);}
            cache->sourceMin=sourceMin;
            cache->sourceMax=sourceMax;
            gNpcVisualCache.emplace(modelPath,cache);
            visualCache=cache;
        }else{
            cache->sourceMin=sourceMin;
            cache->sourceMax=sourceMax;
            gNpcVisualCache.emplace(modelPath,cache);
            visualCache=cache;
        }
    }else{
        visualCache=it->second;
    }
    model=visualCache&&visualCache->model?visualCache->model.get():nullptr;
    if(visualCache){
        Vec3 visualMin=visualCache->sourceMin;
        Vec3 visualMax=visualCache->sourceMax;
        npcNormalizeBounds(visualMin,visualMax);
        npcBuildCollisionBox(visualMin,visualMax,angles,scale,collisionMin,collisionMax);
    }
    if(model&&model->loaded&&model->animated&&!visualCache->renderList){
        visualCache->lastAnimTime=glfwGetTime();
        model->updateAnimation(0.0f,true);
        visualCache->renderList=npcBuildDisplayList(*model,visualCache->textureSystem);
    }
}

NPC_L::~NPC_L(){}

void NPC_L::update(float dt,Camera&,BSP_L*){
    if(!visualCache||!visualCache->model||!visualCache->model->animated)return;
    const double now=glfwGetTime();
    if(visualCache->lastAnimTime<0.0){visualCache->lastAnimTime=now;return;}
    const double delta=now-visualCache->lastAnimTime;
    if(delta<1.0/30.0)return;
    const float step=(float)std::min(0.12,std::max(0.0,delta));
    visualCache->model->updateAnimation(step,true);
    if(visualCache->renderList)glDeleteLists(visualCache->renderList,1);
    visualCache->renderList=npcBuildDisplayList(*visualCache->model,visualCache->textureSystem);
    visualCache->lastAnimTime=now;
}

void NPC_L::render(){
    if(!visualCache)return;
    if(visualCache->renderList){
        glPushMatrix();
        glTranslatef(pos.x,pos.y,pos.z);
        glScalef(scale,scale,scale);
        glRotatef(angles.y,0,1,0);
        glRotatef(-angles.x,1,0,0);
        glRotatef(angles.z,0,0,1);
        glRotatef(-90.0f,1,0,0);
        glCallList(visualCache->renderList);
        glPopMatrix();
        return;
    }
    if(model&&model->loaded){
        glPushMatrix();
        glTranslatef(pos.x,pos.y,pos.z);
        glScalef(scale,scale,scale);
        glRotatef(angles.y,0,1,0);
        glRotatef(-angles.x,1,0,0);
        glRotatef(angles.z,0,0,1);
        glRotatef(-90.0f,1,0,0);
        model->render();
        glPopMatrix();
    }
}

void NPC_L::takeDamage(float damage,const Vec3&,const Vec3&){if(health<=0||damage<=0)return;health=std::max(0,health-std::max(1,(int)std::ceil(damage)));if(health==0){shouldDestroy=true;Sound_L::Get().play("sound/weapons/bodydrop3.wav",pos,0.55f,true,true);}}

bool NPC_L::traceRay(const Vec3& orig,const Vec3& dir,float& outDist,Vec3& outNormal){if(health<=0||shouldDestroy||!visualCache)return false;const Vec3 mn=pos+collisionMin,mx=pos+collisionMax;int axis=0;bool positive=false;float t=0;if(!npcRayAabb(orig,dir,mn,mx,t,axis,positive))return false;outDist=t;outNormal=npcBoxNormal(axis,positive);return outDist>0.0001f;}

void NPC_L::updateAI(float dt,Camera& player,BSP_L* map,EntityManager& entities){
    if(health<=0)return;
    const float radius=520.0f;
    const float radiusSq=radius*radius;
    const float safeDt=std::max(0.001f,dt);
    auto distSq=[&](const Vec3& a,const Vec3& b){const Vec3 d=a-b;return d.x*d.x+d.y*d.y+d.z*d.z;};
    auto validEntity=[&](Entity* e){
        if(!e||e==this||e->shouldDestroy)return false;
        if(auto* npc=dynamic_cast<NPC_L*>(e))return npc->isAlive();
        return true;
    };
    for(auto it=lastObservedPositions.begin();it!=lastObservedPositions.end();){
        bool alive=false;
        for(Entity* e:entities.entities)if(e==it->first){alive=true;break;}
        if(!alive)it=lastObservedPositions.erase(it);else++it;
    }
    for(auto it=activityScores.begin();it!=activityScores.end();){
        bool alive=false;
        for(Entity* e:entities.entities)if(e==it->first){alive=true;break;}
        if(!alive)it=activityScores.erase(it);else++it;
    }
    auto motionFor=[&](Entity* e){
        auto it=lastObservedPositions.find(e);
        float speed=0.0f;
        if(it!=lastObservedPositions.end())speed=(e->pos-it->second).length()/safeDt;
        lastObservedPositions[e]=e->pos;
        float& stored=activityScores[e];
        stored=stored*std::exp(-safeDt*3.0f)+speed;
        return std::clamp(stored,0.0f,520.0f);
    };

    std::vector<Sound_L::SoundEvent> sounds;
    Sound_L::Get().getRecentSounds(sounds,1.1);
    bool heardNewSound=false;
    float bestSoundScore=-1.0f;
    for(const auto& sound:sounds){
        if(sound.time<=lastHeardSoundTime)continue;
        const float d=std::sqrt(distSq(pos,sound.position));
        const float hearingRadius=std::clamp(700.0f+sound.volume*500.0f,700.0f,2600.0f);
        if(d>hearingRadius)continue;
        if(map){
            const Vec3 ray=sound.position-pos;
            const float rayLen=ray.length();
            if(rayLen>1.0f){
                float hitDist=0.0f; Vec3 hitNormal;
                if(map->traceRay(pos,ray.normalize(),hitDist,hitNormal)&&hitDist+8.0f<rayLen)continue;
            }
        }
        const float score=sound.volume*std::max(0.0f,1.0f-d/hearingRadius);
        if(score>bestSoundScore){
            bestSoundScore=score;
            soundLookPos=sound.position;
            lastHeardSoundTime=sound.time;
            heardNewSound=true;
        }
    }
    if(heardNewSound){
        lookAtSound=true;
        soundFocusTimer=std::clamp(0.7f+bestSoundScore*1.1f,0.7f,1.8f);
        lookReaction=std::clamp(0.16f+(1.0f-bestSoundScore)*0.42f,0.16f,0.58f);
        lookAtPlayer=false;
        lookTarget=nullptr;
    }
    if(soundFocusTimer>0.0f){
        soundFocusTimer=std::max(0.0f,soundFocusTimer-dt);
        lookAtSound=true;
        if(lookReaction>0.0f){
            lookReaction=std::max(0.0f,lookReaction-dt);
            return;
        }
        const Vec3 to=soundLookPos-pos;
        if(to.length()>0.001f){
            const float desired=std::atan2(to.x,-to.z)*180.0f/3.14159265358979323846f;
            float delta=desired-angles.y;
            while(delta>180.0f)delta-=360.0f;
            while(delta<-180.0f)delta+=360.0f;
            const float step=std::max(0.0f,dt)*130.0f;
            angles.y+=std::clamp(delta,-step,step);
            if(angles.y>180.0f)angles.y-=360.0f;
            if(angles.y<-180.0f)angles.y+=360.0f;
        }
        idleLookTimer=2.0f;
        if(soundFocusTimer<=0.0f)lookAtSound=false;
        return;
    }
    lookAtSound=false;

    const float playerDistSq=distSq(pos,player.pos);
    const float playerSpeed=std::sqrt(player.velocity.x*player.velocity.x+player.velocity.z*player.velocity.z);
    const float playerActivity=std::clamp(playerSpeed/185.0f,0.0f,2.75f)+(player.onGround&&playerSpeed>45.0f?0.5f:0.0f);

    bool candidateIsPlayer=false;
    Entity* candidate=nullptr;
    float bestScore=1e30f;
    if(playerDistSq<=radiusSq){
        const float distance=std::sqrt(playerDistSq);
        bestScore=distance/(1.0f+playerActivity*1.45f);
        candidateIsPlayer=true;
    }
    for(Entity* e:entities.entities){
        if(!validEntity(e))continue;
        const float d2=distSq(pos,e->pos);
        if(d2<25.0f||d2>radiusSq)continue;
        const float distance=std::sqrt(d2);
        const float speed=motionFor(e);
        const float activity=std::clamp(speed/175.0f,0.0f,2.75f);
        const float score=distance/(1.0f+activity*1.35f);
        if(score<bestScore){
            bestScore=score;
            candidate=e;
            candidateIsPlayer=false;
        }
    }

    bool currentValid=false;
    Vec3 currentPos{};
    float currentDistSq=radiusSq;
    if(lookAtPlayer){
        currentPos=player.pos;
        currentDistSq=playerDistSq;
        currentValid=currentDistSq<=radiusSq;
    }else if(lookTarget&&validEntity(lookTarget)){
        currentPos=lookTarget->pos;
        currentDistSq=distSq(pos,currentPos);
        currentValid=currentDistSq<=radiusSq;
    }

    if(!currentValid){
        if(candidateIsPlayer||candidate){
            const bool changed=(lookAtPlayer!=candidateIsPlayer)||(lookTarget!=candidate);
            if(changed){
                lookAtPlayer=candidateIsPlayer;
                lookTarget=candidate;
                lookReaction=0.55f;
            }
        }else{
            lookAtPlayer=false;
            lookTarget=nullptr;
            lookReaction=0.0f;
        }
    }else{
        const float currentDistance=std::sqrt(currentDistSq);
        float currentActivity=0.0f;
        if(lookAtPlayer){
            currentActivity=playerActivity;
        }else if(lookTarget){
            currentActivity=std::clamp(motionFor(lookTarget)/175.0f,0.0f,2.75f);
        }
        const float currentScore=currentDistance/(1.0f+currentActivity*(lookAtPlayer?1.45f:1.35f));
        const bool candidateChanged=(lookAtPlayer!=candidateIsPlayer)||(lookTarget!=candidate);
        if(candidateChanged&&bestScore+0.15f<currentScore){
            lookAtPlayer=candidateIsPlayer;
            lookTarget=candidate;
            lookReaction=0.6f;
        }
    }

    if(lookAtPlayer||lookTarget){
        const Vec3 target=lookAtPlayer?player.pos:lookTarget->pos;
        if(lookReaction>0.0f){
            lookReaction=std::max(0.0f,lookReaction-dt);
            return;
        }
        const Vec3 to=target-pos;
        if(to.length()<0.001f)return;
        const float desired=std::atan2(to.x,-to.z)*180.0f/3.14159265358979323846f;
        float delta=desired-angles.y;
        while(delta>180.0f)delta-=360.0f;
        while(delta<-180.0f)delta+=360.0f;
        const float step=std::max(0.0f,dt)*125.0f;
        angles.y+=std::clamp(delta,-step,step);
        if(angles.y>180.0f)angles.y-=360.0f;
        if(angles.y<-180.0f)angles.y+=360.0f;
        idleLookTimer=2.3f;
    }else{
        idleLookTimer-=dt;
        if(idleLookTimer<=0.0f){
            const float t=(float)glfwGetTime();
            const float offset=(std::sin(t*0.71f+pos.x*0.013f+pos.z*0.017f)*0.5f+0.5f)*80.0f-40.0f;
            idleLookYaw=angles.y+offset;
            while(idleLookYaw>180.0f)idleLookYaw-=360.0f;
            while(idleLookYaw<-180.0f)idleLookYaw+=360.0f;
            idleLookTimer=2.0f+(std::sin(t*0.37f+pos.x*0.017f+pos.z*0.011f)*0.5f+0.5f)*2.0f;
        }
        float delta=idleLookYaw-angles.y;
        while(delta>180.0f)delta-=360.0f;
        while(delta<-180.0f)delta+=360.0f;
        const float step=std::max(0.0f,dt)*60.0f;
        angles.y+=std::clamp(delta,-step,step);
        if(angles.y>180.0f)angles.y-=360.0f;
        if(angles.y<-180.0f)angles.y+=360.0f;
    }
}

void NPC_L::shutdownRuntime(){for(auto& entry:gNpcVisualCache)if(entry.second&&entry.second->renderList)glDeleteLists(entry.second->renderList,1);gNpcVisualCache.clear();gNpcBoundClasses.clear();gNpcRuntime.shutdown();gNpcRuntimeReady=false;gNpcRuntimeAttempted=false;}
