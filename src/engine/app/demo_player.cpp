#include "demo_player.h"
#include "fs_compat.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace {
constexpr size_t DemoHeaderSize = 1072;
constexpr size_t DemoCommandInfoSize = 76;
constexpr uint32_t DemoButtonAttack = 0x0001;
constexpr uint32_t DemoButtonJump = 0x0002;
constexpr uint32_t DemoButtonDuck = 0x0004;
constexpr uint32_t DemoButtonUse = 0x0020;
constexpr uint32_t DemoButtonAttack2 = 0x0800;
constexpr uint32_t DemoButtonReload = 0x2000;

struct DemoCameraSample {
    int32_t tick = 0;
    Vec3 origin{0, 0, 0};
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    bool noInterpolation = false;
};

struct DemoInputSample {
    int32_t tick = 0;
    uint32_t buttons = 0;
    uint8_t impulse = 0;
    int weaponSelect = 0;
    bool impulsePressed = false;
};

struct DemoUserCommand {
    uint32_t commandNumber = 0;
    uint32_t tickCount = 0;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    int forwardMove = 0;
    int sideMove = 0;
    int upMove = 0;
    uint32_t buttons = 0;
    uint8_t impulse = 0;
    int weaponSelect = 0;
    int weaponSubtype = 0;
    int mouseDx = 0;
    int mouseDy = 0;
};

struct DemoState {
    std::string path;
    std::string map;
    std::string game;
    int protocol = 0;
    int networkProtocol = 0;
    float duration = 0.0f;
    int playbackTicks = 0;
    std::vector<DemoCameraSample> cameras;
    std::vector<DemoInputSample> inputs;
    size_t cameraIndex = 0;
    size_t inputIndex = 0;
    double elapsed = 0.0;
    double tickInterval = 1.0 / 66.0;
    int32_t firstTick = 0;
    int32_t lastTick = 0;
    bool ready = false;
    bool playing = false;
    bool firstUpdate = true;
    bool finalFrameShown = false;
    bool hasInputState = false;
    bool hasLastPosition = false;
    Vec3 lastPosition{0, 0, 0};
};

DemoState g_demo;

class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : bytes(data), byteCount(size) {}

    bool readBit(bool& value) {
        uint32_t bit = 0;
        if (!readBits(1, bit)) return false;
        value = bit != 0;
        return true;
    }

    bool readBits(unsigned count, uint32_t& value) {
        if (count > 32 || bitOffset + count > byteCount * 8) return false;
        value = 0;
        for (unsigned i = 0; i < count; ++i) {
            const size_t absolute = bitOffset + i;
            const uint32_t bit = (bytes[absolute >> 3] >> (absolute & 7)) & 1u;
            value |= bit << i;
        }
        bitOffset += count;
        return true;
    }

    bool readSigned(unsigned count, int& value) {
        uint32_t raw = 0;
        if (!readBits(count, raw) || count == 0 || count > 32) return false;
        if (count == 32) {
            value = static_cast<int32_t>(raw);
            return true;
        }
        const uint32_t sign = 1u << (count - 1);
        value = static_cast<int>((raw ^ sign) - sign);
        return true;
    }

    bool readAngle(unsigned count, float& value) {
        uint32_t raw = 0;
        if (!readBits(count, raw)) return false;
        const double scale = 360.0 / static_cast<double>(uint64_t(1) << count);
        value = static_cast<float>(static_cast<double>(raw) * scale);
        if (value > 180.0f) value -= 360.0f;
        return true;
    }

private:
    const uint8_t* bytes = nullptr;
    size_t byteCount = 0;
    size_t bitOffset = 0;
};

uint32_t readU32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

int32_t readI32(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<int32_t>(readU32(data, offset));
}

float readF32(const std::vector<uint8_t>& data, size_t offset) {
    const uint32_t value = readU32(data, offset);
    float result = 0.0f;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::string readFixedString(const std::vector<uint8_t>& data, size_t offset, size_t length) {
    size_t end = offset;
    const size_t limit = offset + length;
    while (end < limit && data[end] != 0) ++end;
    std::string result(data.begin() + static_cast<std::ptrdiff_t>(offset),
                       data.begin() + static_cast<std::ptrdiff_t>(end));
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t' ||
                               result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}

bool validVector(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::abs(x) < 10000000.0f && std::abs(y) < 10000000.0f &&
           std::abs(z) < 10000000.0f;
}

bool validAngles(float pitch, float yaw, float roll) {
    return std::isfinite(pitch) && std::isfinite(yaw) && std::isfinite(roll) &&
           std::abs(pitch) < 3600.0f && std::abs(yaw) < 3600.0f &&
           std::abs(roll) < 3600.0f;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(std::string value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = value.find_last_not_of(" \t\r\n");
    value = value.substr(first, last - first + 1);
    if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\'')))
        value = value.substr(1, value.size() - 2);
    return value;
}

void addPath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) return;
    const auto normalized = path.lexically_normal();
    for (const auto& current : paths) {
        if (current == normalized) return;
    }
    paths.push_back(normalized);
}

std::filesystem::path resolveDemoPath(const std::string& name, const std::string& gameDirectory) {
    std::string cleanName = trim(name);
    if (cleanName.empty()) return {};
    std::filesystem::path requested = FsCompat::fromUtf8(cleanName);
    std::vector<std::filesystem::path> variants;
    addPath(variants, requested);
    if (requested.extension().empty()) {
        addPath(variants, std::filesystem::path(cleanName + ".dem"));
    }

    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path gamePath = FsCompat::fromUtf8(gameDirectory);
    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    if (!ec) {
        for (const auto& variant : variants) {
            addPath(candidates, cwd / variant);
            addPath(candidates, cwd / "demos" / variant.filename());
        }
    }

    if (!gamePath.empty()) {
        for (const auto& variant : variants) {
            addPath(candidates, gamePath / variant);
            addPath(candidates, gamePath / "demos" / variant.filename());
        }
        const auto parent = gamePath.parent_path();
        for (const auto& variant : variants)
            addPath(candidates, parent / "demos" / variant.filename());
    }

    for (const auto& candidate : candidates) {
        if (FsCompat::isRegularFile(candidate)) return candidate;
    }
    return {};
}

bool decodeUserCommand(const uint8_t* data, size_t size, DemoUserCommand& current,
                       const DemoUserCommand& previous) {
    BitReader bits(data, size);
    current = previous;
    bool changed = false;
    uint32_t raw = 0;
    int signedValue = 0;

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readBits(32, raw)) return false;
        current.commandNumber = raw;
    } else {
        current.commandNumber = previous.commandNumber + 1;
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readBits(32, raw)) return false;
        current.tickCount = raw;
    } else {
        current.tickCount = previous.tickCount + 1;
    }

    if (!bits.readBit(changed)) return false;
    if (changed && !bits.readAngle(16, current.pitch)) return false;
    if (!bits.readBit(changed)) return false;
    if (changed && !bits.readAngle(16, current.yaw)) return false;
    if (!bits.readBit(changed)) return false;
    if (changed && !bits.readAngle(8, current.roll)) return false;

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readSigned(16, signedValue)) return false;
        current.forwardMove = signedValue;
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readSigned(16, signedValue)) return false;
        current.sideMove = signedValue;
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readSigned(16, signedValue)) return false;
        current.upMove = signedValue;
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readBits(32, raw)) return false;
        current.buttons = raw;
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readBits(8, raw)) return false;
        current.impulse = static_cast<uint8_t>(raw);
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readBits(11, raw)) return false;
        current.weaponSelect = static_cast<int>(raw);
        if (!bits.readBit(changed)) return false;
        if (changed) {
            if (!bits.readBits(6, raw)) return false;
            current.weaponSubtype = static_cast<int>(raw);
        }
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readSigned(16, signedValue)) return false;
        current.mouseDx = signedValue;
    }

    if (!bits.readBit(changed)) return false;
    if (changed) {
        if (!bits.readSigned(16, signedValue)) return false;
        current.mouseDy = signedValue;
    }

    return true;
}

struct PacketInfo {
    size_t infoCount = 1;
    size_t payloadOffset = 0;
    size_t payloadLength = 0;
    int32_t sequenceIn = 0;
    int32_t sequenceOut = 0;
};

bool parsePacketInfo(const std::vector<uint8_t>& data, size_t start, size_t end,
                     int protocol, int command, int signonLength, int preferredCount,
                     PacketInfo& result) {
    std::vector<int> counts;
    if (protocol < 4) {
        counts.push_back(1);
    } else {
        if (preferredCount == 4) counts = {4, 2, 1};
        else if (preferredCount == 2) counts = {2, 4, 1};
        else counts = {1, 2, 4};
    }

    bool found = false;
    int bestScore = std::numeric_limits<int>::min();

    for (int count : counts) {
        const size_t infoBytes = DemoCommandInfoSize * static_cast<size_t>(count);
        if (start + infoBytes + 12 > end) continue;

        const size_t sequenceOffset = start + infoBytes;
        const int32_t sequenceIn = readI32(data, sequenceOffset);
        const int32_t sequenceOut = readI32(data, sequenceOffset + 4);
        const int32_t storedLength = readI32(data, sequenceOffset + 8);
        if (storedLength < 0) continue;

        size_t payloadLength = static_cast<size_t>(storedLength);
        if (command == 1 && storedLength == 0) {
            if (signonLength < 0) continue;
            payloadLength = static_cast<size_t>(signonLength);
        }

        const size_t payloadOffset = sequenceOffset + 12;
        if (payloadLength > end - payloadOffset) continue;
        if (payloadLength > 268435456u) continue;

        const uint32_t flags = readU32(data, start);
        const float x = readF32(data, start + 4);
        const float y = readF32(data, start + 8);
        const float z = readF32(data, start + 12);
        const float pitch = readF32(data, start + 16);
        const float yaw = readF32(data, start + 20);
        const float roll = readF32(data, start + 24);

        int score = 0;
        if (count == preferredCount) score += 20;
        if (flags <= 7) score += 8;
        if (validVector(x, y, z)) score += 8;
        if (validAngles(pitch, yaw, roll)) score += 8;
        if (storedLength > 0) score += 4;
        if (sequenceIn >= 0 && sequenceOut >= 0) score += 1;
        if (count == 1 && protocol >= 4) score -= 1;

        if (!found || score > bestScore) {
            found = true;
            bestScore = score;
            result.infoCount = static_cast<size_t>(count);
            result.payloadOffset = payloadOffset;
            result.payloadLength = payloadLength;
            result.sequenceIn = sequenceIn;
            result.sequenceOut = sequenceOut;
        }
    }

    return found;
}

DemoCameraSample readCameraSample(const std::vector<uint8_t>& data, size_t offset,
                                  int32_t tick) {
    DemoCameraSample sample;
    sample.tick = tick;
    const uint32_t flags = readU32(data, offset);
    const size_t originOffset = offset + ((flags & 1u) ? 40 : 4);
    const size_t anglesOffset = offset + ((flags & 2u) ? 52 : 16);
    const float x = readF32(data, originOffset);
    const float y = readF32(data, originOffset + 4);
    const float z = readF32(data, originOffset + 8);
    const float pitch = readF32(data, anglesOffset);
    const float yaw = readF32(data, anglesOffset + 4);
    sample.origin = {x, z, -y};
    sample.pitch = pitch;
    sample.yaw = 90.0f - yaw;
    sample.roll = -readF32(data, anglesOffset + 8);
    sample.noInterpolation = (flags & 4u) != 0;
    return sample;
}

float normalizeAngle(float value) {
    while (value > 180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

float interpolateAngle(float a, float b, float t) {
    const float delta = normalizeAngle(b - a);
    return normalizeAngle(a + delta * t);
}

bool parseDemo(const std::filesystem::path& path, DemoState& state, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        error = "could not open demo file";
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    if (fileSize < static_cast<std::streamoff>(DemoHeaderSize) ||
        fileSize > static_cast<std::streamoff>(1073741824))
    {
        error = "demo file has an invalid size";
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(data.data()), fileSize);
    if (!file || data.size() < DemoHeaderSize) {
        error = "demo file could not be read completely";
        return false;
    }

    if (std::memcmp(data.data(), "HL2DEMO", 7) != 0) {
        error = "unsupported demo signature; expected HL2DEMO";
        return false;
    }

    const int protocol = readI32(data, 8);
    const int networkProtocol = readI32(data, 12);
    if (protocol < 2 || protocol > 4) {
        error = "unsupported Source demo protocol " + std::to_string(protocol);
        return false;
    }

    state.path = FsCompat::toUtf8(path);
    state.protocol = protocol;
    state.networkProtocol = networkProtocol;
    state.game = readFixedString(data, 796, 260);
    state.map = readFixedString(data, 536, 260);
    state.duration = readF32(data, 1056);
    state.playbackTicks = readI32(data, 1060);
    const int signonLength = readI32(data, 1068);

    if (state.map.empty()) {
        error = "demo header does not contain a map name";
        return false;
    }
    if (!std::isfinite(state.duration) || state.duration < 0.0f) state.duration = 0.0f;
    if (signonLength < 0 || static_cast<uint64_t>(signonLength) > data.size()) {
        error = "demo header contains an invalid signon length";
        return false;
    }

    int preferredCount = 1;
    const std::string lowerGame = lower(state.game);
    if (lowerGame.find("left4dead") != std::string::npos || lowerGame == "l4d" ||
        lowerGame == "l4d2") preferredCount = 4;
    else if (lowerGame.find("portal2") != std::string::npos ||
             lowerGame.find("stanley") != std::string::npos)
        preferredCount = 2;

    DemoUserCommand previousCommand;
    bool hasPreviousCommand = false;
    size_t offset = DemoHeaderSize;
    const size_t dataEnd = data.size();

    while (offset + 5 <= dataEnd) {
        const int command = data[offset++];
        const int32_t tick = readI32(data, offset);
        offset += 4;
        int playerSlot = 0;
        if (protocol >= 4) {
            if (offset >= dataEnd) break;
            playerSlot = data[offset++];
        }

        if (command == 1 || command == 2) {
            PacketInfo packet;
            if (!parsePacketInfo(data, offset, dataEnd, protocol, command, signonLength,
                                 preferredCount, packet))
                break;

            const size_t slot = std::min(packet.infoCount - 1, static_cast<size_t>(std::max(0, playerSlot)));
            const size_t infoOffset = offset + slot * DemoCommandInfoSize;
            DemoCameraSample sample = readCameraSample(data, infoOffset, tick);
            const float sourceX = readF32(data, infoOffset + ((readU32(data, infoOffset) & 1u) ? 40 : 4));
            const float sourceY = readF32(data, infoOffset + ((readU32(data, infoOffset) & 1u) ? 44 : 8));
            const float sourceZ = readF32(data, infoOffset + ((readU32(data, infoOffset) & 1u) ? 48 : 12));
            const float sourcePitch = readF32(data, infoOffset + ((readU32(data, infoOffset) & 2u) ? 52 : 16));
            const float sourceYaw = readF32(data, infoOffset + ((readU32(data, infoOffset) & 2u) ? 56 : 20));
            const float sourceRoll = readF32(data, infoOffset + ((readU32(data, infoOffset) & 2u) ? 60 : 24));
            if (validVector(sourceX, sourceY, sourceZ) &&
                validAngles(sourcePitch, sourceYaw, sourceRoll)) {
                if (!state.cameras.empty() && state.cameras.back().tick == sample.tick)
                    state.cameras.back() = sample;
                else
                    state.cameras.push_back(sample);
            }

            offset = packet.payloadOffset + packet.payloadLength;
            continue;
        }

        if (command == 3 || command == 7) {
            if (command == 7) break;
            continue;
        }

        if (command == 4 || command == 6 || command == 8 ||
            (protocol >= 4 && command == 9)) {
            if (offset + 4 > dataEnd) break;
            const int32_t length = readI32(data, offset);
            offset += 4;
            if (length < 0 || static_cast<uint64_t>(length) > dataEnd - offset) break;
            offset += static_cast<size_t>(length);
            continue;
        }

        if (command == 5) {
            if (offset + 8 > dataEnd) break;
            offset += 4;
            const int32_t length = readI32(data, offset);
            offset += 4;
            if (length < 0 || static_cast<uint64_t>(length) > dataEnd - offset) break;
            if (length > 0 && state.inputs.size() < 20000000) {
                DemoUserCommand nextCommand = previousCommand;
                const bool decoded = decodeUserCommand(data.data() + offset,
                                                       static_cast<size_t>(length),
                                                       nextCommand, previousCommand);
                if (decoded) {
                    DemoInputSample sample;
                    sample.tick = tick;
                    sample.buttons = nextCommand.buttons;
                    sample.impulse = nextCommand.impulse;
                    sample.weaponSelect = nextCommand.weaponSelect;
                    sample.impulsePressed = nextCommand.impulse == 100 &&
                        (!hasPreviousCommand || previousCommand.impulse != 100);
                    state.inputs.push_back(sample);
                    previousCommand = nextCommand;
                    hasPreviousCommand = true;
                }
            }
            offset += static_cast<size_t>(length);
            continue;
        }

        break;
    }

    if (state.cameras.empty()) {
        error = "demo contains no usable POV camera samples";
        return false;
    }

    std::stable_sort(state.cameras.begin(), state.cameras.end(),
                     [](const DemoCameraSample& a, const DemoCameraSample& b) {
                         return a.tick < b.tick;
                     });
    std::stable_sort(state.inputs.begin(), state.inputs.end(),
                     [](const DemoInputSample& a, const DemoInputSample& b) {
                         return a.tick < b.tick;
                     });

    std::vector<DemoCameraSample> uniqueCameras;
    uniqueCameras.reserve(state.cameras.size());
    for (const auto& sample : state.cameras) {
        if (!uniqueCameras.empty() && uniqueCameras.back().tick == sample.tick)
            uniqueCameras.back() = sample;
        else
            uniqueCameras.push_back(sample);
    }
    state.cameras.swap(uniqueCameras);

    state.firstTick = state.cameras.front().tick;
    state.lastTick = state.cameras.back().tick;
    const int tickSpan = std::max(1, state.lastTick - state.firstTick);
    if (state.duration <= 0.0001f) {
        const int ticks = state.playbackTicks > 0 ? state.playbackTicks : tickSpan;
        state.duration = static_cast<float>(ticks) / 66.0f;
    }
    state.tickInterval = static_cast<double>(state.duration) / static_cast<double>(tickSpan);
    if (!std::isfinite(state.tickInterval) || state.tickInterval <= 0.000001)
        state.tickInterval = 1.0 / 66.0;

    state.ready = true;
    state.playing = false;
    state.elapsed = 0.0;
    state.cameraIndex = 0;
    state.inputIndex = 0;
    state.firstUpdate = true;
    state.finalFrameShown = false;
    state.hasLastPosition = false;
    return true;
}
}

bool DemoPlayer::prepare(const std::string& name, const std::string& gameDirectory, std::string& error) {
    stop();
    error.clear();
    const auto path = resolveDemoPath(name, gameDirectory);
    if (path.empty()) {
        error = "demo file not found: " + trim(name);
        return false;
    }

    DemoState parsed;
    if (!parseDemo(path, parsed, error)) return false;
    g_demo = std::move(parsed);
    return true;
}

bool DemoPlayer::prepared() {
    return g_demo.ready;
}

bool DemoPlayer::active() {
    return g_demo.playing;
}

const std::string& DemoPlayer::mapName() {
    return g_demo.map;
}

const std::string& DemoPlayer::fileName() {
    return g_demo.path;
}

void DemoPlayer::start() {
    if (!g_demo.ready) return;
    g_demo.playing = true;
    g_demo.elapsed = 0.0;
    g_demo.cameraIndex = 0;
    g_demo.inputIndex = 0;
    g_demo.firstUpdate = true;
    g_demo.finalFrameShown = false;
    g_demo.hasLastPosition = false;
}

bool DemoPlayer::update(float dt, Camera& player, DemoInputState& input) {
    input = DemoInputState{};
    if (!g_demo.playing) return false;
    if (g_demo.finalFrameShown) {
        g_demo.playing = false;
        return false;
    }

    if (g_demo.firstUpdate) {
        g_demo.firstUpdate = false;
    } else {
        g_demo.elapsed += std::max(0.0f, dt);
    }
    if (g_demo.elapsed > g_demo.duration) g_demo.elapsed = g_demo.duration;

    const double targetTick = static_cast<double>(g_demo.firstTick) +
        g_demo.elapsed / g_demo.tickInterval;
    while (g_demo.cameraIndex + 1 < g_demo.cameras.size() &&
           static_cast<double>(g_demo.cameras[g_demo.cameraIndex + 1].tick) <= targetTick)
        ++g_demo.cameraIndex;

    if (!g_demo.inputs.empty() && targetTick >= g_demo.inputs.front().tick) {
        const size_t previousIndex = g_demo.inputIndex;
        while (g_demo.inputIndex + 1 < g_demo.inputs.size() &&
               static_cast<double>(g_demo.inputs[g_demo.inputIndex + 1].tick) <= targetTick)
            ++g_demo.inputIndex;
        const size_t firstChanged = g_demo.hasInputState ? previousIndex + 1 : 0;
        for (size_t i = firstChanged; i <= g_demo.inputIndex; ++i) {
            const uint32_t previousButtons = i == 0 ? 0 : g_demo.inputs[i - 1].buttons;
            input.buttonsPressed |= g_demo.inputs[i].buttons & ~previousButtons;
            input.impulsePressed = input.impulsePressed || g_demo.inputs[i].impulsePressed;
        }
        const DemoInputSample& sample = g_demo.inputs[g_demo.inputIndex];
        input.buttons = sample.buttons;
        input.impulse = sample.impulse;
        input.weaponSelect = sample.weaponSelect;
        input.valid = true;
        g_demo.hasInputState = true;
    }

    const DemoCameraSample& current = g_demo.cameras[g_demo.cameraIndex];
    Vec3 origin = current.origin;
    float pitch = current.pitch;
    float yaw = current.yaw;
    float roll = current.roll;

    if (g_demo.cameraIndex + 1 < g_demo.cameras.size()) {
        const DemoCameraSample& next = g_demo.cameras[g_demo.cameraIndex + 1];
        if (!next.noInterpolation && next.tick > current.tick) {
            const float t = static_cast<float>(std::clamp(
                (targetTick - static_cast<double>(current.tick)) /
                static_cast<double>(next.tick - current.tick), 0.0, 1.0));
            origin = current.origin + (next.origin - current.origin) * t;
            pitch = interpolateAngle(current.pitch, next.pitch, t);
            yaw = interpolateAngle(current.yaw, next.yaw, t);
            roll = interpolateAngle(current.roll, next.roll, t);
        }
    }

    player.height = (input.buttons & DemoButtonDuck) != 0 ? 46.0f : 64.0f;
    Vec3 position = origin;
    position.y += player.height;
    if (g_demo.hasLastPosition && dt > 0.0001f)
        player.velocity = (position - g_demo.lastPosition) * (1.0f / dt);
    else
        player.velocity = Vec3{0, 0, 0};
    g_demo.lastPosition = position;
    g_demo.hasLastPosition = true;

    player.pos = position;
    player.yaw = normalizeAngle(yaw);
    player.pitch = std::clamp(pitch, -89.0f, 89.0f);
    player.roll = normalizeAngle(roll);
    player.onGround = true;

    if (g_demo.elapsed >= g_demo.duration)
        g_demo.finalFrameShown = true;

    return true;
}

void DemoPlayer::stop() {
    g_demo = DemoState{};
}
