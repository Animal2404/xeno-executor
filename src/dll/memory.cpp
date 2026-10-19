#include "memory.hpp"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

namespace xeno::mem {
namespace {

struct compiled_pattern {
    std::vector<std::uint8_t> bytes;
    std::vector<bool> fixed;
};

int hex_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool compile(std::string_view pattern, compiled_pattern& out) {
    size_t cursor = 0;
    while (cursor < pattern.size()) {
        while (cursor < pattern.size() && pattern[cursor] == ' ') {
            ++cursor;
        }
        if (cursor >= pattern.size()) {
            break;
        }

        if (pattern[cursor] == '?') {
            const bool pair = cursor + 1 < pattern.size() && pattern[cursor + 1] == '?';
            out.bytes.push_back(0);
            out.fixed.push_back(false);
            cursor += pair ? 2 : 1;
            continue;
        }

        if (cursor + 1 >= pattern.size()) {
            return false;
        }
        const int high = hex_value(pattern[cursor]);
        const int low = hex_value(pattern[cursor + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out.bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
        out.fixed.push_back(true);
        cursor += 2;
    }
    return !out.bytes.empty();
}

}

region main_module() {
    HMODULE module = GetModuleHandleW(nullptr);
    if (!module) {
        return {};
    }

    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
        return {};
    }

    return {reinterpret_cast<std::uint8_t*>(module), static_cast<std::size_t>(info.SizeOfImage)};
}

std::uint8_t* scan(std::string_view pattern) {
    compiled_pattern compiled;
    if (!compile(pattern, compiled)) {
        return nullptr;
    }

    const region module = main_module();
    if (!module.base || module.size < sizeof(IMAGE_DOS_HEADER)) {
        return nullptr;
    }

    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module.base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }

    const auto header_offset = static_cast<std::size_t>(dos->e_lfanew);
    if (header_offset + sizeof(IMAGE_NT_HEADERS64) > module.size) {
        return nullptr;
    }

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(module.base + header_offset);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) {
            continue;
        }

        if (section->VirtualAddress >= module.size) {
            continue;
        }

        auto* begin = module.base + section->VirtualAddress;
        std::size_t length = section->Misc.VirtualSize;
        if (length == 0) {
            length = section->SizeOfRawData;
        }
        length = std::min(length, module.size - section->VirtualAddress);
        if (length < compiled.bytes.size()) {
            continue;
        }

        const std::size_t last = length - compiled.bytes.size();
        for (std::size_t offset = 0; offset <= last; ++offset) {
            bool matched = true;
            for (std::size_t byte = 0; byte < compiled.bytes.size(); ++byte) {
                if (compiled.fixed[byte] && begin[offset + byte] != compiled.bytes[byte]) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return begin + offset;
            }
        }
    }

    return nullptr;
}

}
