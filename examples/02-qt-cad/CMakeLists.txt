# rgcad -- Qt Widgets 2D CAD, with 3D gated behind a RockyGuard feature flag.
#
# Qt6::Core, Gui and Widgets ONLY. All three offer LGPLv3. Deliberately absent:
# Qt Quick 3D, Qt Charts, Qt Data Visualization and Qt Graphs are all GPL-3.0-only
# in the open-source offering, and linking any of them alongside a proprietary SDK
# would be a violation. See THIRD-PARTY.md.
#
# No qt_* CMake helper functions: qt_standard_project_setup() needs Qt 6.3, and
# Ubuntu 22.04 ships 6.2.4. Plain add_executable plus AUTOMOC does everything a
# Widgets app with no QML and no .qrc needs.

set(CMAKE_AUTOMOC ON)

add_executable(rgcad
    main.cpp
    mainwindow.cpp
    mainwindow.h
    core/document.cpp
    core/document.h
    licensing/gate.cpp
    licensing/gate.h
    ui2d/canvas.cpp
    ui2d/canvas.h
    ui2d/commands.h
)

target_include_directories(rgcad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(rgcad PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    RockyGuard::RockyGuard
)

# WIN32 keeps a console from opening behind the GUI on Windows. Left off for
# Debug so qDebug output and the license diagnostics remain visible while
# integrating -- which is exactly when you want them.
set_target_properties(rgcad PROPERTIES
    WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
)

# The most common Qt-on-Windows first-run failure is a SUCCESSFUL build whose exe
# cannot find Qt6Widgets.dll. windeployqt copies what it needs next to the binary.
# Non-fatal when absent: this is a convenience for the developer, and copying Qt
# DLLs inside a local build tree is building, not conveying, so it raises no LGPL
# question.
if(WIN32)
    # Resolve Qt's bin/ at CONFIGURE time. A generator expression here would be
    # silently useless: find_program runs at configure time and does not expand
    # genexes, so CMake would search for a directory literally named
    # "$<TARGET_FILE_DIR:Qt6::qmake>", never match, and warn about nothing.
    # (The genexes in add_custom_command below ARE expanded, and are correct.)
    get_target_property(_rgx_qmake Qt6::qmake IMPORTED_LOCATION)
    if(_rgx_qmake)
        get_filename_component(_rgx_qt_bin "${_rgx_qmake}" DIRECTORY)
    endif()
    find_program(RGX_WINDEPLOYQT NAMES windeployqt
                 HINTS "${_rgx_qt_bin}" "${QT6_INSTALL_PREFIX}/bin")
    if(RGX_WINDEPLOYQT)
        add_custom_command(TARGET rgcad POST_BUILD
            COMMAND "${RGX_WINDEPLOYQT}" --no-translations --no-system-d3d-compiler
                    --no-opengl-sw "$<TARGET_FILE:rgcad>"
            COMMENT "windeployqt: staging Qt runtime next to rgcad"
            VERBATIM)
    else()
        message(STATUS "windeployqt not found; run rgcad from a shell with Qt's bin/ on PATH")
    endif()

    # Kept for a future shared-library SDK bundle. With the v1.3.2 layout,
    # cmake/RockyGuard.cmake links lib/static/*.lib only, so this glob is empty
    # and the block never fires -- documented so the next reader does not spend
    # time wondering why.
    if(NOT ROCKYGUARD_IS_STUB)
        file(GLOB _rg_dlls "${ROCKYGUARD_ROOT}/lib/shared/*.dll")
        if(_rg_dlls)
            add_custom_command(TARGET rgcad POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_rg_dlls}
                        "$<TARGET_FILE_DIR:rgcad>"
                COMMENT "Copying RockyGuard shared libraries next to rgcad"
                VERBATIM)
        endif()
    endif()
endif()

# Headless smoke test. QT_QPA_PLATFORM=offscreen means this needs no display and
# no xvfb, which is the main reason the 3D pane will use QPainter rather than
# OpenGL: this same test keeps working.
add_test(NAME rgcad_starts COMMAND rgcad)
set_tests_properties(rgcad_starts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RGCAD_SMOKE=1"
    TIMEOUT 30
)
