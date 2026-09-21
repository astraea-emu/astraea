#include <astraea/memory/mapping.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace astraea::memory {
namespace {

constexpr std::uint32_t kPtLoad = 1;

[[nodiscard]] MappingError error(
    MappingErrorCode code,
    std::size_t first,
    std::size_t second = 0) {
    return MappingError{code, first, second};
}

[[nodiscard]] std::uint64_t overlap_start(
    const MappingIntent& lhs,
    const MappingIntent& rhs) noexcept {
    return std::max(lhs.range.base().value(), rhs.range.base().value());
}

[[nodiscard]] std::uint64_t file_position_at(
    const MappingIntent& intent,
    std::uint64_t guest_address) noexcept {
    return intent.backing.file_offset + (guest_address - intent.range.base().value());
}

}  // namespace

MappingResult mapping_intents_from_load(const astraea::loader::ProgramHeader& program_header) {
    if (program_header.type != kPtLoad) {
        return MappingResult::failure(
            error(MappingErrorCode::not_load_segment, program_header.index));
    }

    if (program_header.file_size > program_header.memory_size) {
        return MappingResult::failure(
            error(MappingErrorCode::invalid_load_sizes, program_header.index));
    }

    const auto permissions = GuestPermissions::from_elf_flags(program_header.flags);

    if (program_header.memory_size == 0) {
        return MappingResult::success({});
    }

    auto full_range = GuestRange::create(
        GuestAddress{program_header.virtual_address},
        GuestSize{program_header.memory_size});
    if (!full_range.has_value()) {
        return MappingResult::failure(
            error(MappingErrorCode::guest_range_overflow, program_header.index));
    }

    std::vector<MappingIntent> intents;
    intents.reserve(program_header.file_size < program_header.memory_size ? 2U : 1U);

    if (program_header.file_size > 0) {
        auto file_range = GuestRange::create(
            GuestAddress{program_header.virtual_address},
            GuestSize{program_header.file_size});
        if (!file_range.has_value()) {
            return MappingResult::failure(
                error(MappingErrorCode::guest_range_overflow, program_header.index));
        }

        intents.push_back(MappingIntent{
            .range = file_range.value(),
            .permissions = permissions,
            .backing =
                MappingBacking{
                    .kind = MappingBackingKind::file,
                    .file_offset = program_header.offset,
                    .byte_count = GuestSize{program_header.file_size},
                },
            .source_index = program_header.index,
        });
    }

    if (program_header.memory_size > program_header.file_size) {
        const auto zero_size = program_header.memory_size - program_header.file_size;
        auto zero_base = GuestAddress::checked_add(
            GuestAddress{program_header.virtual_address},
            GuestSize{program_header.file_size});
        if (!zero_base.has_value()) {
            return MappingResult::failure(
                error(MappingErrorCode::address_addition_overflow, program_header.index));
        }

        auto zero_range = GuestRange::create(zero_base.value(), GuestSize{zero_size});
        if (!zero_range.has_value()) {
            return MappingResult::failure(
                error(MappingErrorCode::guest_range_overflow, program_header.index));
        }

        intents.push_back(MappingIntent{
            .range = zero_range.value(),
            .permissions = permissions,
            .backing =
                MappingBacking{
                    .kind = MappingBackingKind::zero_fill,
                    .file_offset = 0,
                    .byte_count = GuestSize{zero_size},
                },
            .source_index = program_header.index,
        });
    }

    std::sort(intents.begin(), intents.end(), mapping_intent_less);
    return MappingResult::success(std::move(intents));
}

OverlapClass classify_overlap(const MappingIntent& lhs, const MappingIntent& rhs) noexcept {
    if (!lhs.range.overlaps(rhs.range)) {
        return OverlapClass::none;
    }

    if (lhs.permissions != rhs.permissions) {
        return OverlapClass::permission_disagreement;
    }

    if (lhs.backing.kind != rhs.backing.kind) {
        return OverlapClass::file_vs_zero_fill;
    }

    if (lhs.backing.kind == MappingBackingKind::file) {
        const auto start = overlap_start(lhs, rhs);
        const auto lhs_file_position = file_position_at(lhs, start);
        const auto rhs_file_position = file_position_at(rhs, start);

        if (lhs_file_position != rhs_file_position) {
            return OverlapClass::conflicting_initialized_bytes;
        }

        if (lhs.range == rhs.range && lhs.backing == rhs.backing) {
            return OverlapClass::identical_compatible;
        }

        return OverlapClass::different_backing;
    }

    if (lhs.range == rhs.range && lhs.backing == rhs.backing) {
        return OverlapClass::identical_compatible;
    }

    return OverlapClass::different_backing;
}

bool mapping_intent_less(const MappingIntent& lhs, const MappingIntent& rhs) noexcept {
    if (lhs.range.base() != rhs.range.base()) {
        return lhs.range.base() < rhs.range.base();
    }
    if (lhs.range.size() != rhs.range.size()) {
        return lhs.range.size() < rhs.range.size();
    }
    if (lhs.source_index != rhs.source_index) {
        return lhs.source_index < rhs.source_index;
    }
    return lhs.backing.kind < rhs.backing.kind;
}

}  // namespace astraea::memory
