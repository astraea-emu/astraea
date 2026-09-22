#include <astraea/trace/graphics_v0.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace astraea::trace {
namespace {

[[nodiscard]] GraphicsTraceAdapterErrorV0 adapter_error(
    GraphicsTraceAdapterErrorCodeV0 code) noexcept {
    return GraphicsTraceAdapterErrorV0{
        .code = code,
    };
}

[[nodiscard]] bool size_to_u64(
    std::size_t value,
    std::uint64_t& output) noexcept {
    if constexpr (
        sizeof(std::size_t) >
        sizeof(std::uint64_t)) {
        if (value >
            static_cast<std::size_t>(
                std::numeric_limits<
                    std::uint64_t>::max())) {
            return false;
        }
    }

    output = static_cast<std::uint64_t>(value);
    return true;
}

[[nodiscard]] TraceFieldV0 u64_field(
    std::string name,
    std::uint64_t value) {
    return TraceFieldV0{
        .name = std::move(name),
        .value = TraceValueV0{value},
    };
}

[[nodiscard]] TraceFieldV0 text_field(
    std::string name,
    std::string value) {
    return TraceFieldV0{
        .name = std::move(name),
        .value =
            TraceValueV0{
                std::move(value)},
    };
}

[[nodiscard]] TraceFieldV0 bytes_field(
    std::string name,
    std::vector<std::byte> value) {
    return TraceFieldV0{
        .name = std::move(name),
        .value =
            TraceValueV0{
                std::move(value)},
    };
}

[[nodiscard]] std::vector<std::byte> packet_bytes(
    const astraea::graphics::RawPacket& packet) {
    std::vector<std::byte> bytes;
    bytes.reserve(
        packet.raw_words.size() *
        astraea::graphics::kPacketWordBytes);

    for (const auto& word : packet.raw_words) {
        bytes.insert(
            bytes.end(),
            word.bytes.begin(),
            word.bytes.end());
    }

    return bytes;
}

[[nodiscard]] std::vector<std::byte> instruction_bytes(
    const astraea::graphics::Rdna2Instruction&
        instruction) {
    std::vector<std::byte> bytes(
        instruction.raw_encoding.begin(),
        instruction.raw_encoding.end());

    if (instruction.sop1.has_value() &&
        instruction.sop1->literal_constant.has_value()) {
        const auto literal =
            instruction.sop1->literal_constant.value();
        bytes.reserve(8);
        bytes.push_back(
            static_cast<std::byte>(literal & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (literal >> 8U) & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (literal >> 16U) & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (literal >> 24U) & 0xffU));
    }

    if (instruction.vop1.has_value() &&
        instruction.vop1->source_extension.has_value()) {
        const auto extension =
            instruction.vop1->source_extension.value();
        bytes.reserve(8);
        bytes.push_back(
            static_cast<std::byte>(extension & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (extension >> 8U) & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (extension >> 16U) & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (extension >> 24U) & 0xffU));
    }

    if (instruction.vop2.has_value() &&
        instruction.vop2->source0_extension.has_value()) {
        const auto extension =
            instruction.vop2->source0_extension.value();
        bytes.reserve(8);
        bytes.push_back(
            static_cast<std::byte>(extension & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (extension >> 8U) & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (extension >> 16U) & 0xffU));
        bytes.push_back(
            static_cast<std::byte>(
                (extension >> 24U) & 0xffU));
    }

    return bytes;
}

[[nodiscard]] std::string packet_error_code_text(
    astraea::graphics::PacketErrorCode code) {
    switch (code) {
    case astraea::graphics::PacketErrorCode::
        command_buffer_not_word_aligned:
        return "command_buffer_not_word_aligned";
    case astraea::graphics::PacketErrorCode::empty_packet:
        return "empty_packet";
    case astraea::graphics::PacketErrorCode::
        packet_range_overflow:
        return "packet_range_overflow";
    case astraea::graphics::PacketErrorCode::
        packet_range_out_of_bounds:
        return "packet_range_out_of_bounds";
    case astraea::graphics::PacketErrorCode::
        host_allocation_failure:
        return "host_allocation_failure";
    }

    return "host_allocation_failure";
}

[[nodiscard]] std::string rdna2_error_code_text(
    astraea::graphics::Rdna2DecodeErrorCode code) {
    switch (code) {
    case astraea::graphics::Rdna2DecodeErrorCode::
        source_offset_overflow:
        return "source_offset_overflow";
    case astraea::graphics::Rdna2DecodeErrorCode::
        instruction_out_of_bounds:
        return "instruction_out_of_bounds";
    }

    return "instruction_out_of_bounds";
}

[[nodiscard]] std::string packet_kind_text(
    astraea::graphics::PacketKind kind) {
    switch (kind) {
    case astraea::graphics::PacketKind::unknown:
        return "unknown";
    }

    return "unknown";
}

[[nodiscard]] std::string graphics_ir_reason_text(
    astraea::graphics::GraphicsIrUnsupportedReason
        reason) {
    switch (reason) {
    case astraea::graphics::
        GraphicsIrUnsupportedReason::
            packet_semantics_unknown:
        return "packet_semantics_unknown";
    }

    return "packet_semantics_unknown";
}

[[nodiscard]] std::string rdna2_format_text(
    astraea::graphics::Rdna2InstructionFormat format) {
    switch (format) {
    case astraea::graphics::Rdna2InstructionFormat::sopp:
        return "sopp";
    case astraea::graphics::Rdna2InstructionFormat::sop1:
        return "sop1";
    case astraea::graphics::Rdna2InstructionFormat::vop1:
        return "vop1";
    case astraea::graphics::Rdna2InstructionFormat::vop2:
        return "vop2";
    case astraea::graphics::
        Rdna2InstructionFormat::unsupported:
        return "unsupported";
    }

    return "unsupported";
}

[[nodiscard]] std::string rdna2_kind_text(
    astraea::graphics::Rdna2InstructionKind kind) {
    switch (kind) {
    case astraea::graphics::Rdna2InstructionKind::s_nop:
        return "s_nop";
    case astraea::graphics::
        Rdna2InstructionKind::s_endpgm:
        return "s_endpgm";
    case astraea::graphics::
        Rdna2InstructionKind::s_branch:
        return "s_branch";
    case astraea::graphics::
        Rdna2InstructionKind::s_cbranch_scc0:
        return "s_cbranch_scc0";
    case astraea::graphics::
        Rdna2InstructionKind::s_cbranch_scc1:
        return "s_cbranch_scc1";
    case astraea::graphics::
        Rdna2InstructionKind::s_cbranch_vccz:
        return "s_cbranch_vccz";
    case astraea::graphics::
        Rdna2InstructionKind::s_cbranch_vccnz:
        return "s_cbranch_vccnz";
    case astraea::graphics::
        Rdna2InstructionKind::s_cbranch_execz:
        return "s_cbranch_execz";
    case astraea::graphics::
        Rdna2InstructionKind::s_cbranch_execnz:
        return "s_cbranch_execnz";
    case astraea::graphics::
        Rdna2InstructionKind::s_barrier:
        return "s_barrier";
    case astraea::graphics::
        Rdna2InstructionKind::s_waitcnt:
        return "s_waitcnt";
    case astraea::graphics::
        Rdna2InstructionKind::s_mov_b32:
        return "s_mov_b32";
    case astraea::graphics::
        Rdna2InstructionKind::s_mov_b64:
        return "s_mov_b64";
    case astraea::graphics::
        Rdna2InstructionKind::v_mov_b32:
        return "v_mov_b32";
    case astraea::graphics::
        Rdna2InstructionKind::v_add_f32:
        return "v_add_f32";
    case astraea::graphics::
        Rdna2InstructionKind::unknown_sopp_opcode:
        return "unknown_sopp_opcode";
    case astraea::graphics::
        Rdna2InstructionKind::unknown_sop1_opcode:
        return "unknown_sop1_opcode";
    case astraea::graphics::
        Rdna2InstructionKind::unknown_vop1_opcode:
        return "unknown_vop1_opcode";
    case astraea::graphics::
        Rdna2InstructionKind::unknown_vop2_opcode:
        return "unknown_vop2_opcode";
    case astraea::graphics::
        Rdna2InstructionKind::unsupported_encoding:
        return "unsupported_encoding";
    }

    return "unsupported_encoding";
}

[[nodiscard]] std::string shader_ir_branch_condition_text(
    astraea::graphics::ShaderIrBranchCondition condition) {
    switch (condition) {
    case astraea::graphics::ShaderIrBranchCondition::scc_zero:
        return "scc_zero";
    case astraea::graphics::ShaderIrBranchCondition::scc_one:
        return "scc_one";
    case astraea::graphics::ShaderIrBranchCondition::vcc_zero:
        return "vcc_zero";
    case astraea::graphics::ShaderIrBranchCondition::vcc_nonzero:
        return "vcc_nonzero";
    case astraea::graphics::ShaderIrBranchCondition::exec_zero:
        return "exec_zero";
    case astraea::graphics::ShaderIrBranchCondition::exec_nonzero:
        return "exec_nonzero";
    }

    return "scc_zero";
}

[[nodiscard]] std::string shader_ir_special_scalar_source_text(
    astraea::graphics::ShaderIrSpecialScalarSourceKind32 kind) {
    using Kind =
        astraea::graphics::ShaderIrSpecialScalarSourceKind32;

    switch (kind) {
    case Kind::vcc_lo:
        return "vcc_lo";
    case Kind::vcc_hi:
        return "vcc_hi";
    case Kind::m0:
        return "m0";
    case Kind::null_register:
        return "null";
    case Kind::exec_lo:
        return "exec_lo";
    case Kind::exec_hi:
        return "exec_hi";
    }

    return "vcc_lo";
}

[[nodiscard]] std::string shader_ir_reason_text(
    astraea::graphics::ShaderIrUnsupportedReason
        reason) {
    switch (reason) {
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unknown_sopp_opcode:
        return "unknown_sopp_opcode";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unknown_sop1_opcode:
        return "unknown_sop1_opcode";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unknown_vop1_opcode:
        return "unknown_vop1_opcode";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unknown_vop2_opcode:
        return "unknown_vop2_opcode";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unsupported_scalar_operand:
        return "unsupported_scalar_operand";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unsupported_vector_operand:
        return "unsupported_vector_operand";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            unsupported_encoding:
        return "unsupported_encoding";
    case astraea::graphics::
        ShaderIrUnsupportedReason::
            invalid_decoded_instruction:
        return "invalid_decoded_instruction";
    }

    return "invalid_decoded_instruction";
}

[[nodiscard]] std::string shader_cfg_edge_kind_text(
    astraea::graphics::ShaderCfgEdgeKind kind) {
    using Kind = astraea::graphics::ShaderCfgEdgeKind;

    switch (kind) {
    case Kind::linear_fallthrough:
        return "linear_fallthrough";
    case Kind::unconditional_branch:
        return "unconditional_branch";
    case Kind::conditional_branch_taken:
        return "conditional_branch_taken";
    case Kind::conditional_branch_fallthrough:
        return "conditional_branch_fallthrough";
    }

    return "linear_fallthrough";
}

[[nodiscard]] std::string shader_branch_decision_kind_text(
    astraea::graphics::ShaderBranchDecisionKind kind) {
    using Kind =
        astraea::graphics::ShaderBranchDecisionKind;

    switch (kind) {
    case Kind::unconditional:
        return "unconditional";
    case Kind::conditional:
        return "conditional";
    }

    return "unconditional";
}

[[nodiscard]] std::uint64_t shader_scalar_write_width_bits(
    astraea::graphics::ShaderScalarWriteWidth width) noexcept {
    switch (width) {
    case astraea::graphics::ShaderScalarWriteWidth::bits32:
        return 32U;
    case astraea::graphics::ShaderScalarWriteWidth::bits64:
        return 64U;
    }

    return 32U;
}

[[nodiscard]] GraphicsTraceEventResultV0
size_failure() {
    return GraphicsTraceEventResultV0::failure(
        adapter_error(
            GraphicsTraceAdapterErrorCodeV0::
                host_size_unrepresentable));
}

}  // namespace

GraphicsTraceEventResultV0
trace_raw_graphics_packet_v0(
    std::uint64_t event_id,
    const astraea::graphics::RawPacket& packet) {
    try {
        std::uint64_t word_offset = 0;
        std::uint64_t word_count = 0;
        if (!size_to_u64(
                packet.extent.word_offset,
                word_offset) ||
            !size_to_u64(
                packet.extent.word_count,
                word_count)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "graphics.frontend",
                .type = "packet",
                .guest = std::nullopt,
                .stable =
                    std::vector<TraceFieldV0>{
                        text_field(
                            "kind",
                            packet_kind_text(
                                packet.kind)),
                    },
                .diagnostics =
                    std::vector<TraceFieldV0>{
                        bytes_field(
                            "raw_words",
                            packet_bytes(packet)),
                        u64_field(
                            "word_count",
                            word_count),
                        u64_field(
                            "word_offset",
                            word_offset),
                    },
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_graphics_packet_error_v0(
    std::uint64_t event_id,
    const astraea::graphics::PacketError& error) {
    try {
        std::uint64_t word_offset = 0;
        std::uint64_t word_count = 0;
        if (!size_to_u64(
                error.word_offset,
                word_offset) ||
            !size_to_u64(
                error.word_count,
                word_count)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "graphics.frontend",
                .type = "error",
                .guest = std::nullopt,
                .stable =
                    std::vector<TraceFieldV0>{
                        text_field(
                            "code",
                            packet_error_code_text(
                                error.code)),
                    },
                .diagnostics =
                    std::vector<TraceFieldV0>{
                        u64_field(
                            "word_count",
                            word_count),
                        u64_field(
                            "word_offset",
                            word_offset),
                    },
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_graphics_ir_v0(
    std::uint64_t event_id,
    const astraea::graphics::GraphicsIrEmission&
        emission) {
    try {
        const auto& unsupported =
            std::get<
                astraea::graphics::
                    GraphicsIrUnsupported>(
                emission.operation);
        const auto& packet =
            emission.provenance.source_packet;

        std::uint64_t word_offset = 0;
        std::uint64_t word_count = 0;
        if (!size_to_u64(
                packet.extent.word_offset,
                word_offset) ||
            !size_to_u64(
                packet.extent.word_count,
                word_count)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "graphics.ir",
                .type = "unsupported",
                .guest = std::nullopt,
                .stable =
                    std::vector<TraceFieldV0>{
                        text_field(
                            "reason",
                            graphics_ir_reason_text(
                                unsupported.reason)),
                    },
                .diagnostics =
                    std::vector<TraceFieldV0>{
                        bytes_field(
                            "source_raw_words",
                            packet_bytes(packet)),
                        u64_field(
                            "source_word_count",
                            word_count),
                        u64_field(
                            "source_word_offset",
                            word_offset),
                    },
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_rdna2_decode_v0(
    std::uint64_t event_id,
    const astraea::graphics::Rdna2Instruction&
        instruction) {
    try {
        std::uint64_t byte_offset = 0;
        if (!size_to_u64(
                instruction.byte_offset,
                byte_offset)) {
            return size_failure();
        }

        std::vector<TraceFieldV0> stable{
            text_field(
                "format",
                rdna2_format_text(
                    instruction.format)),
            text_field(
                "kind",
                rdna2_kind_text(
                    instruction.kind)),
        };

        if (instruction.sopp.has_value()) {
            stable.push_back(
                u64_field(
                    "opcode",
                    instruction.sopp->opcode));
            stable.push_back(
                text_field(
                    "simm16",
                    std::to_string(
                        static_cast<int>(
                            instruction.sopp->simm16))));
        }

        if (instruction.sop1.has_value()) {
            stable.push_back(
                u64_field(
                    "opcode",
                    instruction.sop1->opcode));
            stable.push_back(
                u64_field(
                    "destination_selector",
                    instruction.sop1->
                        destination_selector));
            stable.push_back(
                u64_field(
                    "source_selector",
                    instruction.sop1->
                        source_selector));
            if (instruction.sop1->
                    literal_constant.has_value()) {
                stable.push_back(
                    u64_field(
                        "literal_constant_bits",
                        instruction.sop1->
                            literal_constant.value()));
            }
        }

        if (instruction.vop1.has_value()) {
            stable.push_back(
                u64_field(
                    "opcode",
                    instruction.vop1->opcode));
            stable.push_back(
                u64_field(
                    "destination_selector",
                    instruction.vop1->
                        destination_selector));
            stable.push_back(
                u64_field(
                    "source_selector",
                    instruction.vop1->
                        source_selector));
        }

        if (instruction.vop2.has_value()) {
            stable.push_back(
                u64_field(
                    "opcode",
                    instruction.vop2->opcode));
            stable.push_back(
                u64_field(
                    "destination_selector",
                    instruction.vop2->
                        destination_selector));
            stable.push_back(
                u64_field(
                    "source0_selector",
                    instruction.vop2->
                        source0_selector));
            stable.push_back(
                u64_field(
                    "source1_selector",
                    instruction.vop2->
                        source1_selector));
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.decode",
                .type = "instruction",
                .guest = std::nullopt,
                .stable = std::move(stable),
                .diagnostics =
                    std::vector<TraceFieldV0>{
                        u64_field(
                            "source_byte_offset",
                            byte_offset),
                        bytes_field(
                            "source_raw_encoding",
                            instruction_bytes(
                                instruction)),
                    },
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_rdna2_decode_error_v0(
    std::uint64_t event_id,
    const astraea::graphics::Rdna2DecodeError& error) {
    try {
        std::uint64_t word_index = 0;
        std::uint64_t available_words = 0;
        if (!size_to_u64(
                error.word_index,
                word_index) ||
            !size_to_u64(
                error.available_words,
                available_words)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.decode",
                .type = "error",
                .guest = std::nullopt,
                .stable =
                    std::vector<TraceFieldV0>{
                        text_field(
                            "code",
                            rdna2_error_code_text(
                                error.code)),
                    },
                .diagnostics =
                    std::vector<TraceFieldV0>{
                        u64_field(
                            "available_words",
                            available_words),
                        u64_field(
                            "word_index",
                            word_index),
                    },
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_shader_ir_v0(
    std::uint64_t event_id,
    const astraea::graphics::ShaderIrEmission&
        emission) {
    try {
        std::string event_type;
        std::vector<TraceFieldV0> stable;

        std::visit(
            [&event_type, &stable](const auto& operation) {
                using Operation =
                    std::decay_t<decltype(operation)>;

                if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::ShaderIrNop>) {
                    event_type = "nop";
                    stable.push_back(
                        u64_field(
                            "repeat_count",
                            operation.repeat_count));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrEndProgram>) {
                    event_type = "end_program";
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrRelativeBranch>) {
                    event_type = "relative_branch";
                    stable.push_back(
                        text_field(
                            "byte_delta",
                            std::to_string(
                                operation.byte_delta)));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrConditionalRelativeBranch>) {
                    event_type = "conditional_relative_branch";
                    stable.push_back(
                        text_field(
                            "condition",
                            shader_ir_branch_condition_text(
                                operation.condition)));
                    stable.push_back(
                        text_field(
                            "byte_delta",
                            std::to_string(
                                operation.byte_delta)));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrWorkgroupBarrier>) {
                    event_type = "workgroup_barrier";
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrWaitCount>) {
                    event_type = "wait_count";
                    stable.push_back(
                        u64_field(
                            "vmcnt",
                            operation.vmcnt));
                    stable.push_back(
                        u64_field(
                            "expcnt",
                            operation.expcnt));
                    stable.push_back(
                        u64_field(
                            "lgkmcnt",
                            operation.lgkmcnt));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrScalarMove32>) {
                    event_type = "scalar_move_32";
                    stable.push_back(
                        u64_field(
                            "destination_sgpr",
                            operation.destination.index));
                    if (std::holds_alternative<
                            astraea::graphics::ShaderIrSgpr>(
                            operation.source)) {
                        stable.push_back(
                            text_field(
                                "source_kind",
                                "sgpr"));
                        stable.push_back(
                            u64_field(
                                "source_sgpr",
                                std::get<
                                    astraea::graphics::
                                        ShaderIrSgpr>(
                                    operation.source)
                                    .index));
                    } else if (std::holds_alternative<
                                   astraea::graphics::
                                       ShaderIrSpecialScalarSource32>(
                                   operation.source)) {
                        stable.push_back(
                            text_field(
                                "source_kind",
                                "special_register"));
                        stable.push_back(
                            text_field(
                                "source_special_register",
                                shader_ir_special_scalar_source_text(
                                    std::get<
                                        astraea::graphics::
                                            ShaderIrSpecialScalarSource32>(
                                        operation.source)
                                        .kind)));
                    } else if (std::holds_alternative<
                                   astraea::graphics::
                                       ShaderIrInlineInteger32>(
                                   operation.source)) {
                        stable.push_back(
                            text_field(
                                "source_kind",
                                "inline_integer"));
                        stable.push_back(
                            text_field(
                                "source_inline_integer",
                                std::to_string(
                                    std::get<
                                        astraea::graphics::
                                            ShaderIrInlineInteger32>(
                                        operation.source)
                                        .value)));
                    } else {
                        stable.push_back(
                            text_field(
                                "source_kind",
                                "literal"));
                        stable.push_back(
                            u64_field(
                                "source_literal_bits",
                                std::get<
                                    astraea::graphics::
                                        ShaderIrLiteral32>(
                                    operation.source)
                                    .bits));
                    }
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrScalarMove64>) {
                    event_type = "scalar_move_64";
                    stable.push_back(
                        u64_field(
                            "destination_sgpr_pair_start",
                            operation.destination.first_index));
                    stable.push_back(
                        u64_field(
                            "source_sgpr_pair_start",
                            operation.source.first_index));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrVectorMove32>) {
                    event_type = "vector_move_32";
                    stable.push_back(
                        u64_field(
                            "destination_vgpr",
                            operation.destination.index));
                    stable.push_back(
                        u64_field(
                            "source_vgpr",
                            operation.source.index));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrVectorAddF32>) {
                    event_type = "vector_add_f32";
                    stable.push_back(
                        u64_field(
                            "destination_vgpr",
                            operation.destination.index));
                    stable.push_back(
                        u64_field(
                            "source0_vgpr",
                            operation.source0.index));
                    stable.push_back(
                        u64_field(
                            "source1_vgpr",
                            operation.source1.index));
                } else if constexpr (
                    std::is_same_v<
                        Operation,
                        astraea::graphics::
                            ShaderIrUnsupported>) {
                    event_type = "unsupported";
                    stable.push_back(
                        text_field(
                            "reason",
                            shader_ir_reason_text(
                                operation.reason)));
                }
            },
            emission.operation);

        const auto& instruction =
            emission.provenance.source_instruction;
        std::uint64_t byte_offset = 0;
        if (!size_to_u64(
                instruction.byte_offset,
                byte_offset)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.ir",
                .type = std::move(event_type),
                .guest = std::nullopt,
                .stable = std::move(stable),
                .diagnostics =
                    std::vector<TraceFieldV0>{
                        u64_field(
                            "source_byte_offset",
                            byte_offset),
                        bytes_field(
                            "source_raw_encoding",
                            instruction_bytes(
                                instruction)),
                    },
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_shader_cfg_block_v0(
    std::uint64_t event_id,
    std::size_t block_index,
    const astraea::graphics::ShaderCfgBasicBlock& block) {
    try {
        std::uint64_t block_index_u64 = 0;
        std::uint64_t first_emission_index = 0;
        std::uint64_t emission_count = 0;
        std::uint64_t successor_count = 0;
        if (!size_to_u64(
                block_index,
                block_index_u64) ||
            !size_to_u64(
                block.first_emission_index,
                first_emission_index) ||
            !size_to_u64(
                block.emission_count,
                emission_count) ||
            !size_to_u64(
                block.successors.size(),
                successor_count)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.cfg",
                .type = "block",
                .guest = std::nullopt,
                .stable =
                    std::vector<TraceFieldV0>{
                        u64_field(
                            "block_index",
                            block_index_u64),
                        u64_field(
                            "first_emission_index",
                            first_emission_index),
                        u64_field(
                            "emission_count",
                            emission_count),
                        u64_field(
                            "successor_count",
                            successor_count),
                    },
                .diagnostics = {},
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_shader_cfg_edge_v0(
    std::uint64_t event_id,
    std::size_t source_block_index,
    std::size_t edge_index,
    const astraea::graphics::ShaderCfgEdge& edge) {
    try {
        std::uint64_t source_block_index_u64 = 0;
        std::uint64_t edge_index_u64 = 0;
        std::uint64_t target_block_index = 0;
        if (!size_to_u64(
                source_block_index,
                source_block_index_u64) ||
            !size_to_u64(
                edge_index,
                edge_index_u64) ||
            !size_to_u64(
                edge.target_block_index,
                target_block_index)) {
            return size_failure();
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.cfg",
                .type = "edge",
                .guest = std::nullopt,
                .stable =
                    std::vector<TraceFieldV0>{
                        u64_field(
                            "source_block_index",
                            source_block_index_u64),
                        u64_field(
                            "edge_index",
                            edge_index_u64),
                        text_field(
                            "kind",
                            shader_cfg_edge_kind_text(
                                edge.kind)),
                        u64_field(
                            "target_block_index",
                            target_block_index),
                    },
                .diagnostics = {},
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_shader_scalar_execution_v0(
    std::uint64_t event_id,
    const astraea::graphics::ShaderScalarExecutionEffect&
        effect) {
    try {
        std::vector<TraceFieldV0> stable{
            u64_field(
                "destination_first_sgpr",
                effect.first_destination_sgpr),
            u64_field(
                "write_width_bits",
                shader_scalar_write_width_bits(
                    effect.width)),
            u64_field(
                "written_value0_bits",
                effect.written_values[0]),
        };

        if (effect.width ==
            astraea::graphics::ShaderScalarWriteWidth::
                bits64) {
            stable.push_back(
                u64_field(
                    "written_value1_bits",
                    effect.written_values[1]));
        }

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.execute",
                .type = "scalar_write",
                .guest = std::nullopt,
                .stable = std::move(stable),
                .diagnostics = {},
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

GraphicsTraceEventResultV0
trace_shader_branch_decision_v0(
    std::uint64_t event_id,
    const astraea::graphics::ShaderBranchDecision&
        decision) {
    try {
        std::vector<TraceFieldV0> stable{
            text_field(
                "kind",
                shader_branch_decision_kind_text(
                    decision.kind)),
        };

        if (decision.condition.has_value()) {
            stable.push_back(
                text_field(
                    "condition",
                    shader_ir_branch_condition_text(
                        decision.condition.value())));
        }

        stable.push_back(
            text_field(
                "byte_delta",
                std::to_string(
                    decision.byte_delta)));
        stable.push_back(
            TraceFieldV0{
                .name = "taken",
                .value =
                    TraceValueV0{
                        decision.taken},
            });

        return GraphicsTraceEventResultV0::success(
            TraceEventV0{
                .id = event_id,
                .subsystem = "shader.execute",
                .type = "branch_decision",
                .guest = std::nullopt,
                .stable = std::move(stable),
                .diagnostics = {},
            });
    } catch (const std::bad_alloc&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return GraphicsTraceEventResultV0::failure(
            adapter_error(
                GraphicsTraceAdapterErrorCodeV0::
                    host_allocation_failure));
    }
}

}  // namespace astraea::trace
