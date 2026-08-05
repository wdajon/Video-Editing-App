# Script mode: converts a .spv binary into a C++ header holding a word array.
#
#   cmake -DSPIRV_FILE=... -DHEADER_FILE=... -DSYMBOL=... -P EmbedSpirv.cmake
#
# SPIR-V is a stream of 32-bit words, so it is emitted as uint32_t rather than
# bytes: vkCreateShaderModule wants word-aligned data, and a byte array would
# have to be copied to guarantee alignment on every load.

if(NOT DEFINED SPIRV_FILE OR NOT DEFINED HEADER_FILE OR NOT DEFINED SYMBOL)
    message(FATAL_ERROR "EmbedSpirv.cmake requires SPIRV_FILE, HEADER_FILE and SYMBOL")
endif()

file(READ "${SPIRV_FILE}" _hex HEX)

string(LENGTH "${_hex}" _hex_length)
math(EXPR _byte_count "${_hex_length} / 2")
math(EXPR _remainder "${_byte_count} % 4")
if(NOT _remainder EQUAL 0)
    message(FATAL_ERROR
        "${SPIRV_FILE} is ${_byte_count} bytes, which is not a whole number of "
        "32-bit words; it is not valid SPIR-V")
endif()
math(EXPR _word_count "${_byte_count} / 4")

set(_words "")
math(EXPR _last "${_word_count} - 1")
foreach(_i RANGE ${_last})
    math(EXPR _offset "${_i} * 8")
    string(SUBSTRING "${_hex}" ${_offset} 8 _word_hex)
    # SPIR-V is little-endian on disk; reverse the byte pairs into a word.
    string(SUBSTRING "${_word_hex}" 0 2 _b0)
    string(SUBSTRING "${_word_hex}" 2 2 _b1)
    string(SUBSTRING "${_word_hex}" 4 2 _b2)
    string(SUBSTRING "${_word_hex}" 6 2 _b3)
    string(APPEND _words "    0x${_b3}${_b2}${_b1}${_b0}u,\n")
endforeach()

get_filename_component(_guard_name "${HEADER_FILE}" NAME)
string(TOUPPER "${_guard_name}" _guard)
string(REGEX REPLACE "[^A-Z0-9]" "_" _guard "${_guard}")

file(WRITE "${HEADER_FILE}"
"// Generated from ${SPIRV_FILE} -- do not edit.\n"
"#ifndef RF_GENERATED_${_guard}\n"
"#define RF_GENERATED_${_guard}\n"
"\n"
"#include <cstdint>\n"
"\n"
"namespace rf::gpu::shaders {\n"
"\n"
"inline constexpr std::uint32_t ${SYMBOL}[] = {\n"
"${_words}"
"};\n"
"\n"
"inline constexpr std::size_t ${SYMBOL}_word_count = ${_word_count};\n"
"\n"
"}  // namespace rf::gpu::shaders\n"
"\n"
"#endif\n")
