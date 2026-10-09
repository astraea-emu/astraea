#include <astraea/execution/owned_relative_patch.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace astraea::execution {
namespace {

[[nodiscard]] OwnedRelativePatchError make_error(
    OwnedRelativePatchErrorCode code,
    std::uint32_t type) noexcept {
    return OwnedRelativePatchError{
        .code = code,
        .raw_relocation_type = type,
    };
}

[[nodiscard]] std::array<std::byte, kOwnedX86_64RelativePatchWidth>
encode_le64(std::uint64_t value) noexcept {
    std::array<std::byte, kOwnedX86_64RelativePatchWidth> bytes{};
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(
            (value >> (i * 8U)) & 0xffU);
    }
    return bytes;
}

}  // namespace

OwnedRelativePatchResult build_owned_x86_64_relative_patch(
    const astraea::loader::DynamicRelocation& relocation,
    astraea::memory::GuestAddress load_bias) noexcept {
    const auto type = relocation.relocation_type;
    if (relocation.table_kind !=
        astraea::loader::RelocationTableKind::rela) {
        return OwnedRelativePatchResult::failure(make_error(
            OwnedRelativePatchErrorCode::unsupported_table_kind, type));
    }
    if (type != kOwnedX86_64RelativeType) {
        return OwnedRelativePatchResult::failure(make_error(
            OwnedRelativePatchErrorCode::unsupported_relocation_type, type));
    }
    if (relocation.symbol_index != 0U) {
        return OwnedRelativePatchResult::failure(make_error(
            OwnedRelativePatchErrorCode::symbol_index_not_zero, type));
    }
    if (!relocation.addend.has_value()) {
        return OwnedRelativePatchResult::failure(make_error(
            OwnedRelativePatchErrorCode::missing_rela_addend, type));
    }
    const auto target = astraea::memory::GuestRange::create(
        relocation.target,
        astraea::memory::GuestSize{kOwnedX86_64RelativePatchWidth});
    if (!target.has_value()) {
        return OwnedRelativePatchResult::failure(make_error(
            OwnedRelativePatchErrorCode::target_range_overflow, type));
    }

    const auto base = load_bias.value();
    const auto addend = relocation.addend.value();
    std::uint64_t value = 0U;
    if (addend >= 0) {
        const auto positive = static_cast<std::uint64_t>(addend);
        if (base > std::numeric_limits<std::uint64_t>::max() - positive) {
            return OwnedRelativePatchResult::failure(make_error(
                OwnedRelativePatchErrorCode::relocated_value_overflow, type));
        }
        value = base + positive;
    } else {
        // Avoid negating INT64_MIN (undefined signed arithmetic). Compute
        // the unsigned magnitude with the signed value safely incremented.
        const auto magnitude =
            static_cast<std::uint64_t>(-(addend + 1)) + 1U;
        if (base < magnitude) {
            return OwnedRelativePatchResult::failure(make_error(
                OwnedRelativePatchErrorCode::relocated_value_overflow, type));
        }
        value = base - magnitude;
    }
    return OwnedRelativePatchResult::success(OwnedRelativePatch{
        .target = relocation.target,
        .load_bias = load_bias,
        .raw_addend = addend,
        .relocated_value = astraea::memory::GuestAddress{value},
        .bytes = encode_le64(value),
    });
}

}  // namespace astraea::execution
