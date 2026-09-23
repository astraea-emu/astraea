#pragma once

#include <compare>
#include <cstdint>
#include <variant>
#include <vector>

#include <astraea/graphics/gpu_address.hpp>
#include <astraea/graphics/packet.hpp>

namespace astraea::graphics {

enum class GraphicsIrUnsupportedReason {
    packet_semantics_unknown,
};

struct GraphicsIrUnsupported {
    GraphicsIrUnsupportedReason reason =
        GraphicsIrUnsupportedReason::packet_semantics_unknown;

    auto operator<=>(const GraphicsIrUnsupported&) const = default;
};

struct GraphicsIrShaderRegisterWriteRange {
    std::uint16_t start_offset = 0;
    std::vector<std::uint32_t> values;

    auto operator<=>(const GraphicsIrShaderRegisterWriteRange&) const =
        default;
};

struct GraphicsIrContextRegisterWriteRange {
    std::uint16_t start_offset = 0;
    std::vector<std::uint32_t> values;

    auto operator<=>(const GraphicsIrContextRegisterWriteRange&) const =
        default;
};

struct GraphicsIrUserConfigRegisterWriteRange {
    std::uint16_t start_offset = 0;
    std::vector<std::uint32_t> values;

    auto operator<=>(const GraphicsIrUserConfigRegisterWriteRange&) const =
        default;
};

struct GraphicsIrSetInstanceCount {
    std::uint32_t instance_count = 0;

    auto operator<=>(const GraphicsIrSetInstanceCount&) const = default;
};

struct GraphicsIrDrawIndexAuto {
    std::uint32_t index_count = 0;

    // Preserved exactly from the packet. Later bounded consumers may interpret
    // only evidence-backed fields (for the first PS5 profile, the observed
    // auto-index initiator is 2) rather than assigning semantics here.
    std::uint32_t initiator = 0;

    auto operator<=>(const GraphicsIrDrawIndexAuto&) const = default;
};

struct GraphicsIrGpuMemoryWrite {
    GpuVirtualAddress destination;
    std::vector<std::uint32_t> values;

    auto operator<=>(const GraphicsIrGpuMemoryWrite&) const = default;
};

using GraphicsIrOperation =
    std::variant<
        GraphicsIrUnsupported,
        GraphicsIrShaderRegisterWriteRange,
        GraphicsIrContextRegisterWriteRange,
        GraphicsIrUserConfigRegisterWriteRange,
        GraphicsIrSetInstanceCount,
        GraphicsIrDrawIndexAuto,
        GraphicsIrGpuMemoryWrite>;

struct GraphicsIrProvenance {
    RawPacket source_packet;

    auto operator<=>(const GraphicsIrProvenance&) const = default;
};

// Deliberately has no default comparison operator.
//
// Behavioral identity is the operation. Raw packet data is provenance and must
// not become semantic identity accidentally. Use
// graphics_ir_semantically_equal() when comparing behavior.
struct GraphicsIrEmission {
    GraphicsIrOperation operation;
    GraphicsIrProvenance provenance;
};

[[nodiscard]] GraphicsIrEmission make_unsupported_packet_ir(
    RawPacket packet);

[[nodiscard]] bool graphics_ir_semantically_equal(
    const GraphicsIrEmission& left,
    const GraphicsIrEmission& right) noexcept;

}  // namespace astraea::graphics
