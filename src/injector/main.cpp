#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kDefaultProcess = L"RobloxPlayerBeta.exe";
constexpr std::size_t kMaxImageSize = 256ull * 1024ull * 1024ull;

DWORD find_process(const wchar_t* name) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD process_id = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name) == 0) {
                process_id = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return process_id;
}

bool enable_debug_privilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(
            GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    TOKEN_PRIVILEGES privileges{};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(
            nullptr, SE_DEBUG_NAME, &privileges.Privileges[0].Luid)) {
        CloseHandle(token);
        return false;
    }

    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const BOOL adjusted = AdjustTokenPrivileges(
        token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
    const DWORD error = GetLastError();
    CloseHandle(token);

    return adjusted && error == ERROR_SUCCESS;
}

bool read_file(const wchar_t* path, std::vector<std::uint8_t>& data, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "could not open the DLL";
        return false;
    }

    const std::streamoff size = file.tellg();
    if (size <= 0 || static_cast<std::uint64_t>(size) > kMaxImageSize) {
        error = "DLL size is invalid";
        return false;
    }

    file.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        error = "could not read the DLL";
        return false;
    }

    return true;
}

bool validate_pe(
    const std::vector<std::uint8_t>& data,
    IMAGE_DOS_HEADER*& dos,
    IMAGE_NT_HEADERS64*& nt,
    std::string& error) {
    if (data.size() < sizeof(IMAGE_DOS_HEADER)) {
        error = "file is too small for a PE image";
        return false;
    }

    dos = reinterpret_cast<IMAGE_DOS_HEADER*>(
        const_cast<std::uint8_t*>(data.data()));
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        error = "missing DOS signature";
        return false;
    }

    const std::size_t nt_offset = static_cast<std::size_t>(dos->e_lfanew);
    if (nt_offset + sizeof(IMAGE_NT_HEADERS64) > data.size()) {
        error = "truncated NT headers";
        return false;
    }

    nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(
        const_cast<std::uint8_t*>(data.data()) + nt_offset);

    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        error = "missing NT signature";
        return false;
    }
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
        error = "image is not x86-64";
        return false;
    }
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        error = "image is not PE32+";
        return false;
    }
    if (nt->OptionalHeader.SizeOfImage == 0 ||
        nt->OptionalHeader.SizeOfImage > kMaxImageSize) {
        error = "image size is invalid";
        return false;
    }

    return true;
}

std::vector<std::uint8_t> build_image(
    const std::vector<std::uint8_t>& data,
    IMAGE_NT_HEADERS64* nt,
    std::string& error) {
    std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage, 0);

    const std::size_t header_size = (std::min)(
        static_cast<std::size_t>(nt->OptionalHeader.SizeOfHeaders), data.size());
    std::memcpy(image.data(), data.data(), header_size);

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        if (section->SizeOfRawData == 0) {
            continue;
        }

        const std::size_t virtual_size = section->Misc.VirtualSize != 0
            ? static_cast<std::size_t>(section->Misc.VirtualSize)
            : static_cast<std::size_t>(section->SizeOfRawData);
        const std::size_t copy_size = (std::min)(
            static_cast<std::size_t>(section->SizeOfRawData), virtual_size);
        const std::size_t source = section->PointerToRawData;
        const std::size_t destination = section->VirtualAddress;

        if (source + copy_size > data.size() ||
            destination + copy_size > image.size()) {
            error = "section lies outside the image";
            return {};
        }

        std::memcpy(image.data() + destination, data.data() + source, copy_size);
    }

    return image;
}

bool apply_relocations(
    std::vector<std::uint8_t>& image,
    IMAGE_NT_HEADERS64* nt,
    std::uintptr_t remote_base,
    std::string& error) {
    const std::uintptr_t delta = remote_base - nt->OptionalHeader.ImageBase;
    if (delta == 0) {
        return true;
    }

    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (directory.Size == 0) {
        return true;
    }

    if (directory.VirtualAddress + directory.Size > image.size()) {
        error = "relocation table is outside the image";
        return false;
    }

    std::size_t cursor = 0;
    while (cursor < directory.Size) {
        if (directory.VirtualAddress + cursor + sizeof(IMAGE_BASE_RELOCATION) >
            image.size()) {
            break;
        }

        auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
            image.data() + directory.VirtualAddress + cursor);
        if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) {
            break;
        }

        const DWORD count =
            (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
        auto* entries = reinterpret_cast<WORD*>(block + 1);

        for (DWORD index = 0; index < count; ++index) {
            const WORD type = entries[index] >> 12;
            const WORD offset = entries[index] & 0x0FFF;
            if (type != IMAGE_REL_BASED_DIR64) {
                continue;
            }

            const std::size_t target =
                static_cast<std::size_t>(block->VirtualAddress) + offset;
            if (target + sizeof(std::uint64_t) > image.size()) {
                error = "relocation points outside the image";
                return false;
            }

            auto* value = reinterpret_cast<std::uint64_t*>(image.data() + target);
            *value += delta;
        }

        cursor += block->SizeOfBlock;
    }

    return true;
}

bool resolve_imports(
    std::vector<std::uint8_t>& image,
    IMAGE_NT_HEADERS64* nt,
    std::string& error) {
    const auto& directory =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.Size == 0) {
        return true;
    }

    if (directory.VirtualAddress + directory.Size > image.size()) {
        error = "import table is outside the image";
        return false;
    }

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        image.data() + directory.VirtualAddress);

    for (; descriptor->Name != 0; ++descriptor) {
        if (descriptor->Name >= image.size()) {
            error = "import library name is outside the image";
            return false;
        }

        const char* library =
            reinterpret_cast<const char*>(image.data() + descriptor->Name);
        HMODULE module = LoadLibraryA(library);
        if (!module) {
            error = "could not load import library " + std::string(library);
            return false;
        }

        if (descriptor->FirstThunk == 0) {
            continue;
        }

        const DWORD lookup_rva =
            descriptor->OriginalFirstThunk ? descriptor->OriginalFirstThunk
                                           : descriptor->FirstThunk;
        if (lookup_rva >= image.size() || descriptor->FirstThunk >= image.size()) {
            error = "import thunk is outside the image";
            return false;
        }

        auto* lookup = reinterpret_cast<IMAGE_THUNK_DATA64*>(
            image.data() + lookup_rva);
        auto* target = reinterpret_cast<IMAGE_THUNK_DATA64*>(
            image.data() + descriptor->FirstThunk);

        for (; lookup->u1.AddressOfData != 0; ++lookup, ++target) {
            FARPROC function = nullptr;

            if (IMAGE_SNAP_BY_ORDINAL64(lookup->u1.Ordinal)) {
                const WORD ordinal = static_cast<WORD>(
                    IMAGE_ORDINAL64(lookup->u1.Ordinal));
                function = GetProcAddress(module, MAKEINTRESOURCEA(ordinal));
            } else {
                const std::size_t name_offset =
                    static_cast<std::size_t>(lookup->u1.AddressOfData);
                if (name_offset + sizeof(IMAGE_IMPORT_BY_NAME) > image.size()) {
                    error = "import name is outside the image";
                    return false;
                }
                auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                    image.data() + name_offset);
                function = GetProcAddress(
                    module, reinterpret_cast<const char*>(name->Name));
            }

            if (!function) {
                error = "could not resolve import from " + std::string(library);
                return false;
            }

            target->u1.Function = reinterpret_cast<ULONGLONG>(function);
        }
    }

    return true;
}

DWORD section_protection(DWORD characteristics) {
    const bool executable = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
    const bool writable = (characteristics & IMAGE_SCN_MEM_WRITE) != 0;

    if (executable) {
        return writable ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    }
    return writable ? PAGE_READWRITE : PAGE_READONLY;
}

#pragma pack(push, 1)
struct entry_stub {
    std::uint8_t sub_rsp[4] = {0x48, 0x83, 0xEC, 0x28};
    std::uint8_t mov_rcx[2] = {0x48, 0xB9};
    std::uint64_t image_base = 0;
    std::uint8_t mov_rdx[2] = {0x48, 0xBA};
    std::uint64_t reason = 1;
    std::uint8_t clear_r8[3] = {0x45, 0x33, 0xC0};
    std::uint8_t mov_rax[2] = {0x48, 0xB8};
    std::uint64_t entry = 0;
    std::uint8_t call_rax[2] = {0xFF, 0xD0};
    std::uint8_t add_rsp[4] = {0x48, 0x83, 0xC4, 0x28};
    std::uint8_t ret = 0xC3;
};
#pragma pack(pop)

bool map_image(
    HANDLE process,
    const std::vector<std::uint8_t>& data,
    std::string& error) {
    IMAGE_DOS_HEADER* dos = nullptr;
    IMAGE_NT_HEADERS64* nt = nullptr;
    if (!validate_pe(data, dos, nt, error)) {
        return false;
    }

    std::vector<std::uint8_t> image = build_image(data, nt, error);
    if (image.empty()) {
        if (error.empty()) {
            error = "could not build the image";
        }
        return false;
    }

    void* remote = VirtualAllocEx(
        process,
        nullptr,
        image.size(),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);

    if (!remote) {
        error = "VirtualAllocEx failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }

    const auto remote_base = reinterpret_cast<std::uintptr_t>(remote);

    if (!apply_relocations(image, nt, remote_base, error)) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    if (!resolve_imports(image, nt, error)) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteProcessMemory(
            process, remote, image.data(), image.size(), nullptr)) {
        error = "WriteProcessMemory failed (" +
                std::to_string(GetLastError()) + ")";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        std::size_t size = section->Misc.VirtualSize;
        if (size == 0) {
            size = section->SizeOfRawData;
        }
        if (size == 0) {
            continue;
        }

        DWORD previous = 0;
        VirtualProtectEx(
            process,
            reinterpret_cast<std::uint8_t*>(remote) + section->VirtualAddress,
            size,
            section_protection(section->Characteristics),
            &previous);
    }

    DWORD previous = 0;
    VirtualProtectEx(
        process,
        remote,
        (std::min)(
            static_cast<std::size_t>(nt->OptionalHeader.SizeOfHeaders),
            image.size()),
        PAGE_READONLY,
        &previous);

    FlushInstructionCache(process, remote, image.size());

    const std::uintptr_t entry_point =
        remote_base + nt->OptionalHeader.AddressOfEntryPoint;

    entry_stub stub;
    stub.image_base = remote_base;
    stub.entry = entry_point;

    void* remote_stub = VirtualAllocEx(
        process,
        nullptr,
        sizeof(stub),
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE);

    if (!remote_stub) {
        error = "could not allocate the entry stub";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    if (!WriteProcessMemory(
            process, remote_stub, &stub, sizeof(stub), nullptr)) {
        error = "could not write the entry stub";
        VirtualFreeEx(process, remote_stub, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(
        process,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(remote_stub),
        nullptr,
        0,
        nullptr);

    if (!thread) {
        error = "CreateRemoteThread failed (" +
                std::to_string(GetLastError()) + ")";
        VirtualFreeEx(process, remote_stub, 0, MEM_RELEASE);
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 20000);
    if (wait != WAIT_OBJECT_0) {
        error = "timed out waiting for DllMain";
        CloseHandle(thread);
        return false;
    }

    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);

    VirtualFreeEx(process, remote_stub, 0, MEM_RELEASE);

    if (exit_code == 0) {
        error = "DllMain returned FALSE";
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    std::printf("[xeno] image mapped at %p, entry %p\n", remote, reinterpret_cast<void*>(entry_point));
    return true;
}

void print_usage() {
    std::printf(
        "Xeno injector\n"
        "usage: xeno_injector.exe <xeno.dll> [process.exe]\n"
        "default process: RobloxPlayerBeta.exe\n");
}

}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);

    if (argc < 2) {
        print_usage();
        return 2;
    }

    const wchar_t* dll_path = argv[1];
    const wchar_t* process_name = argc >= 3 ? argv[2] : kDefaultProcess;

    if (!enable_debug_privilege()) {
        std::printf("[xeno] warning: SeDebugPrivilege is unavailable\n");
    }

    const DWORD process_id = find_process(process_name);
    if (process_id == 0) {
        std::printf("[xeno] process not found: %ls\n", process_name);
        return 3;
    }

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
        FALSE,
        process_id);

    if (!process) {
        std::printf(
            "[xeno] OpenProcess failed (%lu). Run the injector as administrator.\n",
            GetLastError());
        return 4;
    }

    std::vector<std::uint8_t> data;
    std::string error;

    if (!read_file(dll_path, data, error)) {
        std::printf("[xeno] %s\n", error.c_str());
        CloseHandle(process);
        return 5;
    }

    std::printf("[xeno] injecting into pid %lu\n", process_id);

    const bool injected = map_image(process, data, error);

    if (!injected) {
        std::printf("[xeno] injection failed: %s\n", error.c_str());
        CloseHandle(process);
        return 6;
    }

    std::printf("[xeno] injected. press INSERT in the game to open the workspace\n");
    CloseHandle(process);
    return 0;
}
