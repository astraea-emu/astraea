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
