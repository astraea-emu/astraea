#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/packet.hpp>
#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_cfg.hpp>
#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_scalar_execution.hpp>
#include <astraea/trace/v0.hpp>

namespace astraea::trace {

enum class GraphicsTraceAdapterErrorCodeV0 {
    host_size_unrepresentable,
    host_allocation_failure,
};

struct GraphicsTraceAdapterErrorV0 {
    GraphicsTraceAdapterErrorCodeV0 code =
        GraphicsTraceAdapterErrorCodeV0::
            host_allocation_failure;

    auto operator<=>(const GraphicsTraceAdapterErrorV0&) const =
        default;
};

using GraphicsTraceEventResultV0 =
    astraea::core::Result<
        TraceEventV0,
        GraphicsTraceAdapterErrorV0>;

[[nodiscard]] GraphicsTraceEventResultV0
trace_raw_graphics_packet_v0(
    std::uint64_t event_id,
    const astraea::graphics::RawPacket& packet);

[[nodiscard]] GraphicsTraceEventResultV0
trace_graphics_packet_error_v0(
    std::uint64_t event_id,
    const astraea::graphics::PacketError& error);

[[nodiscard]] GraphicsTraceEventResultV0
trace_graphics_ir_v0(
    std::uint64_t event_id,
    const astraea::graphics::GraphicsIrEmission& emission);

[[nodiscard]] GraphicsTraceEventResultV0
trace_rdna2_decode_v0(
    std::uint64_t event_id,
    const astraea::graphics::Rdna2Instruction& instruction);

[[nodiscard]] GraphicsTraceEventResultV0
trace_rdna2_decode_error_v0(
    std::uint64_t event_id,
    const astraea::graphics::Rdna2DecodeError& error);

[[nodiscard]] GraphicsTraceEventResultV0
trace_shader_ir_v0(
    std::uint64_t event_id,
    const astraea::graphics::ShaderIrEmission& emission);

[[nodiscard]] GraphicsTraceEventResultV0
trace_shader_cfg_block_v0(
    std::uint64_t event_id,
    std::size_t block_index,
    const astraea::graphics::ShaderCfgBasicBlock& block);

[[nodiscard]] GraphicsTraceEventResultV0
trace_shader_cfg_edge_v0(
    std::uint64_t event_id,
    std::size_t source_block_index,
    std::size_t edge_index,
    const astraea::graphics::ShaderCfgEdge& edge);

[[nodiscard]] GraphicsTraceEventResultV0
trace_shader_scalar_execution_v0(
    std::uint64_t event_id,
    const astraea::graphics::ShaderScalarExecutionEffect& effect);

}  // namespace astraea::trace
