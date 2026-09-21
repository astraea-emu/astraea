#include <astraea/loader/dynamic.hpp>

#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace astraea::loader {
namespace {

constexpr std::uint32_t kPtDynamic = 2;
constexpr std::int64_t kDtNull = 0;
constexpr std::uint64_t kDynamicEntrySize = 16;

template <typename T>
[[nodiscard]] T read_little_endian(std::span<const std::byte> bytes, std::size_t offset) {
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte = static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& result) {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] std::uint64_t input_size_u64(std::span<const std::byte> bytes) {
    if constexpr (sizeof(std::size_t) <= sizeof(std::uint64_t)) {
        return static_cast<std::uint64_t>(bytes.size());
    } else {
        const auto max = static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max());
        return bytes.size() > max ? std::numeric_limits<std::uint64_t>::max()
                                  : static_cast<std::uint64_t>(bytes.size());
    }
}

[[nodiscard]] DynamicError error(
    DynamicErrorCode code,
    std::uint64_t file_offset,
    std::optional<std::size_t> entry_index = std::nullopt) {
    return DynamicError{code, entry_index, file_offset};
}

}  // namespace

DynamicParseResult parse_dynamic_table(
    std::span<const std::byte> bytes,
    const ProgramHeader& dynamic_segment) {
    if (dynamic_segment.type != kPtDynamic) {
        return DynamicParseResult::failure(
            error(DynamicErrorCode::not_dynamic_segment, dynamic_segment.offset));
    }

    if (dynamic_segment.file_size == 0) {
        return DynamicParseResult::failure(
            error(DynamicErrorCode::missing_terminator, dynamic_segment.offset));
    }

    if (dynamic_segment.file_size % kDynamicEntrySize != 0) {
        return DynamicParseResult::failure(
            error(DynamicErrorCode::invalid_segment_size, dynamic_segment.offset));
    }

    std::uint64_t segment_end = 0;
    if (!checked_add(dynamic_segment.offset, dynamic_segment.file_size, segment_end)) {
        return DynamicParseResult::failure(
            error(DynamicErrorCode::integer_overflow, dynamic_segment.offset));
    }

    const auto input_size = input_size_u64(bytes);
    if (dynamic_segment.offset > input_size || segment_end > input_size) {
        return DynamicParseResult::failure(
            error(DynamicErrorCode::segment_out_of_bounds, dynamic_segment.offset));
    }

    const auto entry_count = dynamic_segment.file_size / kDynamicEntrySize;
    std::vector<DynamicEntry> entries;
    entries.reserve(static_cast<std::size_t>(entry_count));

    for (std::uint64_t index = 0; index < entry_count; ++index) {
        const auto relative_offset = index * kDynamicEntrySize;
        const auto entry_offset_u64 = dynamic_segment.offset + relative_offset;
        const auto entry_offset = static_cast<std::size_t>(entry_offset_u64);

        const auto raw_tag = read_little_endian<std::uint64_t>(bytes, entry_offset);
        const auto tag = std::bit_cast<std::int64_t>(raw_tag);
        const auto value = read_little_endian<std::uint64_t>(bytes, entry_offset + 8);
        const auto stable_index = static_cast<std::size_t>(index);

        entries.push_back(DynamicEntry{
            .tag = tag,
            .value = value,
            .index = stable_index,
        });

        if (tag == kDtNull) {
            return DynamicParseResult::success(DynamicTable{.entries = std::move(entries)});
        }
    }

    return DynamicParseResult::failure(
        error(
            DynamicErrorCode::missing_terminator,
            dynamic_segment.offset,
            static_cast<std::size_t>(entry_count)));
}

}  // namespace astraea::loader
