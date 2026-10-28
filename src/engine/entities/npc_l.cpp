#include "npc_l.h"
#include "bsp_l.h"
#include "sound_l.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <memory>
#include <sstream>
#include <queue>
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
    textureSystem=texL;
    className=npcLower(npcClassName(block));
    if(className.empty())className="npc_l";
    if(!gNpcRuntimeAttempted){
        gNpcRuntimeReady=gNpcRuntime.initialize(gameDirectory,{},{});
        gNpcRuntimeAttempted=true;
    }
    if(gNpcBoundClasses.insert(className).second&&gNpcRuntimeReady)gNpcRuntime.bindClass(className);
    weaponClass=npcLower(EntityManager::extractEntityValue(block,"weapon"));
    if(weaponClass.empty()) weaponClass=npcLower(EntityManager::extractEntityValue(block,"weaponname"));
    if(weaponClass.empty()) weaponClass=npcLower(EntityManager::extractEntityValue(block,"primaryweapon"));
    if(weaponClass.empty()) {
        if(className.find("metropolice")!=std::string::npos || className.find("combine_s")!=std::string::npos ||
           className.find("combine")!=std::string::npos || className.find("soldier")!=std::string::npos) weaponClass="smg1";
    }
    modelPath=npcNormalize(EntityManager::extractEntityValue(block,"model"));
    if(modelPath.empty()) modelPath=npcNormalize(EntityManager::extractEntityValue(block,"modelname"));
    if(modelPath.empty()) modelPath=npcNormalize(EntityManager::extractEntityValue(block,"npcmodel"));
    if(modelPath.empty()&&gNpcRuntimeReady)modelPath=npcNormalize(gNpcRuntime.modelPathForClass(className));
    if(modelPath.empty())modelPath=npcDefaultModel(className);
    angles=npcParseVec3(EntityManager::extractEntityValue(block,"angles"),{0,0,0});
    idleLookYaw=angles.y;
    scale=std::clamp(npcFloat(EntityManager::extractEntityValue(block,"modelscale"),1.0f),0.05f,32.0f);
    spawnFlags=std::max(0,npcInt(EntityManager::extractEntityValue(block,"spawnflags"),0));
    waitForScriptFlag=(spawnFlags&128)!=0;
    waitTillSeenSatisfied=(spawnFlags&1)==0;
    const std::string sleepStateValue=npcLower(EntityManager::extractEntityValue(block,"sleepstate"));
    if(!sleepStateValue.empty()) {
        if(sleepStateValue=="awake") sleepState=0;
        else if(sleepStateValue=="waiting for threat"||sleepStateValue=="waiting_for_threat"||sleepStateValue=="threat") sleepState=1;
        else if(sleepStateValue=="waiting for pvs"||sleepStateValue=="waiting_for_pvs"||sleepStateValue=="pvs") sleepState=2;
        else if(sleepStateValue=="waiting for input"||sleepStateValue=="waiting_for_input"||sleepStateValue=="input") sleepState=3;
        else if(sleepStateValue=="auto pvs"||sleepStateValue=="auto_pvs") sleepState=4;
        else if(sleepStateValue=="auto pvs after pvs"||sleepStateValue=="auto_pvs_after_pvs") sleepState=5;
        else sleepState=std::clamp(npcInt(sleepStateValue,0),0,5);
    }
    maxHealth=std::max(1,npcInt(EntityManager::extractEntityValue(block,"health"),100));
    health=maxHealth;
    const std::string cls=npcLower(className);
    hostileToPlayer=cls.rfind("npc_zombie",0)==0 || cls=="npc_zombine" || cls=="npc_fastzombie" ||
        cls=="npc_combine_s" || cls=="npc_metropolice" || cls=="npc_antlion" || cls=="npc_antlionguard" ||
        cls=="npc_headcrab" || cls=="npc_headcrab_fast" || cls=="npc_headcrab_black" || cls=="npc_stalker" ||
        cls=="npc_manhack" || cls=="npc_turret_floor" || cls=="npc_turret_ground" || cls=="npc_turret_ceiling";
    const int bodyValue=std::max(0,npcInt(EntityManager::extractEntityValue(block,"body"),0));
    const int skinValue=std::max(0,npcInt(EntityManager::extractEntityValue(block,"skin"),0));
    const std::string cacheKey=npcLower(modelPath)+"|body="+std::to_string(bodyValue)+"|skin="+std::to_string(skinValue);
    auto it=gNpcVisualCache.find(cacheKey);
    if(it==gNpcVisualCache.end()){
        auto cache=std::make_shared<NPCVisualCache_L>();
        cache->model=std::make_shared<MDL_L>();
        cache->textureSystem=texL;
        Vec3 sourceMin{-16,-16,0},sourceMax{16,16,72};
        if(!modelPath.empty()&&texL&&cache->model->load(modelPath,*texL,false,bodyValue,skinValue)){
            sourceMin=cache->model->hullMin;
            sourceMax=cache->model->hullMax;
            if(!npcValidBounds(sourceMin,sourceMax)&&cache->model->hasRenderBounds){sourceMin=cache->model->renderBoundsMin;sourceMax=cache->model->renderBoundsMax;}
            if(!npcValidBounds(sourceMin,sourceMax)){sourceMin={-16,-16,0};sourceMax={16,16,72};}
            npcNormalizeBounds(sourceMin,sourceMax);
            cache->sourceMin=sourceMin;
            cache->sourceMax=sourceMax;
            gNpcVisualCache.emplace(cacheKey,cache);
            visualCache=cache;
        }else{
            cache->sourceMin=sourceMin;
            cache->sourceMax=sourceMax;
            gNpcVisualCache.emplace(cacheKey,cache);
            visualCache=cache;
        }
    }else{
        visualCache=it->second;
    }
    model=visualCache&&visualCache->model?visualCache->model.get():nullptr;
    if(model && model->animated) {


        instanceModel=std::make_shared<MDL_L>(*model);
        model=instanceModel.get();
        model->setAnimationEventContext(pos,true);
        model->playAnimationHint("idle");
        model->updateAnimation(0.0f,true);
        lastAnimTime=glfwGetTime();
        instanceRenderList=npcBuildDisplayList(*model,textureSystem);
    }
    if(visualCache){
        Vec3 visualMin=visualCache->sourceMin;
        Vec3 visualMax=visualCache->sourceMax;
        npcNormalizeBounds(visualMin,visualMax);
        npcBuildCollisionBox(visualMin,visualMax,angles,scale,collisionMin,collisionMax);
        if(!visualCache->model->animated && !visualCache->renderList)
            visualCache->renderList=npcBuildDisplayList(*visualCache->model,visualCache->textureSystem);
    }
}

NPC_L::~NPC_L(){ if(instanceRenderList) glDeleteLists(instanceRenderList,1); instanceRenderList=0; }

void NPC_L::update(float dt,Camera&,BSP_L*){
    if(!instanceModel || !instanceModel->animated)return;
    const double now=glfwGetTime();
    if(lastAnimTime<0.0){lastAnimTime=now;return;}
    const double elapsed=now-lastAnimTime;
    if(elapsed<1.0/60.0 && dt<1.0f/60.0f)return;
    const float step=(float)std::clamp(std::max(elapsed,(double)dt),0.0,0.12);
    instanceModel->setAnimationEventContext(pos,true);
    instanceModel->updateAnimation(step,true);
    if(!sceneControlled && !scriptedControlled && !aiMoving && !aiAttacking && health>0) {
        ensureIdleAnimation();
        idleSoundTimer-=std::max(0.0f,dt);
        if(idleSoundTimer<=0.0f && instanceModel){
            playNPCSound("idle");
            idleSoundTimer=9.0f+std::fmod((float)glfwGetTime()*3.7f+pos.x*0.017f+pos.z*0.013f,8.0f);
        }
    }
    if(instanceRenderList)glDeleteLists(instanceRenderList,1);
    instanceRenderList=npcBuildDisplayList(*instanceModel,textureSystem);
    lastAnimTime=now;
}

void NPC_L::render(){
    if(!visualCache)return;
    const unsigned int renderList=instanceRenderList?instanceRenderList:visualCache->renderList;
    if(renderList){
        glPushMatrix();
        glTranslatef(pos.x,pos.y,pos.z);
        glScalef(scale,scale,scale);
        glRotatef(angles.y,0,1,0);
        glRotatef(-angles.x,1,0,0);
        glRotatef(angles.z,0,0,1);
        glRotatef(-90.0f,1,0,0);
        glCallList(renderList);
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

void NPC_L::takeDamage(float damage,const Vec3&,const Vec3&){
    if(health<=0||damage<=0)return;
    wasDamaged=true;
    waitForScriptFlag=false;
    if((spawnFlags&1)!=0) waitTillSeenSatisfied=true;
    health=std::max(0,health-std::max(1,(int)std::ceil(damage)));
    playNPCSound(health==0?"die":"pain");
    if(health==0){shouldDestroy=true;Sound_L::Get().play("sound/weapons/bodydrop3.wav",pos,0.55f,true,false);}
}

bool NPC_L::traceRay(const Vec3& orig,const Vec3& dir,float& outDist,Vec3& outNormal){if(health<=0||shouldDestroy||!visualCache)return false;const Vec3 mn=pos+collisionMin,mx=pos+collisionMax;int axis=0;bool positive=false;float t=0;if(!npcRayAabb(orig,dir,mn,mx,t,axis,positive))return false;outDist=t;outNormal=npcBoxNormal(axis,positive);return outDist>0.0001f;}

bool NPC_L::playNPCSequence(const std::string& name){
    if(!model || !model->loaded || !model->animated || name.empty()) return false;
    const std::string current=npcLower(model->currentSequenceName());
    const std::string wanted=npcLower(name);
    auto has=[&](const char* token){return current.find(token)!=std::string::npos;};
    if(current==wanted || (!wanted.empty() && current.find(wanted)!=std::string::npos)) return true;
    if(wanted.find("walk")!=std::string::npos||wanted.find("run")!=std::string::npos||wanted.find("move")!=std::string::npos){
        if(has("walk")||has("run")||has("locomotion")||has("moving"))return true;
    }
    if(wanted.find("idle")!=std::string::npos||wanted.find("stand")!=std::string::npos){
        if(has("idle")||has("stand")||has("crouch_idle"))return true;
    }
    if(wanted.find("attack")!=std::string::npos||wanted.find("melee")!=std::string::npos||wanted.find("shoot")!=std::string::npos){
        if(has("attack")||has("melee")||has("shoot")||has("range"))return true;
    }
    if(wanted.find("reload")!=std::string::npos&&has("reload"))return true;
    return model->playAnimationHint(name) || model->playSequence(name);
}

bool NPC_L::playStateSequence(const std::string& state){
    if(!model || !model->loaded || !model->animated || state.empty()) return false;
    std::string weapon=npcLower(weaponClass);
    if(weapon.rfind("weapon_",0)==0) weapon=weapon.substr(7);
    if(!weapon.empty()){
        if(playNPCSequence(weapon+"_"+state)) return true;
        if(playNPCSequence(state+"_"+weapon)) return true;
    }
    return playNPCSequence(state);
}

void NPC_L::ensureIdleAnimation(){
    if(!model || !model->loaded || !model->animated) return;
    const std::string current=npcLower(model->currentSequenceName());
    std::string weapon=npcLower(weaponClass);
    if(weapon.rfind("weapon_",0)==0) weapon=weapon.substr(7);
    if(current.find("idle")!=std::string::npos || current.find("stand")!=std::string::npos ||
       current.find("breath")!=std::string::npos) {
        if(!weapon.empty() && current.find(weapon)==std::string::npos) {
            if(model->playSequence(weapon+"_idle")) return;
            if(model->playSequence("idle_"+weapon)) return;
        }
        return;
    }
    if(!fallbackIdleSequence.empty() && current==npcLower(fallbackIdleSequence)) return;
    if(playStateSequence("idle")) return;
    for(const auto& sequence:model->sequences){
        const std::string label=npcLower(sequence.label);
        const std::string activity=npcLower(sequence.activity);
        if(label.find("idle")!=std::string::npos || activity.find("idle")!=std::string::npos ||
           label.find("stand")!=std::string::npos || activity.find("stand")!=std::string::npos){
            if(model->playSequence(sequence.label)) return;
        }
    }
    if(!model->sequences.empty()){
        fallbackIdleSequence=model->sequences.front().label;
        model->playSequence(fallbackIdleSequence);
    }
}

void NPC_L::playNPCSound(const std::string& event){
    const std::string cls=npcLower(className);
    std::string sound;
    if(cls.find("metropolice")!=std::string::npos || cls.find("police")!=std::string::npos){
        if(event=="pain") sound="NPC_Metropolice.Pain";
        else if(event=="die") sound="NPC_Metropolice.Die";
        else if(event=="attack") sound="NPC_Metropolice.Shoot";
        else if(event=="alert") sound="NPC_Metropolice.Alert";
        else if(event=="idle") sound="NPC_Metropolice.Alert";
    }else if(cls.find("combine_s")!=std::string::npos || cls.find("soldier")!=std::string::npos){
        if(event=="pain") sound="NPC_CombineS.Pain";
        else if(event=="die") sound="NPC_CombineS.Die";
        else if(event=="attack") sound="NPC_CombineS.Shoot";
        else if(event=="alert") sound="NPC_CombineS.Alert";
        else if(event=="idle") sound="NPC_CombineS.Alert";
    }else if(cls.find("citizen")!=std::string::npos){
        if(event=="pain") sound="NPC_Citizen.Pain";
        else if(event=="die") sound="NPC_Citizen.Die";
        else if(event=="alert") sound="NPC_Citizen.Alert";
        else if(event=="idle") sound="NPC_Citizen.Alert";
    }else if(cls.find("zombie")!=std::string::npos){
        if(event=="pain") sound="Zombie.Pain";
        else if(event=="die") sound="Zombie.Die";
        else if(event=="attack") sound="Zombie.Attack";
    }else if(cls.find("antlion")!=std::string::npos){
        if(event=="pain") sound="NPC_Antlion.Pain";
        else if(event=="die") sound="NPC_Antlion.Die";
        else if(event=="attack") sound="NPC_Antlion.MeleeAttack";
    }else if(cls.find("vortigaunt")!=std::string::npos){
        if(event=="pain") sound="NPC_Vortigaunt.Pain";
        else if(event=="die") sound="NPC_Vortigaunt.Die";
    }else if(cls.find("headcrab")!=std::string::npos){
        if(event=="pain") sound="NPC_Headcrab.Pain";
        else if(event=="die") sound="NPC_Headcrab.Die";
        else if(event=="attack") sound="NPC_Headcrab.Attack";
    }
    if(!sound.empty()) Sound_L::Get().play(sound,pos,0.85f,true,false);
}

void NPC_L::updateGravity(float dt,BSP_L* map){
    const float safeDt=std::clamp(dt,0.0f,0.1f);
    if(safeDt<=0.0f) return;
    verticalVelocity-=800.0f*safeDt;
    const float step=verticalVelocity*safeDt;
    if(map){
        float hitDistance=999999.0f;
        Vec3 hitNormal;
        if(map->traceRay(pos+Vec3{0,2,0},{0,-1,0},hitDistance,hitNormal) &&
           hitDistance<=2.0f+std::max(0.0f,-step)+0.5f){
            pos.y+=2.0f-hitDistance;
            verticalVelocity=0.0f;
            npcOnGround=true;
            return;
        }
    }
    pos.y+=step;
    npcOnGround=false;
}
bool NPC_L::startScriptedPerformance(){
    if(!scriptedEntryName.empty()) {
        scriptedPhase=1;
        if((model && model->playSequence(scriptedEntryName)) || playNPCSequence(scriptedEntryName)) {
            scriptedSequenceHasStarted=true;
            scriptedSequenceTimer=model&&model->currentSequenceLoops()?-1.0f:std::max(0.05f,model?model->currentSequenceDuration():0.05f);
            return true;
        }
        scriptedEntryName.clear();
    }
    if(!scriptedSequenceName.empty()) {
        scriptedPhase=2;
        if((model && model->playSequence(scriptedSequenceName)) || playNPCSequence(scriptedSequenceName)) {
            scriptedSequenceHasStarted=true;
            scriptedSequenceTimer=model&&model->currentSequenceLoops()?-1.0f:std::max(0.05f,model?model->currentSequenceDuration():0.05f);
            return true;
        }
    }
    scriptedPhase=0;
    return false;
}

void NPC_L::beginScriptedMove(const std::string& sequence,const std::string& goalName,float speed,const std::string& controllerName,const std::string& entrySequence,const std::string& idleSequence,const std::string& postIdleSequence,const std::string& customMoveSequence,int moveMode,const Vec3& markPosition,float markYaw,bool waitForBegin,bool loopPostIdle){
    activeScriptGoal=(moveMode>=1&&moveMode<=3)?(goalName.empty()?"__script_mark__":goalName):std::string{};
    activeScriptController=controllerName;
    scriptedMoveSpeed=std::clamp(speed,1.0f,500.0f);
    scriptedSequenceName=sequence;
    scriptedEntryName=entrySequence;
    scriptedIdleName=idleSequence;
    scriptedPostIdleName=postIdleSequence;
    scriptedCustomMoveName=customMoveSequence;
    scriptedMoveMode=moveMode;
    scriptedMarkPos=markPosition;
    scriptedMarkYaw=markYaw;
    scriptedHasMark=true;
    scriptedWaitForBegin=waitForBegin;
    scriptedLoopPostIdle=loopPostIdle;
    scriptedControlled=true;
    scriptedSequenceHasStarted=false;
    scriptedSequenceTimer=-1.0f;
    scriptedPhase=0;
    waitForScriptFlag=false;
    if(moveMode==4) {
        pos=scriptedMarkPos;
        angles.y=scriptedMarkYaw;
    } else if(moveMode==5) {
        angles.y=scriptedMarkYaw;
    }
    if(activeScriptGoal.empty()) {
        if(scriptedWaitForBegin) {
            playNPCSequence(scriptedIdleName.empty()?"idle":scriptedIdleName);
        } else if(!startScriptedPerformance()) {
            scriptedSequenceTimer=0.0f;
        }
    } else if(moveMode==3&&!scriptedCustomMoveName.empty()) {
        playNPCSequence(scriptedCustomMoveName);
    } else {
        playNPCSequence(moveMode==2?"run":"walk");
    }
}

void NPC_L::cancelScriptedMove(){
    activeScriptGoal.clear();
    activeScriptController.clear();
    activePathTarget.clear();
    waitingPathNode.clear();
    pathWaitRemaining=0.0f;
    scriptedSequenceName.clear();
    scriptedEntryName.clear();
    scriptedIdleName.clear();
    scriptedPostIdleName.clear();
    scriptedCustomMoveName.clear();
    scriptedHasMark=false;
    scriptedWaitForBegin=false;
    scriptedLoopPostIdle=false;
    scriptedMoveMode=0;
    scriptedSequenceTimer=-1.0f;
    scriptedSequenceHasStarted=false;
    scriptedPhase=0;
    scriptedControlled=false;
    waitForScriptFlag=false;
    playNPCSequence("idle");
}

void NPC_L::beginSceneControl(){
    cancelScriptedMove();
    sceneControlled=true;
    sceneHasMoveTarget=false;
    sceneHasLookTarget=false;
    sceneHasFaceTarget=false;
    sceneActiveSequence.clear();
    enabled=true;
    aiEnabled=true;
    sleepState=0;
    waitForScriptFlag=false;
}

void NPC_L::endSceneControl(){
    sceneControlled=false;
    sceneHasMoveTarget=false;
    sceneHasLookTarget=false;
    sceneHasFaceTarget=false;
    sceneActiveSequence.clear();
    playNPCSequence("idle");
}

void NPC_L::sceneSetMoveTarget(const Vec3& target,float speed,float stopDistance){
    sceneMoveTarget=target;
    sceneMoveSpeed=std::clamp(speed,10.0f,500.0f);
    sceneStopDistance=std::clamp(stopDistance,0.0f,512.0f);
    sceneHasMoveTarget=true;
}

void NPC_L::sceneClearMoveTarget(){
    sceneHasMoveTarget=false;
}

void NPC_L::sceneSetLookTarget(const Vec3& target){
    sceneLookTarget=target;
    sceneHasLookTarget=true;
}

void NPC_L::sceneClearLookTarget(){
    sceneHasLookTarget=false;
}

void NPC_L::sceneSetFaceTarget(const Vec3& target){
    sceneFaceTarget=target;
    sceneHasFaceTarget=true;
}

void NPC_L::sceneClearFaceTarget(){
    sceneHasFaceTarget=false;
}

void NPC_L::scenePlaySequence(const std::string& name,bool loop){
    (void)loop;
    const std::string lowered=npcLower(name);
    if(lowered.empty()||lowered=="null"||lowered=="none") return;
    if(playNPCSequence(name)) sceneActiveSequence=name;
}

void NPC_L::sceneStopSequence(const std::string& name){
    if(sceneActiveSequence.empty()) return;
    if(!name.empty() && npcLower(sceneActiveSequence)!=npcLower(name)) return;
    sceneActiveSequence.clear();
    playNPCSequence("idle");
}

void NPC_L::updateSceneControl(float dt,BSP_L* map){
    const float safeDt=std::max(0.0f,dt);
    if(sceneHasMoveTarget) {
        const float dx=sceneMoveTarget.x-pos.x;
        const float dz=sceneMoveTarget.z-pos.z;
        const float distance=std::sqrt(dx*dx+dz*dz);
        if(distance<=std::max(2.0f,sceneStopDistance)) {
            sceneHasMoveTarget=false;
            if(sceneActiveSequence.empty()) playNPCSequence("idle");
        } else {
            const Vec3 direction{dx/distance,0,dz/distance};
            const float step=std::min(distance,sceneMoveSpeed*safeDt);
            bool blocked=false;
            if(map) {
                float hit=0.0f; Vec3 normal;
                const Vec3 eye=pos+Vec3{0,24,0};
                if(map->traceRay(eye,direction,hit,normal)&&hit>0.0f&&hit<step+12.0f) blocked=true;
            }
            if(!blocked) {
                pos.x+=direction.x*step;
                pos.z+=direction.z*step;
                if(sceneActiveSequence.empty()) playNPCSequence("walk");
            } else if(sceneActiveSequence.empty()) {
                playNPCSequence("idle");
            }
            angles.y=std::atan2(dx,-dz)*180.0f/3.14159265358979323846f;
        }
    }
    const bool hasFacingTarget=sceneHasFaceTarget||sceneHasLookTarget;
    if(!sceneHasMoveTarget && hasFacingTarget) {
        const Vec3& facingTarget=sceneHasFaceTarget?sceneFaceTarget:sceneLookTarget;
        const float dx=facingTarget.x-pos.x;
        const float dz=facingTarget.z-pos.z;
        if(dx*dx+dz*dz>0.001f) {
            const float desired=std::atan2(dx,-dz)*180.0f/3.14159265358979323846f;
            float delta=desired-angles.y;
            while(delta>180.0f)delta-=360.0f;
            while(delta<-180.0f)delta+=360.0f;
            const float step=safeDt*140.0f;
            angles.y+=std::clamp(delta,-step,step);
            if(angles.y>180.0f)angles.y-=360.0f;
            if(angles.y<-180.0f)angles.y+=360.0f;
        }
    } else if(!sceneHasMoveTarget && sceneActiveSequence.empty()) {
        playNPCSequence("idle");
    }
}

void NPC_L::wakeAI(){
    aiEnabled=true;
    sleepState=0;
    waitForScriptFlag=false;
    wasDamaged=true;
    waitTillSeenSatisfied=true;
}

void NPC_L::sleepAI(int state){
    sleepState=std::clamp(state,0,5);
    aiEnabled=true;
}

void NPC_L::updateAI(float dt,Camera& player,BSP_L* map,EntityManager& entities){
    if(health<=0)return;
    updateGravity(dt,map);
    aiMoving=false;
    aiAttacking=false;
    if(!enabled || !aiEnabled)return;
    if(sceneControlled) {
        updateSceneControl(dt,map);
        return;
    }
    const float radius=520.0f;
    const float radiusSq=radius*radius;
    const float safeDt=std::max(0.001f,dt);
    pathSearchCooldown=std::max(0.0f,pathSearchCooldown-safeDt);
    auto distSq=[&](const Vec3& a,const Vec3& b){const Vec3 d=a-b;return d.x*d.x+d.y*d.y+d.z*d.z;};
    auto validEntity=[&](Entity* e){
        if(!e||e==this||e->shouldDestroy)return false;
        if(auto* npc=dynamic_cast<NPC_L*>(e))return npc->isAlive();
        return true;
    };
    auto keyValue=[](Entity* ent,const std::string& key)->std::string{
        if(!ent)return {};
        auto it=ent->keyValues.find(key); return it==ent->keyValues.end()?std::string{}:it->second;
    };
    auto baseSpeed=[&]()->float{
        if(className.find("zombie")!=std::string::npos)return 85.0f;
        if(className.find("headcrab")!=std::string::npos)return 120.0f;
        if(className.find("antlion")!=std::string::npos)return 150.0f;
        if(className.find("metropolice")!=std::string::npos)return 130.0f;
        if(className.find("combine")!=std::string::npos||className.find("soldier")!=std::string::npos)return 140.0f;
        if(className.find("citizen")!=std::string::npos)return 100.0f;
        return 100.0f;
    };
    auto findNamed=[&](const std::string& name)->Entity*{
        if(name.empty())return nullptr;
        for(Entity* e:entities.entities)if(e&&!e->shouldDestroy&&npcLower(e->targetname)==npcLower(name))return e;
        return nullptr;
    };
    auto isPathNode=[](Entity* e){
        return e && !e->shouldDestroy && (e->classname=="path_corner" || e->classname=="path_track");
    };
    auto nearestPathNode=[&](const Vec3& point)->Entity*{
        Entity* best=nullptr;
        float bestDistance=1e30f;
        for(Entity* e:entities.entities){
            if(!isPathNode(e))continue;
            const float d=distSq(point,e->pos);
            if(d<bestDistance){bestDistance=d;best=e;}
        }
        return best;
    };
    auto findPathNodeStep=[&]()->std::string{
        Entity* start=nearestPathNode(pos);
        Entity* goal=nearestPathNode(player.pos);
        if(!start||!goal||start==goal)return {};
        std::queue<Entity*> pending;
        std::unordered_map<Entity*,Entity*> parent;
        parent[start]=nullptr;
        pending.push(start);
        while(!pending.empty()){
            Entity* current=pending.front();pending.pop();
            if(current==goal)break;
            Entity* next=findNamed(keyValue(current,"target"));
            if(!isPathNode(next)||parent.find(next)!=parent.end())continue;
            parent[next]=current;
            pending.push(next);
        }
        auto goalIt=parent.find(goal);
        if(goalIt==parent.end())return {};
        Entity* step=goal;
        while(parent[step]&&parent[step]!=start)step=parent[step];
        return step->targetname;
    };
    auto blockedByObstacle=[&](const Vec3& direction,float stepDistance)->bool{
        for(Entity* e:entities.entities){
            if(!e||e==this||e->shouldDestroy)continue;
            const std::string cls=npcLower(e->classname);
            NPC_L* otherNpc=dynamic_cast<NPC_L*>(e);
            const bool prop=cls.rfind("prop_",0)==0 || cls.find("physics")!=std::string::npos ||
                cls.find("dynamic")!=std::string::npos || cls.find("func_")!=std::string::npos;
            if(!prop&&!otherNpc)continue;
            Vec3 to=e->pos-pos;
            const float distance=to.length();
            if(distance<1.0f||distance>stepDistance+88.0f)continue;
            const Vec3 heading=to*(1.0f/distance);
            if(heading.dot(direction)<0.5f)continue;
            const Vec3 lateral=to-direction*direction.dot(to);
            const float radius=otherNpc?30.0f:40.0f;
            if(lateral.length()<radius)return true;
        }
        return false;
    };
    auto finishScript=[&](){
        if(!activeScriptController.empty()) {
            Entity* controller=findNamed(activeScriptController);
            if(controller) entities.fireEntityOutput(controller,"onpostidleendsequence",player,map);
        }
        const std::string idleSequence=scriptedIdleName;
        activeScriptController.clear();
        activeScriptGoal.clear();
        scriptedSequenceName.clear();
        scriptedEntryName.clear();
        scriptedIdleName.clear();
        scriptedPostIdleName.clear();
        scriptedCustomMoveName.clear();
        scriptedHasMark=false;
        scriptedWaitForBegin=false;
        scriptedLoopPostIdle=false;
        scriptedMoveMode=0;
        scriptedSequenceTimer=-1.0f;
        scriptedSequenceHasStarted=false;
        scriptedPhase=0;
        scriptedControlled=false;
        waitForScriptFlag=false;
        playNPCSequence(idleSequence.empty()?"idle":idleSequence);
    };
    auto startPostIdle=[&]()->bool{
        if(scriptedPostIdleName.empty()) return false;
        if((model&&model->playSequence(scriptedPostIdleName))||playNPCSequence(scriptedPostIdleName)) {
            scriptedPhase=3;
            scriptedSequenceHasStarted=true;
            scriptedSequenceTimer=scriptedLoopPostIdle?-1.0f:(model?std::max(0.05f,model->currentSequenceDuration()):0.05f);
            return true;
        }
        scriptedPhase=3;
        scriptedSequenceHasStarted=true;
        scriptedSequenceTimer=scriptedLoopPostIdle?-1.0f:1.0f;
        return true;
    };
    auto finishActionSequence=[&](){
        if(!activeScriptController.empty()) {
            Entity* controller=findNamed(activeScriptController);
            if(controller) entities.fireEntityOutput(controller,"onendsequence",player,map);
        }
        if(startPostIdle()) return;
        finishScript();
    };
    auto arriveAtScriptMark=[&](){
        if(scriptedHasMark) angles.y=scriptedMarkYaw;
        activeScriptGoal.clear();
        if(scriptedWaitForBegin) {
            scriptedPhase=0;
            scriptedSequenceTimer=-1.0f;
            playNPCSequence(scriptedIdleName.empty()?"idle":scriptedIdleName);
            return;
        }
        if(!activeScriptController.empty()) {
            Entity* controller=findNamed(activeScriptController);
            if(controller) entities.fireEntityOutput(controller,"onbeginsequence",player,map);
        }
        if(!startScriptedPerformance()) finishActionSequence();
    };
    auto playerCanSeeNPC=[&](){
        const Vec3 eye=player.pos+Vec3{0,48,0};
        const Vec3 target=pos+Vec3{0,32,0};
        const Vec3 delta=target-eye;
        const float distance=delta.length();
        if(distance<=0.001f) return true;
        if(distance>2200.0f) return false;
        if(!map) return distance<900.0f;
        float hit=0.0f; Vec3 normal;
        return !map->traceRay(eye,delta*(1.0f/distance),hit,normal)||hit+8.0f>=distance;
    };
    auto npcCanSeePlayer=[&](){
        const Vec3 eye=pos+Vec3{0,44,0};
        const Vec3 target=player.pos+Vec3{0,44,0};
        const Vec3 delta=target-eye;
        const float distance=delta.length();
        if(distance<=0.001f) return true;
        if(distance>2200.0f) return false;
        if(!map) return distance<900.0f;
        float hit=0.0f; Vec3 normal;
        return !map->traceRay(eye,delta*(1.0f/distance),hit,normal)||hit+8.0f>=distance;
    };

    if(!scriptedControlled) {
        if(waitForScriptFlag && !wasDamaged) return;
        if(sleepState==3) return;
        if(sleepState==1) {
            if(distSq(pos,player.pos)>280.0f*280.0f) return;
            sleepState=0;
        } else if(sleepState==2 || sleepState==4 || sleepState==5) {
            if(!playerCanSeeNPC() && distSq(pos,player.pos)>700.0f*700.0f) return;
            sleepState=0;
        }
    }

    if(scriptedControlled && activeScriptGoal.empty()) {
        if(scriptedSequenceTimer>=0.0f) {
            scriptedSequenceTimer=std::max(0.0f,scriptedSequenceTimer-safeDt);
            if(scriptedSequenceTimer<=0.0f) {
                if(scriptedPhase==1 && !scriptedSequenceName.empty()) {
                    scriptedEntryName.clear();
                    if(startScriptedPerformance()) return;
                    finishActionSequence();
                } else if(scriptedPhase==2) {
                    finishActionSequence();
                } else if(scriptedPhase==3) {
                    finishScript();
                } else {
                    finishActionSequence();
                }
                return;
            }
            else return;
        } else return;
    }

    if(activePathTarget.empty() && !activeScriptGoal.empty()) activePathTarget.clear();
    if(activePathTarget.empty() && activeScriptGoal.empty() && !scriptedControlled) {
        const std::string start=keyValue(this,"target");
        Entity* candidatePath=findNamed(start);
        if(candidatePath && (candidatePath->classname=="path_corner" || candidatePath->classname=="path_track")) activePathTarget=start;
    }
    if(activePathTarget.empty() && activeScriptGoal.empty() && !scriptedControlled &&
       pathSearchCooldown<=0.0f && distSq(pos,player.pos)>220.0f*220.0f) {
        pathSearchCooldown=0.6f;
        const std::string step=findPathNodeStep();
        if(!step.empty())activePathTarget=step;
    }
    const std::string movementTarget=!activeScriptGoal.empty()?activeScriptGoal:activePathTarget;
    if(!movementTarget.empty()) {
        if(scriptedControlled && scriptedHasMark && scriptedMoveMode>=1 && scriptedMoveMode<=3) {
            Vec3 to=scriptedMarkPos-pos;
            const float distance=to.length();
            if(distance<=14.0f) {
                pos=scriptedMarkPos;
                arriveAtScriptMark();
                return;
            }
            const float step=std::min(distance,scriptedMoveSpeed*safeDt);
            if(distance>0.001f) {
                const Vec3 direction=to*(1.0f/distance);
                bool blocked=false;
                if(map) {
                    float hit=0.0f; Vec3 normal;
                    const Vec3 eye=pos+Vec3{0,24,0};
                    if(map->traceRay(eye,direction,hit,normal)&&hit>0.0f&&hit<step+12.0f) blocked=true;
                }
                if(blockedByObstacle(direction,step)) blocked=true;
                if(!blocked) pos=pos+direction*step;
                const float desired=std::atan2(direction.x,-direction.z)*180.0f/3.14159265358979323846f;
                angles.y=desired;
                if(scriptedMoveMode==3&&!scriptedCustomMoveName.empty()) playNPCSequence(scriptedCustomMoveName);
                else playNPCSequence(scriptedMoveMode==2?"run":blocked?"idle":"walk");
            }
            return;
        }
        Entity* goal=findNamed(movementTarget);
        if(!goal) {
            if(!activeScriptGoal.empty()) {
                arriveAtScriptMark();
                return;
            } else activePathTarget.clear();
        } else {
            if(activeScriptGoal.empty() && distSq(pos,player.pos)<160.0f*160.0f) {
                activePathTarget.clear();
                return;
            }
            if(pathWaitRemaining>0.0f) {
                pathWaitRemaining=std::max(0.0f,pathWaitRemaining-safeDt);
                if(pathWaitRemaining>0.0f) { playNPCSequence("idle"); return; }
            }
            Vec3 to=goal->pos-pos;
            const float distance=to.length();
            if(distance<=14.0f) {
                pos=goal->pos;
                if(!activeScriptGoal.empty()) {
                    arriveAtScriptMark();
                } else {
                    const std::string nodeName=goal->targetname;
                    if(waitingPathNode!=nodeName) {
                        entities.fireEntityOutput(goal,"onpass",player,map);
                        waitingPathNode=nodeName;
                        pathWaitRemaining=std::max(0.0f,npcFloat(keyValue(goal,"wait"),0.0f));
                    }
                    if(pathWaitRemaining>0.0f) { playNPCSequence("idle"); return; }
                    activePathTarget=keyValue(goal,"target");
                    waitingPathNode.clear(); pathWaitRemaining=0.0f;
                }
                return;
            }
            const float speed=!activeScriptGoal.empty()?scriptedMoveSpeed:std::clamp(npcFloat(keyValue(this,"speed"),baseSpeed()),20.0f,500.0f);
            const float step=std::min(distance,speed*safeDt);
            if(distance>0.001f) {
                const Vec3 direction=to*(1.0f/distance);
                bool blocked=false;
                if(map) {
                    float hit=0.0f; Vec3 normal;
                    const Vec3 eye=pos+Vec3{0,24,0};
                    if(map->traceRay(eye,direction,hit,normal)&&hit>0.0f&&hit<step+12.0f) blocked=true;
                }
                if(blockedByObstacle(direction,step)) blocked=true;
                if(!blocked) {
                    pos=pos+direction*step;
                    aiMoving=true;
                }
                const float desired=std::atan2(direction.x,-direction.z)*180.0f/3.14159265358979323846f;
                angles.y=desired;
                if(scriptedMoveMode==3&&!scriptedCustomMoveName.empty()) playNPCSequence(scriptedCustomMoveName);
                else if(!activeScriptGoal.empty()) playNPCSequence(scriptedMoveMode==2?"run":blocked?"idle":"walk");
                else playStateSequence(blocked?"idle":(speed>155.0f?"run":"walk"));
            }
            return;
        }
    }
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
    attackCooldown=std::max(0.0f,attackCooldown-safeDt);
    if((spawnFlags&1)!=0 && !waitTillSeenSatisfied && playerCanSeeNPC()) waitTillSeenSatisfied=true;
    const bool hostileReady=hostileToPlayer && (((spawnFlags&1)==0) || waitTillSeenSatisfied);
    const bool hostileTargetDetected=wasDamaged || npcCanSeePlayer();
    if(hostileReady && hostileTargetDetected && player.health>0 && playerDistSq<radiusSq && activePathTarget.empty()) {
        const float horizontalDx=player.pos.x-pos.x, horizontalDz=player.pos.z-pos.z;
        const float horizontalDistance=std::sqrt(horizontalDx*horizontalDx+horizontalDz*horizontalDz);
        if(horizontalDistance>52.0f) {
            const float speed=std::clamp(npcFloat(keyValue(this,"speed"),baseSpeed()),25.0f,320.0f);
            const float advance=std::min(horizontalDistance-48.0f,speed*safeDt);
            if(horizontalDistance>0.001f) {
                const Vec3 direction{horizontalDx/horizontalDistance,0,horizontalDz/horizontalDistance};
                bool blocked=false;
                if(map){float hit=0.0f;Vec3 normal;const Vec3 eye=pos+Vec3{0,24,0};if(map->traceRay(eye,direction,hit,normal)&&hit>0.0f&&hit<advance+12.0f)blocked=true;}
                if(blockedByObstacle(direction,advance))blocked=true;
                if(!blocked) {
                    pos=pos+direction*advance;
                    aiMoving=true;
                }
                angles.y=std::atan2(horizontalDx,-horizontalDz)*180.0f/3.14159265358979323846f;
                if(blocked) {
                    const std::string pathStep=findPathNodeStep();
                    if(!pathStep.empty())activePathTarget=pathStep;
                    else { playStateSequence("attack"); aiAttacking=true; }
                } else {
                    playStateSequence(speed>155.0f?"run":"walk");
                }
            }
        } else if(attackCooldown<=0.0f && npcCanSeePlayer()) {
            playStateSequence("attack");
            aiAttacking=true;
            playNPCSound("attack");
            const float damage=std::clamp(npcFloat(keyValue(this,"damage"),className.find("headcrab")!=std::string::npos?5.0f:10.0f),1.0f,60.0f);
            player.takeDamage((int)std::ceil(damage));
            attackCooldown=className.find("headcrab")!=std::string::npos?1.2f:1.0f;
        }
        if(!alertedToPlayer){alertedToPlayer=true;entities.fireEntityOutput(this,"onfoundplayer",player,map);}
        lookAtPlayer=true; lookTarget=nullptr; lookReaction=0.0f;
        return;
    } else if(alertedToPlayer) {
        alertedToPlayer=false;
        entities.fireEntityOutput(this,"onlostplayer",player,map);
    }

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
