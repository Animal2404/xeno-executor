#pragma once

#include <mutex>

namespace xeno::hooks {

inline std::mutex& mutex() {
    static std::mutex value;
    return value;
}

}
