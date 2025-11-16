cmake_minimum_required(VERSION 3.15)
project(sm C)

# Set C standard (C17 to accommodate sm_8b.c requirement)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build types
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Find SDL2
# Try pkg-config first (works on Linux/macOS/MSYS2)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
    if(SDL2_FOUND)
        # Use LINK_LIBRARIES for proper linking with pkg-config
        set(SDL2_LIBRARIES ${SDL2_LINK_LIBRARIES})
    endif()
endif()

# Fallback to find_package (works with NuGet on Windows/Visual Studio)
if(NOT SDL2_FOUND)
    find_package(SDL2 CONFIG QUIET)
    if(SDL2_FOUND)
        # NuGet/vcpkg style - SDL2::SDL2 target
        if(TARGET SDL2::SDL2)
            set(SDL2_LIBRARIES SDL2::SDL2)
            set(SDL2_INCLUDE_DIRS "")
        endif()
    endif()
endif()

# Last resort: manual search
if(NOT SDL2_FOUND)
    find_path(SDL2_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
    find_library(SDL2_LIBRARY NAMES SDL2)
    if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
        set(SDL2_FOUND TRUE)
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
    endif()
endif()

if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found. Please install SDL2 development libraries.")
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)

# Source files (explicit list matching VS project)
set(SOURCES
    # Main source files
    src/config.c
    src/glsl_shader.c
    src/main.c
    src/opengl.c
    src/sm_80.c
    src/sm_81.c
    src/sm_82.c
    src/sm_84.c
    src/sm_85.c
    src/sm_86.c
    src/sm_87.c
    src/sm_88.c
    src/sm_89.c
    src/sm_8b.c
    src/sm_8d.c
    src/sm_8f.c
    src/sm_90.c
    src/sm_91.c
    src/sm_92.c
    src/sm_93.c
    src/sm_94.c
    src/sm_9b.c
    src/sm_a0.c
    src/sm_a2.c
    src/sm_a3.c
    src/sm_a4.c
    src/sm_a5.c
    src/sm_a6.c
    src/sm_a7.c
    src/sm_a8.c
    src/sm_a9.c
    src/sm_aa.c
    src/sm_ad.c
    src/sm_b2.c
    src/sm_b3.c
    src/sm_b4.c
    src/sm_cpu_infra.c
    src/sm_rtl.c

    # SNES emulation layer
    src/snes/apu.c
    src/snes/cart.c
    src/snes/cpu.c
    src/snes/dma.c
    src/snes/dsp.c
    src/snes/input.c
    src/snes/ppu.c
    src/snes/snes.c
    src/snes/snes_other.c
    src/snes/spc.c

    # Utilities
    src/logging.c
    src/spc_player.c
    src/tracing.c
    src/util.c

    # Third-party dependencies
    third_party/gl_core/gl_core_3_1.c
)

# Platform-specific sources
if(WIN32)
    list(APPEND SOURCES src/platform/win32/volume_control.c)
endif()

# Create executable
add_executable(sm ${SOURCES})

# Include directories
target_include_directories(sm PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
)

# Compiler flags
if(MSVC)
    # Visual Studio flags
    target_compile_options(sm PRIVATE
        /W3                          # Warning level 3
        /wd4996                      # Disable warning C4996 (deprecated functions)
    )

    # Warnings as errors only on x64 Debug (matching VS project)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_options(sm PRIVATE /WX)
    endif()

    target_compile_definitions(sm PRIVATE
        _CRT_SECURE_NO_WARNINGS
    )
else()
    # GCC/Clang flags (Linux/macOS/MSYS2)
    target_compile_options(sm PRIVATE
        -Wall
        -Werror
        -fno-strict-aliasing
        -Wno-unknown-pragmas           # Ignore MSVC-specific #pragma directives
        -Wno-unused-variable           # Reverse-engineered code has unused vars
        -Wno-unused-function           # Static funcs in headers may not be used in all units
        -Wno-unused-but-set-variable   # Vars set but not read (decompilation artifact)
        -Wno-bitwise-op-parentheses    # Intentional operator precedence from original
        -Wno-logical-op-parentheses    # Intentional operator precedence from original
    )

    # Optimization for Release
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(sm PRIVATE -O2)
    endif()
endif()

# Platform-specific definitions
if(WIN32 AND NOT MSVC)
    # MSYS2/MinGW - volume mixer not available
    target_compile_definitions(sm PRIVATE SYSTEM_VOLUME_MIXER_AVAILABLE=0)
elseif(WIN32 AND MSVC)
    # Visual Studio - volume mixer available
    target_compile_definitions(sm PRIVATE SYSTEM_VOLUME_MIXER_AVAILABLE=1)
else()
    # Unix platforms - volume mixer not available
    target_compile_definitions(sm PRIVATE SYSTEM_VOLUME_MIXER_AVAILABLE=0)
endif()

# Per-file optimization settings (matching VS project Debug configuration)
# These files are performance-critical and need specific optimization levels
if(MSVC)
    # Disable optimization for SNES emulation files in Debug builds
    set_source_files_properties(
        src/snes/apu.c
        src/snes/cpu.c
        src/snes/dma.c
        src/snes/dsp.c
        src/snes/ppu.c
        PROPERTIES COMPILE_FLAGS "/Od"
    )

    # MinSpace optimization for cart/input/spc in Debug builds
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_source_files_properties(
            src/snes/cart.c
            src/snes/input.c
            src/snes/spc.c
            PROPERTIES COMPILE_FLAGS "/O1"
        )
    endif()
endif()

# Link libraries
target_link_libraries(sm PRIVATE
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
)

# Platform-specific link libraries
if(WIN32)
    if(MINGW)
        # MSYS2/MinGW specific
        target_link_libraries(sm PRIVATE mingw32)
        # Static linking with SDL2 on Windows MSYS2
        if(SDL2_STATIC_LIBRARIES)
            target_link_libraries(sm PRIVATE ${SDL2_STATIC_LIBRARIES})
        endif()
    endif()
else()
    # Unix platforms need libm
    target_link_libraries(sm PRIVATE m)
endif()

# Set output directory to match Makefile behavior (executable in root)
set_target_properties(sm PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}
)

# Installation (optional)
install(TARGETS sm RUNTIME DESTINATION bin)

# Print configuration summary
message(STATUS "=== Super Metroid Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "SDL2 found: ${SDL2_FOUND}")
if(SDL2_INCLUDE_DIRS)
    message(STATUS "SDL2 include: ${SDL2_INCLUDE_DIRS}")
endif()
if(SDL2_LIBRARIES)
    message(STATUS "SDL2 libraries: ${SDL2_LIBRARIES}")
endif()
message(STATUS "OpenGL libraries: ${OPENGL_LIBRARIES}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "========================================")
