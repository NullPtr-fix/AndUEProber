cmake_minimum_required(VERSION 3.20)
project(sdk_smoke LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Where the dumped SDK lives. Default matches misc/UEDump3r/<game>/SDK_A.
# Override on the command line:
#   cmake -S misc/sdk_smoke -B build-smoke -DSDK_A_DIR=/some/other/SDK_A
set(SDK_A_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../UEDump3r/com.tencent.tmgp.dfm/SDK_A"
    CACHE PATH "Path to the dumped SDK_A directory")

if(NOT EXISTS "${SDK_A_DIR}/SDK.hpp")
    message(WARNING
        "sdk_smoke: SDK.hpp not found at ${SDK_A_DIR} — re-dump the target game "
        "or pass -DSDK_A_DIR=... pointing at a populated SDK_A. Skipping target.")
    return()
endif()

# Recursively pick up every .cpp the dumper emitted: Basic.cpp,
# CoreUObject_functions.cpp, Packages/*_functions.cpp. Demo.cpp is the smoke
# entry point — it currently only #include's SDK.hpp via __has_include and
# defines no main(), so we build everything as an OBJECT library to validate
# compilation without requiring a linker that resolves UE prober runtime
# externs (FName::s_NameResolver, UObject::GObjects, ProcessEvent body, ...).
#
# When you pass `-j N` to the build, keep N small (4–8) — each TU pulls a
# package header that can run several thousand lines, and a high-parallel
# build over a few hundred TUs eats RAM linearly with N.
file(GLOB_RECURSE SDK_SRCS CONFIGURE_DEPENDS "${SDK_A_DIR}/*.cpp")

add_library(sdk_smoke_objs OBJECT
    ${SDK_SRCS}
    "${CMAKE_CURRENT_SOURCE_DIR}/Demo.cpp"
)

target_include_directories(sdk_smoke_objs PRIVATE "${SDK_A_DIR}")

# UE-shaped types break a few clang style warnings that aren't real bugs.
# Suppress them so a clean compile stays a useful signal.
target_compile_options(sdk_smoke_objs PRIVATE
    -Wno-invalid-offsetof    # offsetof() on non-standard-layout (UE classes)
    -Wno-class-conversion    # FAllocatedString -> FString conversion ops
    -Wno-return-stack-address
    -Wno-unused-value
    -Wno-multichar           # UE FName fragment 4-char literals
    -Wno-invalid-source-encoding
)

# Report the source count so the user can sanity-check the dump is whole.
list(LENGTH SDK_SRCS SDK_SRCS_COUNT)
message(STATUS "sdk_smoke: compiling ${SDK_SRCS_COUNT} dumped .cpp files from ${SDK_A_DIR}")
