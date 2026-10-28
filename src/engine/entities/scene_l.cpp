#include "scene_l.h"
#include "entity_l.h"
#include "npc_l.h"
#include "sound_l.h"
#include "texture_l.h"
#include "bsp_l.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

namespace {

enum SceneEventType {
    SCENE_EVENT_UNSPECIFIED = 0,
    SCENE_EVENT_SECTION = 1,
    SCENE_EVENT_EXPRESSION = 2,
    SCENE_EVENT_LOOKAT = 3,
    SCENE_EVENT_MOVETO = 4,
    SCENE_EVENT_SPEAK = 5,
    SCENE_EVENT_GESTURE = 6,
    SCENE_EVENT_SEQUENCE = 7,
    SCENE_EVENT_FACE = 8,
    SCENE_EVENT_FIRETRIGGER = 9,
    SCENE_EVENT_FLEXANIMATION = 10,
    SCENE_EVENT_SUBSCENE = 11,
    SCENE_EVENT_LOOP = 12,
    SCENE_EVENT_INTERRUPT = 13,
    SCENE_EVENT_STOPPOINT = 14,
    SCENE_EVENT_PERMIT_RESPONSES = 15,
    SCENE_EVENT_GENERIC = 16
};

std::string lowerText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trimText(const std::string& value) {
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool matchTextPattern(const std::string& pattern, const std::string& value) {
    size_t pi = 0;
    size_t vi = 0;
    size_t star = std::string::npos;
    size_t resume = 0;
    while (vi < value.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == value[vi])) {
            ++pi;
            ++vi;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            resume = vi;
        } else if (star != std::string::npos) {
            pi = star + 1;
            vi = ++resume;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

std::string entityKeyValue(Entity* entity, const std::string& key) {
    if (!entity) return {};
    const std::string wanted = lowerText(key);
    auto it = entity->keyValues.find(wanted);
    if (it == entity->keyValues.end()) return {};
    return trimText(it->second);
}

bool isPlayerName(const std::string& value) {
    const std::string name = lowerText(trimText(value));
    return name == "!player" || name == "player";
}

bool isTargetSlotName(const std::string& value, int& slot) {
    const std::string name = lowerText(trimText(value));
    if (name.size() != 8 || name.rfind("!target", 0) != 0) return false;
    if (name[7] < '1' || name[7] > '8') return false;
    slot = name[7] - '0';
    return true;
}

bool parseFloatText(const std::string& value, float& out) {
    if (value.empty()) return false;
    try {
        size_t used = 0;
        const float parsed = std::stof(value, &used);
        if (used != value.size()) return false;
        out = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

int sceneTypeFromName(const std::string& value) {
    const std::string name = lowerText(value);
    if (name == "section") return SCENE_EVENT_SECTION;
    if (name == "expression") return SCENE_EVENT_EXPRESSION;
    if (name == "lookat") return SCENE_EVENT_LOOKAT;
    if (name == "moveto") return SCENE_EVENT_MOVETO;
    if (name == "speak") return SCENE_EVENT_SPEAK;
    if (name == "gesture") return SCENE_EVENT_GESTURE;
    if (name == "sequence") return SCENE_EVENT_SEQUENCE;
    if (name == "face") return SCENE_EVENT_FACE;
    if (name == "firetrigger") return SCENE_EVENT_FIRETRIGGER;
    if (name == "flexanimation") return SCENE_EVENT_FLEXANIMATION;
    if (name == "subscene") return SCENE_EVENT_SUBSCENE;
    if (name == "loop") return SCENE_EVENT_LOOP;
    if (name == "interrupt") return SCENE_EVENT_INTERRUPT;
    if (name == "stoppoint" || name == "stopoint") return SCENE_EVENT_STOPPOINT;
    if (name == "permit_responses") return SCENE_EVENT_PERMIT_RESPONSES;
    if (name == "generic") return SCENE_EVENT_GENERIC;
    return SCENE_EVENT_UNSPECIFIED;
}

struct TextTokenReader {
    std::string text;
    size_t position = 0;

    void skipSpace() {
        while (position < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[position]);
            if (std::isspace(c)) {
                ++position;
                continue;
            }
            if (c == '/' && position + 1 < text.size() && text[position + 1] == '/') {
                position += 2;
                while (position < text.size() && text[position] != '\n') ++position;
                continue;
            }
            if (c == '/' && position + 1 < text.size() && text[position + 1] == '*') {
                position += 2;
                while (position + 1 < text.size() && !(text[position] == '*' && text[position + 1] == '/')) ++position;
                if (position + 1 < text.size()) position += 2;
                continue;
            }
            break;
        }
    }

    bool next(std::string& out) {
        skipSpace();
        if (position >= text.size()) return false;
        const char c = text[position];
        if (c == '{' || c == '}') {
            out.assign(1, c);
            ++position;
            return true;
        }
        if (c == '"') {
            ++position;
            out.clear();
            while (position < text.size()) {
                const char ch = text[position++];
                if (ch == '"') break;
                if (ch == '\\' && position < text.size()) {
                    const char escaped = text[position++];
                    if (escaped == 'n') out.push_back('\n');
                    else if (escaped == 't') out.push_back('\t');
                    else if (escaped == '"' || escaped == '\\') out.push_back(escaped);
                    else {
                        out.push_back('\\');
                        out.push_back(escaped);
                    }
                } else {
                    out.push_back(ch);
                }
            }
            return true;
        }
        const size_t start = position;
        while (position < text.size()) {
            const char ch = text[position];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}') break;
            ++position;
        }
        out = text.substr(start, position - start);
        return !out.empty();
    }

    void skipBlock() {
        std::string token;
        while (next(token)) {
            if (token == "{") break;
            if (token == "}") return;
        }
        int depth = 1;
        while (depth > 0 && next(token)) {
            if (token == "{") ++depth;
            else if (token == "}") --depth;
        }
    }
};

uint32_t readU32(const std::vector<char>& data, size_t offset) {
    if (offset + 4 > data.size()) return 0;
    uint32_t value = 0;
    std::memcpy(&value, data.data() + offset, 4);
    return value;
}

struct BinReader {
    const std::vector<char>& data;
    size_t position = 0;
    bool ok = true;

    explicit BinReader(const std::vector<char>& source) : data(source) {}

    int8_t i8() {
        if (position + 1 > data.size()) { ok = false; return 0; }
        return static_cast<int8_t>(data[position++]);
    }

    uint8_t u8() {
        if (position + 1 > data.size()) { ok = false; return 0; }
        return static_cast<uint8_t>(data[position++]);
    }

    uint16_t u16() {
        if (position + 2 > data.size()) { ok = false; return 0; }
        uint16_t value = 0;
        std::memcpy(&value, data.data() + position, 2);
        position += 2;
        return value;
    }

    int32_t i32() {
        if (position + 4 > data.size()) { ok = false; return 0; }
        int32_t value = 0;
        std::memcpy(&value, data.data() + position, 4);
        position += 4;
        return value;
    }

    float f32() {
        if (position + 4 > data.size()) { ok = false; return 0.0f; }
        float value = 0.0f;
        std::memcpy(&value, data.data() + position, 4);
        position += 4;
        return value;
    }
};

}

SceneSystem_L& SceneSystem_L::Get() {
    static SceneSystem_L system;
    return system;
}

void SceneSystem_L::init(const std::string& gameDirectory, Texture_L* texture) {
    if (gameDir != gameDirectory || textureSystem != texture) {
        clear();
        cache.clear();
        stringPool.clear();
        imageData.clear();
        imageStringOffsets.clear();
        imagePoolAttempted = false;
        imagePoolReady = false;
    }
    gameDir = gameDirectory;
    textureSystem = texture;
}

void SceneSystem_L::shutdown() {
    clear();
    cache.clear();
    stringPool.clear();
    imageData.clear();
    imageStringOffsets.clear();
    imagePoolAttempted = false;
    imagePoolReady = false;
    textureSystem = nullptr;
    gameDir.clear();
}

void SceneSystem_L::clear() {
    for (Instance& instance : instances) {
        instance.data.reset();
        instance.actorEntities.clear();
        instance.actorStarted.clear();
        instance.actorEnded.clear();
        instance.globalStarted.clear();
        instance.globalEnded.clear();
    }
    instances.clear();
    error.clear();
}

std::string SceneSystem_L::normalizeScenePath(const std::string& file) {
    std::string path = trimText(file);
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty()) return path;
    if (path.find('/') == std::string::npos) path = "scenes/" + path;
    if (path.size() < 4 || lowerText(path.substr(path.size() - 4)) != ".vcd") path += ".vcd";
    return path;
}

std::string SceneSystem_L::imageString(int id) const {
    if (id < 0 || static_cast<size_t>(id) >= stringPool.size()) return {};
    return stringPool[static_cast<size_t>(id)];
}

bool SceneSystem_L::loadImagePool() {
    if (imagePoolAttempted) return imagePoolReady;
    imagePoolAttempted = true;
    if (!textureSystem) return false;
    std::vector<char> data;
    if (!textureSystem->getFileRawData("scenes/scenes.image", data) || data.size() < 20) return false;
    const uint32_t id = readU32(data, 0);
    const uint32_t version = readU32(data, 4);
    const uint32_t numStrings = readU32(data, 12);
    if (id != 0x46495356u || version != 2u || numStrings == 0 || numStrings > 2000000u) return false;
    if (20ull + static_cast<unsigned long long>(numStrings) * 4ull > data.size()) return false;
    imageData = std::move(data);
    imageStringOffsets.resize(numStrings);
    for (uint32_t i = 0; i < numStrings; ++i) imageStringOffsets[i] = readU32(imageData, 20 + static_cast<size_t>(i) * 4);
    stringPool.resize(numStrings);
    for (uint32_t i = 0; i < numStrings; ++i) {
        const size_t offset = imageStringOffsets[i];
        if (offset >= imageData.size()) continue;
        const char* begin = imageData.data() + offset;
        const size_t maxLength = imageData.size() - offset;
        size_t length = 0;
        while (length < maxLength && begin[length] != '\0') ++length;
        stringPool[i].assign(begin, length);
    }
    imagePoolReady = true;
    return true;
}

bool SceneSystem_L::parseBinaryScene(const std::vector<char>& bytes, SceneData_L& out) {
    loadImagePool();
    BinReader reader(bytes);
    if (reader.i32() != 0x64637662) return false;
    if (reader.u8() != 4) return false;
    reader.i32();

    const int globalEvents = reader.u8();
    for (int i = 0; i < globalEvents && reader.ok; ++i) {
        SceneEvent_L event;
        const int type = reader.i8();
        event.type = type;
        event.name = imageString(reader.u16());
        event.startTime = reader.f32();
        event.endTime = reader.f32();
        event.params = imageString(reader.u16());
        event.params2 = imageString(reader.u16());
        event.params3 = imageString(reader.u16());
        const int curveCount = reader.u8();
        for (int c = 0; c < curveCount; ++c) { reader.f32(); reader.u8(); }
        event.flags = reader.u8();
        event.distance = reader.f32();
        const int relativeTags = reader.u8();
        for (int t = 0; t < relativeTags; ++t) { reader.u16(); reader.u8(); }
        const int timingTags = reader.u8();
        for (int t = 0; t < timingTags; ++t) { reader.u16(); reader.u8(); }
        for (int set = 0; set < 2; ++set) {
            const int tags = reader.u8();
            for (int t = 0; t < tags; ++t) { reader.u16(); reader.u16(); }
        }
        if (event.type == SCENE_EVENT_GESTURE) event.gestureDuration = reader.f32();
        if (reader.i8() == 1) { reader.u16(); reader.u16(); }
        const int flexTracks = reader.u8();
        for (int track = 0; track < flexTracks; ++track) {
            reader.u16();
            const int flags = reader.u8();
            reader.f32();
            reader.f32();
            const int samples = reader.u16();
            for (int s = 0; s < samples; ++s) { reader.f32(); reader.u8(); reader.u16(); }
            if ((flags & 2) != 0) {
                const int combo = reader.u16();
                for (int s = 0; s < combo; ++s) { reader.f32(); reader.u8(); reader.u16(); }
            }
        }
        if (event.type == SCENE_EVENT_LOOP) event.loopCount = reader.i8();
        if (event.type == SCENE_EVENT_SPEAK) { reader.i8(); reader.u16(); reader.i8(); }
        if (!reader.ok) return false;
        out.globalEvents.push_back(event);
    }

    const int actorCount = reader.u8();
    for (int i = 0; i < actorCount && reader.ok; ++i) {
        SceneActor_L actor;
        actor.name = imageString(reader.u16());
        const int channelCount = reader.u8();
        for (int c = 0; c < channelCount && reader.ok; ++c) {
            reader.u16();
            const int eventCount = reader.u8();
            for (int e = 0; e < eventCount && reader.ok; ++e) {
                SceneEvent_L event;
                event.channel = c;
                const int type = reader.i8();
                event.type = type;
                event.name = imageString(reader.u16());
                event.startTime = reader.f32();
                event.endTime = reader.f32();
                event.params = imageString(reader.u16());
                event.params2 = imageString(reader.u16());
                event.params3 = imageString(reader.u16());
                const int curveCount = reader.u8();
                for (int r = 0; r < curveCount; ++r) { reader.f32(); reader.u8(); }
                event.flags = reader.u8();
                event.distance = reader.f32();
                const int relativeTags = reader.u8();
                for (int t = 0; t < relativeTags; ++t) { reader.u16(); reader.u8(); }
                const int timingTags = reader.u8();
                for (int t = 0; t < timingTags; ++t) { reader.u16(); reader.u8(); }
                for (int set = 0; set < 2; ++set) {
                    const int tags = reader.u8();
                    for (int t = 0; t < tags; ++t) { reader.u16(); reader.u16(); }
                }
                if (event.type == SCENE_EVENT_GESTURE) event.gestureDuration = reader.f32();
                if (reader.i8() == 1) { reader.u16(); reader.u16(); }
                const int flexTracks = reader.u8();
                for (int track = 0; track < flexTracks; ++track) {
                    reader.u16();
                    const int flags = reader.u8();
                    reader.f32();
                    reader.f32();
                    const int samples = reader.u16();
                    for (int s = 0; s < samples; ++s) { reader.f32(); reader.u8(); reader.u16(); }
                    if ((flags & 2) != 0) {
                        const int combo = reader.u16();
                        for (int s = 0; s < combo; ++s) { reader.f32(); reader.u8(); reader.u16(); }
                    }
                }
                if (event.type == SCENE_EVENT_LOOP) event.loopCount = reader.i8();
                if (event.type == SCENE_EVENT_SPEAK) { reader.i8(); reader.u16(); reader.i8(); }
                if (!reader.ok) return false;
                actor.events.push_back(event);
            }
            reader.i8();
        }
        actor.active = reader.i8() == 1;
        out.actors.push_back(actor);
    }

    const int sceneRamp = reader.u8();
    for (int i = 0; i < sceneRamp && reader.ok; ++i) { reader.f32(); reader.u8(); }
    reader.u8();

    float duration = 0.0f;
    for (const SceneEvent_L& event : out.globalEvents) duration = std::max(duration, event.endTime >= 0.0f ? event.endTime : event.startTime);
    for (const SceneActor_L& actor : out.actors)
        for (const SceneEvent_L& event : actor.events) duration = std::max(duration, event.endTime >= 0.0f ? event.endTime : event.startTime);
    out.duration = duration + 0.25f;
    out.valid = reader.ok && (!out.globalEvents.empty() || !out.actors.empty());
    out.binary = true;
    return out.valid;
}

bool SceneSystem_L::parseTextScene(const std::string& text, SceneData_L& out) {
    TextTokenReader reader;
    reader.text = text;
    std::string token;

    auto parseEvent = [&](int actorIndex, SceneEvent_L& event) -> bool {
        std::string typeName;
        std::string eventName;
        if (!reader.next(typeName) || !reader.next(eventName)) return false;
        event.type = sceneTypeFromName(typeName);
        event.name = eventName;
        std::string block;
        if (!reader.next(block) || block != "{") return false;
        while (reader.next(token)) {
            if (token == "}") return true;
            if (token == "time") {
                std::string first;
                if (!reader.next(first)) break;
                float start = 1.0f;
                parseFloatText(first, start);
                event.startTime = start;
                event.endTime = 1.0f;
                const size_t save = reader.position;
                std::string second;
                float end = 0.0f;
                if (reader.next(second) && parseFloatText(second, end)) event.endTime = end;
                else reader.position = save;
            } else if (token == "param") {
                reader.next(event.params);
            } else if (token == "param2") {
                reader.next(event.params2);
            } else if (token == "param3") {
                reader.next(event.params3);
            } else if (token == "pitch" || token == "yaw" || token == "loopcount" || token == "active") {
                std::string value;
                reader.next(value);
                if (token == "loopcount") event.loopCount = std::atoi(value.c_str());
            } else if (token == "distancetotarget") {
                std::string value;
                reader.next(value);
                parseFloatText(value, event.distance);
            } else if (token == "sequenceduration") {
                std::string value;
                reader.next(value);
                parseFloatText(value, event.gestureDuration);
            } else if (token == "relativetag") {
                std::string value;
                reader.next(value);
                reader.next(value);
            } else if (token == "cctype" || token == "cctoken") {
                std::string value;
                reader.next(value);
            } else if (token == "tags" || token == "absolutetags" || token == "flextimingtags" || token == "event_ramp" || token == "flexanimations") {
                reader.skipBlock();
            } else if (token == "resumecondition" || token == "fixedlength" || token == "lockbodyfacing" ||
                       token == "forceshortmovement" || token == "synctofollowinggesture" || token == "playoverscript" ||
                       token == "cc_usingcombinedfile" || token == "cc_combinedusesgender" || token == "cc_noattenuate") {
            } else {
                const size_t save = reader.position;
                std::string value;
                if (reader.next(value) && value == "{") reader.skipBlock();
                else reader.position = save;
            }
        }
        (void)actorIndex;
        return false;
    };

    while (reader.next(token)) {
        if (token == "event") {
            SceneEvent_L event;
            if (parseEvent(-1, event)) out.globalEvents.push_back(event);
        } else if (token == "actor") {
            SceneActor_L actor;
            if (!reader.next(actor.name)) break;
            std::string block;
            if (!reader.next(block) || block != "{") break;
            while (reader.next(token)) {
                if (token == "}") break;
                if (token == "channel") {
                    std::string channelName;
                    if (!reader.next(channelName)) break;
                    if (!reader.next(block) || block != "{") break;
                    int channelIndex = 0;
                    while (reader.next(token)) {
                        if (token == "}") break;
                        if (token == "event") {
                            SceneEvent_L event;
                            event.channel = channelIndex;
                            if (parseEvent(static_cast<int>(out.actors.size()), event)) actor.events.push_back(event);
                        } else if (token == "active") {
                            std::string value;
                            reader.next(value);
                        } else {
                            const size_t save = reader.position;
                            std::string value;
                            if (reader.next(value) && value == "{") reader.skipBlock();
                            else reader.position = save;
                        }
                    }
                    ++channelIndex;
                } else if (token == "faceposermodel") {
                    std::string value;
                    reader.next(value);
                } else if (token == "active") {
                    std::string value;
                    reader.next(value);
                    actor.active = value.empty() || std::atoi(value.c_str()) != 0;
                } else {
                    const size_t save = reader.position;
                    std::string value;
                    if (reader.next(value) && value == "{") reader.skipBlock();
                    else reader.position = save;
                }
            }
            out.actors.push_back(actor);
        } else if (token == "mapname") {
            std::string value;
            reader.next(value);
        } else if (token == "fps" || token == "snap" || token == "ignorePhonemes") {
            std::string value;
            reader.next(value);
        } else if (token == "scene_ramp" || token == "scalesettings") {
            reader.skipBlock();
        }
    }

    float duration = 0.0f;
    for (const SceneEvent_L& event : out.globalEvents) duration = std::max(duration, event.endTime >= 0.0f ? event.endTime : event.startTime);
    for (const SceneActor_L& actor : out.actors)
        for (const SceneEvent_L& event : actor.events) duration = std::max(duration, event.endTime >= 0.0f ? event.endTime : event.startTime);
    out.duration = duration + 0.25f;
    out.valid = !out.globalEvents.empty() || !out.actors.empty();
    out.binary = false;
    return out.valid;
}

std::shared_ptr<SceneData_L> SceneSystem_L::loadScene(const std::string& file, bool& ok) {
    ok = false;
    const std::string normalized = normalizeScenePath(file);
    if (normalized.empty()) {
        error = "empty scene file";
        return nullptr;
    }
    auto cached = cache.find(normalized);
    if (cached != cache.end()) {
        ok = cached->second && cached->second->valid;
        return cached->second;
    }
    if (!textureSystem) {
        error = "no filesystem for " + normalized;
        return nullptr;
    }
    std::vector<char> bytes;
    std::string usedPath = normalized;
    if (!textureSystem->getFileRawData(usedPath, bytes) || bytes.empty()) {
        std::string alternate = normalized;
        if (alternate.rfind("scenes/", 0) == 0) alternate = alternate.substr(7);
        else alternate = "scenes/" + alternate;
        if (textureSystem->getFileRawData(alternate, bytes) && !bytes.empty()) usedPath = alternate;
    }
    if (bytes.empty()) {
        error = "scene not found: " + normalized;
        return nullptr;
    }
    auto data = std::make_shared<SceneData_L>();
    data->path = usedPath;
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "bvcd", 4) == 0) {
        if (!parseBinaryScene(bytes, *data)) {
            error = "invalid binary scene: " + usedPath;
            return nullptr;
        }
    } else {
        size_t offset = 0;
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
            static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) offset = 3;
        const std::string text(bytes.data() + offset, bytes.size() - offset);
        if (!parseTextScene(text, *data)) {
            error = "invalid text scene: " + usedPath;
            return nullptr;
        }
    }
    cache[normalized] = data;
    ok = data->valid;
    return data;
}

Entity* SceneSystem_L::findEntity(EntityManager& entities, const std::string& name) const {
    const std::string wanted = lowerText(trimText(name));
    if (wanted.empty()) return nullptr;
    for (Entity* entity : entities.entities) {
        if (!entity || entity->shouldDestroy) continue;
        if (lowerText(trimText(entity->targetname)) == wanted) return entity;
    }
    if (wanted.find('*') != std::string::npos || wanted.find('?') != std::string::npos) {
        for (Entity* entity : entities.entities) {
            if (!entity || entity->shouldDestroy) continue;
            if (matchTextPattern(wanted, lowerText(trimText(entity->targetname)))) return entity;
        }
    }
    return nullptr;
}

Entity* SceneSystem_L::findNamedEntity(EntityManager& entities, Entity* owner, const std::string& name) const {
    const std::string trimmed = trimText(name);
    const std::string wanted = lowerText(trimmed);
    if (wanted.empty()) return nullptr;
    if (isPlayerName(wanted)) return nullptr;
    if (wanted == "!self" || wanted == "self" || wanted == "!caller" || wanted == "!activator") return owner;
    int slot = 0;
    if (isTargetSlotName(wanted, slot)) {
        const std::string targetName = entityKeyValue(owner, "target" + std::to_string(slot));
        return targetName.empty() ? nullptr : findEntity(entities, targetName);
    }
    return findEntity(entities, trimmed);
}

Entity* SceneSystem_L::findActorEntity(EntityManager& entities, Entity* owner, const std::string& name, const std::string& actorFilter) const {
    if (isPlayerName(name)) return nullptr;
    Entity* target = findNamedEntity(entities, owner, name);
    if (target) return target;
    if (!actorFilter.empty()) {
        target = findNamedEntity(entities, owner, actorFilter);
        if (target) return target;
    }
    const std::string wanted = lowerText(trimText(name));
    if (!wanted.empty() && wanted[0] != '!') {
        for (Entity* entity : entities.entities) {
            auto* npc = dynamic_cast<NPC_L*>(entity);
            if (!npc || npc->shouldDestroy || npc->health <= 0) continue;
            const std::string cls = lowerText(npc->classname);
            if (cls == "npc_" + wanted || cls.find(wanted) != std::string::npos) return npc;
            const std::string model = lowerText(npc->modelPath);
            if (model.find(wanted) != std::string::npos) return npc;
        }
    }
    return nullptr;
}

Entity* SceneSystem_L::ownerEntity(EntityManager& entities, unsigned long long id) const {
    if (id == 0) return nullptr;
    for (Entity* entity : entities.entities) if (entity && entity->ioId == id) return entity;
    return nullptr;
}

bool SceneSystem_L::entityAlive(EntityManager& entities, Entity* entity) const {
    if (!entity) return false;
    for (Entity* candidate : entities.entities) if (candidate == entity) return true;
    return false;
}

void SceneSystem_L::bindActors(Instance& instance, Entity* owner, EntityManager& entities, const std::string& actorFilter) {
    const size_t actorCount = instance.data ? instance.data->actors.size() : 0;
    instance.actorEntities.assign(actorCount, nullptr);
    instance.actorStarted.assign(actorCount, {});
    instance.actorEnded.assign(actorCount, {});
    std::vector<Entity*> used;
    for (size_t i = 0; i < actorCount; ++i) {
        const SceneActor_L& actor = instance.data->actors[i];
        instance.actorStarted[i].assign(actor.events.size(), 0);
        instance.actorEnded[i].assign(actor.events.size(), 0);
        Entity* target = findActorEntity(entities, owner, actor.name, actorFilter);
        if (std::find(used.begin(), used.end(), target) != used.end()) target = nullptr;
        instance.actorEntities[i] = target;
        if (target) used.push_back(target);
        if (auto* npc = dynamic_cast<NPC_L*>(target)) npc->beginSceneControl();
    }
    if (actorCount == 1 && !instance.actorEntities[0] && !isPlayerName(instance.data->actors[0].name)) {
        NPC_L* onlyNpc = nullptr;
        for (Entity* entity : entities.entities) {
            auto* npc = dynamic_cast<NPC_L*>(entity);
            if (!npc || npc->shouldDestroy || npc->health <= 0) continue;
            if (onlyNpc) { onlyNpc = nullptr; break; }
            onlyNpc = npc;
        }
        if (onlyNpc) {
            instance.actorEntities[0] = onlyNpc;
            onlyNpc->beginSceneControl();
        }
    }
    if (instance.data) {
        instance.globalStarted.assign(instance.data->globalEvents.size(), 0);
        instance.globalEnded.assign(instance.data->globalEvents.size(), 0);
    }
}

bool SceneSystem_L::startScene(Entity* sceneEntity, const std::string& file, EntityManager& entities,
                               Camera& player, BSP_L* map, const std::string& actorFilter) {
    if (!sceneEntity || sceneEntity->shouldDestroy) return false;
    bool ok = false;
    std::shared_ptr<SceneData_L> data = loadScene(file, ok);
    if (!ok || !data) return false;
    for (size_t i = 0; i < instances.size();) {
        if (instances[i].ownerId == sceneEntity->ioId) {
            finishInstance(instances[i], entities, player, map, true);
            instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    Instance instance;
    instance.ownerId = sceneEntity->ioId;
    instance.data = data;
    instance.actorFilter = actorFilter;
    bindActors(instance, sceneEntity, entities, actorFilter);
    for (size_t i = 0; i < instance.actorEntities.size(); ++i) {
        if (!instance.actorEntities[i] && !isPlayerName(instance.data->actors[i].name)) {
            instance.awaitingActors = true;
            break;
        }
    }
    instance.started = true;
    instance.onStartFired = !instance.awaitingActors;
    const bool fireStart = instance.onStartFired;
    instances.push_back(std::move(instance));
    if (fireStart) entities.fireEntityOutput(sceneEntity, "onstart", player, map);
    return true;
}

void SceneSystem_L::finishInstance(Instance& instance, EntityManager& entities, Camera& player, BSP_L* map, bool canceled) {
    Entity* owner = ownerEntity(entities, instance.ownerId);
    for (Entity* actor : instance.actorEntities) {
        if (!entityAlive(entities, actor) || !actor) continue;
        if (auto* npc = dynamic_cast<NPC_L*>(actor)) npc->endSceneControl();
    }
    if (owner && !owner->shouldDestroy) {
        if (canceled) {
            entities.fireEntityOutput(owner, "oncanceled", player, map);
            entities.fireEntityOutput(owner, "oncancel", player, map);
        } else if (!instance.completionFired) {
            entities.fireEntityOutput(owner, "oncompletion", player, map);
        }
    }
}

void SceneSystem_L::stopScene(Entity* sceneEntity, EntityManager& entities, Camera& player, BSP_L* map) {
    if (!sceneEntity) return;
    for (size_t i = 0; i < instances.size();) {
        if (instances[i].ownerId == sceneEntity->ioId) {
            finishInstance(instances[i], entities, player, map, true);
            instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

void SceneSystem_L::pauseScene(Entity* sceneEntity, bool paused) {
    if (!sceneEntity) return;
    for (Instance& instance : instances) if (instance.ownerId == sceneEntity->ioId) instance.paused = paused;
}

bool SceneSystem_L::isScenePlaying(Entity* sceneEntity) const {
    if (!sceneEntity) return false;
    for (const Instance& instance : instances) if (instance.ownerId == sceneEntity->ioId) return true;
    return false;
}

bool SceneSystem_L::triggerScene(Entity* sceneEntity, EntityManager& entities, Camera& player, BSP_L* map) {
    if (!sceneEntity) return false;
    for (const Instance& instance : instances) {
        if (instance.ownerId == sceneEntity->ioId) {
            entities.fireEntityOutput(sceneEntity, "ontrigger", player, map);
            return true;
        }
    }
    return false;
}

void SceneSystem_L::startEvent(Instance& instance, int actorIndex, size_t eventIndex, EntityManager& entities, Camera& player, BSP_L* map) {
    if (!instance.data) return;
    const SceneEvent_L* event = nullptr;
    Entity* actor = nullptr;
    if (actorIndex >= 0) {
        if (static_cast<size_t>(actorIndex) >= instance.data->actors.size()) return;
        const SceneActor_L& sceneActor = instance.data->actors[static_cast<size_t>(actorIndex)];
        if (eventIndex >= sceneActor.events.size()) return;
        event = &sceneActor.events[eventIndex];
        if (static_cast<size_t>(actorIndex) < instance.actorEntities.size()) actor = instance.actorEntities[static_cast<size_t>(actorIndex)];
    } else {
        if (eventIndex >= instance.data->globalEvents.size()) return;
        event = &instance.data->globalEvents[eventIndex];
    }
    if (!event) return;
    if (actor && !entityAlive(entities, actor)) actor = nullptr;
    NPC_L* npc = dynamic_cast<NPC_L*>(actor);
    Entity* owner = ownerEntity(entities, instance.ownerId);
    const Vec3 eventOrigin = npc ? npc->pos : (actor ? actor->pos : (owner ? owner->pos : Vec3{0,0,0}));

    const std::string primaryParam = !event->params.empty() ? event->params : event->name;
    auto resolveTarget = [&](const std::string& value) -> Entity* {
        if (value.empty() || isPlayerName(value)) return nullptr;
        return findNamedEntity(entities, owner, value);
    };
    auto targetPoint = [&](const std::string& value, Vec3& point) -> bool {
        std::istringstream stream(value);
        return static_cast<bool>(stream >> point.x >> point.y >> point.z);
    };
    switch (event->type) {
    case SCENE_EVENT_GESTURE:
    case SCENE_EVENT_SEQUENCE:
        if (npc) {
            const std::string sequence = !event->params.empty() ? event->params : event->name;
            npc->scenePlaySequence(sequence);
        }
        break;
    case SCENE_EVENT_LOOKAT:
    case SCENE_EVENT_FACE: {
        Entity* target = resolveTarget(primaryParam);
        if (npc) {
            auto setTarget = [&](const Vec3& point) {
                if (event->type == SCENE_EVENT_FACE) npc->sceneSetFaceTarget(point);
                else npc->sceneSetLookTarget(point);
            };
            if (isPlayerName(primaryParam)) setTarget(player.pos + Vec3{0, 48, 0});
            else if (target) setTarget(target->pos);
            else {
                Vec3 point{};
                if (targetPoint(primaryParam, point)) setTarget(point);
            }
        }
        break;
    }
    case SCENE_EVENT_MOVETO: {
        Entity* target = resolveTarget(primaryParam);
        if (npc) {
            const float stopDistance = event->distance > 0.0f ? std::clamp(event->distance, 0.0f, 512.0f) : 16.0f;
            const float targetSpeed = lowerText(event->params2).find("run") != std::string::npos ? 220.0f : 110.0f;
            if (isPlayerName(primaryParam)) npc->sceneSetMoveTarget(player.pos, targetSpeed, stopDistance);
            else if (target) npc->sceneSetMoveTarget(target->pos, targetSpeed, stopDistance);
            else {
                Vec3 point{};
                if (targetPoint(primaryParam, point)) npc->sceneSetMoveTarget(point, targetSpeed, stopDistance);
            }
        }
        break;
    }
    case SCENE_EVENT_SPEAK: {
        const std::string sound = event->params.empty() ? event->name : event->params;
        if (!sound.empty()) Sound_L::Get().play(sound, eventOrigin, 1.0f, true, true);
        break;
    }
    case SCENE_EVENT_FIRETRIGGER: {
        if (owner) {
            const int trigger = std::atoi(event->params.c_str());
            if (trigger >= 1 && trigger <= 16) entities.fireEntityOutput(owner, "ontrigger" + std::to_string(trigger), player, map);
        }
        break;
    }
    case SCENE_EVENT_LOOP: {
        instance.loop = true;
        instance.loopRemaining = event->loopCount;
        float loopTime = event->startTime;
        if (!event->params.empty()) parseFloatText(event->params, loopTime);
        instance.loopStart = std::max(0.0f, loopTime);
        break;
    }
    case SCENE_EVENT_STOPPOINT:
        if (!instance.completionFired) {
            instance.completionFired = true;
            if (owner) entities.fireEntityOutput(owner, "oncompletion", player, map);
        }
        break;
    case SCENE_EVENT_GENERIC:
        if (npc && !event->params.empty() && lowerText(primaryParam).rfind("ai_", 0) != 0) npc->scenePlaySequence(primaryParam);
        break;
    default:
        break;
    }
}

void SceneSystem_L::endEvent(Instance& instance, int actorIndex, size_t eventIndex, EntityManager& entities, Camera& player, BSP_L* map) {
    (void)player;
    (void)map;
    if (!instance.data) return;
    const SceneEvent_L* event = nullptr;
    Entity* actor = nullptr;
    if (actorIndex >= 0) {
        if (static_cast<size_t>(actorIndex) >= instance.data->actors.size()) return;
        const SceneActor_L& sceneActor = instance.data->actors[static_cast<size_t>(actorIndex)];
        if (eventIndex >= sceneActor.events.size()) return;
        event = &sceneActor.events[eventIndex];
        if (static_cast<size_t>(actorIndex) < instance.actorEntities.size()) actor = instance.actorEntities[static_cast<size_t>(actorIndex)];
    } else {
        if (eventIndex >= instance.data->globalEvents.size()) return;
        event = &instance.data->globalEvents[eventIndex];
    }
    if (!event) return;
    if (actor && !entityAlive(entities, actor)) actor = nullptr;
    NPC_L* npc = dynamic_cast<NPC_L*>(actor);
    if (!npc) return;
    switch (event->type) {
    case SCENE_EVENT_MOVETO:
        npc->sceneClearMoveTarget();
        break;
    case SCENE_EVENT_LOOKAT:
        npc->sceneClearLookTarget();
        break;
    case SCENE_EVENT_FACE:
        npc->sceneClearFaceTarget();
        break;
    case SCENE_EVENT_GESTURE:
    case SCENE_EVENT_SEQUENCE: {
        const std::string sequence = !event->params.empty() ? event->params : event->name;
        npc->sceneStopSequence(sequence);
        break;
    }
    case SCENE_EVENT_GENERIC:
        if (!event->params.empty()) npc->sceneStopSequence(event->params);
        break;
    default:
        break;
    }
}

void SceneSystem_L::update(float dt, Camera& player, BSP_L* map, EntityManager& entities) {
    (void)player;
    if (instances.empty() || dt <= 0.0f) return;
    for (size_t i = 0; i < instances.size();) {
        Instance& instance = instances[i];
        Entity* owner = ownerEntity(entities, instance.ownerId);
        if (!owner || owner->shouldDestroy || !instance.data) {
            finishInstance(instance, entities, player, map, true);
            instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        if (instance.awaitingActors) {
            std::vector<Entity*> used;
            for (Entity* actor : instance.actorEntities) if (actor) used.push_back(actor);
            bool ready = true;
            for (size_t a = 0; a < instance.actorEntities.size(); ++a) {
                if (instance.actorEntities[a] || isPlayerName(instance.data->actors[a].name)) continue;
                Entity* target = findActorEntity(entities, owner, instance.data->actors[a].name, instance.actorFilter);
                if (target && std::find(used.begin(), used.end(), target) != used.end()) target = nullptr;
                if (target) {
                    instance.actorEntities[a] = target;
                    used.push_back(target);
                    if (auto* npc = dynamic_cast<NPC_L*>(target)) npc->beginSceneControl();
                } else {
                    ready = false;
                }
            }
            instance.awaitingActors = !ready;
            if (instance.awaitingActors) {
                ++i;
                continue;
            }
        }
        if (!instance.onStartFired) {
            instance.onStartFired = true;
            entities.fireEntityOutput(owner, "onstart", player, map);
        }
        if (!instance.paused) {
            instance.currentTime += dt;
            if (instance.currentTime >= instance.data->duration) {
                const bool canLoop = instance.loopRemaining < 0 || instance.loopRemaining > 0;
                if (instance.loop && canLoop && instance.loopStart >= 0.0f && instance.data->duration > instance.loopStart + 0.01f) {
                    if (instance.loopRemaining > 0) --instance.loopRemaining;
                    const float span = instance.data->duration - instance.loopStart;
                    instance.currentTime = instance.loopStart + std::fmod(instance.currentTime - instance.loopStart, span);
                    for (size_t a = 0; a < instance.data->actors.size(); ++a) {
                        for (size_t e = 0; e < instance.data->actors[a].events.size(); ++e) {
                            if (instance.data->actors[a].events[e].startTime >= instance.loopStart - 0.001f) {
                                instance.actorStarted[a][e] = 0;
                                instance.actorEnded[a][e] = 0;
                            }
                        }
                    }
                    for (size_t e = 0; e < instance.data->globalEvents.size(); ++e) {
                        if (instance.data->globalEvents[e].startTime >= instance.loopStart - 0.001f) {
                            instance.globalStarted[e] = 0;
                            instance.globalEnded[e] = 0;
                        }
                    }
                } else {
                    finishInstance(instance, entities, player, map, false);
                    instances.erase(instances.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
            }

            for (size_t a = 0; a < instance.data->actors.size(); ++a) {
                const SceneActor_L& sceneActor = instance.data->actors[a];
                for (size_t e = 0; e < sceneActor.events.size(); ++e) {
                    const SceneEvent_L& event = sceneActor.events[e];
                    if (!instance.actorStarted[a][e] && event.startTime <= instance.currentTime) {
                        instance.actorStarted[a][e] = 1;
                        startEvent(instance, static_cast<int>(a), e, entities, player, map);
                    }
                    if (instance.actorStarted[a][e] && !instance.actorEnded[a][e] && event.endTime >= 0.0f && event.endTime <= instance.currentTime) {
                        instance.actorEnded[a][e] = 1;
                        endEvent(instance, static_cast<int>(a), e, entities, player, map);
                    }
                }
            }
            for (size_t e = 0; e < instance.data->globalEvents.size(); ++e) {
                const SceneEvent_L& event = instance.data->globalEvents[e];
                if (!instance.globalStarted[e] && event.startTime <= instance.currentTime) {
                    instance.globalStarted[e] = 1;
                    startEvent(instance, -1, e, entities, player, map);
                }
                if (instance.globalStarted[e] && !instance.globalEnded[e] && event.endTime >= 0.0f && event.endTime <= instance.currentTime) {
                    instance.globalEnded[e] = 1;
                    endEvent(instance, -1, e, entities, player, map);
                }
            }
            for (size_t a = 0; a < instance.data->actors.size(); ++a) {
                if (a >= instance.actorEntities.size()) break;
                NPC_L* npc = dynamic_cast<NPC_L*>(instance.actorEntities[a]);
                if (!npc || !npc->sceneControlActive()) continue;
                const SceneActor_L& sceneActor = instance.data->actors[a];
                for (size_t e = 0; e < sceneActor.events.size(); ++e) {
                    if (!instance.actorStarted[a][e] || instance.actorEnded[a][e]) continue;
                    const SceneEvent_L& event = sceneActor.events[e];
                    if (event.type != SCENE_EVENT_LOOKAT && event.type != SCENE_EVENT_FACE && event.type != SCENE_EVENT_MOVETO) continue;
                    const std::string value = !event.params.empty() ? event.params : event.name;
                    auto applyTarget = [&](const Vec3& point) {
                        if (event.type == SCENE_EVENT_MOVETO) {
                            const float stopDistance = event.distance > 0.0f ? std::clamp(event.distance, 0.0f, 512.0f) : 16.0f;
                            const float targetSpeed = lowerText(event.params2).find("run") != std::string::npos ? 220.0f : 110.0f;
                            npc->sceneSetMoveTarget(point, targetSpeed, stopDistance);
                        } else if (event.type == SCENE_EVENT_FACE) {
                            npc->sceneSetFaceTarget(point);
                        } else {
                            npc->sceneSetLookTarget(point);
                        }
                    };
                    if (isPlayerName(value)) applyTarget(player.pos + (event.type == SCENE_EVENT_MOVETO ? Vec3{0, 0, 0} : Vec3{0, 48, 0}));
                    else if (Entity* target = findNamedEntity(entities, owner, value)) applyTarget(target->pos);
                    else {
                        Vec3 point{};
                        std::istringstream stream(value);
                        if (stream >> point.x >> point.y >> point.z) applyTarget(point);
                    }
                }
            }
        }
        ++i;
    }
}
