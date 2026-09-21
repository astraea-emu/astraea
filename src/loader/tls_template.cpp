#include <astraea/loader/tls_template.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::loader {
namespace {

static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));

constexpr std::uint32_t kPtTls = 7;
constexpr std::uint32_t kPfRead = 0x4;

[[nodiscard]] TlsTemplateError tls_error(
    TlsTemplateErrorCode code,
    std::optional<std::size_t> source_index = std::nullopt,
    std::optional<std::size_t> conflicting_index = std::nullopt,
    std::optional<std::uint64_t> file_offset = std::nullopt) {
    return TlsTemplateError{
        .code = code,
        .source_program_header_index = source_index,
        .conflicting_program_header_index = conflicting_index,
        .file_offset = file_offset,
    };
}

[[nodiscard]] bool is_power_of_two(std::uint64_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] bool file_range_is_valid(
    std::uint64_t offset,
    std::uint64_t size,
    std::span<const std::byte> image_bytes,
    bool& overflow) noexcept {
    overflow = false;

    if (size == 0) {
        return true;
    }

    if (offset > std::numeric_limits<std::uint64_t>::max() - size) {
        overflow = true;
        return false;
    }

    const auto end = offset + size;
    const auto image_size = static_cast<std::uint64_t>(image_bytes.size());
    return offset <= image_size && end <= image_size;
}

[[nodiscard]] astraea::core::Result<std::size_t, TlsTemplateError>
host_size(
    std::uint64_t size,
    std::size_t source_index,
    std::uint64_t file_offset) {
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (size >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return astraea::core::Result<std::size_t, TlsTemplateError>::failure(
                tls_error(
                    TlsTemplateErrorCode::host_size_unrepresentable,
                    source_index,
                    std::nullopt,
                    file_offset));
        }
    }

    return astraea::core::Result<std::size_t, TlsTemplateError>::success(
        static_cast<std::size_t>(size));
}

}  // namespace

TlsTemplateDescriptorResult build_tls_template_descriptor(
    std::span<const ProgramHeader> program_headers,
    std::span<const std::byte> image_bytes) {
    const ProgramHeader* tls = nullptr;

    for (const auto& header : program_headers) {
        if (header.type != kPtTls) {
            continue;
        }

        if (tls != nullptr) {
            return TlsTemplateDescriptorResult::failure(
                tls_error(
                    TlsTemplateErrorCode::duplicate_tls_segment,
                    header.index,
                    tls->index));
        }

        tls = &header;
    }

    if (tls == nullptr) {
        return TlsTemplateDescriptorResult::success(std::nullopt);
    }

    if (tls->flags != kPfRead) {
        return TlsTemplateDescriptorResult::failure(
            tls_error(
                TlsTemplateErrorCode::invalid_tls_flags,
                tls->index));
    }

    if (tls->file_size > tls->memory_size) {
        return TlsTemplateDescriptorResult::failure(
            tls_error(
                TlsTemplateErrorCode::tls_initialized_size_exceeds_total,
                tls->index,
                std::nullopt,
                tls->offset));
    }

    if (tls->alignment > 1) {
        if (!is_power_of_two(tls->alignment)) {
            return TlsTemplateDescriptorResult::failure(
                tls_error(
                    TlsTemplateErrorCode::invalid_tls_alignment,
                    tls->index));
        }

        if ((tls->virtual_address % tls->alignment) !=
            (tls->offset % tls->alignment)) {
            return TlsTemplateDescriptorResult::failure(
                tls_error(
                    TlsTemplateErrorCode::invalid_tls_alignment_congruence,
                    tls->index));
        }
    }

    bool file_range_overflow = false;
    if (!file_range_is_valid(
            tls->offset,
            tls->file_size,
            image_bytes,
            file_range_overflow)) {
        return TlsTemplateDescriptorResult::failure(
            tls_error(
                file_range_overflow
                    ? TlsTemplateErrorCode::tls_file_range_overflow
                    : TlsTemplateErrorCode::tls_file_range_out_of_bounds,
                tls->index,
                std::nullopt,
                tls->offset));
    }

    return TlsTemplateDescriptorResult::success(
        TlsTemplateDescriptor{
            .source_program_header_index = tls->index,
            .file_offset = tls->offset,
            .initialization_address =
                astraea::memory::GuestAddress{tls->virtual_address},
            .initialized_size =
                astraea::memory::GuestSize{tls->file_size},
            .total_size =
                astraea::memory::GuestSize{tls->memory_size},
            .alignment = tls->alignment,
        });
}

TlsTemplateMaterializeResult materialize_tls_template(
    const TlsTemplateDescriptor& descriptor,
    std::span<const std::byte> image_bytes) {
    const auto total_size = descriptor.total_size.value();
    const auto initialized_size = descriptor.initialized_size.value();

    if (initialized_size > total_size) {
        return TlsTemplateMaterializeResult::failure(
            tls_error(
                TlsTemplateErrorCode::tls_initialized_size_exceeds_total,
                descriptor.source_program_header_index,
                std::nullopt,
                descriptor.file_offset));
    }

    bool file_range_overflow = false;
    if (!file_range_is_valid(
            descriptor.file_offset,
            initialized_size,
            image_bytes,
            file_range_overflow)) {
        return TlsTemplateMaterializeResult::failure(
            tls_error(
                file_range_overflow
                    ? TlsTemplateErrorCode::tls_file_range_overflow
                    : TlsTemplateErrorCode::tls_file_range_out_of_bounds,
                descriptor.source_program_header_index,
                std::nullopt,
                descriptor.file_offset));
    }

    auto host_total = host_size(
        total_size,
        descriptor.source_program_header_index,
        descriptor.file_offset);
    if (!host_total.has_value()) {
        return TlsTemplateMaterializeResult::failure(host_total.error());
    }

    std::vector<std::byte> result;
    if (host_total.value() > result.max_size()) {
        return TlsTemplateMaterializeResult::failure(
            tls_error(
                TlsTemplateErrorCode::host_size_unrepresentable,
                descriptor.source_program_header_index,
                std::nullopt,
                descriptor.file_offset));
    }

    try {
        result.assign(host_total.value(), std::byte{0});
    } catch (const std::bad_alloc&) {
        return TlsTemplateMaterializeResult::failure(
            tls_error(
                TlsTemplateErrorCode::host_allocation_failure,
                descriptor.source_program_header_index,
                std::nullopt,
                descriptor.file_offset));
    } catch (const std::length_error&) {
        return TlsTemplateMaterializeResult::failure(
            tls_error(
                TlsTemplateErrorCode::host_size_unrepresentable,
                descriptor.source_program_header_index,
                std::nullopt,
                descriptor.file_offset));
    }

    if (initialized_size > 0) {
        auto host_initialized = host_size(
            initialized_size,
            descriptor.source_program_header_index,
            descriptor.file_offset);
        if (!host_initialized.has_value()) {
            return TlsTemplateMaterializeResult::failure(
                host_initialized.error());
        }

        const auto source_offset =
            static_cast<std::size_t>(descriptor.file_offset);
        for (std::size_t i = 0; i < host_initialized.value(); ++i) {
            result[i] = image_bytes[source_offset + i];
        }
    }

    return TlsTemplateMaterializeResult::success(std::move(result));
}

}  // namespace astraea::loader
