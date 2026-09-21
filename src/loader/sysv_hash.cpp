#include <astraea/loader/sysv_hash.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::int64_t kDtHash = 4;
constexpr std::uint64_t kHeaderSize = 8;

struct HashLocation {
    std::uint64_t address;
    std::size_t source_entry_index;
};

[[nodiscard]] SysvHashError hash_error(
    SysvHashErrorCode code,
    std::optional<std::size_t> source_entry_index = std::nullopt,
    std::optional<std::size_t> conflicting_entry_index = std::nullopt,
    std::optional<astraea::memory::GuestAddress> guest_address = std::nullopt,
    std::optional<astraea::memory::InitializedImageError> image_error = std::nullopt) {
    return SysvHashError{
        .code = code,
        .source_entry_index = source_entry_index,
        .conflicting_entry_index = conflicting_entry_index,
        .guest_address = guest_address,
        .image_error = std::move(image_error),
    };
}

[[nodiscard]] std::uint32_t read_u32(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto byte =
            static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<std::uint32_t>(value);
}

}  // namespace

SysvHashResult build_sysv_hash_count_evidence(
    const DynamicTable& table,
    const astraea::memory::InitializedImageView& image_view) {
    std::optional<HashLocation> hash;

    for (const auto& entry : table.entries) {
        if (entry.tag != kDtHash) {
            continue;
        }

        if (!hash.has_value()) {
            hash = HashLocation{
                .address = entry.value,
                .source_entry_index = entry.index,
            };
            continue;
        }

        if (hash->address != entry.value) {
            return SysvHashResult::failure(
                hash_error(
                    SysvHashErrorCode::conflicting_dynamic_tag,
                    entry.index,
                    hash->source_entry_index));
        }
    }

    if (!hash.has_value()) {
        return SysvHashResult::success(std::nullopt);
    }

    const auto base = astraea::memory::GuestAddress{hash->address};
    auto range = astraea::memory::GuestRange::create(
        base,
        astraea::memory::GuestSize{kHeaderSize});
    if (!range.has_value()) {
        return SysvHashResult::failure(
            hash_error(
                SysvHashErrorCode::hash_header_range_overflow,
                hash->source_entry_index,
                std::nullopt,
                base));
    }

    std::array<std::byte, kHeaderSize> header{};
    auto copied = image_view.copy_bytes(range.value(), header);
    if (!copied.has_value()) {
        return SysvHashResult::failure(
            hash_error(
                SysvHashErrorCode::hash_header_unreadable,
                hash->source_entry_index,
                std::nullopt,
                copied.error().guest_address.value_or(base),
                copied.error()));
    }

    const auto nbucket = read_u32(header, 0);
    const auto nchain = read_u32(header, 4);

    if (nchain == 0) {
        return SysvHashResult::failure(
            hash_error(
                SysvHashErrorCode::invalid_symbol_count,
                hash->source_entry_index,
                std::nullopt,
                base));
    }

    return SysvHashResult::success(
        SysvHashCountEvidence{
            .hash_address = base,
            .bucket_count = nbucket,
            .symbol_count = nchain,
            .source_entry_index = hash->source_entry_index,
        });
}

}  // namespace astraea::loader
