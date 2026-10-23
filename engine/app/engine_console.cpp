#include <windows.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cctype>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <streambuf>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "engine_console.h"
#include "menu.h"
#include "multiplayer.h"

std::deque<std::string> consoleQueue;
std::function<bool(const std::string&)> consolePassthrough;
std::string networkMapName;
std::string consoleJoinMap;
bool consoleJoinArmed = false;
std::mutex consoleMutex;
std::mutex consoleLogMutex;
std::deque<ConsoleLineEntry> consoleLines;
std::vector<std::string> consoleHistory;
std::string consoleInput;
int consoleHistoryIndex = -1;
bool consoleOpen = false;
double consoleInputLockUntil = 0.0;
bool inGameplay = false;
bool pauseMenuRequest = false;
bool pauseMenuActive = false;
std::unordered_map<int, std::string> consoleBinds;
std::unordered_map<int, bool> consoleBindStates;

static bool ConsoleLooksLikeError(const std::string& text) {
    std::string s = text;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.find("error") != std::string::npos ||
           s.find("failed") != std::string::npos ||
           s.find("missing") != std::string::npos ||
           s.find("cannot") != std::string::npos ||
           s.find("not found") != std::string::npos ||
           s.find("overflow") != std::string::npos;
}

void ConsolePushLine(const std::string& text, bool forceError) {
    std::string value = text;
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    if (value.empty()) return;
    std::lock_guard<std::mutex> lock(consoleLogMutex);
    consoleLines.push_back({value, forceError || ConsoleLooksLikeError(value)});
    while (consoleLines.size() > 800) consoleLines.pop_front();
}

class ConsoleCaptureBuf final : public std::streambuf {
public:
    explicit ConsoleCaptureBuf(bool isError) : errorStream(isError) {}
    ~ConsoleCaptureBuf() override = default;

protected:
    int_type overflow(int_type ch) override {
        if (ch != traits_type::eof()) append(static_cast<char>(ch));
        return ch;
    }

    std::streamsize xsputn(const char* str, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i) append(str[i]);
        return count;
    }

    int sync() override {
        flushPending();
        return 0;
    }

private:
    bool errorStream;
    std::string pending;

    void append(char c) {
        if (c == '\n') flushPending();
        else if (c != '\r') pending.push_back(c);
    }

    void flushPending() {
        if (!pending.empty()) {
            ConsolePushLine(pending, errorStream);
            pending.clear();
        }
    }
};

static ConsoleCaptureBuf gConsoleOutBuf(false);
static ConsoleCaptureBuf gConsoleErrBuf(true);
static std::streambuf* gOriginalCout = nullptr;
static std::streambuf* gOriginalCerr = nullptr;

void InstallGameConsoleOutputCapture() {
    gOriginalCout = std::cout.rdbuf(&gConsoleOutBuf);
    gOriginalCerr = std::cerr.rdbuf(&gConsoleErrBuf);
}

bool GameConsole_IsOpen() {
    return consoleOpen;
}

std::vector<std::pair<std::string, bool>> GameConsole_GetLines() {
    std::lock_guard<std::mutex> lock(consoleLogMutex);
    std::vector<std::pair<std::string, bool>> result;
    result.reserve(consoleLines.size());
    for (const ConsoleLineEntry& entry : consoleLines)
        result.emplace_back(entry.text, entry.error);
    return result;
}

std::string GameConsole_GetInput() {
    return consoleInput;
}

void GameConsole_SetInput(const std::string& value) {
    consoleInput = value.substr(0, 512);
    consoleHistoryIndex = -1;
}

static std::vector<std::string> GameConsoleCommandNames() {
    return {
        "noclip",
        "impulse 101",
        "give",
        "npc_create",
        "npc_spawn",
        "npc_list",
        "ent_create",
        "connect",
        "disconnect",
        "host",
        "map",
        "changelevel",
        "status",
        "bind",
        "unbind",
        "sauce_use_fixed_weaponspreads"
    };
}

std::vector<std::string> GameConsole_GetSuggestions() {
    std::string typed = consoleInput;
    size_t first = typed.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    typed = typed.substr(first);
    size_t split = typed.find_first_of(" \t");
    std::string token = split == std::string::npos ? typed : typed.substr(0, split);
    std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (token.empty()) return {};
    const std::vector<std::string> commands = GameConsoleCommandNames();
    std::vector<std::string> result;
    for (const std::string& command : commands) {
        std::string low = command;
        std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (low.rfind(token, 0) == 0 || low.find(token) != std::string::npos) result.push_back(command);
    }
    std::sort(result.begin(), result.end(), [&](const std::string& a, const std::string& b) {
        std::string al = a, bl = b;
        std::transform(al.begin(), al.end(), al.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(bl.begin(), bl.end(), bl.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool ap = al.rfind(token, 0) == 0;
        const bool bp = bl.rfind(token, 0) == 0;
        if (ap != bp) return ap > bp;
        return al < bl;
    });
    if (result.size() > 7) result.resize(7);
    return result;
}

static std::string ConsoleKeyNameLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int ConsoleParseBindKey(std::string keyName) {
    keyName = ConsoleKeyNameLower(keyName);
    if (keyName.size() == 1) {
        const char c = keyName[0];
        if (c >= 'a' && c <= 'z') return GLFW_KEY_A + (c - 'a');
        if (c >= '0' && c <= '9') return GLFW_KEY_0 + (c - '0');
    }
    static const std::unordered_map<std::string, int> table = {
        {"space", GLFW_KEY_SPACE}, {"enter", GLFW_KEY_ENTER}, {"kp_enter", GLFW_KEY_KP_ENTER}, {"tab", GLFW_KEY_TAB},
        {"backspace", GLFW_KEY_BACKSPACE}, {"delete", GLFW_KEY_DELETE}, {"insert", GLFW_KEY_INSERT}, {"home", GLFW_KEY_HOME},
        {"end", GLFW_KEY_END}, {"pageup", GLFW_KEY_PAGE_UP}, {"pagedown", GLFW_KEY_PAGE_DOWN}, {"up", GLFW_KEY_UP},
        {"down", GLFW_KEY_DOWN}, {"left", GLFW_KEY_LEFT}, {"right", GLFW_KEY_RIGHT}, {"escape", GLFW_KEY_ESCAPE},
        {"esc", GLFW_KEY_ESCAPE}, {"minus", GLFW_KEY_MINUS}, {"equal", GLFW_KEY_EQUAL}, {"comma", GLFW_KEY_COMMA},
        {"period", GLFW_KEY_PERIOD}, {"slash", GLFW_KEY_SLASH}, {"semicolon", GLFW_KEY_SEMICOLON}, {"apostrophe", GLFW_KEY_APOSTROPHE},
        {"leftbracket", GLFW_KEY_LEFT_BRACKET}, {"backslash", GLFW_KEY_BACKSLASH}, {"rightbracket", GLFW_KEY_RIGHT_BRACKET},
        {"grave", GLFW_KEY_GRAVE_ACCENT}, {"grave_accent", GLFW_KEY_GRAVE_ACCENT}, {"left_shift", GLFW_KEY_LEFT_SHIFT},
        {"right_shift", GLFW_KEY_RIGHT_SHIFT}, {"shift", GLFW_KEY_LEFT_SHIFT}, {"left_ctrl", GLFW_KEY_LEFT_CONTROL},
        {"right_ctrl", GLFW_KEY_RIGHT_CONTROL}, {"ctrl", GLFW_KEY_LEFT_CONTROL}, {"left_alt", GLFW_KEY_LEFT_ALT},
        {"right_alt", GLFW_KEY_RIGHT_ALT}, {"alt", GLFW_KEY_LEFT_ALT}, {"left_super", GLFW_KEY_LEFT_SUPER},
        {"right_super", GLFW_KEY_RIGHT_SUPER}, {"capslock", GLFW_KEY_CAPS_LOCK}, {"numlock", GLFW_KEY_NUM_LOCK},
        {"scrolllock", GLFW_KEY_SCROLL_LOCK}, {"printscreen", GLFW_KEY_PRINT_SCREEN}, {"pause", GLFW_KEY_PAUSE},
        {"kp_0", GLFW_KEY_KP_0}, {"kp_1", GLFW_KEY_KP_1}, {"kp_2", GLFW_KEY_KP_2}, {"kp_3", GLFW_KEY_KP_3},
        {"kp_4", GLFW_KEY_KP_4}, {"kp_5", GLFW_KEY_KP_5}, {"kp_6", GLFW_KEY_KP_6}, {"kp_7", GLFW_KEY_KP_7},
        {"kp_8", GLFW_KEY_KP_8}, {"kp_9", GLFW_KEY_KP_9}, {"kp_decimal", GLFW_KEY_KP_DECIMAL}, {"kp_divide", GLFW_KEY_KP_DIVIDE},
        {"kp_multiply", GLFW_KEY_KP_MULTIPLY}, {"kp_subtract", GLFW_KEY_KP_SUBTRACT}, {"kp_add", GLFW_KEY_KP_ADD},
        {"f1", GLFW_KEY_F1}, {"f2", GLFW_KEY_F2}, {"f3", GLFW_KEY_F3}, {"f4", GLFW_KEY_F4}, {"f5", GLFW_KEY_F5},
        {"f6", GLFW_KEY_F6}, {"f7", GLFW_KEY_F7}, {"f8", GLFW_KEY_F8}, {"f9", GLFW_KEY_F9}, {"f10", GLFW_KEY_F10},
        {"f11", GLFW_KEY_F11}, {"f12", GLFW_KEY_F12}, {"f13", GLFW_KEY_F13}, {"f14", GLFW_KEY_F14}, {"f15", GLFW_KEY_F15},
        {"f16", GLFW_KEY_F16}, {"f17", GLFW_KEY_F17}, {"f18", GLFW_KEY_F18}, {"f19", GLFW_KEY_F19}, {"f20", GLFW_KEY_F20},
        {"f21", GLFW_KEY_F21}, {"f22", GLFW_KEY_F22}, {"f23", GLFW_KEY_F23}, {"f24", GLFW_KEY_F24}, {"f25", GLFW_KEY_F25}
    };
    auto it = table.find(keyName);
    return it == table.end() ? GLFW_KEY_UNKNOWN : it->second;
}

static void GameConsoleQueueCommand(const std::string& command) {
    if (command.empty()) return;
    std::lock_guard<std::mutex> lock(consoleMutex);
    consoleQueue.push_back(command);
}

void GameConsoleProcessBinds(GLFWwindow* window) {
    if (!window || consoleOpen || consoleBinds.empty()) return;
    for (const auto& kv : consoleBinds) {
        const int key = kv.first;
        const bool down = glfwGetKey(window, key) == GLFW_PRESS;
        const bool wasDown = consoleBindStates[key];
        if (down && !wasDown) GameConsoleQueueCommand(kv.second);
        consoleBindStates[key] = down;
    }
}

static void GameConsoleSubmitInput() {
    std::string line = consoleInput;
    size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos) {
        size_t last = line.find_last_not_of(" \t");
        line = line.substr(first, last - first + 1);
    } else {
        line.clear();
    }
    if (line.empty()) return;

    ConsolePushLine("> " + line);
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        consoleQueue.push_back(line);
    }
    if (consoleHistory.empty() || consoleHistory.back() != line)
        consoleHistory.push_back(line);
    if (consoleHistory.size() > 128) consoleHistory.erase(consoleHistory.begin());
    consoleHistoryIndex = -1;
    consoleInput.clear();
}

bool GameConsole_TakeMapCommand(std::string& outMap) {
    outMap.clear();

    std::lock_guard<std::mutex> lock(consoleMutex);
    if (consoleQueue.empty()) return false;

    std::deque<std::string> remaining;
    bool found = false;

    while (!consoleQueue.empty()) {
        std::string cmd = consoleQueue.front();
        consoleQueue.pop_front();

        std::string trimmed = cmd;
        size_t first = trimmed.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        size_t last = trimmed.find_last_not_of(" \t\r\n");
        trimmed = trimmed.substr(first, last - first + 1);

        size_t split = trimmed.find_first_of(" \t");
        std::string verb = (split == std::string::npos) ? trimmed : trimmed.substr(0, split);
        std::transform(verb.begin(), verb.end(), verb.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (!found && verb == "map" && split != std::string::npos) {
            size_t argStart = trimmed.find_first_not_of(" \t", split);
            if (argStart != std::string::npos) {
                size_t argEnd = trimmed.find_last_not_of(" \t");
                outMap = trimmed.substr(argStart, argEnd - argStart + 1);
                if (!outMap.empty()) {
                    found = true;
                    continue;
                }
            }
        }

        remaining.push_back(trimmed);
    }

    consoleQueue.swap(remaining);

    if (found) {
                                                                                
                                                                              
        consoleOpen = false;
        consoleInput.clear();
        consoleHistoryIndex = -1;
        consoleInputLockUntil = 0.0;
    }
    return found;
}

std::string TrimCommand(std::string s);
std::string LowerCommand(std::string s);

bool GameConsole_PollPassthrough() {
    if (consoleJoinArmed && consoleJoinMap.empty() && Multiplayer_L::connected() && !Multiplayer_L::isHost()) {
        const std::string hostMap = Multiplayer_L::remoteState().mapName;
        if (!hostMap.empty()) {
            consoleJoinMap = hostMap;
            consoleJoinArmed = false;
        }
    }
    std::string command;
    {
        std::lock_guard<std::mutex> lock(consoleMutex);
        if (consoleQueue.empty()) return false;
        command = consoleQueue.front();
        consoleQueue.pop_front();
    }
    std::string trimmed = TrimCommand(command);
    std::string lowered = LowerCommand(trimmed);
    if (lowered == "map" || lowered.rfind("map ", 0) == 0 ||
        lowered == "changelevel" || lowered.rfind("changelevel ", 0) == 0) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        consoleQueue.push_front(command);
        return false;
    }
    if (!consolePassthrough || !consolePassthrough(trimmed)) {
        std::lock_guard<std::mutex> lock(consoleMutex);
        consoleQueue.push_front(command);
        return false;
    }
    return true;
}

bool GameConsole_TakeJoinMapRequest(std::string& outMap) {
    outMap.clear();
    std::lock_guard<std::mutex> lock(consoleMutex);
    if (consoleJoinMap.empty()) return false;
    outMap = consoleJoinMap;
    consoleJoinMap.clear();
    return true;
}

void GameConsoleApplyOpenState(GLFWwindow* window) {
    if (!window) return;
    if (consoleOpen) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else if (pauseMenuActive || !inGameplay) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

void GameConsole_HandleMenuToggle(GLFWwindow* window) {
    GameConsoleApplyOpenState(window);
}

static void GameConsoleSetClipboard(const std::string& text) {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return;
    if (!EmptyClipboard()) { CloseClipboard(); return; }
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (wideLen <= 0) { CloseClipboard(); return; }
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (wideLen + 1) * sizeof(wchar_t));
    if (!mem) { CloseClipboard(); return; }
    wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem));
    if (!dst) { GlobalFree(mem); CloseClipboard(); return; }
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), dst, wideLen);
    dst[wideLen] = L'\0';
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
#endif
}

static std::string GameConsoleGetClipboard() {
#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return {};
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return {}; }
    const wchar_t* src = static_cast<const wchar_t*>(GlobalLock(h));
    if (!src) { CloseClipboard(); return {}; }
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1) { GlobalUnlock(h); CloseClipboard(); return {}; }
    std::string out((size_t)utf8Len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, src, -1, out.data(), utf8Len, nullptr, nullptr);
    out.resize((size_t)utf8Len - 1);
    GlobalUnlock(h);
    CloseClipboard();
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    std::replace(out.begin(), out.end(), '\n', ' ');
    return out;
#else
    return {};
#endif
}

void GameConsoleKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    (void)scancode;
    if (action != GLFW_PRESS && action != GLFW_REPEAT) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (consoleOpen) {
                                                                                 
                                                                         
            consoleOpen = false;
            GameConsoleApplyOpenState(window);
        } else if (inGameplay) {
            pauseMenuRequest = true;
        }
        return;
    }

    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS) {
        if (consoleOpen) {
                                                                                   
                                                                                 
            consoleOpen = false;
            GameConsoleApplyOpenState(window);
        } else if (inGameplay) {
                                                                                
                                                              
            consoleOpen = true;
            consoleInput.clear();
            consoleHistoryIndex = -1;
            consoleInputLockUntil = glfwGetTime() + 0.20;
            pauseMenuRequest = true;
            GameConsoleApplyOpenState(window);
        } else if (pauseMenuActive || !inGameplay) {
                                                                              
                                                                                
            consoleOpen = true;
            consoleInput.clear();
            consoleHistoryIndex = -1;
            consoleInputLockUntil = glfwGetTime() + 0.20;
            GameConsoleApplyOpenState(window);
        }
        return;
    }

    if (!consoleOpen) return;

    const bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
    if (ctrl && key == GLFW_KEY_V && action == GLFW_PRESS) {
        std::string pasted = GameConsoleGetClipboard();
        if (!pasted.empty() && consoleInput.size() < 512) {
            const size_t room = 512 - consoleInput.size();
            consoleInput += pasted.substr(0, room);
        }
        return;
    }
    if (ctrl && key == GLFW_KEY_C && action == GLFW_PRESS) {
        std::string text = consoleInput;
        if (text.empty()) {
            std::lock_guard<std::mutex> lock(consoleLogMutex);
            if (!consoleLines.empty()) text = consoleLines.back().text;
        }
        if (!text.empty()) GameConsoleSetClipboard(text);
        return;
    }
    if (glfwGetTime() < consoleInputLockUntil) return;

    if (key == GLFW_KEY_BACKSPACE) {
        if (!consoleInput.empty()) consoleInput.pop_back();
        return;
    }
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        GameConsoleSubmitInput();
        return;
    }
    if (key == GLFW_KEY_UP) {
        if (!consoleHistory.empty()) {
            if (consoleHistoryIndex < 0) consoleHistoryIndex = static_cast<int>(consoleHistory.size()) - 1;
            else consoleHistoryIndex = std::max(0, consoleHistoryIndex - 1);
            consoleInput = consoleHistory[static_cast<size_t>(consoleHistoryIndex)];
        }
        return;
    }
    if (key == GLFW_KEY_DOWN) {
        if (!consoleHistory.empty() && consoleHistoryIndex >= 0) {
            if (consoleHistoryIndex + 1 < static_cast<int>(consoleHistory.size())) {
                ++consoleHistoryIndex;
                consoleInput = consoleHistory[static_cast<size_t>(consoleHistoryIndex)];
            } else {
                consoleHistoryIndex = -1;
                consoleInput.clear();
            }
        }
    }
}

void GameConsoleCharCallback(GLFWwindow* window, unsigned int codepoint) {
    if (consoleOpen) {
        if (glfwGetTime() < consoleInputLockUntil) return;
        if (codepoint >= 32 && codepoint != 127 && consoleInput.size() < 512) {
            if (codepoint <= 0x7F) consoleInput.push_back(static_cast<char>(codepoint));
            else if (codepoint <= 0x7FF) {
                consoleInput.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
                consoleInput.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint <= 0xFFFF) {
                consoleInput.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
                consoleInput.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                consoleInput.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            } else if (codepoint <= 0x10FFFF) {
                consoleInput.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
                consoleInput.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
                consoleInput.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
                consoleInput.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
            }
        }
        return;
    }
    Menu_L* menu = static_cast<Menu_L*>(glfwGetWindowUserPointer(window));
    if (menu) menu->handleCharInput(codepoint);
}


std::string TrimCommand(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
    size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

std::string LowerCommand(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
