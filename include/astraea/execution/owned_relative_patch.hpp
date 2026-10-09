#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint32_t kOwnedX86_64RelativeType = 8U;
inline constexpr std::size_t kOwnedX86_64RelativePatchWidth = 8U;

enum class OwnedRelativePatchErrorCode {
    unsupported_table_kind,
    unsupported_relocation_type,
    symbol_index_not_zero,
    missing_rela_addend,
    target_range_overflow,
    relocated_value_overflow,
};

struct OwnedRelativePatchError {
    OwnedRelativePatchErrorCode code =
        OwnedRelativePatchErrorCode::unsupported_relocation_type;
    std::uint32_t raw_relocation_type = 0;

    auto operator<=>(const OwnedRelativePatchError&) const = default;
};

struct OwnedRelativePatch {
    astraea::memory::GuestAddress target;
    astraea::memory::GuestAddress load_bias;
    std::int64_t raw_addend = 0;
    astraea::memory::GuestAddress relocated_value;
    std::array<std::byte, kOwnedX86_64RelativePatchWidth> bytes{};

    auto operator<=>(const OwnedRelativePatch&) const = default;
};

using OwnedRelativePatchResult =
    astraea::core::Result<OwnedRelativePatch, OwnedRelativePatchError>;

// Source-owned x86-64 ELF semantics: R_X86_64_RELATIVE word64 = B + A,
// where B is an EXPLICIT caller-selected guest load bias and A is the
// signed RELA addend. Refuse overflow/underflow rather than guessing or
// wrapping the address. No symbol lookup, guest write or retail entry.
[[nodiscard]] OwnedRelativePatchResult
build_owned_x86_64_relative_patch(
    const astraea::loader::DynamicRelocation& relocation,
    astraea::memory::GuestAddress load_bias) noexcept;

}  // namespace astraea::execution
