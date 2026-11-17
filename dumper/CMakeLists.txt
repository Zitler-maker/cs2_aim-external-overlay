cmake_minimum_required(VERSION 3.20)
project(cs2_offsets_dumper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cs2_offsets_dumper
    src/main.cpp
    src/json.cpp
    src/log.cpp
    src/pe.cpp
    src/sig_scan.cpp
    src/process.cpp
    src/netvar.cpp
    src/offsets.cpp
    src/config.cpp
    src/hash.cpp
)

if (MSVC)
    target_compile_options(cs2_offsets_dumper PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_compile_definitions(cs2_offsets_dumper PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_property(TARGET cs2_offsets_dumper PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif ()

target_link_libraries(cs2_offsets_dumper PRIVATE kernel32 advapi32)
