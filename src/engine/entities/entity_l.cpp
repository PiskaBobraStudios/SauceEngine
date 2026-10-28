#include "entity_l.h"
#include "prop_dynamic.h"
#include "prop_physics.h"
#include "weapon_l.h"
#include "npc_l.h"
#include "scene_l.h"
#include "hammer_entity.h"
#include "bsp_l.h"
#include "sound_l.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
std::string LowerEntityString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::string TrimEntityString(const std::string& value) {
    const size_t a = value.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = value.find_last_not_of(" \t\r\n");
    return value.substr(a, b - a + 1);
}
std::vector<std::pair<std::string, std::string>> ParseEntityPairs(const std::string& block) {
    std::vector<std::pair<std::string, std::string>> result;
    size_t i = 0;
    auto skipSpace = [&]() { while (i < block.size() && std::isspace(static_cast<unsigned char>(block[i]))) ++i; };
    auto readQuoted = [&]() -> std::string {
        std::string out;
        if (i >= block.size() || block[i] != '"') return out;
        ++i;
        while (i < block.size()) {
            char c = block[i++];
            if (c == '"') break;
            if (c == '\\' && i < block.size()) {
                char n = block[i++];
                if (n == 'n') out.push_back('\n');
                else if (n == 't') out.push_back('\t');
                else out.push_back(n);
            } else out.push_back(c);
        }
        return out;
    };
    while (i < block.size()) {
        skipSpace();
        if (i >= block.size()) break;
        if (block[i] != '"') { ++i; continue; }
        std::string key = readQuoted();
        skipSpace();
        if (i >= block.size() || block[i] != '"') continue;
        std::string value = readQuoted();
        if (!key.empty()) result.emplace_back(LowerEntityString(key), value);
    }
    return result;
}
std::vector<std::string> SplitHammerCSV(const std::string& value, size_t maxParts = 5) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (parts.size() + 1 < maxParts) {
        size_t comma = value.find(',', start);
        if (comma == std::string::npos) break;
        parts.push_back(TrimEntityString(value.substr(start, comma - start)));
        start = comma + 1;
    }
    parts.push_back(TrimEntityString(value.substr(start)));
    return parts;
}
float EntityFloat(const std::string& value, float fallback) {
    try { size_t n = 0; float v = std::stof(value, &n); return n ? v : fallback; } catch (...) { return fallback; }
}
int EntityInt(const std::string& value, int fallback = 0) {
    try { size_t n = 0; int v = std::stoi(value, &n); return n ? v : fallback; } catch (...) { return fallback; }
}
bool EntityClassStarts(const std::string& cls, const char* prefix) {
    return cls.rfind(prefix, 0) == 0;
}
std::string NormalizeSoundName(std::string path) {
    path = TrimEntityString(path);
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == ' ') path.erase(path.begin());
    return path;
}
}

void EntityManager::init(const std::string& gameDirectory, Texture_L* textureSystem) {
    gameDir = gameDirectory;
    texL = textureSystem;
    SceneSystem_L::Get().init(gameDirectory, textureSystem);
    Weapon_L::discoverAndRegister(*this, gameDir, texL);
}

void EntityManager::registerEntity(const std::string& classname, EntityFactory factory) {
    factories[LowerEntityString(classname)] = factory;
}

std::string EntityManager::extractEntityValue(const std::string& block, const std::string& key) {
    const std::string wanted = LowerEntityString(key);
    for (const auto& kv : ParseEntityPairs(block)) if (kv.first == wanted) return kv.second;
    return "";
}

static bool IsNPCClassName(const std::string& value) {
    const std::string classname = LowerEntityString(value);
    static const char* prefixes[] = {"npc_", "monster_", "ally_", "enemy_", "creature_", "character_", "animal_", "ai_"};
    for (const char* prefix : prefixes) if (classname.rfind(prefix, 0) == 0) return true;
    return false;
}

void EntityManager::parseBlockMetadata(Entity* ent, const std::string& block) {
    if (!ent) return;
    ent->classname = LowerEntityString(extractEntityValue(block, "classname"));
    ent->targetname = extractEntityValue(block, "targetname");
    if (ent->ioId == 0) ent->ioId = nextIoId++;
    ent->keyValues.clear();
    ent->hammerOutputs.clear();
    for (const auto& kv : ParseEntityPairs(block)) {
        if (kv.first.rfind("on", 0) == 0 && kv.first.size() > 2) {
            const auto fields = SplitHammerCSV(kv.second, 5);
            if (fields.size() >= 2 && !fields[0].empty() && !fields[1].empty()) {
                HammerOutput out;
                out.event = kv.first;
                out.target = fields[0];
                out.input = fields[1];
                out.parameter = fields.size() > 2 ? fields[2] : "";
                out.delay = fields.size() > 3 ? std::max(0.0f, EntityFloat(fields[3], 0.0f)) : 0.0f;
                out.maxFires = fields.size() > 4 ? EntityInt(fields[4], -1) : -1;
                ent->hammerOutputs.push_back(out);
            }
        } else {
            ent->keyValues[kv.first] = kv.second;
        }
    }
    ent->enabled = true;
    auto flagsIt = ent->keyValues.find("spawnflags");
    const int spawnflags = flagsIt == ent->keyValues.end() ? 0 : EntityInt(flagsIt->second, 0);
    if (ent->classname == "logic_timer" && (spawnflags & 1)) ent->enabled = false;
    if (ent->classname == "ambient_generic") {

        if (spawnflags & 16) ent->enabled = false;
    }
}

void EntityManager::parseMapEntities(const std::string& entityData, const BSP_L* map) {
    clear();
    mapSpawnQueued = false;
    size_t pos = 0;
    while ((pos = entityData.find('{', pos)) != std::string::npos) {
        size_t endPos = entityData.find('}', pos + 1);
        if (endPos == std::string::npos) break;
        const std::string block = entityData.substr(pos, endPos - pos + 1);
        const auto pairs = ParseEntityPairs(block);
        std::string classname;
        std::string originStr;
        for (const auto& kv : pairs) {
            if (kv.first == "classname") classname = LowerEntityString(kv.second);
            else if (kv.first == "origin") originStr = kv.second;
        }


        if (classname.empty() || classname == "worldspawn") {
            pos = endPos + 1;
            continue;
        }
        Vec3 entPos{0,0,0};
        if (!originStr.empty()) {
            std::istringstream ss(originStr);
            float x=0, y=0, z=0;
            if (ss >> x >> y >> z) entPos = {x, z, -y};
        }

        Entity* newEnt = nullptr;
        auto fit = factories.find(classname);
        if (fit != factories.end()) {
            newEnt = fit->second(block, entPos, gameDir, texL);
        } else if (IsNPCClassName(classname)) {
            newEnt = NPC_L::Create(block, entPos, gameDir, texL);
        } else {


            newEnt = new HammerEntity(block, entPos, map, gameDir, texL);
        }
        if (newEnt) {
            parseBlockMetadata(newEnt, block);
            entities.push_back(newEnt);
        } else {
            std::cout << "[Entity] Factory returned null for classname: " << classname << std::endl;
        }
        pos = endPos + 1;
    }
    std::cout << "[HammerIO] Parsed " << entities.size() << " map entities (including compatibility entities)." << std::endl;
}

Entity* EntityManager::spawn(const std::string& classname, const Vec3& position, bool inventorySpawn) {
    const std::string key = LowerEntityString(classname);
    auto fit = factories.find(key);
    if (fit != factories.end()) {
        std::string block = "{\n\"classname\" \"" + key + "\"\n";
        if (!IsNPCClassName(key)) block += "\"model\" \"models/items/hevsuit.mdl\"\n\"solid\" \"6\"\n";
        if (inventorySpawn) block += "\"__inventory\" \"1\"\n";
        block += "}";
        Entity* newEnt = fit->second(block, position, gameDir, texL);
        if (newEnt) { parseBlockMetadata(newEnt, block); entities.push_back(newEnt); }
        return newEnt;
    }
    if (IsNPCClassName(key)) {
        std::string block = "{\n\"classname\" \"" + key + "\"\n}";
        Entity* newEnt = NPC_L::Create(block, position, gameDir, texL);
        if (newEnt) { parseBlockMetadata(newEnt, block); entities.push_back(newEnt); }
        return newEnt;
    }
    std::cout << "[Entity] No factory for spawn classname: " << key << std::endl;
    return nullptr;
}

Entity* EntityManager::spawnNetwork(const std::string& classname, const Vec3& position, const std::string& modelPath,
                                      const Vec3& angles, float scale, int netId, int owner) {
    const std::string key = LowerEntityString(classname);
    auto fit = factories.find(key);
    if (fit == factories.end()) return nullptr;
    std::string block = "{\n\"classname\" \"" + key + "\"\n";
    if (!modelPath.empty()) block += "\"model\" \"" + modelPath + "\"\n";
    block += "\"angles\" \"" + std::to_string(angles.x) + " " + std::to_string(angles.y) + " " + std::to_string(angles.z) + "\"\n";
    block += "\"modelscale\" \"" + std::to_string(scale > 0.001f ? scale : 1.0f) + "\"\n\"solid\" \"6\"\n}";
    Entity* ent = fit->second(block, position, gameDir, texL);
    if (!ent) return nullptr;
    parseBlockMetadata(ent, block);
    ent->multiplayerNetId = netId;
    ent->multiplayerOwner = owner;
    ent->multiplayerRemote = owner != 0 && owner != 1 ? true : owner != (netId >= 0x40000000 ? 1 : 0);
    entities.push_back(ent);
    return ent;
}

Entity* EntityManager::findMultiplayerEntity(int netId) const {
    for (Entity* ent : entities) if (ent && ent->multiplayerNetId == netId) return ent;
    return nullptr;
}

void EntityManager::fireEntityOutput(Entity* source, const std::string& output, Camera& player, BSP_L* map) {
    fireOutput(source, output, player, map);
}

void EntityManager::fireOutput(Entity* source, const std::string& output, Camera&, BSP_L*) {
    if (!source) return;
    const std::string eventName = LowerEntityString(output);
    for (HammerOutput& out : source->hammerOutputs) {
        if (out.event != eventName) continue;
        if (out.maxFires >= 0 && out.fireCount >= out.maxFires) continue;
        ++out.fireCount;
        PendingHammerInput command;
        command.remaining = std::max(0.0f, out.delay);
        command.target = out.target;
        command.input = out.input;
        command.parameter = out.parameter;
        command.directTargetId = 0;
        if (command.target == "!self" || command.target == "!caller") {
            command.directTargetId = source->ioId;
            command.target.clear();
        } else if (command.target == "!activator" || command.target == "!player") command.target = "!player";
        if (!command.target.empty() || command.directTargetId != 0) pendingInputs.push_back(std::move(command));
    }
}

void EntityManager::dispatchInput(const PendingHammerInput& command, Camera& player, BSP_L* map) {
    const std::string input = LowerEntityString(command.input);
    if (command.target == "!player") {
        if (input == "sethealth") player.setHealth(EntityInt(command.parameter, player.health));
        else if (input == "addhealth") player.addHealth(EntityInt(command.parameter, 0));
        else if (input == "setvelocity") {
            std::istringstream ss(command.parameter); float x=0,y=0,z=0;
            if (ss >> x >> y >> z) player.velocity = {x,z,-y};
        } else if (input == "setangle" || input == "setangles") {
            std::istringstream ss(command.parameter); float pitch=0,yaw=0,roll=0;
            if (ss >> pitch >> yaw >> roll) { player.pitch = pitch; player.yaw = yaw; }
        } else if (input == "kill") player.kill();
        return;
    }
    for (Entity* ent : entities) {
        if (!ent) continue;
        const bool directMatch = command.directTargetId != 0 && ent->ioId == command.directTargetId;
        const bool namedMatch = command.directTargetId == 0 && !ent->targetname.empty() && LowerEntityString(ent->targetname) == LowerEntityString(command.target);
        if ((!directMatch && !namedMatch) || (ent->shouldDestroy && !directMatch)) continue;
        const std::string cls = ent->classname;
        if (input == "kill" || input == "remove") {
            if (!ent->destroyOutputsFired) fireOutput(ent, "onkilled", player, map);
            ent->destroyOutputsFired = true;
            ent->shouldDestroy = true;
            continue;
        }
        if (auto* npc = dynamic_cast<NPC_L*>(ent)) {
            if (input == "wake" || input == "wakeup" || input == "forcepursue" || input == "startpatrolling" || input == "startfollowing") {
                npc->wakeAI();
                ent->enabled = true;
                continue;
            }
            if (input == "sleep") {
                npc->sleepAI(3);
                continue;
            }
            if (input == "sethealth") {
                npc->health = std::max(0, EntityInt(command.parameter, npc->health));
                if (npc->health == 0) npc->shouldDestroy = true;
                continue;
            }
        }
        if (input == "enable" || input == "turnon") {
            ent->enabled = true;
            if (auto* npc = dynamic_cast<NPC_L*>(ent)) npc->wakeAI();
            if (auto* h = dynamic_cast<HammerEntity*>(ent); h && cls == "ambient_generic" && !h->soundPlaying) {
                const std::string sound = NormalizeSoundName(h->keyValues["message"]);
                const int flags = EntityInt(h->keyValues["spawnflags"], 0);
                const float volume = std::clamp(EntityFloat(h->keyValues["health"], 10.0f) / 10.0f, 0.0f, 1.0f);
                if (!sound.empty()) {
                    if ((flags & 32) == 0) { h->loopSoundHandle = Sound_L::Get().playLoop(sound, h->pos, volume, (flags & 1) == 0); h->soundPlaying = true; }
                    else Sound_L::Get().play(sound, h->pos, volume, (flags & 1) == 0, true);
                }
            }
            continue;
        }
        if (input == "disable" || input == "turnoff") {
            ent->enabled = false;
            if (auto* npc = dynamic_cast<NPC_L*>(ent)) npc->setAIEnabled(false);
            if (auto* h = dynamic_cast<HammerEntity*>(ent); h && cls == "ambient_generic") {
                if (h->loopSoundHandle) Sound_L::Get().stopLoop(h->loopSoundHandle);
                h->loopSoundHandle = 0; h->soundPlaying = false;
            }
            continue;
        }
        if (input == "toggle") {
            if (auto* h = dynamic_cast<HammerEntity*>(ent); h && cls == "ambient_generic") {
                if (h->soundPlaying) {
                    if (h->loopSoundHandle) Sound_L::Get().stopLoop(h->loopSoundHandle);
                    h->loopSoundHandle = 0; h->soundPlaying = false; ent->enabled = false;
                } else {
                    ent->enabled = true;
                    const std::string sound = NormalizeSoundName(h->keyValues["message"]);
                    const int flags = EntityInt(h->keyValues["spawnflags"], 0);
                    const float volume = std::clamp(EntityFloat(h->keyValues["health"], 10.0f) / 10.0f, 0.0f, 1.0f);
                    if (!sound.empty()) {
                        if ((flags & 32) == 0) { h->loopSoundHandle = Sound_L::Get().playLoop(sound, h->pos, volume, (flags & 1) == 0); h->soundPlaying = true; }
                        else Sound_L::Get().play(sound, h->pos, volume, (flags & 1) == 0, true);
                    }
                }
            } else {
                ent->enabled = !ent->enabled;
                if (auto* h = dynamic_cast<HammerEntity*>(ent)) {
                    if (cls == "logic_relay") { if (ent->enabled) fireOutput(ent, "ontrigger", player, map); }
                    else if (cls == "func_door" || cls == "func_door_rotating" || cls == "func_door_prismatic") {
                        h->opened = !h->opened; fireOutput(ent, h->opened ? "onopen" : "onclose", player, map);
                    } else if (cls.find("button") != std::string::npos) {
                        h->pressed = !h->pressed; fireOutput(ent, h->pressed ? "onpressed" : "onunpressed", player, map);
                    }
                }
            }
            continue;
        }
        if (input == "fireuser1" || input == "fireuser2" || input == "fireuser3" || input == "fireuser4") {
            fireOutput(ent, "onuser" + input.substr(input.size()-1), player, map); continue;
        }
        if (input == "setvalue" || input == "settext" || input == "setanimation" || input == "setdefaultanimation" || input == "setskin") {
            ent->keyValues["__" + input] = command.parameter;
            if (input == "setanimation" || input == "setdefaultanimation") {
                if (auto* hModel = dynamic_cast<HammerEntity*>(ent); hModel && hModel->model.loaded) hModel->model.playSequence(command.parameter);
                else if (auto* prop = dynamic_cast<PropDynamic*>(ent)) prop->model.playSequence(command.parameter);
                else if (auto* prop = dynamic_cast<PropPhysics*>(ent)) prop->model.playSequence(command.parameter);
                else if (auto* npc = dynamic_cast<NPC_L*>(ent)) npc->playNPCSequence(command.parameter);
            }
        }
        auto* h = dynamic_cast<HammerEntity*>(ent);
        if (!h) {
            if (input == "use" || input == "trigger" || input == "press") fireOutput(ent, "onuse", player, map);
            continue;
        }

        if (cls == "scripted_sequence") {
            auto getValue=[&](std::initializer_list<const char*> keys)->std::string {
                for(const char* key:keys) {
                    auto it=h->keyValues.find(key);
                    if(it!=h->keyValues.end()&&!it->second.empty())return it->second;
                }
                return {};
            };
            const std::string actorName=getValue({"m_iszentity","entity","target"});
            const std::string wantedActor=LowerEntityString(actorName);
            auto findChosen=[&]()->NPC_L*{
                NPC_L* chosen=nullptr;
                float bestDistance=1e30f;
                for(Entity* candidate:entities) {
                    auto* npc=dynamic_cast<NPC_L*>(candidate);
                    if(!npc || npc->shouldDestroy) continue;
                    if(!wantedActor.empty()) {
                        const std::string target=LowerEntityString(npc->targetname);
                        const std::string klass=LowerEntityString(npc->classname);
                        const std::string model=LowerEntityString(npc->modelPath);
                        if(target!=wantedActor && klass!=wantedActor && model.find(wantedActor)==std::string::npos) continue;
                    }
                    const Vec3 delta=npc->pos-ent->pos;
                    const float distanceSq=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
                    if(!chosen || distanceSq<bestDistance) {
                        chosen=npc;
                        bestDistance=distanceSq;
                    }
                }
                return chosen;
            };
            const bool moveOnly=input=="movetoposition";
            if(input=="beginsequence"||input=="begin"||input=="start"||input=="trigger"||input=="use"||moveOnly) {
                std::string sequence=getValue({"m_iszplay","sequence","play"});
                if(sequence.empty()) {
                    const std::string sequenceList=getValue({"m_iszsequence_list","sequence_list","sequencelist"});
                    std::istringstream listStream(sequenceList);
                    listStream>>sequence;
                }
                const std::string entry=getValue({"m_iszentry","entry","entrysequence"});
                const std::string preIdle=getValue({"m_iszidle","idle","preidle","preidlesequence"});
                const std::string postIdle=getValue({"m_iszpostidle","postidle","postidlesequence"});
                const std::string customMove=getValue({"m_iszcustommove","custommove"});
                const int moveMode=EntityInt(getValue({"m_fmoveto"}),0);
                const int flags=EntityInt(getValue({"spawnflags"}),0);
                const std::string speedValue=getValue({"m_flmovementspeed","movementspeed","speed"});
                const float speed=!speedValue.empty()?EntityFloat(speedValue,100.0f):((moveMode==2||moveMode==3)?220.0f:100.0f);
                const std::string goalName=(moveMode>=1&&moveMode<=3)?ent->targetname:std::string{};
                NPC_L* chosen=findChosen();
                if(chosen) {
                    chosen->beginScriptedMove(sequence,goalName,speed,ent->targetname,entry,preIdle,postIdle,customMove,
                                              moveMode,ent->pos,h->angles.y,moveOnly,(flags&256)!=0);
                    const bool movesToMark=(moveMode>=1&&moveMode<=3);
                    if(!moveOnly&&!movesToMark) fireOutput(ent,"onbeginsequence",player,map);
                }
            } else if(input=="cancelsequence"||input=="endsequence"||input=="cancel") {
                NPC_L* chosen=findChosen();
                const bool started=chosen&&chosen->scriptedSequenceStarted();
                if(chosen) chosen->cancelScriptedMove();
                fireOutput(ent,"oncancelsequence",player,map);
                if(!started) fireOutput(ent,"oncancelfailedsequence",player,map);
            }
            continue;
        }

        if (cls == "scripted_scene" || cls == "logic_choreographed_scene" || cls == "instanced_scripted_scene") {
            auto sceneValue=[&](const char* a,const char* b,const char* c)->std::string {
                auto it=h->keyValues.find(a); if(it!=h->keyValues.end()&&!it->second.empty())return it->second;
                it=h->keyValues.find(b); if(it!=h->keyValues.end()&&!it->second.empty())return it->second;
                it=h->keyValues.find(c); if(it!=h->keyValues.end()&&!it->second.empty())return it->second;
                return {};
            };
            const std::string sceneFile=sceneValue("scenefile","m_iszscenefile","scene");
            const std::string actorFilter=sceneValue("m_iszactor","actor","m_iszentity");
            if (input == "start" || input == "begin" || input == "startforward" || input == "startbackward" || input == "use") {
                if (!SceneSystem_L::Get().startScene(ent, sceneFile, *this, player, map, actorFilter))
                    fireOutput(ent, "oncanceled", player, map);
            } else if (input == "pause") {
                SceneSystem_L::Get().pauseScene(ent, true);
            } else if (input == "resume") {
                SceneSystem_L::Get().pauseScene(ent, false);
            } else if (input == "cancel" || input == "stop" || input == "cancelatnextinterrupt") {
                SceneSystem_L::Get().stopScene(ent, *this, player, map);
            } else if (input == "trigger") {
                const int trigger = EntityInt(command.parameter, 1);
                if (trigger >= 1 && trigger <= 16) fireOutput(ent, "ontrigger" + std::to_string(trigger), player, map);
                fireOutput(ent, "ontrigger", player, map);
            } else if (input == "setscene" || input == "changescene") {
                if (!command.parameter.empty()) h->keyValues["scenefile"] = command.parameter;
            }
            continue;
        }

        if (cls == "logic_relay") {
            if (input == "trigger" || input == "fire" || input == "use") fireOutput(ent, "ontrigger", player, map);
            else if (input == "cancelpending") {                                                           }
        } else if (cls == "logic_auto") {
            if (input == "fire" || input == "trigger") fireOutput(ent, "onmapspawn", player, map);
        } else if (cls == "logic_timer") {
            if (input == "firetimer" || input == "trigger") fireOutput(ent, "ontimer", player, map);
            if (input == "refiretime") h->keyValues["refiretime"] = command.parameter;
            if (input == "reset") { h->thinkTimer = std::max(0.05f,EntityFloat(h->keyValues["refiretime"],1.0f)); ent->enabled = true; }
            if (input == "enable" || input == "start") { ent->enabled = true; h->thinkTimer = std::max(0.05f,EntityFloat(h->keyValues["refiretime"],1.0f)); }
            if (input == "disable" || input == "stop") ent->enabled = false;
        } else if (cls == "logic_branch") {
            if (input == "setvalue" || input == "set" || input == "settext") h->counterValue = EntityFloat(command.parameter, 0.0f);
            if (input == "settrue") h->counterValue = 1.0f;
            if (input == "setfalse") h->counterValue = 0.0f;
            if (input == "toggle") h->counterValue = h->counterValue == 0.0f ? 1.0f : 0.0f;
            if (input == "test" || input == "trigger") fireOutput(ent, h->counterValue != 0.0f ? "ontrue" : "onfalse", player, map);
        } else if (cls == "logic_case") {
            if (input == "pickrandom") {
                std::vector<int> cases;
                for (int n=1;n<=16;++n) { char key[16]; std::snprintf(key,sizeof(key),"case%02d",n); if (h->keyValues.count(key)) cases.push_back(n); }
                if (!cases.empty()) { int chosen = cases[static_cast<size_t>(std::rand()) % cases.size()]; char ev[20]; std::snprintf(ev,sizeof(ev),"oncase%02d",chosen); fireOutput(ent,ev,player,map); }
            } else if (input == "invalue" || input == "test") {
                int found = 0;
                for (int n=1;n<=16;++n) { char key[16], ev[20]; std::snprintf(key,sizeof(key),"case%02d",n); std::snprintf(ev,sizeof(ev),"oncase%02d",n); auto it=h->keyValues.find(key); if (it!=h->keyValues.end() && it->second==command.parameter) { fireOutput(ent,ev,player,map); found=1; break; } }
                if (!found) fireOutput(ent,"ondefault",player,map);
            }
        } else if (cls == "math_counter") {
            const float old = h->counterValue;
            if (input == "add") h->counterValue += EntityFloat(command.parameter, 0.0f);
            else if (input == "subtract") h->counterValue -= EntityFloat(command.parameter, 0.0f);
            else if (input == "multiply") h->counterValue *= EntityFloat(command.parameter, 1.0f);
            else if (input == "divide") { float d=EntityFloat(command.parameter,1.0f); if(std::abs(d)>1e-6f) h->counterValue/=d; }
            else if (input == "setvalue" || input == "set") h->counterValue = EntityFloat(command.parameter, 0.0f);
            else if (input == "reset") h->counterValue = EntityFloat(h->keyValues["startvalue"], 0.0f);
            else if (input == "setmin") h->counterValue = EntityFloat(h->keyValues["min"] , 0.0f);
            else if (input == "setmax") h->counterValue = EntityFloat(h->keyValues["max"] , 0.0f);
            if (h->counterValue != old) fireOutput(ent,"onchanged",player,map);
            if (h->counterValue <= EntityFloat(h->keyValues["min"],0.0f)) fireOutput(ent,"onhitmin",player,map);
            if (h->counterValue >= EntityFloat(h->keyValues["max"],0.0f)) fireOutput(ent,"onhitmax",player,map);
        } else if (cls == "logic_compare") {
            if (input == "set comparer" || input == "setcomparevalue") h->keyValues["comparevalue"] = command.parameter;
            if (input == "compare" || input == "trigger") {
                const float a = EntityFloat(h->keyValues["comparevalue"], 0.0f);
                const float b = EntityFloat(h->keyValues["initialvalue"], 0.0f);
                if (std::abs(a-b)<0.0001f) fireOutput(ent,"onequal",player,map);
                else if (a<b) fireOutput(ent,"onlessthan",player,map);
                else fireOutput(ent,"ongreaterthan",player,map);
                if (std::abs(a-b)>=0.0001f) fireOutput(ent,"onnotequal",player,map);
            }
        } else if (cls == "func_button" || cls == "func_rot_button" || cls == "momentary_rot_button") {
            if (input == "press" || input == "use" || input == "trigger") {
                if (!h->locked && !h->pressed) {
                    h->pressed = true; h->thinkTimer = std::max(0.0f, EntityFloat(h->keyValues["wait"], 0.5f));
                    fireOutput(ent,"onpressed",player,map); fireOutput(ent,"onin",player,map);
                }
            } else if (input == "unlock") h->locked = false;
            else if (input == "lock") h->locked = true;
            else if (input == "reset" || input == "unpress") { h->pressed=false; fireOutput(ent,"onreleased",player,map); fireOutput(ent,"onout",player,map); }
        } else if (cls == "func_door" || cls == "func_door_rotating" || cls == "func_door_prismatic" || cls == "prop_door_rotating") {
            if (input == "open" || input == "toggle" || input == "use") {
                if (!h->locked && !h->opened) {
                    h->opened=true; h->thinkTimer=std::max(0.0f,EntityFloat(h->keyValues["wait"],3.0f));
                    if (h->model.loaded) h->model.playAnimationHint("open");
                    fireOutput(ent,"onopen",player,map); fireOutput(ent,"onfullyopen",player,map);
                } else if ((input == "toggle" || input == "use") && h->opened) {
                    h->opened=false; if(h->model.loaded) h->model.playAnimationHint("close");
                    fireOutput(ent,"onclose",player,map); fireOutput(ent,"onfullyclosed",player,map);
                }
            } else if (input == "close") { if(h->opened){h->opened=false;if(h->model.loaded)h->model.playAnimationHint("close");fireOutput(ent,"onclose",player,map);fireOutput(ent,"onfullyclosed",player,map);} }
            else if (input == "lock") h->locked=true;
            else if (input == "unlock") h->locked=false;
        } else if (cls == "func_breakable" || cls == "func_physbox") {
            if (input == "break" || input == "kill" || input == "use") { fireOutput(ent,"onbreak",player,map); ent->shouldDestroy=true; }
        } else if (cls == "ambient_generic") {
            std::string sound = NormalizeSoundName(h->keyValues["message"]);
            const int flags = EntityInt(h->keyValues["spawnflags"],0);
            const float volume = std::clamp(EntityFloat(h->keyValues["health"],10.0f)/10.0f,0.0f,1.0f);
            if (input == "playsound" || input == "play" || input == "toggle") {
                if (h->soundPlaying && input == "toggle") { if(h->loopSoundHandle) Sound_L::Get().stopLoop(h->loopSoundHandle); h->loopSoundHandle=0;h->soundPlaying=false; }
                else if (!sound.empty()) {
                    if ((flags & 32) == 0) { h->loopSoundHandle=Sound_L::Get().playLoop(sound,ent->pos,volume,(flags&1)==0); h->soundPlaying=true; }
                    else Sound_L::Get().play(sound,ent->pos,volume,(flags&1)==0,true);
                }
            } else if (input == "stopsound" || input == "stop") {
                if (h->loopSoundHandle) Sound_L::Get().stopLoop(h->loopSoundHandle);
                h->loopSoundHandle = 0;
                h->soundPlaying = false;
            }
        } else if (cls == "point_servercommand" && input == "command") {

            const std::string commandText = command.parameter.empty() ? h->keyValues["command"] : command.parameter;
            std::istringstream ss(commandText); std::string op, a, b, c; ss >> op >> a >> b; std::getline(ss,c); c=TrimEntityString(c);
            op=LowerEntityString(op);
            if (op == "ent_fire" && !a.empty() && !b.empty()) pendingInputs.push_back({0.0f,a,b,c,0});
            else if (op == "play" && !a.empty()) Sound_L::Get().play(NormalizeSoundName(a),player.pos,1.0f,false,false);
            else if (op == "hurtme") player.takeDamage(EntityInt(a,0));
        } else if (cls == "point_teleport" && (input == "teleport" || input == "trigger")) {
            const std::string destination = h->keyValues["target"];
            for (Entity* dest : entities) if (dest && dest->targetname == destination && (dest->classname == "info_teleport_destination" || dest->classname == "info_target")) {
                player.pos = dest->pos + Vec3{0, player.height, 0};
                player.velocity = {0,0,0};
                fireOutput(ent,"onteleport",player,map);
                break;
            }
        } else if (cls == "env_explosion" && (input == "explode" || input == "use" || input == "trigger")) {
            const float magnitude = std::max(1.0f, EntityFloat(h->keyValues["iMagnitude"], EntityFloat(h->keyValues["magnitude"], 100.0f)));
            const float radius = std::max(64.0f, EntityFloat(h->keyValues["iRadiusOverride"], magnitude * 2.5f));
            const float distance = (player.pos - ent->pos).length();
            if (distance <= radius) player.takeDamage(static_cast<int>(magnitude * (1.0f - distance / radius)));
            for (Entity* target : entities) {
                if (!target || target == ent || target->shouldDestroy) continue;
                const float targetDistance = (target->pos - ent->pos).length();
                if (targetDistance > radius) continue;
                const std::string targetClass = target->classname;
                if (targetClass == "func_breakable" || targetClass == "func_physbox") {
                    fireOutput(target, "onbreak", player, map);
                    target->shouldDestroy = true;
                } else if (auto* npc = dynamic_cast<NPC_L*>(target)) {
                    npc->health = std::max(0.0f, npc->health - magnitude * (1.0f - targetDistance / radius));
                    if (npc->health <= 0.0f) npc->shouldDestroy = true;
                }
            }
            Sound_L::Get().play("weapons/explode3.wav",ent->pos,0.9f,true,true);
            fireOutput(ent,"onexplode",player,map);
        } else if (cls == "func_rotating" && (input == "start" || input == "stop" || input == "toggle")) {
            if (input == "start") { ent->enabled=true; fireOutput(ent,"onstart",player,map); }
            else if (input == "stop") { ent->enabled=false; fireOutput(ent,"onstop",player,map); }
            else { ent->enabled=!ent->enabled; fireOutput(ent,ent->enabled?"onstart":"onstop",player,map); }
        } else if (cls == "game_text" && (input == "display" || input == "show")) {
            std::string text = h->keyValues["message"];
            if (!text.empty()) std::cout << "[GameText] " << text << std::endl;
            fireOutput(ent,"ondisplay",player,map);
        } else {
            if (input == "use" || input == "trigger" || input == "press") {
                fireOutput(ent,"onuse",player,map);
                fireOutput(ent,"ontrigger",player,map);
            }
        }
    }
}

void EntityManager::processPendingInputs(float dt, Camera& player, BSP_L* map) {
    for (auto& cmd : pendingInputs) cmd.remaining -= dt;
    size_t processed = 0;
    while (processed < 2048) {
        auto it = std::find_if(pendingInputs.begin(), pendingInputs.end(), [](const PendingHammerInput& c) { return c.remaining <= 0.0f; });
        if (it == pendingInputs.end()) break;
        PendingHammerInput cmd = *it;
        pendingInputs.erase(it);
        dispatchInput(cmd, player, map);
        ++processed;
    }
    if (processed >= 2048) std::cout << "[HammerIO] Output recursion safety cap reached; remaining events deferred." << std::endl;
}

void EntityManager::processHammerEntity(HammerEntity& h, float dt, Camera& player, BSP_L* map) {
    const std::string& cls = h.classname;
    if (cls == "ambient_generic" && h.keyValues["__initialized"] != "1") {
        h.keyValues["__initialized"] = "1";
        if (h.enabled) {
            const std::string sound = NormalizeSoundName(h.keyValues["message"]);
            const int flags = EntityInt(h.keyValues["spawnflags"], 0);
            const float volume = std::clamp(EntityFloat(h.keyValues["health"], 10.0f) / 10.0f, 0.0f, 1.0f);
            if (!sound.empty()) {
                if ((flags & 32) == 0) { h.loopSoundHandle = Sound_L::Get().playLoop(sound, h.pos, volume, (flags & 1) == 0); h.soundPlaying = true; }
                else Sound_L::Get().play(sound, h.pos, volume, (flags & 1) == 0, true);
            }
        }
    }
    if (cls == "logic_timer" && h.keyValues["__timerInitialized"] != "1") {
        h.keyValues["__timerInitialized"] = "1";
        h.thinkTimer = std::max(0.05f, EntityFloat(h.keyValues["refiretime"], 1.0f));
    }
    if ((cls == "scripted_scene" || cls == "logic_choreographed_scene" || cls == "instanced_scripted_scene") && h.keyValues["__sceneInitialized"] != "1") {
        h.keyValues["__sceneInitialized"] = "1";
        const int flags = EntityInt(h.keyValues["spawnflags"], 0);
        if ((flags & 1) != 0) {
            auto it = h.keyValues.find("scenefile");
            if (it == h.keyValues.end()) it = h.keyValues.find("m_iszscenefile");
            if (it == h.keyValues.end()) it = h.keyValues.find("scene");
            auto actorIt = h.keyValues.find("m_iszactor");
            if (actorIt == h.keyValues.end()) actorIt = h.keyValues.find("actor");
            SceneSystem_L::Get().startScene(&h, it == h.keyValues.end() ? std::string() : it->second, *this, player, map,
                                            actorIt == h.keyValues.end() ? std::string() : actorIt->second);
        }
    }
    if (cls == "scripted_sequence" && h.keyValues["__scriptInitialized"] != "1") {
        h.keyValues["__scriptInitialized"] = "1";
        const int flags = EntityInt(h.keyValues["spawnflags"], 0);
        if (h.targetname.empty() || (flags & 16) != 0) {
            PendingHammerInput command;
            command.target = h.targetname;
            command.input = h.targetname.empty() ? "BeginSequence" : "MoveToPosition";
            command.directTargetId = h.targetname.empty() ? h.ioId : 0;
            pendingInputs.push_back(std::move(command));
        }
    }
    if (!h.enabled) { h.wasTouching = false; return; }

    if (cls == "func_rotating" && h.model.loaded) {
        h.angles.y += EntityFloat(h.keyValues["speed"], 100.0f) * dt;
        if (h.angles.y >= 360.0f || h.angles.y <= -360.0f) h.angles.y = std::fmod(h.angles.y, 360.0f);
    }
    if (cls == "logic_timer") {
        h.thinkTimer -= dt;
        if (h.thinkTimer <= 0.0f) {
            fireOutput(&h,"ontimer",player,map);
            const float interval = std::max(0.05f, EntityFloat(h.keyValues["refiretime"],1.0f));
            const float random = std::max(0.0f, EntityFloat(h.keyValues["randomizetime"],0.0f));
            h.thinkTimer = interval + (random > 0 ? (static_cast<float>(std::rand())/RAND_MAX * random) : 0.0f);
        }
    }
    if ((cls == "func_button" || cls == "func_rot_button" || cls == "momentary_rot_button") && h.pressed && h.thinkTimer > 0.0f) {
        h.thinkTimer -= dt;
        if (h.thinkTimer <= 0.0f) { h.pressed=false; fireOutput(&h,"onout",player,map); fireOutput(&h,"onreleased",player,map); }
    }
    if ((cls == "func_door" || cls == "func_door_rotating" || cls == "func_door_prismatic" || cls == "prop_door_rotating") && h.opened) {
        h.thinkTimer -= dt;
        if (h.thinkTimer <= 0.0f && EntityFloat(h.keyValues["wait"],3.0f) >= 0.0f) {
            h.opened=false; if(h.model.loaded)h.model.playAnimationHint("close");
            fireOutput(&h,"onclose",player,map); fireOutput(&h,"onfullyclosed",player,map);
        }
    }
    if (!h.isTrigger()) return;
    const bool touching = h.containsPlayer(player);
    if (touching && !h.wasTouching && !h.triggeredOnce) {
        fireOutput(&h,"onstarttouch",player,map);
        fireOutput(&h,"ontrigger",player,map);
        if (cls == "trigger_once") h.triggeredOnce = true;
        if (cls == "trigger_multiple") h.nextThink = std::max(0.05f, EntityFloat(h.keyValues["wait"], 0.2f));
        if (cls == "trigger_teleport") {
            const std::string destName = h.keyValues.count("target") ? h.keyValues["target"] : "";
            for (Entity* dest : entities) if (dest && dest->targetname == destName && dest->classname == "info_teleport_destination") {
                player.pos = dest->pos + Vec3{0, player.height, 0}; player.velocity = {0,0,0};
                fireOutput(&h,"onteleport",player,map); break;
            }
        }
        if (cls == "trigger_hurt") h.damageTimer=0.0f;
    }
    if (touching && h.wasTouching && cls == "trigger_multiple" && !h.triggeredOnce) {
        h.nextThink -= dt;
        if (h.nextThink <= 0.0f) {
            fireOutput(&h,"ontrigger",player,map);
            h.nextThink = std::max(0.05f, EntityFloat(h.keyValues["wait"], 0.2f));
        }
    }

    if (touching && cls == "trigger_push") {
        std::istringstream a(h.keyValues["angles"]);
        float pitch=0.0f, yaw=0.0f, roll=0.0f;
        a >> pitch >> yaw >> roll;
        const float yr=yaw*3.14159265f/180.0f, pr=pitch*3.14159265f/180.0f;
        const float speed=EntityFloat(h.keyValues["speed"],1000.0f);
        player.velocity={std::sin(yr)*std::cos(pr)*speed,-std::sin(pr)*speed,-std::cos(yr)*std::cos(pr)*speed};
    }
    if (touching && cls == "trigger_hurt") {
        const float damagePerSecond = static_cast<float>(std::max(0, EntityInt(h.keyValues["damage"], 10)));
        h.damageTimer += std::max(0.0f, dt) * damagePerSecond;
        const int wholeDamage = static_cast<int>(h.damageTimer);
        if (wholeDamage > 0) {
            player.takeDamage(wholeDamage);
            h.damageTimer -= static_cast<float>(wholeDamage);
        }
    }
    if (!touching && h.wasTouching) {
        fireOutput(&h,"onendtouch",player,map);
        if (cls == "trigger_multiple") h.nextThink = 0.0f;
        if (cls == "trigger_hurt") h.damageTimer = 0.0f;
    }
    h.wasTouching = touching;
    if (cls == "trigger_once" && h.triggeredOnce) h.enabled=false;
}

void EntityManager::updateAll(float dt, Camera& player, BSP_L* map) {
    for (Entity* ent : entities) {
        auto* h = dynamic_cast<HammerEntity*>(ent);
        if (!h || h->classname != "scripted_sequence" || h->keyValues["__scriptInitialized"] == "1") continue;
        h->keyValues["__scriptInitialized"] = "1";
        const int flags = EntityInt(h->keyValues["spawnflags"], 0);
        if (h->targetname.empty() || (flags & 16) != 0) {
            PendingHammerInput command;
            command.target = h->targetname;
            command.input = h->targetname.empty() ? "BeginSequence" : "MoveToPosition";
            command.directTargetId = h->targetname.empty() ? h->ioId : 0;
            pendingInputs.push_back(std::move(command));
        }
    }
    if (!mapSpawnQueued) {
        mapSpawnQueued = true;
        for (Entity* ent : entities) if (ent && ent->classname == "logic_auto") fireOutput(ent,"onmapspawn",player,map);
    }


    processPendingInputs(dt, player, map);
    SceneSystem_L::Get().update(dt, player, map, *this);
    for (auto it = entities.begin(); it != entities.end();) {
        Entity* ent = *it;
        if (!ent) { it = entities.erase(it); continue; }
        if (ent->shouldDestroy) {
            if (!ent->destroyOutputsFired) {
                fireOutput(ent, "onkilled", player, map);
                if (dynamic_cast<NPC_L*>(ent)) fireOutput(ent, "ondeath", player, map);
                ent->destroyOutputsFired = true;
            }
            ++it;
            continue;
        }
        if (!ent->enabled) {
            if (auto* hammer = dynamic_cast<HammerEntity*>(ent)) processHammerEntity(*hammer, dt, player, map);
            ++it;
            continue;
        }
        ent->update(dt, player, map);
        if (auto* npc = dynamic_cast<NPC_L*>(ent)) npc->updateAI(dt, player, map, *this);
        if (auto* hammer = dynamic_cast<HammerEntity*>(ent)) processHammerEntity(*hammer,dt,player,map);
        if (ent->shouldDestroy && !ent->destroyOutputsFired) {
            fireOutput(ent, "onkilled", player, map);
            if (dynamic_cast<NPC_L*>(ent)) fireOutput(ent, "ondeath", player, map);
            ent->destroyOutputsFired = true;
        }
        ++it;
    }
    processPendingInputs(0.0f, player, map);

    for (auto it=entities.begin(); it!=entities.end();) {
        if (*it && (*it)->shouldDestroy) { delete *it; it=entities.erase(it); }
        else ++it;
    }
}

bool EntityManager::activateUse(const Vec3& origin, const Vec3& direction, float maxDistance, Camera& player, BSP_L* map) {
    Entity* best = nullptr;
    float bestDistance = maxDistance;
    Vec3 bestNormal{0,0,0};
    for (Entity* ent : entities) {
        if (!ent || !ent->enabled || ent->shouldDestroy || ent->multiplayerRemote) continue;
        float distance=0.0f; Vec3 normal{0,0,0};
        bool hit=false;
        if (auto* h=dynamic_cast<HammerEntity*>(ent)) hit=h->traceRay(origin,direction,distance,normal);
        else if (ent->traceRay(origin,direction,distance,normal)) {

            hit=std::any_of(ent->hammerOutputs.begin(),ent->hammerOutputs.end(),[](const HammerOutput& o){return o.event=="onuse";});
        }
        if (hit && distance >= 0.0f && distance <= bestDistance) { best=ent; bestDistance=distance; bestNormal=normal; }
    }
    (void)bestNormal;
    if (!best) return false;
    const std::string cls=best->classname;
    if (auto* h=dynamic_cast<HammerEntity*>(best)) {
        if (h->locked) { Sound_L::Get().play("buttons/button10.wav",best->pos,0.5f,true,false); return true; }
        if (cls == "func_button" || cls == "func_rot_button" || cls == "momentary_rot_button") {
            if (!h->pressed) {
                h->pressed=true; h->thinkTimer=std::max(0.0f,EntityFloat(h->keyValues["wait"],0.5f));
                fireOutput(best,"onpressed",player,map); fireOutput(best,"onin",player,map); fireOutput(best,"onuse",player,map);
            }
        } else if (cls == "func_door" || cls == "func_door_rotating" || cls == "func_door_prismatic" || cls == "prop_door_rotating") {
            if (!h->opened) {
                h->opened=true; h->thinkTimer=std::max(0.0f,EntityFloat(h->keyValues["wait"],3.0f));
                if (h->model.loaded) h->model.playAnimationHint("open");
                fireOutput(best,"onopen",player,map); fireOutput(best,"onfullyopen",player,map);
            } else if (EntityInt(h->keyValues["spawnflags"],0)&32) {
                h->opened=false; if (h->model.loaded) h->model.playAnimationHint("close"); fireOutput(best,"onclose",player,map);
            } else return true;
        } else if (cls == "func_breakable" || cls == "func_physbox") {
            fireOutput(best,"onbreak",player,map); best->shouldDestroy=true;
        } else {
            fireOutput(best,"onuse",player,map); fireOutput(best,"ontrigger",player,map);
        }
    } else {
        fireOutput(best,"onuse",player,map);
    }
    return true;
}

void EntityManager::renderAll() {
    for (Entity* ent : entities) if (ent && ent->enabled && !ent->shouldDestroy) ent->render();
}

bool EntityManager::traceRay(const Vec3& orig, const Vec3& dir, float& outDist, Vec3& outNormal) {
    bool hitAnything = false;
    float closestDist = 999999.0f;
    Vec3 closestNormal;
    for (Entity* ent : entities) {
        if (!ent || !ent->enabled || ent->shouldDestroy) continue;
        float tempDist=0.0f; Vec3 tempNormal;
        if (ent->traceRay(orig, dir, tempDist, tempNormal) && tempDist < closestDist) {
            closestDist=tempDist; closestNormal=tempNormal; hitAnything=true;
        }
    }
    if (hitAnything) { outDist=closestDist; outNormal=closestNormal; }
    return hitAnything;
}

Weapon_L* EntityManager::getActiveWeapon(const Camera& player) const {
    for (Entity* ent : entities) {
        Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
        if (weapon && weapon->isActive(player)) return weapon;
    }
    return nullptr;
}

void EntityManager::selectWeaponSlot(int slot, Camera& player) {
    if (slot <= 0) return;
    std::vector<Weapon_L*> choices;
    for (const std::string& owned : player.weaponInventory) {
        for (Entity* ent : entities) {
            Weapon_L* weapon = dynamic_cast<Weapon_L*>(ent);
            if (!weapon || weapon->className() != owned || weapon->slot() != slot) continue;
            choices.push_back(weapon);
            break;
        }
    }
    if (choices.empty()) return;
    std::sort(choices.begin(), choices.end(), [](Weapon_L* a, Weapon_L* b) {
        if (a->definition.bucketPosition != b->definition.bucketPosition)
            return a->definition.bucketPosition < b->definition.bucketPosition;
        return a->className() < b->className();
    });
    int nextIndex = 0;
    if (player.activeWeaponSlot == slot) {
        int current = -1;
        for (size_t i=0; i<choices.size(); ++i) {
            if (choices[i]->isActive(player)) { current=(int)i; break; }
        }
        if (current >= 0) nextIndex=(current+1)%(int)choices.size();
    }
    choices[(size_t)nextIndex]->equip(player);
    player.weaponCycleCursor[slot]=nextIndex;
}

PropPhysics* EntityManager::findPickupProp(const Vec3& orig, const Vec3& dir, float maxDistance, const Vec3& playerPos) {
    PropPhysics* best = nullptr;
    float bestScore = 999999.0f;
    float bestAlong = 999999.0f;

    for (Entity* ent : entities) {
        PropPhysics* prop = dynamic_cast<PropPhysics*>(ent);
        if (!prop) continue;
        if (prop->multiplayerRemote) continue;
        if (!prop->canPlayerPickup(playerPos)) continue;

        float along = 0.0f;
        float score = prop->pickupAimScore(orig, dir, maxDistance, along);
        if (score < bestScore || (std::fabs(score - bestScore) < 0.001f && along < bestAlong)) {
            best = prop;
            bestScore = score;
            bestAlong = along;
        }
    }

    return best;
}

void EntityManager::clear() {
    SceneSystem_L::Get().clear();
    for (Entity* ent : entities) delete ent;
    entities.clear();
    pendingInputs.clear();
    mapSpawnQueued = false;
}
