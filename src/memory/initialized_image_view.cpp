#include <astraea/memory/initialized_image_view.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace astraea::memory {
namespace {

static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));

[[nodiscard]] InitializedImageError make_error(
    InitializedImageErrorCode code,
    std::optional<GuestAddress> guest_address = std::nullopt,
    std::optional<std::size_t> first_source_index = std::nullopt,
    std::optional<std::size_t> second_source_index = std::nullopt,
    std::optional<std::uint64_t> file_offset = std::nullopt) {
    return InitializedImageError{
        .code = code,
        .guest_address = guest_address,
        .first_source_index = first_source_index,
        .second_source_index = second_source_index,
        .file_offset = file_offset,
    };
}

[[nodiscard]] std::uint64_t image_size_u64(std::span<const std::byte> bytes) noexcept {
    return static_cast<std::uint64_t>(bytes.size());
}

[[nodiscard]] bool file_range_is_valid(
    const MappingIntent& intent,
    std::uint64_t image_size) noexcept {
    const auto offset = intent.backing.file_offset;
    const auto size = intent.backing.byte_count.value();

    if (offset > std::numeric_limits<std::uint64_t>::max() - size) {
        return false;
    }

    const auto end = offset + size;
    return offset <= image_size && end <= image_size;
}

enum class ResolvedSourceKind {
    file,
    zero_fill,
};

struct ResolvedSource {
    ResolvedSourceKind kind;
    std::uint64_t file_offset;
    std::size_t source_index;
};

[[nodiscard]] astraea::core::Result<ResolvedSource, InitializedImageError> source_at(
    const MappingIntent& intent,
    GuestAddress address) {
    if (intent.backing.kind == MappingBackingKind::zero_fill) {
        return astraea::core::Result<ResolvedSource, InitializedImageError>::success(
            ResolvedSource{
                .kind = ResolvedSourceKind::zero_fill,
                .file_offset = 0,
                .source_index = intent.source_index,
            });
    }

    const auto delta = address.value() - intent.range.base().value();
    if (intent.backing.file_offset >
        std::numeric_limits<std::uint64_t>::max() - delta) {
        return astraea::core::Result<ResolvedSource, InitializedImageError>::failure(
            make_error(
                InitializedImageErrorCode::invalid_file_backing_range,
                address,
                intent.source_index,
                std::nullopt,
                intent.backing.file_offset));
    }

    return astraea::core::Result<ResolvedSource, InitializedImageError>::success(
        ResolvedSource{
            .kind = ResolvedSourceKind::file,
            .file_offset = intent.backing.file_offset + delta,
            .source_index = intent.source_index,
        });
}

[[nodiscard]] bool sources_match(
    const ResolvedSource& lhs,
    const ResolvedSource& rhs) noexcept {
    if (lhs.kind != rhs.kind) {
        return false;
    }

    if (lhs.kind == ResolvedSourceKind::zero_fill) {
        return true;
    }

    return lhs.file_offset == rhs.file_offset;
}

}  // namespace

InitializedImageView::CreateResult InitializedImageView::create(
    std::span<const std::byte> image_bytes,
    std::span<const MappingIntent> intents) {
    std::vector<MappingIntent> validated;
    validated.reserve(intents.size());

    const auto image_size = image_size_u64(image_bytes);

    for (const auto& intent : intents) {
        if (intent.backing.byte_count != intent.range.size()) {
            return CreateResult::failure(
                make_error(
                    InitializedImageErrorCode::backing_size_mismatch,
                    intent.range.base(),
                    intent.source_index));
        }

        switch (intent.backing.kind) {
        case MappingBackingKind::file:
            if (!file_range_is_valid(intent, image_size)) {
                return CreateResult::failure(
                    make_error(
                        InitializedImageErrorCode::invalid_file_backing_range,
                        intent.range.base(),
                        intent.source_index,
                        std::nullopt,
                        intent.backing.file_offset));
            }
            break;

        case MappingBackingKind::zero_fill:
            break;

        case MappingBackingKind::anonymous:
            return CreateResult::failure(
                make_error(
                    InitializedImageErrorCode::unsupported_backing_kind,
                    intent.range.base(),
                    intent.source_index));
        }

        validated.push_back(intent);
    }

    std::sort(validated.begin(), validated.end(), mapping_intent_less);

    return CreateResult::success(
        InitializedImageView{image_bytes, std::move(validated)});
}

InitializedImageView::ByteResult InitializedImageView::read_byte(
    GuestAddress address) const {
    std::optional<ResolvedSource> resolved;

    for (const auto& intent : intents_) {
        if (intent.range.base() > address) {
            break;
        }

        if (!intent.range.contains(address)) {
            continue;
        }

        auto candidate = source_at(intent, address);
        if (!candidate.has_value()) {
            return ByteResult::failure(candidate.error());
        }

        if (!resolved.has_value()) {
            resolved = candidate.value();
            continue;
        }

        if (!sources_match(*resolved, candidate.value())) {
            return ByteResult::failure(
                make_error(
                    InitializedImageErrorCode::mapping_overlap_conflict,
                    address,
                    resolved->source_index,
                    candidate->source_index));
        }
    }

    if (!resolved.has_value()) {
        return ByteResult::failure(
            make_error(
                InitializedImageErrorCode::unmapped_guest_address,
                address));
    }

    if (resolved->kind == ResolvedSourceKind::zero_fill) {
        return ByteResult::success(std::byte{0});
    }

    const auto offset = static_cast<std::size_t>(resolved->file_offset);
    return ByteResult::success(image_bytes_[offset]);
}

InitializedImageView::CopyResult InitializedImageView::copy_bytes(
    const GuestRange& range,
    std::span<std::byte> output) const {
    const auto guest_size = range.size().value();

    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (guest_size > static_cast<std::uint64_t>(
                             std::numeric_limits<std::size_t>::max())) {
            return CopyResult::failure(
                make_error(
                    InitializedImageErrorCode::host_size_unrepresentable,
                    range.base()));
        }
    }

    const auto host_size = static_cast<std::size_t>(guest_size);
    if (output.size() != host_size) {
        return CopyResult::failure(
            make_error(
                InitializedImageErrorCode::output_size_mismatch,
                range.base()));
    }

    for (std::size_t i = 0; i < host_size; ++i) {
        auto address = GuestAddress::checked_add(
            range.base(),
            GuestSize{static_cast<std::uint64_t>(i)});
        if (!address.has_value()) {
            return CopyResult::failure(
                make_error(
                    InitializedImageErrorCode::unmapped_guest_address,
                    range.base()));
        }

        auto byte = read_byte(address.value());
        if (!byte.has_value()) {
            return CopyResult::failure(byte.error());
        }

        output[i] = byte.value();
    }

    return CopyResult::success(host_size);
}

}  // namespace astraea::memory
