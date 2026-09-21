#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/memory/initialized_image_view.hpp>

namespace astraea::loader {

enum class DynamicStringErrorCode {
    offset_out_of_bounds,
    unterminated,
    image_read_failure,
    guest_address_overflow,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct DynamicStringError {
    DynamicStringErrorCode code;
    std::size_t source_entry_index;
    std::uint64_t offset;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<astraea::memory::InitializedImageError> image_error;
};

using DynamicStringResult =
    astraea::core::Result<std::string, DynamicStringError>;

[[nodiscard]] DynamicStringResult resolve_dynamic_string(
    const DynamicStringTableDescriptor& table,
    const DynamicStringRef& reference,
    const astraea::memory::InitializedImageView& image_view);

}  // namespace astraea::loader
