#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/initialized_image_view.hpp>

namespace astraea::loader {

enum class SysvHashErrorCode {
    conflicting_dynamic_tag,
    hash_header_range_overflow,
    hash_header_unreadable,
    invalid_symbol_count,
};

struct SysvHashError {
    SysvHashErrorCode code;
    std::optional<std::size_t> source_entry_index;
    std::optional<std::size_t> conflicting_entry_index;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<astraea::memory::InitializedImageError> image_error;
};

struct SysvHashCountEvidence {
    astraea::memory::GuestAddress hash_address;
    std::uint32_t bucket_count;
    std::uint32_t symbol_count;
    std::size_t source_entry_index;
};

using SysvHashResult =
    astraea::core::Result<std::optional<SysvHashCountEvidence>, SysvHashError>;

[[nodiscard]] SysvHashResult build_sysv_hash_count_evidence(
    const DynamicTable& table,
    const astraea::memory::InitializedImageView& image_view);

}  // namespace astraea::loader
