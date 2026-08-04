# Defines rf_warnings, an INTERFACE target carrying the project warning policy.
# Every ReelForge target links it; third-party code never does.

add_library(rf_warnings INTERFACE)
add_library(rf::warnings ALIAS rf_warnings)

if(MSVC)
    target_compile_options(rf_warnings INTERFACE
        /W4
        /permissive-        # conforming mode; two-phase lookup
        /Zc:__cplusplus     # report the real __cplusplus value
        /Zc:preprocessor    # conforming preprocessor
        /Zc:inline
        /utf-8
        /EHsc
        /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296
        /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619
        /w14640 /w14826 /w14905 /w14906 /w14928)
    if(RF_WARNINGS_AS_ERRORS)
        target_compile_options(rf_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(rf_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wdouble-promotion
        -Wnull-dereference
        -Wformat=2)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(rf_warnings INTERFACE
            -Wmisleading-indentation
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
            -Wuseless-cast)
    endif()
    if(RF_WARNINGS_AS_ERRORS)
        target_compile_options(rf_warnings INTERFACE -Werror)
    endif()
endif()
