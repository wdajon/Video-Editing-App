# Defines rf_sanitizers, an INTERFACE target that is empty unless a sanitizer
# option is on. ASan and TSan are mutually exclusive by construction.

add_library(rf_sanitizers INTERFACE)
add_library(rf::sanitizers ALIAS rf_sanitizers)

if(RF_ENABLE_ASAN AND RF_ENABLE_TSAN)
    message(FATAL_ERROR
        "RF_ENABLE_ASAN and RF_ENABLE_TSAN cannot be enabled together; "
        "AddressSanitizer and ThreadSanitizer are incompatible runtimes.")
endif()

set(_rf_san_list "")

if(RF_ENABLE_ASAN)
    list(APPEND _rf_san_list address)
endif()
if(RF_ENABLE_TSAN)
    list(APPEND _rf_san_list thread)
endif()
if(RF_ENABLE_UBSAN)
    if(MSVC)
        message(FATAL_ERROR "RF_ENABLE_UBSAN is not supported by MSVC; use the Clang or GCC presets.")
    endif()
    list(APPEND _rf_san_list undefined)
endif()

if(_rf_san_list)
    list(JOIN _rf_san_list "," _rf_san_flags)
    if(MSVC)
        # MSVC ships ASan only, and it is incompatible with the /RTC and
        # incremental-link defaults, so strip them here rather than in presets.
        target_compile_options(rf_sanitizers INTERFACE /fsanitize=${_rf_san_flags})
        target_link_options(rf_sanitizers INTERFACE /INCREMENTAL:NO)
    else()
        target_compile_options(rf_sanitizers INTERFACE
            -fsanitize=${_rf_san_flags}
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
            -g)
        target_link_options(rf_sanitizers INTERFACE -fsanitize=${_rf_san_flags})
    endif()
    message(STATUS "ReelForge sanitizers enabled: ${_rf_san_flags}")
endif()

unset(_rf_san_list)
unset(_rf_san_flags)
