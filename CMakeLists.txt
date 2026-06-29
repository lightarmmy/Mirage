cmake_minimum_required(VERSION 3.16)
project(Mirage LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build for small exe
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Ob2 /DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/OPT:REF /OPT:ICF")

# Use static CRT for single exe distribution
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(mirage
    src/main.cpp
    src/config_manager.cpp
    src/overlay_window.cpp
    src/input_manager.cpp
    src/shader_manager.cpp
    src/d3d11_renderer.cpp
)

target_include_directories(mirage PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)

target_link_libraries(mirage
    d3d11
    dxgi
    dwmapi
    d3dcompiler
)

# Subsystem:Windows = no console window
set_target_properties(mirage PROPERTIES
    WIN32_EXECUTABLE TRUE
)
