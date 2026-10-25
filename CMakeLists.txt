cmake_minimum_required(VERSION 3.21)

# VERSION holds "vMAJOR.MINOR.PATCH", like sprat-cli.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SPRAT_LIB_VERSION_TAG)
# Re-run configure when VERSION changes, so sprat_version() never goes stale.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS VERSION)
string(STRIP "${SPRAT_LIB_VERSION_TAG}" SPRAT_LIB_VERSION_TAG)
string(REGEX REPLACE "^v" "" SPRAT_LIB_VERSION "${SPRAT_LIB_VERSION_TAG}")

project(sprat-lib VERSION ${SPRAT_LIB_VERSION} LANGUAGES C)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_SHARED_LIBS "Build sprat as a shared library" OFF)
option(SPRAT_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
option(SPRAT_SANITIZE "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)

if(SPRAT_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Keep sprat.dll next to the tests and examples so Windows can load it.
if(PROJECT_IS_TOP_LEVEL AND NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

add_library(sprat
    src/bind.c
    src/error.c
    src/memory.c
    src/reader.c
    src/sheet.c
)
add_library(sprat::sprat ALIAS sprat)

target_include_directories(sprat
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(sprat PRIVATE SPRAT_VERSION_STRING="${SPRAT_LIB_VERSION_TAG}")
set_target_properties(sprat PROPERTIES
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(sprat PUBLIC SPRAT_SHARED PRIVATE SPRAT_BUILDING)
endif()

if(MSVC)
    target_compile_options(sprat PRIVATE /W4)
    # fopen/snprintf are fine here; the "secure" variants are not portable.
    target_compile_definitions(sprat PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sprat PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
endif()

if(SPRAT_BUILD_EXAMPLES)
    add_executable(sprat-dump examples/sprat_dump.c)
    target_link_libraries(sprat-dump PRIVATE sprat::sprat)
endif()

include(CTest)
if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

install(TARGETS sprat EXPORT sprat-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT sprat-targets
    NAMESPACE sprat::
    FILE sprat-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sprat
)
# Lets find_package(sprat 0.4) accept any 0.x.y from 0.4 on; a new major is incompatible.
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/sprat-config-version.cmake
    COMPATIBILITY SameMajorVersion
)
install(FILES ${PROJECT_BINARY_DIR}/sprat-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sprat
)

# For builds without CMake: pkg-config --cflags --libs sprat
if(BUILD_SHARED_LIBS)
    set(SPRAT_PC_CFLAGS "-DSPRAT_SHARED")
endif()
configure_file(sprat.pc.in ${PROJECT_BINARY_DIR}/sprat.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/sprat.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
