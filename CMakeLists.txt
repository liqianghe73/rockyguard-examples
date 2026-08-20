# rockyguard-examples -- top-level build
#
# 3.19 is the floor on purpose: it is what Ubuntu 22.04 ships (3.22) minus
# headroom, and it lets us avoid the qt_* CMake helpers entirely. Those need Qt
# 6.3+, while Ubuntu 22.04's system Qt is 6.2.4 -- and for a Widgets app with no
# QML and no .qrc they buy nothing.
cmake_minimum_required(VERSION 3.19)

project(rockyguard_examples
        VERSION 0.1.0
        DESCRIPTION "Worked examples of RockyGuard license gating"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators default to an empty build type, which silently means
# no optimisation and no NDEBUG. Pick one so a bare `cmake -B build` is sane.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Warnings on. Verified against GCC 11 / Qt 6.2.4 and MSVC 19.44: the whole
# tree builds with exactly zero warnings, so there is no noise budget to spend
# and a new warning means something real.
#
# NOT -Werror / /WX, deliberately. A future compiler inventing a new warning
# would break the build for someone merely trying to evaluate RockyGuard, and
# an example repo that will not compile is worse than one that is chatty. CI
# greps the logs instead.
#
# Qt and OpenSSL headers arrive through IMPORTED targets, which CMake treats as
# SYSTEM includes, so third-party headers stay quiet on their own.
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

include(cmake/RockyGuard.cmake)

# MUST come before any add_subdirectory that calls add_test(). CMake silently
# ignores add_test() in directories processed before enable_testing(), so tests
# just vanish with no warning.
enable_testing()

# Look for Qt up front, not inside the RGX_BUILD_QT_CAD branch. tests/ needs
# Qt6::Core for the geometry tests, and add_subdirectory(tests) runs after the
# branch -- so a find_package hidden inside it would leave Qt6::Core undefined
# there and the tests would silently not be built.
find_package(Qt6 6.2 COMPONENTS Core Gui Widgets QUIET)

option(RGX_BUILD_QT_CAD "Build the Qt CAD example (needs Qt 6 Widgets)" ON)
# CI sets this. A missing Qt makes the CAD example SKIP, which means a missing
# dependency would show up as a GREEN build that compiled none of it -- worse
# than a red one. Turning the skip into a hard error is the only way the
# required check can be trusted.
option(RGX_REQUIRE_QT "Fail configuration if Qt 6 Widgets is not found" OFF)

add_subdirectory(examples/01-minimal)

if(RGX_BUILD_QT_CAD)
    if(Qt6_FOUND)
        # LGPLv3 permits linking a proprietary application against Qt only while
        # the app remains a "work that uses the Library". Qt's own legal guidance
        # warns that static linking can dissolve that separation and pull the
        # application under the LGPL -- which would be fatal for a repo that links
        # a proprietary SDK. So refuse to configure against a static Qt rather
        # than produce a binary nobody may distribute.
        get_target_property(_qt_core_type Qt6::Core TYPE)
        if(_qt_core_type STREQUAL "STATIC_LIBRARY")
            message(FATAL_ERROR
                "This Qt is a STATIC build. Refusing to configure.\n"
                "These examples link the proprietary RockyGuard SDK, and static "
                "Qt linking puts the LGPLv3 'work that uses the Library' "
                "separation in doubt. Use a shared/dynamic Qt 6.")
        endif()
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/examples/02-qt-cad/CMakeLists.txt")
            add_subdirectory(examples/02-qt-cad)
        else()
            message(STATUS "examples/02-qt-cad not present yet -- skipping.")
        endif()
    else()
        set(_rgx_qt_help
            "  Ubuntu/Debian: sudo apt install qt6-base-dev libgl-dev\n"
            "                 (libgl-dev is REQUIRED -- Qt6Gui runs\n"
            "                 find_package(OpenGL) and needs GL/gl.h, which\n"
            "                 qt6-base-dev does not pull in. Look above for\n"
            "                 'Could NOT find OpenGL'.)\n"
            "  Windows:       -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64")
        if(RGX_REQUIRE_QT)
            message(FATAL_ERROR "Qt6 Widgets not found, and RGX_REQUIRE_QT is ON.\n"
                                ${_rgx_qt_help})
        endif()
        message(STATUS "Qt6 Widgets not found -- skipping the Qt CAD example.")
        message(STATUS ${_rgx_qt_help})
    endif()
endif()

add_subdirectory(tests)

# Plain if/else, not a generator expression: message() is evaluated at
# configure time and would print the $<...> text verbatim.
if(ROCKYGUARD_IS_STUB)
    set(_rg_kind "API stub (no license can be verified)")
else()
    set(_rg_kind "SDK at ${ROCKYGUARD_ROOT}")
endif()

message(STATUS "")
message(STATUS "rockyguard-examples ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  RockyGuard : ${_rg_kind}")
message(STATUS "")
