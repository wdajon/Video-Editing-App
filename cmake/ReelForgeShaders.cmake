# rf_add_shader(<target> SOURCE <file.comp> SYMBOL <identifier>)
#
# Compiles a GLSL shader to SPIR-V at build time and embeds it as a word array
# in a generated header. Nothing is loaded from disk at runtime.
#
# Compiling offline rather than at startup means a shader that fails to compile
# breaks the build, not the user's first playback. Embedding rather than
# shipping .spv files alongside the binary means there is no search path to get
# wrong, no partial install, and no way for a stale shader to sit next to a new
# executable.

# The tool lands under <installed>/<host triplet>/tools/glslang, but which of
# vcpkg's several path variables is set depends on how the project was
# configured -- passing VCPKG_INSTALLED_DIR explicitly, as the space-in-path
# workaround requires, leaves VCPKG_HOST_TRIPLET undefined. So gather every
# plausible root and glob for the triplet directory rather than guess one.
set(_rf_shader_hints "")
foreach(_root "${VCPKG_INSTALLED_DIR}" "${_VCPKG_INSTALLED_DIR}"
              "${CMAKE_BINARY_DIR}/vcpkg_installed" "${PROJECT_SOURCE_DIR}/vcpkg_installed")
    if(_root AND IS_DIRECTORY "${_root}")
        foreach(_triplet "${VCPKG_HOST_TRIPLET}" "${VCPKG_TARGET_TRIPLET}")
            if(_triplet)
                list(APPEND _rf_shader_hints "${_root}/${_triplet}/tools/glslang")
            endif()
        endforeach()
        file(GLOB _rf_globbed "${_root}/*/tools/glslang")
        list(APPEND _rf_shader_hints ${_rf_globbed})
    endif()
endforeach()

find_program(RF_GLSLANG_VALIDATOR
    NAMES glslangValidator glslang
    HINTS ${_rf_shader_hints}
    DOC "GLSL to SPIR-V compiler from the glslang port")
unset(_rf_shader_hints)
unset(_rf_globbed)

if(NOT RF_GLSLANG_VALIDATOR)
    message(FATAL_ERROR
        "glslangValidator was not found. It comes from the vcpkg 'glslang' port with the "
        "'tools' feature, declared as a host dependency in vcpkg.json. If you configured "
        "with a custom VCPKG_INSTALLED_DIR, pass -DRF_GLSLANG_VALIDATOR=<path>.")
endif()

function(rf_add_shader target)
    cmake_parse_arguments(RF_SHADER "" "SOURCE;SYMBOL" "" ${ARGN})
    if(NOT RF_SHADER_SOURCE OR NOT RF_SHADER_SYMBOL)
        message(FATAL_ERROR "rf_add_shader requires SOURCE and SYMBOL")
    endif()

    get_filename_component(_source_path "${RF_SHADER_SOURCE}" ABSOLUTE)
    set(_generated_dir "${CMAKE_BINARY_DIR}/generated/rf/gpu/shaders")
    set(_spirv "${_generated_dir}/${RF_SHADER_SYMBOL}.spv")
    set(_header "${_generated_dir}/${RF_SHADER_SYMBOL}.hpp")

    file(MAKE_DIRECTORY "${_generated_dir}")

    add_custom_command(
        OUTPUT "${_header}"
        # --target-env vulkan1.1 matches kMinimumApiVersion in device_info.hpp.
        # Letting glslang default here would silently allow a shader that needs
        # a newer environment than ReelForge claims to require.
        COMMAND "${RF_GLSLANG_VALIDATOR}" --target-env vulkan1.1 -o "${_spirv}" "${_source_path}"
        COMMAND "${CMAKE_COMMAND}"
                -DSPIRV_FILE=${_spirv}
                -DHEADER_FILE=${_header}
                -DSYMBOL=${RF_SHADER_SYMBOL}
                -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
        DEPENDS "${_source_path}" "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
        COMMENT "Compiling shader ${RF_SHADER_SYMBOL}"
        VERBATIM)

    target_sources(${target} PRIVATE "${_header}" "${_source_path}")
    target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated")
endfunction()
