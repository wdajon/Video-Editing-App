# Resolves the git revision at configure time. A source tree exported without
# git metadata must still build, so absence of git is not an error: the revision
# degrades to "unknown" and the build is explicitly marked as unreproducible.

function(rf_resolve_git_revision out_revision out_dirty)
    set(_rev "unknown")
    set(_dirty "")

    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE _rev_out
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _rev_result)
        if(_rev_result EQUAL 0 AND _rev_out)
            set(_rev "${_rev_out}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            OUTPUT_VARIABLE _status_out
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _status_result)
        if(_status_result EQUAL 0 AND NOT _status_out STREQUAL "")
            set(_dirty "+dirty")
        endif()
    endif()

    set(${out_revision} "${_rev}" PARENT_SCOPE)
    set(${out_dirty} "${_dirty}" PARENT_SCOPE)
endfunction()
