#pragma once

#include <cstdint>

#include <astraea/loader/elf64.hpp>

namespace astraea::loader {

enum class SceProgramHeaderKind {
    dynlib_data,
    process_parameter,
    relro,
    unknown,
};

struct SceProgramHeaderRecord {
    SceProgramHeaderKind kind = SceProgramHeaderKind::unknown;
    ProgramHeader raw;
};

// Classifies only SCE program-header values supported by the #8 evidence map.
// The complete parsed ProgramHeader remains raw evidence; no runtime mapping,
// process-parameter, RELRO timing, or loader-order semantics are inferred.
[[nodiscard]] constexpr SceProgramHeaderKind
classify_sce_program_header_type(std::uint32_t type) noexcept {
    switch (type) {
    case 0x61000000U:
        return SceProgramHeaderKind::dynlib_data;
    case 0x61000001U:
        return SceProgramHeaderKind::process_parameter;
    case 0x61000010U:
        return SceProgramHeaderKind::relro;
    default:
        return SceProgramHeaderKind::unknown;
    }
}

[[nodiscard]] constexpr SceProgramHeaderRecord
classify_sce_program_header(const ProgramHeader& header) noexcept {
    return SceProgramHeaderRecord{
        .kind = classify_sce_program_header_type(header.type),
        .raw = header,
    };
}

}  // namespace astraea::loader
