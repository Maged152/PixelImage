cmake_minimum_required(VERSION 3.18)
include(FetchContent)
include(ProcessorCount)

# ==============================================================================
# OpenH264
#
# Builds Cisco OpenH264 (encoder + decoder) as a static library with its native
# Makefile and exposes it to the project as:
#
#     openh264::openh264
#
# Supported targets:
#   - Linux   : x86, x86_64, arm, arm64            (GCC or Clang)
#   - Windows : x86, x86_64 via MinGW/MSYS2        (GCC or Clang, not MSVC/clang-cl)
#
# Options:
#   OPENH264_VERSION   Version tag to fetch (default 2.6.0)
#   OPENH264_USE_ASM   AUTO (default) | ON | OFF
#                        AUTO: use assembly on x86/x86_64 when NASM is found
#                        ON  : require assembly (NASM is required on x86/x86_64)
#                        OFF : build the plain C sources
#
# Requires CMake >= 3.18. The enclosing project must enable C and CXX.
# ==============================================================================

set(OPENH264_VERSION "2.6.0" CACHE STRING "OpenH264 version")
set(OPENH264_USE_ASM "AUTO" CACHE STRING "Assembly optimizations: AUTO, ON or OFF")
set_property(CACHE OPENH264_USE_ASM PROPERTY STRINGS AUTO ON OFF)


# ------------------------------------------------------------------------------
# Validation first, so nothing is downloaded when the environment cannot build it
# ------------------------------------------------------------------------------

# Target operating system
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(_openh264_os "mingw_nt")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_openh264_os "linux")
else()
    message(FATAL_ERROR
        "OpenH264 integration supports only Windows (MinGW) and Linux. "
        "Detected platform: ${CMAKE_SYSTEM_NAME}")
endif()

# Compilers
if(MSVC)
    message(FATAL_ERROR
        "OpenH264 is built by a Makefile that needs GNU make and GCC or Clang, which an "
        "MSVC-style compiler (cl, clang-cl) does not provide. Configure with the "
        "gnu_debug or gnu_release preset instead.")
endif()

foreach(_lang C CXX)
    if(NOT CMAKE_${_lang}_COMPILER)
        message(FATAL_ERROR
            "A ${_lang} compiler is required to build OpenH264. "
            "Enable it with project(... LANGUAGES C CXX).")
    endif()
endforeach()

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
        "OpenH264 integration expects GCC or Clang.\n"
        "Detected compiler: ${CMAKE_C_COMPILER_ID} (${CMAKE_C_COMPILER})")
endif()

# Target architecture. CMAKE_SYSTEM_PROCESSOR can describe the host rather than the
# target (for example a 32-bit MinGW on 64-bit Windows), so it is checked against the
# pointer size of the compiler.
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _openh264_processor)

if(_openh264_processor MATCHES "^(x86_64|amd64|x64)$")
    set(_openh264_arch "x86_64")
elseif(_openh264_processor MATCHES "^(x86|i[3-6]86)$")
    set(_openh264_arch "x86")
elseif(_openh264_processor MATCHES "^(aarch64|arm64)$")
    set(_openh264_arch "arm64")
elseif(_openh264_processor MATCHES "^arm")
    set(_openh264_arch "arm")
else()
    message(FATAL_ERROR
        "Unsupported OpenH264 architecture: CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}'\n"
        "Supported: x86, x86_64, arm, arm64")
endif()

if(_openh264_arch STREQUAL "x86_64" AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_openh264_arch "x86")
elseif(_openh264_arch STREQUAL "x86" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_openh264_arch "x86_64")
endif()

if(_openh264_os STREQUAL "mingw_nt" AND _openh264_arch MATCHES "^arm")
    message(WARNING
        "OpenH264 on Windows/${_openh264_arch} has not been verified with this module.")
endif()


# ------------------------------------------------------------------------------
# GNU make
#
# Any CMake generator works: make is only driven as an external command. With a
# Makefile generator its own make is reused, otherwise one is looked up.
# ------------------------------------------------------------------------------

get_filename_component(_openh264_gen_make_name "${CMAKE_MAKE_PROGRAM}" NAME_WE)

if(CMAKE_GENERATOR MATCHES "Makefiles" AND _openh264_gen_make_name MATCHES "^(mingw32-|g)?make$")
    set(_openh264_make "${CMAKE_MAKE_PROGRAM}")
else()
    if(CMAKE_HOST_WIN32)
        find_program(OPENH264_MAKE NAMES mingw32-make.exe make.exe mingw32-make make)
    else()
        find_program(OPENH264_MAKE NAMES make gmake)
    endif()

    if(NOT OPENH264_MAKE)
        message(FATAL_ERROR
            "GNU make was not found.\n"
            "OpenH264 is built with its native Makefile.\n"
            "Windows: install MinGW/MSYS2 and make sure mingw32-make is available.\n"
            "Linux: install GNU make (for example: sudo apt install make).")
    endif()
    set(_openh264_make "${OPENH264_MAKE}")
endif()


# ------------------------------------------------------------------------------
# POSIX shell
#
# The Makefile generates the version header with a shell script and calls "sh"
# by name. Normally /bin/sh on Linux; on Windows it comes from MSYS2.
# ------------------------------------------------------------------------------

if(CMAKE_HOST_WIN32)
    # Prefer the shell of the toolchain that provides make and the compiler: an
    # unrelated sh.exe found earlier on PATH (for example Git's) must not be mixed
    # with an MSYS2 compiler. Typical MSYS2 layout:
    #   C:/msys64/usr/bin/sh.exe
    #   C:/msys64/mingw64/bin/gcc.exe, mingw32-make.exe
    set(_openh264_sh_hints)
    foreach(_openh264_tool "${_openh264_make}" "${CMAKE_C_COMPILER}")
        get_filename_component(_openh264_tool_dir  "${_openh264_tool}"     DIRECTORY)
        get_filename_component(_openh264_tool_root "${_openh264_tool_dir}" DIRECTORY)
        get_filename_component(_openh264_tool_root "${_openh264_tool_root}" DIRECTORY)
        list(APPEND _openh264_sh_hints "${_openh264_tool_dir}" "${_openh264_tool_root}/usr/bin")
    endforeach()

    find_program(OPENH264_SH NAMES sh.exe sh HINTS ${_openh264_sh_hints} NO_DEFAULT_PATH)
endif()

# Linux, or a Windows toolchain that carries no shell of its own: whatever is on PATH.
find_program(OPENH264_SH NAMES sh sh.exe)

if(NOT OPENH264_SH)
    message(FATAL_ERROR
        "A POSIX shell (sh) was not found.\n"
        "OpenH264's Makefile requires sh.\n"
        "Windows/MSYS2: make sure usr/bin/sh.exe of the toolchain is installed.")
endif()


# ------------------------------------------------------------------------------
# Assembly
#
# x86/x86_64 assembly needs NASM. On ARM the assembler is the compiler's own, so
# no extra tool is needed there.
# ------------------------------------------------------------------------------

string(TOUPPER "${OPENH264_USE_ASM}" _openh264_asm_mode)
if(_openh264_asm_mode STREQUAL "AUTO")
    set(_openh264_asm_want AUTO)
elseif(_openh264_asm_mode MATCHES "^(ON|TRUE|YES|Y|1)$")
    set(_openh264_asm_want ON)
elseif(_openh264_asm_mode MATCHES "^(OFF|FALSE|NO|N|0)$")
    set(_openh264_asm_want OFF)
else()
    message(FATAL_ERROR
        "Invalid OPENH264_USE_ASM='${OPENH264_USE_ASM}'. Expected AUTO, ON or OFF.")
endif()

set(_openh264_use_asm No)

if(_openh264_arch MATCHES "^x86")
    find_program(OPENH264_NASM NAMES nasm nasm.exe)

    if(_openh264_asm_want STREQUAL "ON" AND NOT OPENH264_NASM)
        message(FATAL_ERROR "OPENH264_USE_ASM=ON, but NASM was not found.")
    endif()

    if(OPENH264_NASM AND NOT _openh264_asm_want STREQUAL "OFF")
        set(_openh264_use_asm Yes)
    endif()
elseif(_openh264_asm_want STREQUAL "ON")
    set(_openh264_use_asm Yes)
endif()


# ------------------------------------------------------------------------------
# Fetch OpenH264
# ------------------------------------------------------------------------------

FetchContent_Declare(openh264
    GIT_REPOSITORY https://github.com/cisco/openh264.git
    GIT_TAG        "v${OPENH264_VERSION}"
    GIT_SHALLOW    TRUE
    # Upstream has no CMakeLists.txt. A subdirectory that does not exist makes
    # FetchContent_MakeAvailable() download the source without add_subdirectory().
    SOURCE_SUBDIR  _no_cmake_here
)
FetchContent_MakeAvailable(openh264)


# ------------------------------------------------------------------------------
# Build settings
# ------------------------------------------------------------------------------

# The Makefile knows Debug and Release; every non-Debug type maps to Release.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_openh264_build_type Debug)
else()
    set(_openh264_build_type Release)
endif()

ProcessorCount(_openh264_jobs)
if(_openh264_jobs EQUAL 0)
    set(_openh264_jobs 1)
endif()

set(_openh264_make_args
    "OS=${_openh264_os}"
    "ARCH=${_openh264_arch}"
    "BUILDTYPE=${_openh264_build_type}"
    "USE_ASM=${_openh264_use_asm}"
    "CC=${CMAKE_C_COMPILER}"
    "CXX=${CMAKE_CXX_COMPILER}")

if(CMAKE_AR)
    list(APPEND _openh264_make_args "AR=${CMAKE_AR}")
endif()


# ------------------------------------------------------------------------------
# Output locations and public headers
# ------------------------------------------------------------------------------

set(OPENH264_INSTALL_DIR "${openh264_BINARY_DIR}/openh264-install")
set(OPENH264_INCLUDE_DIR "${OPENH264_INSTALL_DIR}/include")
set(OPENH264_LIBRARY     "${OPENH264_INSTALL_DIR}/lib/libopenh264.a")

# Headers are copied at configure time so the imported target's include directory
# exists and sources including "codec_api.h" compile in any build order.
file(MAKE_DIRECTORY "${OPENH264_INCLUDE_DIR}" "${OPENH264_INSTALL_DIR}/lib")

foreach(_header codec_api.h codec_app_def.h codec_def.h)
    if(NOT EXISTS "${openh264_SOURCE_DIR}/codec/api/wels/${_header}")
        message(FATAL_ERROR
            "OpenH264 public header was not found:\n"
            "${openh264_SOURCE_DIR}/codec/api/wels/${_header}")
    endif()
endforeach()

file(GLOB _openh264_headers "${openh264_SOURCE_DIR}/codec/api/wels/codec*.h")
file(COPY ${_openh264_headers} DESTINATION "${OPENH264_INCLUDE_DIR}")


# ------------------------------------------------------------------------------
# Stamp
#
# The Makefile builds inside the source directory and does not notice changed
# settings. A stamp that records them (rewritten only when its content changes)
# triggers a clean rebuild when any of them changes.
# ------------------------------------------------------------------------------

set(_openh264_stamp "${openh264_BINARY_DIR}/openh264-settings.stamp")
file(CONFIGURE OUTPUT "${_openh264_stamp}"
    CONTENT "version=${OPENH264_VERSION}\nos=${_openh264_os}\narch=${_openh264_arch}\nbuild_type=${_openh264_build_type}\nasm=${_openh264_use_asm}\ncc=${CMAKE_C_COMPILER}\ncxx=${CMAKE_CXX_COMPILER}\nar=${CMAKE_AR}\nmake=${_openh264_make}\n"
    @ONLY)


# ------------------------------------------------------------------------------
# Environment (Windows hosts only)
#
# The Makefile calls sh and the compiler by name, which a plain Windows prompt cannot
# resolve. Their directories go in front of PATH for this one command only, so the rest
# of the build environment is untouched. On Linux nothing is changed.
# (Semicolons in the value are written as $<SEMICOLON>.)
# ------------------------------------------------------------------------------

set(_openh264_env)

if(CMAKE_HOST_WIN32)
    get_filename_component(_openh264_make_dir "${_openh264_make}"      DIRECTORY)
    get_filename_component(_openh264_sh_dir   "${OPENH264_SH}"         DIRECTORY)
    get_filename_component(_openh264_cc_dir   "${CMAKE_C_COMPILER}"    DIRECTORY)

    set(_openh264_path_dirs "${_openh264_make_dir}" "${_openh264_sh_dir}" "${_openh264_cc_dir}")
    if(OPENH264_NASM)
        get_filename_component(_openh264_nasm_dir "${OPENH264_NASM}" DIRECTORY)
        list(APPEND _openh264_path_dirs "${_openh264_nasm_dir}")
    endif()
    list(REMOVE_DUPLICATES _openh264_path_dirs)

    list(JOIN _openh264_path_dirs "$<SEMICOLON>" _openh264_path_prefix)
    string(REPLACE ";" "$<SEMICOLON>" _openh264_old_path "$ENV{PATH}")
    set(_openh264_env "PATH=${_openh264_path_prefix}$<SEMICOLON>${_openh264_old_path}")
endif()

set(_openh264_run "${CMAKE_COMMAND}" -E env ${_openh264_env} "${_openh264_make}")


# ------------------------------------------------------------------------------
# Build the library only (the default goal also builds tests and demos).
# Sources are not listed as dependencies: they belong to the pinned revision.
# ------------------------------------------------------------------------------

add_custom_command(
    OUTPUT "${OPENH264_LIBRARY}"

    COMMAND ${_openh264_run} OS=${_openh264_os} ARCH=${_openh264_arch} clean

    COMMAND ${_openh264_run} -j${_openh264_jobs}
            ${_openh264_make_args}
            libopenh264.a

    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${openh264_SOURCE_DIR}/libopenh264.a" "${OPENH264_LIBRARY}"

    WORKING_DIRECTORY "${openh264_SOURCE_DIR}"
    DEPENDS "${openh264_SOURCE_DIR}/Makefile" "${_openh264_stamp}"
    COMMENT "Building OpenH264 ${OPENH264_VERSION} (${_openh264_os}/${_openh264_arch})"
    VERBATIM
)

add_custom_target(openh264_ext DEPENDS "${OPENH264_LIBRARY}")


# ------------------------------------------------------------------------------
# Imported target
#
# Consumers only need:
#     target_link_libraries(my_target PRIVATE openh264::openh264)
#
# The target carries the canonical namespaced name itself; the configuration file of
# an installed PixelImage must state exactly this one (see
# cmake/PixelImageConfig.cmake.in, which also needs find_dependency(Threads) on
# non-Windows targets).
# ------------------------------------------------------------------------------

add_library(openh264::openh264 STATIC IMPORTED GLOBAL)
add_dependencies(openh264::openh264 openh264_ext)

set_target_properties(openh264::openh264 PROPERTIES
    IMPORTED_LOCATION                 "${OPENH264_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES     "${OPENH264_INCLUDE_DIR}"
    IMPORTED_LINK_INTERFACE_LANGUAGES CXX)   # the library is C++: link with the C++ runtime

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
    find_package(Threads REQUIRED)
    set_property(TARGET openh264::openh264 PROPERTY INTERFACE_LINK_LIBRARIES Threads::Threads)
endif()


# ------------------------------------------------------------------------------
# Diagnostics
# ------------------------------------------------------------------------------

message(STATUS
    "OpenH264 ${OPENH264_VERSION}: OS=${_openh264_os}, ARCH=${_openh264_arch}, "
    "BUILD=${_openh264_build_type}, ASM=${_openh264_use_asm}, JOBS=${_openh264_jobs}")
message(STATUS "OpenH264 tools: make=${_openh264_make}, sh=${OPENH264_SH}")

# Only OPENH264_VERSION and OPENH264_USE_ASM are meant to be configured by users.
foreach(_openh264_var OPENH264_MAKE OPENH264_SH OPENH264_NASM)
    if(DEFINED CACHE{${_openh264_var}})
        mark_as_advanced(${_openh264_var})
    endif()
endforeach()