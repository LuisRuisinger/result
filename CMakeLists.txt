cmake_minimum_required(VERSION 3.16)

project(result
        VERSION 0.1.0
        LANGUAGES CXX
)

# ================================================================================================
# Library
# ================================================================================================

add_library(result INTERFACE)
add_library(lsr::result ALIAS result)

include(GNUInstallDirs)

target_include_directories(result
        INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(result INTERFACE cxx_std_20)

# ================================================================================================
# Options
# ================================================================================================

option(RESULT_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
option(RESULT_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# ================================================================================================
# clang-tidy
# ================================================================================================

if (RESULT_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
endif ()

# ================================================================================================
# Tests
# ================================================================================================

include(CTest)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()

# ================================================================================================
# Install / package config
# ================================================================================================

include(CMakePackageConfigHelpers)

install(TARGETS result
        EXPORT result_targets
)

install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT result_targets
        FILE result_targets.cmake
        NAMESPACE lsr::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/result
)

configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/resultConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/resultConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/result
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/resultConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/resultConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/resultConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/result
)