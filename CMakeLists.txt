cmake_minimum_required(VERSION 3.24)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
project(xeno LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Xeno targets Windows only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(XENO_USE_STATIC_RUNTIME "Link the MSVC runtime statically" ON)
if(XENO_USE_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(FetchContent)

set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    luau
    GIT_REPOSITORY https://github.com/luau-lang/luau.git
    GIT_TAG 0.635
)
FetchContent_MakeAvailable(luau)

# Luau can throw C++ exceptions on VM errors. Build the VM with longjmp so
# errors stay inside lua_pcall instead of relying on x64 unwind data in a
# manually mapped image.
target_compile_definitions(Luau.VM PUBLIC LUA_USE_LONGJMP=1)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
)
FetchContent_MakeAvailable(imgui)

add_subdirectory(src/dll)
add_subdirectory(src/injector)

message(STATUS "Xeno configured for ${CMAKE_GENERATOR} (${CMAKE_GENERATOR_PLATFORM})")
