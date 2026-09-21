#include <astraea/graphics/graphics_ir.hpp>

#include <utility>

namespace astraea::graphics {

GraphicsIrEmission make_unsupported_packet_ir(
    RawPacket packet) {
    return GraphicsIrEmission{
        .operation =
            GraphicsIrUnsupported{
                .reason =
                    GraphicsIrUnsupportedReason::
                        packet_semantics_unknown,
            },
        .provenance =
            GraphicsIrProvenance{
                .source_packet = std::move(packet),
            },
    };
}

bool graphics_ir_semantically_equal(
    const GraphicsIrEmission& left,
    const GraphicsIrEmission& right) noexcept {
    return left.operation == right.operation;
}

}  // namespace astraea::graphics
