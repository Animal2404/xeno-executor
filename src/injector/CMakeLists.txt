add_executable(xeno_injector main.cpp)

set_target_properties(xeno_injector PROPERTIES
    OUTPUT_NAME "xeno_injector"
)

target_compile_definitions(xeno_injector PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
)

target_compile_options(xeno_injector PRIVATE /W4 /permissive- /EHsc)

target_link_libraries(xeno_injector PRIVATE advapi32)

target_link_options(xeno_injector PRIVATE
    "/MANIFESTUAC:level='requireAdministrator'"
    /MANIFEST:EMBED
)
