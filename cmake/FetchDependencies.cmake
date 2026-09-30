include(FetchContent)

###################################### OpenH264 ######################################
# The H.264 decoder VideoReader reads H.264 tracks with.
#
# Only the source of OpenH264 is fetched, not its CMakeLists.txt: the library is built
# here by the Makefile the project ships, which is the build system its own authors
# use on Windows, and the one artifact that is needed is put where the imported target
# below points. Only the revision of the release is fetched: the history of the project
# is a decade of commits that nothing here reads.
FetchContent_Declare(openh264
  GIT_REPOSITORY https://github.com/cisco/openh264.git
  GIT_TAG        v2.6.0
  GIT_SHALLOW    TRUE
)

# Populate without add_subdirectory(): the CMakeLists.txt of OpenH264 is not used
FetchContent_GetProperties(openh264)
if(NOT openh264_POPULATED)
  FetchContent_Populate(openh264)
endif()

if(NOT CMAKE_GENERATOR MATCHES "Makefiles")
  message(FATAL_ERROR
    "The H.264 decoder (OpenH264) of VideoReader is built by a Makefile that needs GNU make "
    "and GCC or Clang, which the \"${CMAKE_GENERATOR}\" generator does not provide. "
    "Configure PixelImage with the gnu_debug or gnu_release preset instead.")
endif()

# The Makefile names the target system with one of its own values, and it reads it from
# uname when it is not told, which is not a name a build here can rely on
set(_openh264_platform)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_openh264_platform OS=mingw_nt ARCH=x86_64)
  else()
    set(_openh264_platform OS=mingw_nt ARCH=x86)
  endif()
endif()

# The assembly a release build uses needs NASM, which this toolchain does not carry, so
# the C sources of the decoder are what is built. The build type of the Makefile is what
# gives them their debug information or their optimization.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_openh264_build_type Debug)
else()
  set(_openh264_build_type Release)
endif()

set(OPENH264_INSTALL_DIR "${openh264_BINARY_DIR}/openh264-install")
set(OPENH264_INCLUDE_DIR "${OPENH264_INSTALL_DIR}/include")
set(OPENH264_LIBRARY "${OPENH264_INSTALL_DIR}/lib/libopenh264.a")

# The public headers are source files of the project: they are put where the imported
# target points at configure time, so that a source file which includes "codec_api.h"
# can be compiled no matter in which order the library itself is built.
file(MAKE_DIRECTORY "${OPENH264_INCLUDE_DIR}" "${OPENH264_INSTALL_DIR}/lib")
file(GLOB _openh264_headers "${openh264_SOURCE_DIR}/codec/api/wels/codec*.h")
file(COPY ${_openh264_headers} DESTINATION "${OPENH264_INCLUDE_DIR}")

# The Makefile builds the header that states the version of the library with a shell script, and
# the recipe that runs it calls "sh" by name, which a Windows prompt does not carry. The shell
# belongs to the environment the compiler comes from, which is the first place it is looked for,
# and its directory goes in front of the search path of that one command. The directory of the
# compiler comes with it, because the recipes call the compiler by name as well.
get_filename_component(_openh264_bin_dir "${CMAKE_MAKE_PROGRAM}" DIRECTORY)
find_program(OPENH264_SH NAMES sh)
if(NOT OPENH264_SH)
  get_filename_component(_openh264_toolchain_dir "${_openh264_bin_dir}" DIRECTORY)
  get_filename_component(_openh264_toolchain_dir "${_openh264_toolchain_dir}" DIRECTORY)
  find_program(OPENH264_SH NAMES sh PATHS "${_openh264_toolchain_dir}/usr/bin" NO_DEFAULT_PATH)
endif()

if(NOT OPENH264_SH)
  message(FATAL_ERROR
    "The Makefile of OpenH264 runs a shell script with \"sh\", which was not found. Install the "
    "shell of the toolchain that provides the compiler, which MSYS2 carries under usr/bin.")
endif()

get_filename_component(_openh264_sh_dir "${OPENH264_SH}" DIRECTORY)

# The library alone is what is asked for: the default goal of the Makefile also builds
# the programs of its tests and of its demos. The sources of the library are not listed
# as dependencies: they belong to the fetched revision, and the Makefile rebuilds them
# when this command runs.
add_custom_command(
  OUTPUT "${OPENH264_LIBRARY}"
  COMMAND "${CMAKE_COMMAND}" -E env
          "PATH=${_openh264_bin_dir};${_openh264_sh_dir};$ENV{PATH}"
          "${CMAKE_MAKE_PROGRAM}"
          ${_openh264_platform}
          BUILDTYPE=${_openh264_build_type} USE_ASM=No
          "CC=${CMAKE_C_COMPILER}"
          "CXX=${CMAKE_CXX_COMPILER}"
          libopenh264.a
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${openh264_SOURCE_DIR}/libopenh264.a" "${OPENH264_LIBRARY}"
  WORKING_DIRECTORY "${openh264_SOURCE_DIR}"
  DEPENDS "${openh264_SOURCE_DIR}/Makefile"
  COMMENT "Building OpenH264 (H.264 decoder)"
  VERBATIM
)

add_custom_target(openh264_ext DEPENDS "${OPENH264_LIBRARY}")

# The target carries the canonical namespaced name itself. A static library states its
# private dependencies to whoever links it, and the exported name is the name of the target,
# so the configuration file of an installed PixelImage has to state exactly this one as well
# (see cmake/PixelImageConfig.cmake.in).
add_library(openh264::openh264 STATIC IMPORTED GLOBAL)
add_dependencies(openh264::openh264 openh264_ext)
set_target_properties(openh264::openh264 PROPERTIES
  IMPORTED_LOCATION             "${OPENH264_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${OPENH264_INCLUDE_DIR}")
