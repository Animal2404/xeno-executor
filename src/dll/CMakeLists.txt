add_library(xeno_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(xeno_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(xeno_imgui PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

target_compile_definitions(xeno_imgui PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(xeno_imgui PUBLIC d3d11 dxgi d3dcompiler dwmapi imm32)

add_library(xeno SHARED
    dllmain.cpp
    executor.cpp
    state.cpp
    execution.cpp
    api.cpp
    memory.cpp
    overlay.cpp
)

set_target_properties(xeno PROPERTIES
    PREFIX ""
    OUTPUT_NAME "xeno"
)

target_include_directories(xeno PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${luau_SOURCE_DIR}/VM/include
    ${luau_SOURCE_DIR}/Compiler/include
    ${luau_SOURCE_DIR}/Ast/include
    ${luau_SOURCE_DIR}/Common/include
    ${minhook_SOURCE_DIR}/include
)

target_compile_definitions(xeno PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(xeno PRIVATE /W4 /permissive- /EHsc)

target_link_libraries(xeno PRIVATE
    Luau.VM
    Luau.Compiler
    Luau.Ast
    xeno_imgui
    minhook
    psapi
    winhttp
    shell32
    ole32
    user32
    gdi32
)
