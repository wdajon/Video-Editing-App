# rf_deploy_qt(<target>)
#
# Places the Qt runtime next to <target> so the binary is runnable straight from
# the build tree. Without this, an executable that links a shared Qt build links
# fine and then fails to start, which is a worse failure than not building.
#
# Call this for AT MOST ONE target per output directory. windeployqt copies into
# the directory containing the target, so two targets in the same directory each
# carrying a deployment step will race under a parallel build and fail with
# "Existing file ... is not writable". Other targets sharing the directory should
# take a build dependency on the deployed one instead.
#
# On non-Windows platforms CMake's build-tree RPATH already resolves Qt, so this
# is a no-op there.

# rf_deploy_qt_offscreen_platform(<target>)
#
# windeployqt deploys only the platform plugin the application would use
# interactively (qwindows), so a widget test binary asking for QT_QPA_PLATFORM=
# offscreen aborts at startup with exit code 3 and no assertion output. Copying
# the offscreen plugin explicitly is what makes headless widget tests possible.
#
# The plugin is located through Qt's imported target rather than by path, so
# this stays correct across configurations and Qt installations.
function(rf_deploy_qt_offscreen_platform target)
    if(NOT WIN32)
        # Elsewhere Qt resolves plugins relative to the library location.
        return()
    endif()

    if(NOT TARGET Qt6::QOffscreenIntegrationPlugin)
        message(FATAL_ERROR
            "Qt6::QOffscreenIntegrationPlugin is not available, so widget tests "
            "cannot run without a display server. Reinstall Qt with the default "
            "platform plugins (see scripts/install_qt.ps1).")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${target}>/platforms"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>"
                "$<TARGET_FILE_DIR:${target}>/platforms/"
        COMMENT "Deploying Qt offscreen platform plugin for ${target}"
        VERBATIM)
endfunction()

function(rf_deploy_qt target)
    if(NOT WIN32)
        return()
    endif()

    if(NOT RF_WINDEPLOYQT_EXECUTABLE)
        find_program(RF_WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            HINTS "${QT6_INSTALL_PREFIX}" "$ENV{QT_ROOT}"
            PATH_SUFFIXES bin)
    endif()

    if(NOT RF_WINDEPLOYQT_EXECUTABLE)
        message(FATAL_ERROR
            "windeployqt was not found. Set QT_ROOT to the Qt installation "
            "(see scripts/install_qt.ps1); without it the built executables "
            "cannot start.")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${RF_WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:${target}>"
        COMMENT "Deploying Qt runtime next to ${target}"
        VERBATIM)
endfunction()
