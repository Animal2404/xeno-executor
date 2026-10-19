#pragma once

#include <ctime>
#include <mutex>
#include <string>
#include <vector>

namespace xeno::log {

enum class level {
    info,
    warn,
    error,
};

struct entry {
    std::string time;
    level kind;
    std::string text;
};

inline std::mutex& mutex() {
    static std::mutex value;
    return value;
}

inline std::vector<entry>& store() {
    static std::vector<entry> value;
    return value;
}

inline void write(level kind, std::string text) {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);

    char stamp[16]{};
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);

    std::lock_guard lock(mutex());
    auto& entries = store();
    entries.push_back({stamp, kind, std::move(text)});
    if (entries.size() > 1024) {
        entries.erase(entries.begin(), entries.begin() + 256);
    }
}

inline void info(std::string text) {
    write(level::info, std::move(text));
}

inline void warn(std::string text) {
    write(level::warn, std::move(text));
}

inline void error(std::string text) {
    write(level::error, std::move(text));
}

inline std::vector<entry> snapshot() {
    std::lock_guard lock(mutex());
    return store();
}

inline void clear() {
    std::lock_guard lock(mutex());
    store().clear();
}

}
