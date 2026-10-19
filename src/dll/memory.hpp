#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace xeno::mem {

struct region {
    std::uint8_t* base = nullptr;
    std::size_t size = 0;
};

region main_module();

std::uint8_t* scan(std::string_view pattern);

}
