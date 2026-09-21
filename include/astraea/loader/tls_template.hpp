#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/elf64.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::loader {

enum class TlsTemplateErrorCode {
    duplicate_tls_segment,
    tls_initialized_size_exceeds_total,
    tls_file_range_overflow,
    tls_file_range_out_of_bounds,
    invalid_tls_flags,
    invalid_tls_alignment,
    invalid_tls_alignment_congruence,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct TlsTemplateError {
    TlsTemplateErrorCode code;
    std::optional<std::size_t> source_program_header_index;
    std::optional<std::size_t> conflicting_program_header_index;
    std::optional<std::uint64_t> file_offset;
};

struct TlsTemplateDescriptor {
    std::size_t source_program_header_index;
    std::uint64_t file_offset;
    astraea::memory::GuestAddress initialization_address;
    astraea::memory::GuestSize initialized_size;
    astraea::memory::GuestSize total_size;
    std::uint64_t alignment;

    auto operator<=>(const TlsTemplateDescriptor&) const = default;
};

using TlsTemplateDescriptorResult =
    astraea::core::Result<std::optional<TlsTemplateDescriptor>, TlsTemplateError>;

using TlsTemplateMaterializeResult =
    astraea::core::Result<std::vector<std::byte>, TlsTemplateError>;

[[nodiscard]] TlsTemplateDescriptorResult build_tls_template_descriptor(
    std::span<const ProgramHeader> program_headers,
    std::span<const std::byte> image_bytes);

[[nodiscard]] TlsTemplateMaterializeResult materialize_tls_template(
    const TlsTemplateDescriptor& descriptor,
    std::span<const std::byte> image_bytes);

}  // namespace astraea::loader
