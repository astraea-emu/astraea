#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/mapping.hpp>

namespace astraea::memory {

enum class InitializedImageErrorCode {
    invalid_file_backing_range,
    backing_size_mismatch,
    unsupported_backing_kind,
    unmapped_guest_address,
    mapping_overlap_conflict,
    output_size_mismatch,
    host_size_unrepresentable,
};

struct InitializedImageError {
    InitializedImageErrorCode code;
    std::optional<GuestAddress> guest_address;
    std::optional<std::size_t> first_source_index;
    std::optional<std::size_t> second_source_index;
    std::optional<std::uint64_t> file_offset;
};

class InitializedImageView {
public:
    using CreateResult =
        astraea::core::Result<InitializedImageView, InitializedImageError>;
    using ByteResult =
        astraea::core::Result<std::byte, InitializedImageError>;
    using CopyResult =
        astraea::core::Result<std::size_t, InitializedImageError>;

    [[nodiscard]] static CreateResult create(
        std::span<const std::byte> image_bytes,
        std::span<const MappingIntent> intents);

    [[nodiscard]] ByteResult read_byte(GuestAddress address) const;

    [[nodiscard]] CopyResult copy_bytes(
        const GuestRange& range,
        std::span<std::byte> output) const;

    [[nodiscard]] std::size_t mapping_count() const noexcept {
        return intents_.size();
    }

private:
    InitializedImageView(
        std::span<const std::byte> image_bytes,
        std::vector<MappingIntent> intents)
        : image_bytes_(image_bytes), intents_(std::move(intents)) {}

    std::span<const std::byte> image_bytes_;
    std::vector<MappingIntent> intents_;
};

}  // namespace astraea::memory
