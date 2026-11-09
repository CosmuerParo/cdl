cmake_minimum_required(VERSION 3.16)

project(cdl
    VERSION 1.0.0
    DESCRIPTION "A small cross-platform C wrapper for libdl and the Windows loader"
    LANGUAGES C)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(CDL_IS_TOP_LEVEL ON)
else()
    set(CDL_IS_TOP_LEVEL OFF)
endif()

option(CDL_BUILD_SHARED "Build cdl as a shared library" ON)
option(CDL_BUILD_TESTS "Build the cdl test suite" ${CDL_IS_TOP_LEVEL})
option(CDL_BUILD_EXAMPLES "Build the cdl examples" ${CDL_IS_TOP_LEVEL})
option(CDL_INSTALL "Generate install and package rules" ${CDL_IS_TOP_LEVEL})

set(CDL_SOURCES
    include/cdl/cdl.h
    src/cdl.c
    src/cdl_internal.h)

if(WIN32)
    list(APPEND CDL_SOURCES src/cdl_windows.c)
elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    list(APPEND CDL_SOURCES src/cdl_posix.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CDL_SOURCES src/cdl_posix.c)
else()
    message(FATAL_ERROR
        "cdl supports Windows, Linux and Android only "
        "(CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}')")
endif()

if(CDL_BUILD_SHARED)
    add_library(cdl SHARED ${CDL_SOURCES})
    target_compile_definitions(cdl PRIVATE CDL_BUILDING)
    if(NOT WIN32)
        set_target_properties(cdl PROPERTIES
            VERSION ${PROJECT_VERSION}
            SOVERSION ${PROJECT_VERSION_MAJOR})
    endif()
else()
    add_library(cdl STATIC ${CDL_SOURCES})
    # Windows consumers of a static library must not see dllimport.
    target_compile_definitions(cdl PUBLIC CDL_STATIC)
endif()

add_library(cdl::cdl ALIAS cdl)

set_target_properties(cdl PROPERTIES
    OUTPUT_NAME cdl
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)

if(CDL_BUILD_SHARED AND NOT MSVC)
    set_target_properties(cdl PROPERTIES C_VISIBILITY_PRESET hidden)
endif()

target_include_directories(cdl
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# On Linux/Android this is "dl" (a stub library on modern Android).  PUBLIC so
# that static-library consumers also inherit it.
if(CMAKE_DL_LIBS)
    target_link_libraries(cdl PUBLIC ${CMAKE_DL_LIBS})
endif()

function(cdl_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

cdl_enable_warnings(cdl)

if(CDL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CDL_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CDL_INSTALL)
    set(CDL_CMAKE_INSTALL_DIR
        "${CMAKE_INSTALL_LIBDIR}/cmake/cdl")

    install(TARGETS cdl
        EXPORT cdlTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT cdlTargets
        FILE cdlTargets.cmake
        NAMESPACE cdl::
        DESTINATION ${CDL_CMAKE_INSTALL_DIR})

    configure_package_config_file(
        cmake/cdlConfig.cmake.in
        "${CMAKE_CURRENT_BINARY_DIR}/cdlConfig.cmake"
        INSTALL_DESTINATION ${CDL_CMAKE_INSTALL_DIR})

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/cdlConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/cdlConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cdlConfigVersion.cmake"
        DESTINATION ${CDL_CMAKE_INSTALL_DIR})

    set(CDL_PC_LIBS_PRIVATE "")
    if(CMAKE_DL_LIBS)
        set(CDL_PC_LIBS_PRIVATE "-l${CMAKE_DL_LIBS}")
    endif()

    configure_file(
        cmake/cdl.pc.in
        "${CMAKE_CURRENT_BINARY_DIR}/cdl.pc"
        @ONLY)

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cdl.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

    install(FILES README.md LICENSE
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
    install(DIRECTORY docs/
        DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs")
endif()
