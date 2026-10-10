#include <astraea/loader/gnu_hash.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::int64_t kDtGnuHash = 0x6ffffef5;
constexpr std::uint64_t kHeaderBytes = 16U;
constexpr std::uint64_t kBloomWordBytes = 8U;
constexpr std::uint64_t kBucketBytes = 4U;
constexpr std::uint32_t kMaxBuckets = 65536U;
constexpr std::uint32_t kMaxBloomWords = 65536U;
constexpr std::uint32_t kMaxSymbols = 1000000U;

struct HashLocation {
    astraea::memory::GuestAddress address;
    std::size_t source_entry_index;
};

[[nodiscard]] GnuHashError error(
    GnuHashErrorCode code,
    std::optional<std::size_t> source = std::nullopt,
    std::optional<std::size_t> conflict = std::nullopt,
    std::optional<astraea::memory::GuestAddress> address = std::nullopt,
    std::optional<astraea::memory::InitializedImageError> image = std::nullopt) {
    return GnuHashError{
        .code = code,
        .source_entry_index = source,
        .conflicting_entry_index = conflict,
        .guest_address = address,
        .image_error = std::move(image),
    };
}

using WordResult = astraea::core::Result<std::uint32_t, GnuHashError>;

[[nodiscard]] WordResult read_word(
    const astraea::memory::InitializedImageView& view,
    HashLocation location,
    std::uint64_t offset) {
    const auto address = astraea::memory::GuestAddress::checked_add(
        location.address, astraea::memory::GuestSize{offset});
    if (!address.has_value()) {
        return WordResult::failure(error(
            GnuHashErrorCode::hash_range_overflow,
            location.source_entry_index, std::nullopt, location.address));
    }
    const auto range = astraea::memory::GuestRange::create(
        address.value(), astraea::memory::GuestSize{kBucketBytes});
    if (!range.has_value()) {
        return WordResult::failure(error(
            GnuHashErrorCode::hash_range_overflow,
            location.source_entry_index, std::nullopt, address.value()));
    }
    std::array<std::byte, 4U> bytes{};
    auto copied = view.copy_bytes(range.value(), bytes);
    if (!copied.has_value()) {
        return WordResult::failure(error(
            GnuHashErrorCode::hash_data_unreadable,
            location.source_entry_index,
            std::nullopt,
            copied.error().guest_address.value_or(address.value()),
            copied.error()));
    }
    std::uint32_t result = 0U;
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        result |= std::to_integer<std::uint32_t>(bytes[i]) << (8U * i);
    }
    return WordResult::success(result);
}

[[nodiscard]] constexpr bool power_of_two(std::uint32_t value) {
    return value != 0U && (value & (value - 1U)) == 0U;
}

}  // namespace

GnuHashResult build_gnu_hash_count_evidence(
    const DynamicTable& table,
    const astraea::memory::InitializedImageView& view) {
    std::optional<HashLocation> location;
    for (const auto& entry : table.entries) {
        if (entry.tag != kDtGnuHash) {
            continue;
        }
        const auto address = astraea::memory::GuestAddress{entry.value};
        if (!location.has_value()) {
            location = HashLocation{address, entry.index};
        } else if (location->address != address) {
            return GnuHashResult::failure(error(
                GnuHashErrorCode::conflicting_dynamic_tag,
                entry.index, location->source_entry_index));
        }
    }
    if (!location.has_value()) {
        return GnuHashResult::success(std::nullopt);
    }

    auto nbuckets = read_word(view, *location, 0U);
    auto symoffset = read_word(view, *location, 4U);
    auto bloom_words = read_word(view, *location, 8U);
    auto bloom_shift = read_word(view, *location, 12U);
    if (!nbuckets.has_value()) return GnuHashResult::failure(nbuckets.error());
    if (!symoffset.has_value()) return GnuHashResult::failure(symoffset.error());
    if (!bloom_words.has_value()) return GnuHashResult::failure(bloom_words.error());
    if (!bloom_shift.has_value()) return GnuHashResult::failure(bloom_shift.error());
    // The shift affects lookup, not the symbol count; preserve rather than
    // inventing a restriction on its value.
    (void)bloom_shift;

    if (nbuckets.value() == 0U || nbuckets.value() > kMaxBuckets ||
        !power_of_two(bloom_words.value()) ||
        bloom_words.value() > kMaxBloomWords ||
        symoffset.value() == 0U || symoffset.value() > kMaxSymbols) {
        return GnuHashResult::failure(error(
            GnuHashErrorCode::invalid_hash_header,
            location->source_entry_index, std::nullopt, location->address));
    }

    // Header, ELF64 bloom and bucket sizes are bounded above; these
    // unsigned additions cannot wrap under the explicit caps above.
    const std::uint64_t buckets_offset =
        kHeaderBytes + kBloomWordBytes * bloom_words.value();
    const std::uint64_t chains_offset =
        buckets_offset + kBucketBytes * nbuckets.value();

    std::uint32_t highest_bucket = 0U;
    for (std::uint32_t i = 0U; i < nbuckets.value(); ++i) {
        auto bucket = read_word(
            view, *location, buckets_offset + kBucketBytes * i);
        if (!bucket.has_value()) {
            return GnuHashResult::failure(bucket.error());
        }
        if (bucket.value() != 0U &&
            (bucket.value() < symoffset.value() ||
             bucket.value() >= kMaxSymbols)) {
            return GnuHashResult::failure(error(
                GnuHashErrorCode::invalid_bucket,
                location->source_entry_index, std::nullopt, location->address));
        }
        highest_bucket = std::max(highest_bucket, bucket.value());
    }

    std::uint64_t count = symoffset.value();
    if (highest_bucket != 0U) {
        auto index = static_cast<std::uint64_t>(highest_bucket);
        for (;;) {
            if (index >= kMaxSymbols) {
                return GnuHashResult::failure(error(
                    GnuHashErrorCode::symbol_count_limit,
                    location->source_entry_index, std::nullopt,
                    location->address));
            }
            auto chain = read_word(
                view, *location,
                chains_offset + kBucketBytes *
                    (index - symoffset.value()));
            if (!chain.has_value()) {
                return GnuHashResult::failure(chain.error());
            }
            ++index;
            if ((chain.value() & 1U) != 0U) {
                count = index;
                break;
            }
        }
    }
    return GnuHashResult::success(
        GnuHashCountEvidence{
            .hash_address = location->address,
            .bucket_count = nbuckets.value(),
            .first_hashed_symbol = symoffset.value(),
            .symbol_count = count,
            .source_entry_index = location->source_entry_index,
        });
}

}  // namespace astraea::loader
