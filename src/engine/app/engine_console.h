#pragma once
#include <GLFW/glfw3.h>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct ConsoleLineEntry {
    std::string text;
    bool error = false;
};

extern std::deque<std::string> consoleQueue;
extern std::function<bool(const std::string&)> consolePassthrough;
extern std::string networkMapName;
extern std::string consoleJoinMap;
extern bool consoleJoinArmed;
extern std::mutex consoleMutex;
extern std::mutex consoleLogMutex;
extern std::deque<ConsoleLineEntry> consoleLines;
extern std::vector<std::string> consoleHistory;
extern std::string consoleInput;
extern int consoleHistoryIndex;
extern bool consoleOpen;
extern double consoleInputLockUntil;
extern bool inGameplay;
extern bool pauseMenuRequest;
extern bool pauseMenuActive;
extern std::unordered_map<int, std::string> consoleBinds;
extern std::unordered_map<int, bool> consoleBindStates;

int ConsoleParseBindKey(std::string keyName);
void ConsolePushLine(const std::string& text, bool forceError = false);
void InstallGameConsoleOutputCapture();
bool GameConsole_IsOpen();
std::vector<std::pair<std::string, bool>> GameConsole_GetLines();
std::string GameConsole_GetInput();
void GameConsole_SetInput(const std::string& value);
std::vector<std::string> GameConsole_GetSuggestions();
bool GameConsole_TakeMapCommand(std::string& outMap);
bool GameConsole_PollPassthrough();
bool GameConsole_TakeJoinMapRequest(std::string& outMap);
void GameConsole_HandleMenuToggle(GLFWwindow* window);
void GameConsoleProcessBinds(GLFWwindow* window);
void GameConsoleApplyOpenState(GLFWwindow* window);
void GameConsoleKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void GameConsoleCharCallback(GLFWwindow* window, unsigned int codepoint);
std::string TrimCommand(std::string s);
std::string LowerCommand(std::string s);
