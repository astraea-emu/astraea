#pragma once

#include <array>
#include <bitset>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/pm4_set_context_reg_ir.hpp>

namespace astraea::graphics {

inline constexpr std::size_t kContextRegisterStateDwords =
    kPm4ContextRegisterWindowDwords;

struct ContextRegisterState {
    std::array<std::uint32_t, kContextRegisterStateDwords> values{};
    std::bitset<kContextRegisterStateDwords> initialized{};

    bool operator==(const ContextRegisterState&) const = default;
};

struct ContextRegisterWriteEffect {
    std::uint16_t start_offset = 0;
    std::size_t value_count = 0;

    auto operator<=>(const ContextRegisterWriteEffect&) const = default;
};

enum class ContextRegisterApplyErrorCode {
    unsupported_operation,
    register_range_out_of_bounds,
};

struct ContextRegisterApplyError {
    ContextRegisterApplyErrorCode code =
        ContextRegisterApplyErrorCode::unsupported_operation;
    std::uint16_t start_offset = 0;
    std::size_t value_count = 0;
    std::optional<GraphicsIrUnsupportedReason> unsupported_reason;

    auto operator<=>(const ContextRegisterApplyError&) const = default;
};

using ContextRegisterApplyResult =
    astraea::core::Result<
        ContextRegisterWriteEffect,
        ContextRegisterApplyError>;

// Applies one generic context-register Graphics IR range atomically.
//
// The complete destination extent is validated before any state mutation.
// Unsupported Graphics IR never mutates the state. Register values remain
// generic AMD context-register state; no PS5-specific meaning is assigned.
[[nodiscard]] ContextRegisterApplyResult
apply_context_register_graphics_ir(
    const GraphicsIrEmission& emission,
    ContextRegisterState& state) noexcept;

}  // namespace astraea::graphics
