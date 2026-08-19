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

include(cmake/RockyGuard.cmake)

# MUST come before any add_subdirectory that calls add_test(). CMake silently
# ignores add_test() in directories processed before enable_testing(), so tests
# just vanish with no warning.
enable_testing()

option(RGX_BUILD_QT_CAD "Build the Qt CAD example (needs Qt 6 Widgets)" ON)

add_subdirectory(examples/01-minimal)

if(RGX_BUILD_QT_CAD)
    find_package(Qt6 6.2 COMPONENTS Core Gui Widgets QUIET)
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
        message(STATUS "Qt6 Widgets not found -- skipping the Qt CAD example.")
        message(STATUS "  Ubuntu/Debian: sudo apt install qt6-base-dev")
        message(STATUS "  Windows:       -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64")
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
