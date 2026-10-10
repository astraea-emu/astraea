#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/initialized_image_view.hpp>

namespace astraea::loader {

enum class GnuHashErrorCode {
    conflicting_dynamic_tag,
    hash_range_overflow,
    hash_data_unreadable,
    invalid_hash_header,
    invalid_bucket,
    symbol_count_limit,
};

struct GnuHashError {
    GnuHashErrorCode code;
    std::optional<std::size_t> source_entry_index;
    std::optional<std::size_t> conflicting_entry_index;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<astraea::memory::InitializedImageError> image_error;
};

struct GnuHashCountEvidence {
    astraea::memory::GuestAddress hash_address;
    std::uint32_t bucket_count;
    std::uint32_t first_hashed_symbol;
    std::uint64_t symbol_count;
    std::size_t source_entry_index;
};

using GnuHashResult =
    astraea::core::Result<std::optional<GnuHashCountEvidence>, GnuHashError>;

// Read-only GNU ELF DT_GNU_HASH symbol-count evidence.
// The highest non-empty bucket starts the final contiguous chain. Its last
// low-bit-set entry determines the .dynsym bound, including the preceding
// unhashed symbols. Bounds, table reads and the maximum walk are checked.
// This never resolves, binds, relocates or executes an external symbol.
[[nodiscard]] GnuHashResult build_gnu_hash_count_evidence(
    const DynamicTable& table,
    const astraea::memory::InitializedImageView& image_view);

}  // namespace astraea::loader
