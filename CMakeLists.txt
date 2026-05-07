cmake_minimum_required(VERSION 3.22.1)

if(DEFINED ENV{NDK_HOME})
    set(ANDROID_PLATFORM 27)
    set(ANDROID_ABI arm64-v8a)
    file(TO_CMAKE_PATH "$ENV{NDK_HOME}" NDK_HOME_CMAKE)
    set(CMAKE_TOOLCHAIN_FILE "${NDK_HOME_CMAKE}/build/cmake/android.toolchain.cmake")
else()
    message(FATAL_ERROR "NDK_HOME environment variable not set. Please set it to the path of your Android NDK.")
endif()

project(AndUEProber C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-fvisibility=hidden -ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections -Wl,--strip-all -Wl,--exclude-libs,ALL)
endif()

set(ANDSWAPCHAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/AndSwapChainHook)
set(ANDUEDUMPER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/external/AndUEDumper)

# KittyMemoryEx — single source of truth
set(KITTY_ROOT  ${ANDSWAPCHAIN_DIR}/external/KittyMemoryEx)
set(KITTY_INNER ${KITTY_ROOT}/KittyMemoryEx)

file(GLOB KITTY_SOURCES CONFIGURE_DEPENDS ${KITTY_INNER}/*.cpp)
list(FILTER KITTY_SOURCES EXCLUDE REGEX "MemoryPatch\\.cpp$")
list(FILTER KITTY_SOURCES EXCLUDE REGEX "MemoryBackup\\.cpp$")

add_library(KittyMemoryExLib STATIC ${KITTY_SOURCES})
target_include_directories(KittyMemoryExLib PUBLIC
    ${KITTY_ROOT}                # for "KittyMemoryEx/KittyMemoryEx.hpp"
    ${KITTY_INNER}               # for unprefixed <KittyMemoryMgr.hpp> etc.
    ${KITTY_INNER}/Deps
)
target_compile_definitions(KittyMemoryExLib PUBLIC
    kUSE_KITTYMEMORYEX
    kNO_KEYSTONE
)
target_link_libraries(KittyMemoryExLib PRIVATE log)

# AndSwapChainHook — static library
file(GLOB_RECURSE ANDSWAPCHAIN_SOURCES CONFIGURE_DEPENDS
    ${ANDSWAPCHAIN_DIR}/source/*.cpp
    ${ANDSWAPCHAIN_DIR}/source/*.c
    ${ANDSWAPCHAIN_DIR}/source/*.s
)
list(FILTER ANDSWAPCHAIN_SOURCES EXCLUDE REGEX "Library\\.cpp$")

file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS
    ${ANDSWAPCHAIN_DIR}/external/imgui/imgui.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/imgui_demo.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/imgui_draw.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/imgui_tables.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/imgui_widgets.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/backends/imgui_impl_android.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/backends/imgui_impl_vulkan.cpp
    ${ANDSWAPCHAIN_DIR}/external/imgui/backends/imgui_impl_opengl3.cpp
)

add_library(AndSwapChainHookLib STATIC
    ${ANDSWAPCHAIN_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(AndSwapChainHookLib PUBLIC
    ${ANDSWAPCHAIN_DIR}/source
    ${ANDSWAPCHAIN_DIR}/source/Utils
    ${ANDSWAPCHAIN_DIR}/external
    ${ANDSWAPCHAIN_DIR}/external/imgui
)

target_compile_definitions(AndSwapChainHookLib PRIVATE
    kANDROID_LOG
    kANDROID_LOG_TAG="INJECT"
    kPROJECT_NAME="${PROJECT_NAME}"
)

target_link_libraries(AndSwapChainHookLib
    PUBLIC  KittyMemoryExLib
    PRIVATE android log vulkan GLESv3 EGL
)

# AndUEDumper — core dumper static library
set(ANDUEDUMPER_DIR  ${ANDUEDUMPER_ROOT}/AndUEDumper)
set(ANDUEDUMPER_SRC  ${ANDUEDUMPER_DIR}/src)
set(ANDUEDUMPER_DEPS ${ANDUEDUMPER_ROOT}/deps)

file(GLOB ANDUEDUMPER_UE_SRC    CONFIGURE_DEPENDS ${ANDUEDUMPER_SRC}/UE/*.cpp)
file(GLOB ANDUEDUMPER_UTILS_SRC CONFIGURE_DEPENDS ${ANDUEDUMPER_SRC}/Utils/*.cpp)

add_library(AndUEDumperLib STATIC
    ${ANDUEDUMPER_UE_SRC}
    ${ANDUEDUMPER_UTILS_SRC}
    ${ANDUEDUMPER_SRC}/Dumper.cpp
    ${ANDUEDUMPER_SRC}/UPackageGenerator.cpp
    ${ANDUEDUMPER_DEPS}/fmt/format.cc
)

target_include_directories(AndUEDumperLib PUBLIC
    ${ANDUEDUMPER_SRC}        # exposed UE/* and Utils/BufferFmt.hpp consumed by UEProber
    ${ANDUEDUMPER_DEPS}       # BufferFmt.hpp transitively needs fmt/; UEProber uses utfcpp/
)

target_link_libraries(AndUEDumperLib PUBLIC KittyMemoryExLib)

# AndUEProber — final shared library
file(GLOB_RECURSE UEPROBER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/source/UEProber/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/source/UEProber/*.c
)

add_library(AndUEProber SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/source/Library.cpp
    ${UEPROBER_SOURCES}
)

target_include_directories(AndUEProber PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
)

target_compile_definitions(AndUEProber PRIVATE
    kANDROID_LOG
    kANDROID_LOG_TAG="INJECT"
    kPROJECT_NAME="${PROJECT_NAME}"
)

target_link_libraries(AndUEProber PRIVATE
    AndSwapChainHookLib
    AndUEDumperLib
    ${ANDSWAPCHAIN_DIR}/external/Dobby/arm64-v8a/libdobby.a
)
