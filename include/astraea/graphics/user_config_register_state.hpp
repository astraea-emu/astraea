#pragma once

#include <array>
#include <bitset>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_set_uconfig_reg_ir.hpp>

namespace astraea::graphics {

inline constexpr std::size_t kUserConfigRegisterStateDwords =
    kPm4UserConfigRegisterWindowDwords;

struct UserConfigRegisterState {
    std::array<std::uint32_t, kUserConfigRegisterStateDwords>
        values{};
    std::bitset<kUserConfigRegisterStateDwords>
        initialized{};

    bool operator==(const UserConfigRegisterState&) const = default;
};

struct UserConfigRegisterWriteEffect {
    std::uint16_t start_offset = 0;
    std::size_t value_count = 0;

    auto operator<=>(
        const UserConfigRegisterWriteEffect&) const = default;
};

enum class UserConfigRegisterApplyErrorCode {
    unsupported_operation,
    register_range_out_of_bounds,
};

struct UserConfigRegisterApplyError {
    UserConfigRegisterApplyErrorCode code =
        UserConfigRegisterApplyErrorCode::
            unsupported_operation;
    std::uint16_t start_offset = 0;
    std::size_t value_count = 0;
    std::optional<GraphicsIrUnsupportedReason>
        unsupported_reason;

    auto operator<=>(
        const UserConfigRegisterApplyError&) const = default;
};

using UserConfigRegisterApplyResult =
    astraea::core::Result<
        UserConfigRegisterWriteEffect,
        UserConfigRegisterApplyError>;

// Applies one generic UCONFIG-register Graphics IR range atomically.
//
// The complete destination extent is validated before any state mutation.
// Register values remain generic AMD state; PS5-specific field meaning is
// interpreted only by later evidence-bounded consumers.
[[nodiscard]] UserConfigRegisterApplyResult
apply_user_config_register_graphics_ir(
    const GraphicsIrEmission& emission,
    UserConfigRegisterState& state) noexcept;

}  // namespace astraea::graphics
