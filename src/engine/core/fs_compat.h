#pragma once
#include <filesystem>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace FsCompat {
inline std::filesystem::path fromUtf8(const std::string& value) {
#ifdef _WIN32
    if (value.empty()) return std::filesystem::path();
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0);
    }
    if (count <= 0) return std::filesystem::path();
    std::wstring wide(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(codePage, flags, value.data(), static_cast<int>(value.size()), wide.data(), count) <= 0) {
        return std::filesystem::path();
    }
    return std::filesystem::path(wide);
#else
    return std::filesystem::path(value);
#endif
}

inline std::string toUtf8(const std::filesystem::path& value) {
#ifdef _WIN32
    const std::wstring wide = value.native();
    if (wide.empty()) return std::string();
    int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return std::string();
    std::string result(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), count, nullptr, nullptr) <= 0) return std::string();
    return result;
#else
    return value.generic_string();
#endif
}

inline std::string wideToUtf8(const std::wstring& value) {
#ifdef _WIN32
    if (value.empty()) return std::string();
    int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return std::string();
    std::string result(static_cast<size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) <= 0) return std::string();
    return result;
#else
    return std::string(value.begin(), value.end());
#endif
}

inline bool exists(const std::filesystem::path& value) noexcept {
    std::error_code ec;
    return std::filesystem::exists(value, ec) && !ec;
}

inline bool isDirectory(const std::filesystem::path& value) noexcept {
    std::error_code ec;
    return std::filesystem::is_directory(value, ec) && !ec;
}

inline bool isRegularFile(const std::filesystem::path& value) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(value, ec) && !ec;
}
}
