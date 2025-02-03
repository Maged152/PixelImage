cmake_minimum_required(VERSION 3.22)
project(PixelImage VERSION 0.0.1)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PARENT_PROJECT_NAME ${PROJECT_NAME})
    option(PixelImage_BUILD_EXAMPLES "Enable examples" ON)
endif()

# Configuration
set(version_config "${PROJECT_NAME}ConfigVersion.cmake")
set(project_config "${PROJECT_NAME}Config.cmake")
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
set(namespace "qlm::")
set(INSTALL_INC_DIR "include")
set(INSTALL_BIN_DIR "bin")
set(INSTALL_LIB_DIR "lib")
set(INSTALL_RUN_DIR "dll")
set(INSTALL_CFG_DIR "cmake")

# Source files
file(GLOB_RECURSE SOURCES source/*.cpp)

# Create the static library
add_library(${PROJECT_NAME} STATIC ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES 
                        CXX_STANDARD 20
                        CXX_EXTENSIONS OFF)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/dependencies>
    $<INSTALL_INTERFACE:${INSTALL_INC_DIR}>
)

add_library(qlm::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Check if examples are enabled
if(PixelImage_BUILD_EXAMPLES)
    message(STATUS "Examples for ${PROJECT_NAME} are enabled")
    add_subdirectory(examples)
endif()


# Install the library
install(TARGETS ${PROJECT_NAME}
    EXPORT "${TARGETS_EXPORT_NAME}"
    LIBRARY DESTINATION ${INSTALL_LIB_DIR}
    ARCHIVE DESTINATION ${INSTALL_LIB_DIR}
    RUNTIME DESTINATION ${INSTALL_RUN_DIR}
    INCLUDES DESTINATION ${INSTALL_INC_DIR}
)

install(EXPORT "${TARGETS_EXPORT_NAME}"
    NAMESPACE "${namespace}"
    DESTINATION "${INSTALL_CFG_DIR}"
)

install(
    DIRECTORY include/ dependencies/
    DESTINATION ${INSTALL_INC_DIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Create the package configuration file
include(CMakePackageConfigHelpers)
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${project_config}.in"
  "${CMAKE_CURRENT_BINARY_DIR}/${project_config}"
  INSTALL_DESTINATION "${INSTALL_CFG_DIR}"
)

# Write the library version file
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/${version_config}"
  COMPATIBILITY AnyNewerVersion
)

# Install configuration and version files
install(FILES 
    "${CMAKE_CURRENT_BINARY_DIR}/${version_config}"
    "${CMAKE_CURRENT_BINARY_DIR}/${project_config}"
    DESTINATION ${INSTALL_CFG_DIR}
)