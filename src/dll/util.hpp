#pragma once

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace xeno::util {

inline std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline std::filesystem::path xeno_dir() {
    wchar_t* buffer = nullptr;
    size_t length = 0;
    _wdupenv_s(&buffer, &length, L"LOCALAPPDATA");

    std::filesystem::path root = buffer ? buffer : L".";
    std::free(buffer);

    root /= L"Xeno";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root;
}

inline std::filesystem::path autoexec_dir() {
    auto path = xeno_dir() / "autoexec";
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

inline std::wstring widen(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

inline std::string narrow(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

inline bool write_text(const std::filesystem::path& path, std::string_view value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(value.data(), static_cast<std::streamsize>(value.size()));
    return file.good();
}

inline std::string config_value(std::string_view key, std::string_view fallback) {
    const auto path = xeno_dir() / "signatures.ini";
    std::ifstream file(path);
    if (!file) {
        return std::string(fallback);
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        const auto offset = line.find('=');
        if (offset == std::string::npos) {
            continue;
        }
        if (trim(line.substr(0, offset)) == std::string(key)) {
            return trim(line.substr(offset + 1));
        }
    }
    return std::string(fallback);
}

}
