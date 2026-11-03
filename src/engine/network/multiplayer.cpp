#include "multiplayer.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

namespace {
struct Packet { std::vector<char> data; };

SOCKET g_listen = INVALID_SOCKET;
SOCKET g_sock = INVALID_SOCKET;
bool g_hostMode = false;
bool g_ready = false;
bool g_wsaStarted = false;
bool g_suppress = false;
int g_port = 27015;
int g_nextNetId = 1;
std::string g_localNick = "Player";
std::string g_localModel = "models/player.mdl";
std::string g_localMap;
std::string g_peerAddress;
std::mutex g_mutex;
std::deque<Packet> g_out;
std::vector<char> g_in;
MPPlayerState g_remote;
std::vector<MPPropSpawnEv> g_spawnQueue;
std::vector<MPPropMoveEv> g_moveQueue;
std::vector<MPPropSpawnEv> g_localSpawns;
std::unordered_map<int, int> g_propOwners;
int g_defaultPort = 27015;
bool g_handshakeDone = false;
std::string g_lastError;
std::chrono::steady_clock::time_point g_helloDeadline;

using Clock = std::chrono::steady_clock;

void armHelloDeadline() {
    g_helloDeadline = Clock::now() + std::chrono::seconds(10);
}

bool helloTimedOut() {
    return Clock::now() > g_helloDeadline;
}

bool parsePortDigits(const std::string& digits, int& out) {
    if (digits.empty() || digits.size() > 5) return false;
    for (char c : digits) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    long value = 0;
    for (char c : digits) value = value * 10 + (c - '0');
    if (value < 1 || value > 65535) return false;
    out = (int)value;
    return true;
}

std::string wsaErrorText(int code) {
    switch (code) {
        case 0: return "no error";
        case WSAEACCES: return "access denied (firewall or permissions)";
        case WSAEADDRINUSE: return "address already in use";
        case WSAEADDRNOTAVAIL: return "address not available";
        case WSAEAFNOSUPPORT: return "address family not supported";
        case WSAECONNABORTED: return "connection aborted";
        case WSAECONNREFUSED: return "connection refused (nobody is listening on that port)";
        case WSAECONNRESET: return "connection reset by peer";
        case WSAEDESTADDRREQ: return "destination address required";
        case WSAEFAULT: return "bad address";
        case WSAEHOSTUNREACH: return "host unreachable";
        case WSAEINPROGRESS: return "connect in progress";
        case WSAEINVAL: return "invalid argument";
        case WSAEISCONN: return "socket already connected";
        case WSAEMFILE: return "too many open sockets";
        case WSAEMSGSIZE: return "message too long";
        case WSAENETDOWN: return "network is down";
        case WSAENETUNREACH: return "network unreachable";
        case WSAENOTCONN: return "socket not connected";
        case WSAENOTSOCK: return "socket is not a socket";
        case WSAETIMEDOUT: return "connection timed out";
        case WSAEWOULDBLOCK: return "operation would block";
        default: return "winsock error " + std::to_string(code);
    }
}

void setError(const std::string& stage, const std::string& detail) {
    g_lastError = stage + ": " + detail;
}

void setError(const std::string& stage, const std::string& detail, int code) {
    g_lastError = stage + ": " + detail + " (winsock " + std::to_string(code) + " - " + wsaErrorText(code) + ")";
}

void ensureWSA() {
    if (g_wsaStarted) return;
    WSADATA d{};
    if (WSAStartup(MAKEWORD(2, 2), &d) == 0) g_wsaStarted = true;
}

void putU32(std::vector<char>& b, unsigned int v) {
    b.push_back((char)(v & 0xFF));
    b.push_back((char)((v >> 8) & 0xFF));
    b.push_back((char)((v >> 16) & 0xFF));
    b.push_back((char)((v >> 24) & 0xFF));
}

void putF32(std::vector<char>& b, float v) {
    unsigned int u = 0;
    std::memcpy(&u, &v, sizeof(u));
    putU32(b, u);
}

void putStr(std::vector<char>& b, const std::string& s) {
    putU32(b, (unsigned int)s.size());
    b.insert(b.end(), s.begin(), s.end());
}

bool getU32(const std::vector<char>& b, size_t& off, unsigned int& v) {
    if (off + 4 > b.size()) return false;
    v = (unsigned char)b[off] | ((unsigned char)b[off + 1] << 8) |
        ((unsigned char)b[off + 2] << 16) | ((unsigned char)b[off + 3] << 24);
    off += 4;
    return true;
}

bool getF32(const std::vector<char>& b, size_t& off, float& v) {
    unsigned int u = 0;
    if (!getU32(b, off, u)) return false;
    std::memcpy(&v, &u, sizeof(v));
    return true;
}

bool getStr(const std::vector<char>& b, size_t& off, std::string& s) {
    unsigned int n = 0;
    if (!getU32(b, off, n)) return false;
    if (n > (1u << 20) || off + n > b.size()) return false;
    s.assign(b.begin() + off, b.begin() + off + n);
    off += n;
    return true;
}

void queuePacket(std::vector<char>& payload) {
    std::vector<char> framed;
    framed.reserve(payload.size() + 4);
    putU32(framed, (unsigned int)payload.size());
    framed.insert(framed.end(), payload.begin(), payload.end());
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_out.size() + g_out.size() / 2 >= 512) {
        g_out.clear();
    }
    Packet p;
    p.data.swap(framed);
    g_out.push_back(std::move(p));
}

void sendHello() {
    std::vector<char> pkt;
    pkt.push_back(1);
    putStr(pkt, g_localNick);
    putStr(pkt, g_localModel);
    putStr(pkt, g_localMap);
    queuePacket(pkt);
}

void sendMap() {
    if (g_sock == INVALID_SOCKET) return;
    std::vector<char> pkt;
    pkt.push_back(5);
    putStr(pkt, g_localMap);
    queuePacket(pkt);
}

void disconnectSocket(bool clearQueue);

void flushOut() {
    if (g_sock == INVALID_SOCKET) return;
    for (;;) {
        Packet front;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_out.empty()) return;
            front.data = g_out.front().data;
        }
        int sent = send(g_sock, front.data.data(), (int)front.data.size(), 0);
        if (sent == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) return;
            disconnectSocket(true);
            return;
        }
        if (sent <= 0) return;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_out.empty()) return;
            Packet& p = g_out.front();
            if ((size_t)sent >= p.data.size()) g_out.pop_front();
            else p.data.erase(p.data.begin(), p.data.begin() + sent);
        }
        if ((size_t)sent < front.data.size()) return;
    }
}

void disconnectSocket(bool clearQueue) {
    if (g_sock != INVALID_SOCKET) {
        shutdown(g_sock, SD_BOTH);
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
    }
    g_in.clear();
    g_remote = MPPlayerState();
    g_peerAddress.clear();
    g_handshakeDone = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (clearQueue) g_out.clear();
    }
    if (g_listen == INVALID_SOCKET) g_ready = false;
}

void processBuffer() {
    while (g_in.size() >= 5) {
        unsigned int len = (unsigned char)g_in[0] | ((unsigned char)g_in[1] << 8) |
                           ((unsigned char)g_in[2] << 16) | ((unsigned char)g_in[3] << 24);
        if (len == 0 || len > (1u << 22)) { g_in.clear(); return; }
        if (g_in.size() < (size_t)len + 4) return;
        unsigned char type = (unsigned char)g_in[4];
        std::vector<char> body(g_in.begin() + 5, g_in.begin() + 4 + len);
        g_in.erase(g_in.begin(), g_in.begin() + 4 + len);
        size_t off = 0;
        if (type == 1) {
            std::string nick, model, map;
            if (getStr(body, off, nick) && getStr(body, off, model) && getStr(body, off, map)) {
                g_remote.nick = nick.empty() ? "Player" : nick;
                g_remote.model = model.empty() ? "models/player.mdl" : model;
                g_remote.mapName = map;
                g_remote.valid = true;
                g_handshakeDone = true;
            }
        } else if (type == 2) {
            float x, y, z, yaw, pitch, h;
            unsigned int paused = 0;
            if (getF32(body, off, x) && getF32(body, off, y) && getF32(body, off, z) &&
                getF32(body, off, yaw) && getF32(body, off, pitch) && getF32(body, off, h) &&
                getU32(body, off, paused)) {
                g_remote.pos = {x, y, z};
                g_remote.yaw = yaw;
                g_remote.pitch = pitch;
                g_remote.height = h;
                g_remote.paused = paused != 0;
                g_remote.valid = true;
            }
        } else if (type == 3) {
            unsigned int id = 0, owner = 0;
            float x, y, z, ax, ay, az, scale;
            std::string cls, model;
            if (getU32(body, off, id) && getU32(body, off, owner) && getF32(body, off, x) &&
                getF32(body, off, y) && getF32(body, off, z) && getF32(body, off, ax) &&
                getF32(body, off, ay) && getF32(body, off, az) && getF32(body, off, scale) &&
                getStr(body, off, cls) && getStr(body, off, model)) {
                MPPropSpawnEv ev;
                ev.netId = (int)id;
                ev.owner = (int)owner;
                ev.classname = cls;
                ev.model = model;
                ev.pos = {x, y, z};
                ev.angles = {ax, ay, az};
                ev.scale = scale;
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_propOwners[ev.netId] = ev.owner;
                    g_spawnQueue.push_back(std::move(ev));
                }
            }
        } else if (type == 4) {
            unsigned int id = 0;
            float x, y, z, ax, ay, az;
            if (getU32(body, off, id) && getF32(body, off, x) && getF32(body, off, y) &&
                getF32(body, off, z) && getF32(body, off, ax) && getF32(body, off, ay) &&
                getF32(body, off, az)) {
                MPPropMoveEv ev;
                ev.netId = (int)id;
                ev.pos = {x, y, z};
                ev.angles = {ax, ay, az};
                std::lock_guard<std::mutex> lock(g_mutex);
                g_moveQueue.push_back(std::move(ev));
            }
        } else if (type == 5) {
            std::string map;
            if (getStr(body, off, map)) {
                g_remote.mapName = map;
                g_handshakeDone = true;
            }
        }
    }
}

void recvAll() {
    char buf[8192];
    for (;;) {
        int n = recv(g_sock, buf, sizeof(buf), 0);
        if (n > 0) {
            g_in.insert(g_in.end(), buf, buf + n);
            if (g_in.size() > (1u << 22)) g_in.clear();
        } else if (n == 0) {
            disconnectSocket(true);
            return;
        } else {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) disconnectSocket(true);
            return;
        }
    }
}

bool setNonBlocking(SOCKET s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

void tuneSocket(SOCKET s) {
    BOOL yes = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char*>(&yes), sizeof(yes));
}

bool resolveIPv4(const std::string& host, unsigned short port, sockaddr_in& out) {
    if (host.empty()) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (InetPtonA(AF_INET, host.c_str(), &addr.sin_addr) == 1) {
        out = addr;
        return true;
    }
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) return false;
    bool found = false;
    for (addrinfo* it = result; it; it = it->ai_next) {
        if (!it->ai_addr || it->ai_family != AF_INET) continue;
        addr.sin_addr = reinterpret_cast<const sockaddr_in*>(it->ai_addr)->sin_addr;
        found = true;
        break;
    }
    freeaddrinfo(result);
    if (found) out = addr;
    return found;
}

std::string readRegistryString(HKEY root, const char* path, const char* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExA(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS) return {};
    char buf[256]{};
    DWORD type = 0;
    DWORD size = sizeof(buf);
    std::string out;
    if (RegQueryValueExA(key, value, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) out.assign(buf);
    RegCloseKey(key);
    return out;
}

std::string trySteamPersonaName() {
    const char* names[] = { "steam_api64.dll", "steam_api.dll" };
    for (const char* dllName : names) {
        HMODULE module = GetModuleHandleA(dllName);
        bool loadedHere = false;
        if (!module) {
            module = LoadLibraryA(dllName);
            loadedHere = module != nullptr;
        }
        if (!module) continue;
        using InitFn = bool (*)();
        using FriendsFn = void* (*)();
        using PersonaFn = const char* (*)(void*);
        InitFn init = reinterpret_cast<InitFn>(GetProcAddress(module, "SteamAPI_Init"));
        PersonaFn persona = reinterpret_cast<PersonaFn>(GetProcAddress(module, "SteamAPI_ISteamFriends_GetPersonaName"));
        std::string result;
        if (init && persona && init()) {
            for (int version = 30; version >= 10; --version) {
                char exportName[64]{};
                std::snprintf(exportName, sizeof(exportName), "SteamAPI_SteamFriends_v%03d", version);
                FriendsFn friendsFn = reinterpret_cast<FriendsFn>(GetProcAddress(module, exportName));
                if (!friendsFn) continue;
                void* friends = friendsFn();
                if (!friends) continue;
                const char* value = persona(friends);
                if (value && *value) {
                    result.assign(value);
                    break;
                }
            }
        }
        if (loadedHere && result.empty()) FreeLibrary(module);
        if (!result.empty()) return result;
    }
    return {};
}

std::string bestLocalIPv4() {
    ensureWSA();
    if (!g_wsaStarted) return "127.0.0.1";

    SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe != INVALID_SOCKET) {
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(53);
        InetPtonA(AF_INET, "1.1.1.1", &target.sin_addr);
        if (connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0) {
            sockaddr_in local{};
            int size = sizeof(local);
            if (getsockname(probe, reinterpret_cast<sockaddr*>(&local), &size) == 0) {
                char text[INET_ADDRSTRLEN]{};
                if (InetNtopA(AF_INET, &local.sin_addr, text, sizeof(text))) {
                    closesocket(probe);
                    return text;
                }
            }
        }
        closesocket(probe);
    }

    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            for (addrinfo* it = result; it; it = it->ai_next) {
                if (!it->ai_addr || it->ai_family != AF_INET) continue;
                const sockaddr_in* addr = reinterpret_cast<const sockaddr_in*>(it->ai_addr);
                char text[INET_ADDRSTRLEN]{};
                if (!InetNtopA(AF_INET, &addr->sin_addr, text, sizeof(text))) continue;
                if (std::string(text) != "127.0.0.1") {
                    freeaddrinfo(result);
                    return text;
                }
            }
            freeaddrinfo(result);
        }
    }
    return "127.0.0.1";
}
}

bool Multiplayer_L::parsePort(const std::string& digits, int& out) {
    return parsePortDigits(digits, out);
}

std::string Multiplayer_L::steamNick() {
    std::string value = trySteamPersonaName();
    if (value.empty()) value = readRegistryString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "AutoLoginUserName");
    if (value.empty()) {
        char buf[256]{};
        DWORD n = sizeof(buf);
        if (GetUserNameA(buf, &n) && n > 1) value.assign(buf);
    }
    if (value.empty()) value = "Player";
    return value;
}

bool Multiplayer_L::host(int port) {
    ensureWSA();
    shutdown();
    g_lastError.clear();
    if (!g_wsaStarted) {
        setError("host", "WSAStartup failed, winsock is unavailable");
        return false;
    }
    if (port < 1 || port > 65535) {
        setError("host", "port " + std::to_string(port) + " is out of range 1-65535");
        return false;
    }
    g_port = port;
    g_defaultPort = g_port;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen == INVALID_SOCKET) {
        setError("host", "could not create the listening socket", WSAGetLastError());
        return false;
    }
    BOOL yes = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    if (!setNonBlocking(g_listen)) {
        setError("host", "could not switch the listening socket to non blocking mode", WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)g_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_listen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        setError("host", "bind to port " + std::to_string(g_port) + " failed", WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    if (listen(g_listen, 8) == SOCKET_ERROR) {
        setError("host", "listen on port " + std::to_string(g_port) + " failed", WSAGetLastError());
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        return false;
    }
    g_hostMode = true;
    g_handshakeDone = false;
    g_peerAddress.clear();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_out.clear();
    }
    g_ready = true;
    return true;
}

int Multiplayer_L::defaultPort() { return g_defaultPort > 0 ? g_defaultPort : 27015; }

bool Multiplayer_L::parseAddress(const std::string& spec, std::string& hostOut, int& portOut) {
    hostOut.clear();
    portOut = defaultPort();
    size_t begin = spec.find_first_not_of(" \t");
    if (begin == std::string::npos) return false;
    size_t end = spec.find_last_not_of(" \t");
    std::string text = spec.substr(begin, end - begin + 1);
    if (text.empty()) return false;

    const auto& parsePort = parsePortDigits;

    if (text.front() == '[') {
        size_t close = text.find(']');
        if (close == std::string::npos) return false;
        hostOut = text.substr(1, close - 1);
        std::string rest = text.substr(close + 1);
        if (!rest.empty()) {
            if (rest.front() != ':') return false;
            return parsePort(rest.substr(1), portOut);
        }
        return !hostOut.empty();
    }

    size_t firstColon = text.find(':');
    size_t lastColon = text.rfind(':');
    if (firstColon == std::string::npos) {
        hostOut = text;
        return true;
    }
    if (firstColon != lastColon) {
        hostOut = text;
        return true;
    }
    hostOut = text.substr(0, firstColon);
    std::string portText = text.substr(lastColon + 1);
    if (portText.empty()) return true;
    return parsePort(portText, portOut);
}

bool Multiplayer_L::connectTo(const std::string& ip, int port) {
    ensureWSA();
    shutdown();
    g_lastError.clear();
    if (!g_wsaStarted) {
        setError("connect", "WSAStartup failed, winsock is unavailable");
        return false;
    }
    if (ip.empty()) {
        setError("connect", "no address given, expected connect <ip>:<port>");
        return false;
    }
    if (port < 1 || port > 65535) {
        setError("connect", "port " + std::to_string(port) + " is out of range 1-65535");
        return false;
    }
    g_port = port;
    g_defaultPort = g_port;
    g_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_sock == INVALID_SOCKET) {
        setError("connect", "could not create the client socket", WSAGetLastError());
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    tuneSocket(g_sock);
    sockaddr_in addr{};
    if (!resolveIPv4(ip, (unsigned short)g_port, addr)) {
        setError("connect", "cannot resolve the address \"" + ip + "\" to an IPv4 host");
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
    char resolvedText[INET_ADDRSTRLEN]{};
    InetNtopA(AF_INET, &addr.sin_addr, resolvedText, sizeof(resolvedText));
    if (!setNonBlocking(g_sock)) {
        setError("connect", "could not switch the client socket to non blocking mode", WSAGetLastError());
        closesocket(g_sock);
        g_sock = INVALID_SOCKET;
        return false;
    }
    int result = connect(g_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (result == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEALREADY) {
            setError("connect", "connect to " + std::string(resolvedText) + ":" + std::to_string(g_port) + " was rejected right away", err);
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return false;
        }
        fd_set writeSet;
        fd_set readSet;
        fd_set exceptSet;
        timeval timeout{};
        int socketError = 0;
        int socketErrorSize = sizeof(socketError);
        int pendingError = 0;
        bool established = false;
        for (int attempt = 0; attempt < 4 && !established; ++attempt) {
            FD_ZERO(&writeSet);
            FD_ZERO(&readSet);
            FD_ZERO(&exceptSet);
            FD_SET(g_sock, &writeSet);
            FD_SET(g_sock, &readSet);
            FD_SET(g_sock, &exceptSet);
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
            const int ready = select(0, &readSet, &writeSet, &exceptSet, &timeout);
            if (ready == SOCKET_ERROR) {
                pendingError = WSAGetLastError();
                break;
            }
            if (ready <= 0) continue;
            socketError = 0;
            socketErrorSize = sizeof(socketError);
            if (getsockopt(g_sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &socketErrorSize) == SOCKET_ERROR) {
                pendingError = WSAGetLastError();
                break;
            }
            if (socketError == 0) established = true;
            else pendingError = socketError;
        }
        if (!established) {
            if (pendingError == 0) {
                setError("connect", "timed out waiting for " + std::string(resolvedText) + ":" + std::to_string(g_port) +
                                    " - the host did not answer, check the port and that the host is still running");
            } else {
                setError("connect", "connect to " + std::string(resolvedText) + ":" + std::to_string(g_port) + " failed", pendingError);
            }
            closesocket(g_sock);
            g_sock = INVALID_SOCKET;
            return false;
        }
    }
    g_peerAddress = ip;
    g_hostMode = false;
    g_handshakeDone = false;
    armHelloDeadline();
    g_ready = true;
    sendHello();
    sendMap();
    flushOut();
    return true;
}

void Multiplayer_L::shutdown() {
    disconnectSocket(true);
    if (g_listen != INVALID_SOCKET) {
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_spawnQueue.clear();
        g_moveQueue.clear();
        g_localSpawns.clear();
        g_propOwners.clear();
    }
    g_ready = false;
    g_hostMode = false;
    g_handshakeDone = false;
    g_nextNetId = 1;
    g_suppress = false;
}

bool Multiplayer_L::active() { return g_ready; }
bool Multiplayer_L::isHost() { return g_hostMode; }
bool Multiplayer_L::connected() { return g_ready && g_handshakeDone; }
const std::string& Multiplayer_L::lastError() { return g_lastError; }
int Multiplayer_L::localPort() { return g_port; }
int Multiplayer_L::localOwnerId() { return g_hostMode ? 0 : 1; }
std::string Multiplayer_L::localAddress() { return bestLocalIPv4(); }

std::string Multiplayer_L::statusLine(const std::string& hostname, const std::string& mapName) {
    const std::string title = hostname.empty() ? "Sauce" : hostname;
    const std::string map = mapName.empty() ? "unknown" : mapName;
    std::string out;
    out += "hostname: " + title + "\n";
    out += "map     : " + map + "\n";

    if (!g_ready) {
        out += "state   : singleplayer\n";
        out += "players : 1 (singleplayer)\n";
        out += "note    : no server running. Use \"Host a Game\" in the main menu to host, or \"connect <ip>:<port>\" to join.\n";
        return out;
    }

    if (g_hostMode) {
        const std::string address = localAddress();
        const bool joined = g_handshakeDone;
        out += "state   : " + std::string(joined ? "hosting" : "waiting for player") + "\n";
        out += "tcp/ip  : " + address + ":" + std::to_string(g_port) + "\n";
        out += "players : " + std::to_string(joined ? 2 : 1) + " (2 max)\n";
        out += "# 1 \"" + g_localNick + "\"\n";
        if (joined && !g_remote.nick.empty()) out += "# 2 \"" + g_remote.nick + "\"\n";
        out += "connect : connect " + address + ":" + std::to_string(g_port) + "\n";
        out += "connect : connect 127.0.0.1:" + std::to_string(g_port);
        return out;
    }

    const std::string address = g_peerAddress.empty() ? "?" : g_peerAddress;
    out += "state   : " + std::string(g_handshakeDone ? "connected" : "handshaking") + "\n";
    out += "tcp/ip  : " + address + ":" + std::to_string(g_port) + "\n";
    out += "players : " + std::to_string(g_handshakeDone ? 2 : 1) + " (2 max)\n";
    out += "# 1 \"" + g_localNick + "\"\n";
    if (g_handshakeDone && !g_remote.nick.empty()) out += "# 2 \"" + g_remote.nick + "\"\n";
    out += "server  : " + address + ":" + std::to_string(g_port);
    return out;
}

int Multiplayer_L::allocateNetId() {
    const int base = g_nextNetId++ & 0x03FFFFFF;
    return localOwnerId() == 0 ? base + 1 : 0x40000000 + base + 1;
}

void Multiplayer_L::setCurrentMap(const std::string& map) {
    g_localMap = map;
    if (g_ready && g_sock != INVALID_SOCKET) sendMap();
}

void Multiplayer_L::setLocalIdentity(const std::string& nick, const std::string& model) {
    g_localNick = nick.empty() ? steamNick() : nick;
    g_localModel = model.empty() ? "models/player.mdl" : model;
    if (g_ready && g_sock != INVALID_SOCKET) sendHello();
}

void Multiplayer_L::setLocalState(const Vec3& pos, float yaw, float pitch, float height, bool paused) {
    if (!g_ready || g_sock == INVALID_SOCKET) return;
    std::vector<char> pkt;
    pkt.push_back(2);
    putF32(pkt, pos.x); putF32(pkt, pos.y); putF32(pkt, pos.z);
    putF32(pkt, yaw); putF32(pkt, pitch); putF32(pkt, height);
    putU32(pkt, paused ? 1u : 0u);
    queuePacket(pkt);
}

const MPPlayerState& Multiplayer_L::remoteState() { return g_remote; }

static void QueueSpawnPacket(const MPPropSpawnEv& ev) {
    std::vector<char> pkt;
    pkt.push_back(3);
    putU32(pkt, (unsigned int)ev.netId);
    putU32(pkt, (unsigned int)ev.owner);
    putF32(pkt, ev.pos.x); putF32(pkt, ev.pos.y); putF32(pkt, ev.pos.z);
    putF32(pkt, ev.angles.x); putF32(pkt, ev.angles.y); putF32(pkt, ev.angles.z);
    putF32(pkt, ev.scale);
    putStr(pkt, ev.classname);
    putStr(pkt, ev.model);
    queuePacket(pkt);
}

void Multiplayer_L::noteLocalSpawn(int netId, const std::string& classname, const std::string& model,
                                    const Vec3& pos, const Vec3& angles, float scale) {
    if (suppressed()) return;
    MPPropSpawnEv ev;
    ev.netId = netId;
    ev.owner = localOwnerId();
    ev.classname = classname;
    ev.model = model;
    ev.pos = pos;
    ev.angles = angles;
    ev.scale = scale;
    setPropOwner(netId, ev.owner);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_localSpawns.push_back(ev);
    }
    if (g_ready && g_sock != INVALID_SOCKET) QueueSpawnPacket(ev);
}

void Multiplayer_L::queueMove(int netId, const Vec3& pos, const Vec3& angles) {
    if (!g_ready || g_sock == INVALID_SOCKET || suppressed() || !ownsProp(netId)) return;
    std::vector<char> pkt;
    pkt.push_back(4);
    putU32(pkt, (unsigned int)netId);
    putF32(pkt, pos.x); putF32(pkt, pos.y); putF32(pkt, pos.z);
    putF32(pkt, angles.x); putF32(pkt, angles.y); putF32(pkt, angles.z);
    queuePacket(pkt);
}

void Multiplayer_L::setPropOwner(int netId, int owner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_propOwners[netId] = owner;
    if (g_propOwners.size() > 8192) g_propOwners.clear();
}

bool Multiplayer_L::ownsProp(int netId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_propOwners.find(netId);
    const int owner = it == g_propOwners.end() ? localOwnerId() : it->second;
    return owner == localOwnerId();
}

void Multiplayer_L::resetProps() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_propOwners.clear();
    g_localSpawns.clear();
}

void Multiplayer_L::setSuppress(bool on) { g_suppress = on; }
bool Multiplayer_L::suppressed() { return g_suppress; }

std::vector<MPPropSpawnEv> Multiplayer_L::takeSpawns() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<MPPropSpawnEv> out;
    out.swap(g_spawnQueue);
    return out;
}

std::vector<MPPropMoveEv> Multiplayer_L::takeMoves() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<MPPropMoveEv> out;
    out.swap(g_moveQueue);
    return out;
}

void Multiplayer_L::tick(float) {
    if (!g_ready) return;
    if (g_hostMode && g_sock == INVALID_SOCKET && g_listen != INVALID_SOCKET) {
        g_peerAddress.clear();
        for (int i = 0; i < 4; ++i) {
            SOCKET c = accept(g_listen, nullptr, nullptr);
            if (c == INVALID_SOCKET) break;
            setNonBlocking(c);
            tuneSocket(c);
            sockaddr_in peer{};
            int peerSize = sizeof(peer);
            char peerText[INET_ADDRSTRLEN]{};
            if (getpeername(c, reinterpret_cast<sockaddr*>(&peer), &peerSize) == 0 &&
                InetNtopA(AF_INET, &peer.sin_addr, peerText, sizeof(peerText))) {
                g_peerAddress = peerText;
            } else {
                g_peerAddress = "127.0.0.1";
            }
            g_sock = c;
            g_in.clear();
            g_handshakeDone = false;
            armHelloDeadline();
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                g_out.clear();
            }
            sendHello();
            sendMap();
            std::vector<MPPropSpawnEv> known;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                known = g_localSpawns;
            }
            for (const MPPropSpawnEv& ev : known) QueueSpawnPacket(ev);
        }
    }
    if (g_sock == INVALID_SOCKET) return;
    recvAll();
    if (g_sock == INVALID_SOCKET) return;
    processBuffer();
    if (!g_handshakeDone && helloTimedOut()) {
        disconnectSocket(true);
        return;
    }
    flushOut();
}
