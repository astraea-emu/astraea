#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::graphics {

enum class AgcShaderStage {
    compute,
    pixel,
    geometry,
    hull,
    geometry_front,
    hull_front,
    geometry_back,
    hull_back,
    function,
};

struct AgcShaderProgramType {
    std::uint8_t raw = 0;
    std::optional<AgcShaderStage> known;

    auto operator<=>(const AgcShaderProgramType&) const = default;
};

enum class AgcRegisterListKind {
    context,
    shader,
};

struct AgcRegisterWrite {
    std::uint16_t register_offset = 0;
    std::uint32_t value = 0;

    auto operator<=>(const AgcRegisterWrite&) const = default;
};

// Canonical raw runtime shader representation. This is the evidence-backed
// header/text pair supplied to sceAgcCreateShader before the native API
// prepares the writable header in place.
struct AgcShaderBinary {
    std::uint32_t header_magic = 0;
    std::uint32_t header_version = 0;
    std::uint32_t declared_header_size = 0;
    std::uint32_t declared_shader_text_size = 0;
    AgcShaderProgramType program_type{};

    std::vector<AgcRegisterWrite> context_registers;
    std::vector<AgcRegisterWrite> shader_registers;

    std::uint32_t program_byte_size = 0;
    std::uint32_t trailer_sl00_byte_size = 0;

    // Unknown bytes remain available as provenance. Offsets inside the raw
    // header are not converted to prepared pointers here.
    std::vector<std::byte> shader_header_bytes;
    std::vector<std::byte> shader_text_bytes;

    // Only the evidence-bounded machine-code prefix is exposed as generic
    // RDNA2 dwords.
    std::vector<std::uint32_t> rdna2_words;

    auto operator<=>(const AgcShaderBinary&) const = default;
};

enum class AgcShaderBinaryRegion {
    none,
    shader_header,
    shader_text,
};

enum class AgcShaderBinaryErrorCode {
    shader_header_too_small,
    bad_shader_header_magic,
    declared_header_size_mismatch,
    declared_shader_text_size_mismatch,
    register_table_extent_overflow,
    register_table_extent_out_of_bounds,
    shader_text_too_small_for_trailer,
    program_extent_out_of_bounds,
    program_size_not_dword_aligned,
    host_allocation_failure,
};

struct AgcShaderBinaryError {
    AgcShaderBinaryErrorCode code =
        AgcShaderBinaryErrorCode::shader_header_too_small;
    AgcShaderBinaryRegion region =
        AgcShaderBinaryRegion::none;
    std::uint64_t byte_offset = 0;
    std::optional<AgcRegisterListKind> register_list_kind;

    auto operator<=>(const AgcShaderBinaryError&) const = default;
};

using AgcShaderBinaryResult =
    astraea::core::Result<
        AgcShaderBinary,
        AgcShaderBinaryError>;

// Parses the raw AGC shader header/text pair used at the runtime shader-create
// boundary. Only publicly evidenced fields are typed. Resource tables,
// descriptors, prepared pointers, hashes, and other unknown fields remain
// opaque in shader_header_bytes/shader_text_bytes.
[[nodiscard]] AgcShaderBinaryResult
parse_agc_shader_binary(
    std::span<const std::byte> shader_header,
    std::span<const std::byte> shader_text);

}  // namespace astraea::graphics
