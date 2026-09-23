#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/packet.hpp>
#include <astraea/graphics/pm4_set_sh_reg_ir.hpp>
#include <astraea/graphics/pm4_type3_framing.hpp>
#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_cfg.hpp>
#include <astraea/graphics/shader_control_execution.hpp>
#include <astraea/graphics/shader_ir.hpp>
#include <astraea/graphics/shader_scalar_execution.hpp>
#include <astraea/trace/diff_v0.hpp>
#include <astraea/trace/graphics_v0.hpp>
#include <astraea/trace/v0.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;
constexpr std::uint32_t kSop1Base = 0xbe800000U;
constexpr std::uint32_t kVop1Base = 0x7e000000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
}

astraea::graphics::RawPacket make_packet(
    std::array<std::byte, 4> bytes) {
    const std::vector<std::byte> command_buffer(
        bytes.begin(),
        bytes.end());

    auto result =
        astraea::graphics::parse_raw_packet(
            command_buffer,
            astraea::graphics::PacketExtent{
                .word_offset = 0,
                .word_count = 1,
            });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::graphics::Rdna2Instruction decode_one(
    std::uint32_t word) {
    const std::array<std::uint32_t, 1> words{word};

    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::graphics::Rdna2Instruction decode_literal(
    std::uint32_t word,
    std::uint32_t literal) {
    const std::array<std::uint32_t, 2> words{
        word,
        literal,
    };

    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::trace::TraceDocumentV0 document(
    astraea::trace::TraceEventV0 event) {
    return astraea::trace::TraceDocumentV0{
        .run =
            astraea::trace::TraceRunMetadataV0{
                .target_kind = "synthetic-probe",
                .platform_family = "astraea",
                .architecture = "x86-64",
                .probe_id = std::nullopt,
                .probe_version = std::nullopt,
                .case_sha256 = std::nullopt,
            },
        .provenance =
            astraea::trace::TraceProvenanceV0{
                .source_kind =
                    "astraea_owned_synthetic",
                .producer =
                    astraea::trace::TraceProducerV0{
                        .name = "astraea",
                        .version = "v0",
                        .commit = std::nullopt,
                    },
                .artifact_digests = {},
            },
        .events =
            std::vector<astraea::trace::TraceEventV0>{
                std::move(event)},
        .diagnostics = {},
    };
}

void require_equivalent(
    astraea::trace::TraceEventV0 left,
    astraea::trace::TraceEventV0 right) {
    auto result =
        astraea::trace::diff_trace_v0(
            document(std::move(left)),
            document(std::move(right)));

    REQUIRE(result.has_value());
    REQUIRE(result->equivalent);
    REQUIRE(result->matched_event_count == 1);
    REQUIRE_FALSE(
        result->first_divergence.has_value());
}

constexpr std::uint32_t make_sop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint8_t source) {
    return kSop1Base |
           (static_cast<std::uint32_t>(destination) << 16U) |
           (static_cast<std::uint32_t>(opcode) << 8U) |
           static_cast<std::uint32_t>(source);
}

constexpr std::uint32_t make_vop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source) {
    return kVop1Base |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(opcode) << 9U) |
           static_cast<std::uint32_t>(source);
}

constexpr std::uint32_t make_vop2(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source0,
    std::uint8_t source1) {
    return (static_cast<std::uint32_t>(opcode) << 25U) |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(source1) << 9U) |
           static_cast<std::uint32_t>(source0);
}

astraea::graphics::Rdna2Instruction decode_vop1_extension(
    std::uint32_t word,
    std::uint32_t extension) {
    const std::array<std::uint32_t, 2> words{
        word,
        extension,
    };
    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::graphics::Rdna2Instruction decode_vop2_extension(
    std::uint32_t word,
    std::uint32_t extension) {
    const std::array<std::uint32_t, 2> words{
        word,
        extension,
    };
    auto result =
        astraea::graphics::decode_rdna2_instruction(
            words,
            0);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

}  // namespace

TEST_CASE(
    "raw graphics packet adapter keeps bytes and offsets diagnostic-only",
    "[trace][graphics][v0]") {
    auto left =
        astraea::trace::trace_raw_graphics_packet_v0(
            10,
            make_packet(
                std::array<std::byte, 4>{
                    std::byte{0x10},
                    std::byte{0x11},
                    std::byte{0x12},
                    std::byte{0x13},
                }));
    auto right =
        astraea::trace::trace_raw_graphics_packet_v0(
            10,
            make_packet(
                std::array<std::byte, 4>{
                    std::byte{0x20},
                    std::byte{0x21},
                    std::byte{0x22},
                    std::byte{0x23},
                }));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->subsystem == "graphics.frontend");
    REQUIRE(left->type == "packet");
    REQUIRE(left->stable.size() == 1);
    REQUIRE(left->stable[0].name == "kind");
    REQUIRE(left->diagnostics.size() == 3);

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}

TEST_CASE(
    "Graphics IR adapter compares semantics instead of packet provenance",
    "[trace][graphics][v0]") {
    auto left_ir =
        astraea::graphics::make_unsupported_packet_ir(
            make_packet(
                std::array<std::byte, 4>{
                    std::byte{0x10},
                    std::byte{0x11},
                    std::byte{0x12},
                    std::byte{0x13},
                }));
    auto right_ir =
        astraea::graphics::make_unsupported_packet_ir(
            make_packet(
                std::array<std::byte, 4>{
                    std::byte{0x30},
                    std::byte{0x31},
                    std::byte{0x32},
                    std::byte{0x33},
                }));

    auto left =
        astraea::trace::trace_graphics_ir_v0(
            20,
            left_ir);
    auto right =
        astraea::trace::trace_graphics_ir_v0(
            20,
            right_ir);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->subsystem == "graphics.ir");
    REQUIRE(left->type == "unsupported");
    REQUIRE(left->stable.size() == 1);
    REQUIRE(left->stable[0].name == "reason");

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}

TEST_CASE(
    "RDNA2 decode adapter makes decoded immediate behavior stable",
    "[trace][graphics][v0]") {
    const auto left_instruction =
        decode_one(make_sopp(2, 4));
    const auto right_instruction =
        decode_one(make_sopp(2, 5));

    auto left =
        astraea::trace::trace_rdna2_decode_v0(
            30,
            left_instruction);
    auto right =
        astraea::trace::trace_rdna2_decode_v0(
            30,
            right_instruction);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->subsystem == "shader.decode");
    REQUIRE(left->type == "instruction");

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"simm16"});
}

TEST_CASE(
    "Shader IR adapter exposes branch semantic divergence",
    "[trace][graphics][v0]") {
    const auto left_ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(2, 4)));
    const auto right_ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(2, 5)));

    auto left =
        astraea::trace::trace_shader_ir_v0(
            40,
            left_ir);
    auto right =
        astraea::trace::trace_shader_ir_v0(
            40,
            right_ir);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->subsystem == "shader.ir");
    REQUIRE(left->type == "relative_branch");

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"byte_delta"});
}

TEST_CASE(
    "Shader IR adapter excludes unsupported raw encoding from behavioral equality",
    "[trace][graphics][v0]") {
    const auto left_ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(0x80000000U));
    const auto right_ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(0x87654321U));

    auto left =
        astraea::trace::trace_shader_ir_v0(
            50,
            left_ir);
    auto right =
        astraea::trace::trace_shader_ir_v0(
            50,
            right_ir);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->type == "unsupported");
    REQUIRE(right->type == "unsupported");

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}

TEST_CASE(
    "graphics adapter events normalize under Trace v0",
    "[trace][graphics][v0]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(0, 0x000f)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            60,
            ir);
    REQUIRE(event.has_value());

    auto normalized =
        astraea::trace::normalize_trace_v0(
            document(std::move(event).value()));

    REQUIRE(normalized.has_value());
    REQUIRE(
        normalized->events[0].stable.size() == 1);
    REQUIRE(
        normalized->events[0].stable[0].name ==
        "repeat_count");
    REQUIRE(
        normalized->events[0].diagnostics.size() ==
        2);
    REQUIRE(
        normalized->events[0].
            diagnostics[0].name ==
        "source_byte_offset");
    REQUIRE(
        normalized->events[0].
            diagnostics[1].name ==
        "source_raw_encoding");
}


TEST_CASE(
    "typed graphics frontend errors remain observable without making ranges semantic",
    "[trace][graphics][v0]") {
    const astraea::graphics::PacketError left_error{
        .code =
            astraea::graphics::PacketErrorCode::
                packet_range_out_of_bounds,
        .word_offset = 2,
        .word_count = 4,
    };
    const astraea::graphics::PacketError right_error{
        .code =
            astraea::graphics::PacketErrorCode::
                packet_range_out_of_bounds,
        .word_offset = 7,
        .word_count = 9,
    };

    auto left =
        astraea::trace::trace_graphics_packet_error_v0(
            70,
            left_error);
    auto right =
        astraea::trace::trace_graphics_packet_error_v0(
            70,
            right_error);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->type == "error");
    REQUIRE(left->stable.size() == 1);
    REQUIRE(left->stable[0].name == "code");
    REQUIRE(left->diagnostics.size() == 2);

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}

TEST_CASE(
    "typed RDNA2 decode errors remain observable without making host ranges semantic",
    "[trace][graphics][v0]") {
    const astraea::graphics::Rdna2DecodeError left_error{
        .code =
            astraea::graphics::Rdna2DecodeErrorCode::
                instruction_out_of_bounds,
        .word_index = 3,
        .available_words = 2,
    };
    const astraea::graphics::Rdna2DecodeError right_error{
        .code =
            astraea::graphics::Rdna2DecodeErrorCode::
                instruction_out_of_bounds,
        .word_index = 8,
        .available_words = 5,
    };

    auto left =
        astraea::trace::trace_rdna2_decode_error_v0(
            80,
            left_error);
    auto right =
        astraea::trace::trace_rdna2_decode_error_v0(
            80,
            right_error);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    REQUIRE(left->subsystem == "shader.decode");
    REQUIRE(left->type == "error");
    REQUIRE(left->stable.size() == 1);
    REQUIRE(left->stable[0].name == "code");
    REQUIRE(left->diagnostics.size() == 2);

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}


TEST_CASE(
    "conditional Shader IR branch exposes condition and displacement as stable semantics",
    "[trace][graphics][v0][conditional-branch]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(7, 0xfffe)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            90,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "conditional_relative_branch");
    REQUIRE(event->stable.size() == 2);
    REQUIRE(event->stable[0].name == "condition");
    REQUIRE(
        std::get<std::string>(
            event->stable[0].value) ==
        "vcc_nonzero");
    REQUIRE(event->stable[1].name == "byte_delta");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) ==
        "-4");
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "conditional Shader IR branch condition participates in trace divergence",
    "[trace][graphics][v0][conditional-branch]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            91,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(4, 4))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            91,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(5, 4))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"condition"});
}


TEST_CASE(
    "S_BARRIER Shader IR trace exposes barrier semantics only",
    "[trace][graphics][v0][barrier]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(10, 0x1234)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            92,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "workgroup_barrier");
    REQUIRE(event->stable.empty());
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "S_BARRIER trace equality excludes unused immediate provenance",
    "[trace][graphics][v0][barrier]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            93,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(10, 0x0000))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            93,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(10, 0x7fff))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}


TEST_CASE(
    "S_WAITCNT Shader IR trace exposes counter thresholds as stable semantics",
    "[trace][graphics][v0][waitcnt]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sopp(12, 0xaa35)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            94,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "wait_count");
    REQUIRE(event->stable.size() == 3);
    REQUIRE(event->stable[0].name == "vmcnt");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 37);
    REQUIRE(event->stable[1].name == "expcnt");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 3);
    REQUIRE(event->stable[2].name == "lgkmcnt");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) == 42);
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "S_WAITCNT reserved bit is diagnostic-only for trace equality",
    "[trace][graphics][v0][waitcnt]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            95,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(12, 0xaa35))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            95,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(12, 0xaab5))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    require_equivalent(
        std::move(left).value(),
        std::move(right).value());
}

TEST_CASE(
    "S_WAITCNT threshold change participates in trace divergence",
    "[trace][graphics][v0][waitcnt]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            96,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(12, 0xaa35))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            96,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sopp(12, 0xaa34))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"vmcnt"});
}


TEST_CASE(
    "SOP1 decode trace exposes raw selectors as stable decoded fields",
    "[trace][graphics][v0][sop1]") {
    const auto instruction =
        decode_one(make_sop1(3, 5, 17));

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            97,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.decode");
    REQUIRE(event->type == "instruction");
    REQUIRE(event->stable.size() == 5);
    REQUIRE(event->stable[0].name == "format");
    REQUIRE(
        std::get<std::string>(
            event->stable[0].value) == "sop1");
    REQUIRE(event->stable[1].name == "kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) == "s_mov_b32");
    REQUIRE(event->stable[2].name == "opcode");
    REQUIRE(event->stable[3].name == "destination_selector");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 5);
    REQUIRE(event->stable[4].name == "source_selector");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[4].value) == 17);
}

TEST_CASE(
    "S_MOV_B32 Shader IR trace exposes SGPR move semantics",
    "[trace][graphics][v0][sop1][mov]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 17)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            98,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "scalar_move_32");
    REQUIRE(event->stable.size() == 3);
    REQUIRE(event->stable[0].name == "destination_sgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 5);
    REQUIRE(event->stable[1].name == "source_kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) == "sgpr");
    REQUIRE(event->stable[2].name == "source_sgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) == 17);
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "S_MOV_B32 inline integer trace exposes signed semantic value",
    "[trace][graphics][v0][sop1][mov][inline-integer]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 208)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            105,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "scalar_move_32");
    REQUIRE(event->stable.size() == 3);
    REQUIRE(event->stable[0].name == "destination_sgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 5);
    REQUIRE(event->stable[1].name == "source_kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) == "inline_integer");
    REQUIRE(
        event->stable[2].name ==
        "source_inline_integer");
    REQUIRE(
        std::get<std::string>(
            event->stable[2].value) == "-16");
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "S_MOV_B32 inline integer value participates in trace divergence",
    "[trace][graphics][v0][sop1][mov][inline-integer]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            106,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 128))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            106,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 129))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "source_inline_integer"});
}

TEST_CASE(
    "S_MOV_B32 source kind participates in trace divergence",
    "[trace][graphics][v0][sop1][mov][inline-integer]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            107,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 0))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            107,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 128))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_missing_left);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "source_inline_integer"});
}

TEST_CASE(
    "S_MOV_B32 SGPR destination participates in trace divergence",
    "[trace][graphics][v0][sop1][mov]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            99,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 17))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            99,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 6, 17))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"destination_sgpr"});
}


TEST_CASE(
    "S_MOV_B32 literal decode trace exposes value and full raw encoding",
    "[trace][graphics][v0][sop1][mov][literal]") {
    const auto instruction =
        decode_literal(
            make_sop1(3, 5, 255),
            0xdeadbeefU);

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            100,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.decode");
    REQUIRE(event->stable.size() == 6);
    REQUIRE(
        event->stable[5].name ==
        "literal_constant_bits");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[5].value) ==
        0xdeadbeefU);
    REQUIRE(event->diagnostics.size() == 2);
    REQUIRE(
        std::get<std::vector<std::byte>>(
            event->diagnostics[1].value)
            .size() == 8);
}

TEST_CASE(
    "S_MOV_B32 literal Shader IR trace exposes raw 32-bit semantic bits",
    "[trace][graphics][v0][sop1][mov][literal]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_literal(
                make_sop1(3, 5, 255),
                0xdeadbeefU));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            108,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "scalar_move_32");
    REQUIRE(event->stable.size() == 3);
    REQUIRE(event->stable[1].name == "source_kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) ==
        "literal");
    REQUIRE(
        event->stable[2].name ==
        "source_literal_bits");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) ==
        0xdeadbeefU);
    REQUIRE(event->diagnostics.size() == 2);
    REQUIRE(
        std::get<std::vector<std::byte>>(
            event->diagnostics[1].value)
            .size() == 8);
}

TEST_CASE(
    "S_MOV_B32 literal bits participate in trace divergence",
    "[trace][graphics][v0][sop1][mov][literal]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            109,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_literal(
                    make_sop1(3, 5, 255),
                    0x12345678U)));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            109,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_literal(
                    make_sop1(3, 5, 255),
                    0x12345679U)));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "source_literal_bits"});
}


TEST_CASE(
    "S_MOV_B64 decode trace exposes opcode and pair selectors",
    "[trace][graphics][v0][sop1][mov64]") {
    const auto instruction =
        decode_one(make_sop1(4, 4, 16));

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            101,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.decode");
    REQUIRE(event->type == "instruction");
    REQUIRE(event->stable.size() == 5);
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) == "s_mov_b64");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 4);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[4].value) == 16);
}

TEST_CASE(
    "S_MOV_B64 Shader IR trace exposes SGPR-pair move semantics",
    "[trace][graphics][v0][sop1][mov64]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(4, 4, 16)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            102,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "scalar_move_64");
    REQUIRE(event->stable.size() == 2);
    REQUIRE(
        event->stable[0].name ==
        "destination_sgpr_pair_start");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 4);
    REQUIRE(
        event->stable[1].name ==
        "source_sgpr_pair_start");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 16);
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "S_MOV_B64 pair destination participates in trace divergence",
    "[trace][graphics][v0][sop1][mov64]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            103,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(4, 4, 16))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            103,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(4, 6, 16))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "destination_sgpr_pair_start"});
}

TEST_CASE(
    "S_MOV_B64 odd pair remains typed unsupported in Shader IR trace",
    "[trace][graphics][v0][sop1][mov64]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(4, 5, 16)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            104,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->type == "unsupported");
    REQUIRE(event->stable.size() == 1);
    REQUIRE(
        std::get<std::string>(
            event->stable[0].value) ==
        "unsupported_scalar_operand");
}


TEST_CASE(
    "S_MOV_B32 special scalar source trace exposes named source semantics",
    "[trace][graphics][v0][sop1][mov][special-source]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_sop1(3, 5, 126)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            110,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "scalar_move_32");
    REQUIRE(event->stable.size() == 3);
    REQUIRE(event->stable[1].name == "source_kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) ==
        "special_register");
    REQUIRE(
        event->stable[2].name ==
        "source_special_register");
    REQUIRE(
        std::get<std::string>(
            event->stable[2].value) ==
        "exec_lo");
    REQUIRE(event->diagnostics.size() == 2);
}

TEST_CASE(
    "S_MOV_B32 special scalar source participates in trace divergence",
    "[trace][graphics][v0][sop1][mov][special-source]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            111,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 106))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            111,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_sop1(3, 5, 107))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "source_special_register"});
}


TEST_CASE(
    "VOP1 decode trace exposes vector selectors as stable fields",
    "[trace][graphics][v0][vop1][mov]") {
    const auto instruction =
        decode_one(make_vop1(1, 5, 273));

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            112,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.decode");
    REQUIRE(event->type == "instruction");
    REQUIRE(event->stable.size() == 5);
    REQUIRE(
        std::get<std::string>(
            event->stable[0].value) == "vop1");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) == "v_mov_b32");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) == 1);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 5);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[4].value) == 273);
}

TEST_CASE(
    "VOP1 extension remains diagnostic provenance only",
    "[trace][graphics][v0][vop1][extension]") {
    const auto instruction =
        decode_vop1_extension(
            make_vop1(1, 5, 255),
            0xdeadbeefU);

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            113,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->stable.size() == 5);
    REQUIRE(event->diagnostics.size() == 2);
    REQUIRE(
        std::get<std::vector<std::byte>>(
            event->diagnostics[1].value)
            .size() == 8);
}

TEST_CASE(
    "V_MOV_B32 Shader IR trace exposes VGPR move semantics",
    "[trace][graphics][v0][vop1][mov]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop1(1, 5, 273)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            114,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "vector_move_32");
    REQUIRE(event->stable.size() == 2);
    REQUIRE(event->stable[0].name == "destination_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 5);
    REQUIRE(event->stable[1].name == "source_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 17);
}

TEST_CASE(
    "V_MOV_B32 VGPR identity participates in trace divergence",
    "[trace][graphics][v0][vop1][mov]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            115,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_vop1(1, 5, 273))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            115,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_vop1(1, 6, 273))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "destination_vgpr"});
}


TEST_CASE(
    "VOP2 decode trace exposes arithmetic selectors as stable fields",
    "[trace][graphics][v0][vop2][add-f32]") {
    const auto instruction =
        decode_one(make_vop2(3, 5, 273, 9));

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            116,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.decode");
    REQUIRE(event->type == "instruction");
    REQUIRE(event->stable.size() == 6);
    REQUIRE(
        std::get<std::string>(
            event->stable[0].value) == "vop2");
    REQUIRE(
        std::get<std::string>(
            event->stable[1].value) == "v_add_f32");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) == 3);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 5);
    REQUIRE(event->stable[4].name == "source0_selector");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[4].value) == 273);
    REQUIRE(event->stable[5].name == "source1_selector");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[5].value) == 9);
}

TEST_CASE(
    "VOP2 SRC0 extension remains diagnostic provenance only",
    "[trace][graphics][v0][vop2][extension]") {
    const auto instruction =
        decode_vop2_extension(
            make_vop2(3, 5, 255, 9),
            0xdeadbeefU);

    auto event =
        astraea::trace::trace_rdna2_decode_v0(
            117,
            instruction);

    REQUIRE(event.has_value());
    REQUIRE(event->stable.size() == 6);
    REQUIRE(event->diagnostics.size() == 2);
    REQUIRE(
        std::get<std::vector<std::byte>>(
            event->diagnostics[1].value)
            .size() == 8);
}

TEST_CASE(
    "V_ADD_F32 Shader IR trace exposes VGPR arithmetic semantics",
    "[trace][graphics][v0][vop2][add-f32]") {
    const auto ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(make_vop2(3, 5, 273, 9)));

    auto event =
        astraea::trace::trace_shader_ir_v0(
            118,
            ir);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.ir");
    REQUIRE(event->type == "vector_add_f32");
    REQUIRE(event->stable.size() == 3);
    REQUIRE(event->stable[0].name == "destination_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 5);
    REQUIRE(event->stable[1].name == "source0_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 17);
    REQUIRE(event->stable[2].name == "source1_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) == 9);
}

TEST_CASE(
    "V_ADD_F32 source identity participates in trace divergence",
    "[trace][graphics][v0][vop2][add-f32]") {
    auto left =
        astraea::trace::trace_shader_ir_v0(
            119,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_vop2(3, 5, 273, 9))));
    auto right =
        astraea::trace::trace_shader_ir_v0(
            119,
            astraea::graphics::lower_rdna2_to_shader_ir(
                decode_one(make_vop2(3, 5, 273, 10))));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "source1_vgpr"});
}


TEST_CASE(
    "Shader CFG block trace exposes stable topology",
    "[trace][graphics][v0][shader-cfg][block]") {
    const astraea::graphics::ShaderCfgBasicBlock block{
        .first_emission_index = 2,
        .emission_count = 3,
        .successors =
            std::vector<astraea::graphics::ShaderCfgEdge>{
                astraea::graphics::ShaderCfgEdge{
                    .kind =
                        astraea::graphics::ShaderCfgEdgeKind::
                            conditional_branch_taken,
                    .target_block_index = 4,
                },
                astraea::graphics::ShaderCfgEdge{
                    .kind =
                        astraea::graphics::ShaderCfgEdgeKind::
                            conditional_branch_fallthrough,
                    .target_block_index = 2,
                },
            },
    };

    auto event =
        astraea::trace::trace_shader_cfg_block_v0(
            120,
            1,
            block);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.cfg");
    REQUIRE(event->type == "block");
    REQUIRE_FALSE(event->guest.has_value());
    REQUIRE(event->stable.size() == 4);
    REQUIRE(event->stable[0].name == "block_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 1);
    REQUIRE(
        event->stable[1].name ==
        "first_emission_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 2);
    REQUIRE(event->stable[2].name == "emission_count");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) == 3);
    REQUIRE(event->stable[3].name == "successor_count");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 2);
    REQUIRE(event->diagnostics.empty());
}

TEST_CASE(
    "Shader CFG edge trace exposes typed topology",
    "[trace][graphics][v0][shader-cfg][edge]") {
    const astraea::graphics::ShaderCfgEdge edge{
        .kind =
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_taken,
        .target_block_index = 5,
    };

    auto event =
        astraea::trace::trace_shader_cfg_edge_v0(
            121,
            2,
            0,
            edge);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.cfg");
    REQUIRE(event->type == "edge");
    REQUIRE_FALSE(event->guest.has_value());
    REQUIRE(event->stable.size() == 4);
    REQUIRE(
        event->stable[0].name ==
        "source_block_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 2);
    REQUIRE(event->stable[1].name == "edge_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 0);
    REQUIRE(event->stable[2].name == "kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[2].value) ==
        "conditional_branch_taken");
    REQUIRE(
        event->stable[3].name ==
        "target_block_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 5);
    REQUIRE(event->diagnostics.empty());
}

TEST_CASE(
    "Shader CFG target change participates in trace divergence",
    "[trace][graphics][v0][shader-cfg][diff]") {
    const astraea::graphics::ShaderCfgEdge left_edge{
        .kind =
            astraea::graphics::ShaderCfgEdgeKind::
                unconditional_branch,
        .target_block_index = 3,
    };
    const astraea::graphics::ShaderCfgEdge right_edge{
        .kind =
            astraea::graphics::ShaderCfgEdgeKind::
                unconditional_branch,
        .target_block_index = 4,
    };

    auto left =
        astraea::trace::trace_shader_cfg_edge_v0(
            122,
            1,
            0,
            left_edge);
    auto right =
        astraea::trace::trace_shader_cfg_edge_v0(
            122,
            1,
            0,
            right_edge);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "target_block_index"});
}

TEST_CASE(
    "Shader CFG edge kind change participates in trace divergence",
    "[trace][graphics][v0][shader-cfg][diff]") {
    const astraea::graphics::ShaderCfgEdge left_edge{
        .kind =
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_taken,
        .target_block_index = 3,
    };
    const astraea::graphics::ShaderCfgEdge right_edge{
        .kind =
            astraea::graphics::ShaderCfgEdgeKind::
                conditional_branch_fallthrough,
        .target_block_index = 3,
    };

    auto left =
        astraea::trace::trace_shader_cfg_edge_v0(
            123,
            1,
            0,
            left_edge);
    auto right =
        astraea::trace::trace_shader_cfg_edge_v0(
            123,
            1,
            0,
            right_edge);

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"kind"});
}


TEST_CASE(
    "scalar execution Trace v0 exposes 32-bit SGPR write semantics",
    "[trace][graphics][v0][shader-execution][scalar]") {
    astraea::graphics::ShaderScalarState state{};
    state.sgprs[2] = 0xdeadbeefU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove32{
            .destination =
                astraea::graphics::ShaderIrSgpr{
                    .index = 5,
                },
            .source =
                astraea::graphics::ShaderIrSgpr{
                    .index = 2,
                },
        };

    const auto execution =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);
    REQUIRE(execution.has_value());

    auto event =
        astraea::trace::
            trace_shader_scalar_execution_v0(
                124,
                execution.value());

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.execute");
    REQUIRE(event->type == "scalar_write");
    REQUIRE_FALSE(event->guest.has_value());
    REQUIRE(event->stable.size() == 3);
    REQUIRE(
        event->stable[0].name ==
        "destination_first_sgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 5);
    REQUIRE(
        event->stable[1].name ==
        "write_width_bits");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 32);
    REQUIRE(
        event->stable[2].name ==
        "written_value0_bits");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) ==
        0xdeadbeefULL);
    REQUIRE(event->diagnostics.empty());
}

TEST_CASE(
    "scalar execution Trace v0 exposes both 64-bit SGPR dwords",
    "[trace][graphics][v0][shader-execution][scalar]") {
    astraea::graphics::ShaderScalarState state{};
    state.sgprs[2] = 0x01234567U;
    state.sgprs[3] = 0x89abcdefU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrScalarMove64{
            .destination =
                astraea::graphics::ShaderIrSgprPair{
                    .first_index = 8,
                },
            .source =
                astraea::graphics::ShaderIrSgprPair{
                    .first_index = 2,
                },
        };

    const auto execution =
        astraea::graphics::
            execute_shader_scalar_operation(
                operation,
                state);
    REQUIRE(execution.has_value());

    auto event =
        astraea::trace::
            trace_shader_scalar_execution_v0(
                125,
                execution.value());

    REQUIRE(event.has_value());
    REQUIRE(event->stable.size() == 4);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 8);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[1].value) == 64);
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[2].value) ==
        0x01234567ULL);
    REQUIRE(
        event->stable[3].name ==
        "written_value1_bits");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) ==
        0x89abcdefULL);
}

TEST_CASE(
    "scalar execution write value participates in trace divergence",
    "[trace][graphics][v0][shader-execution][scalar][diff]") {
    const auto make_event =
        [](std::uint32_t source_value) {
            astraea::graphics::ShaderScalarState state{};
            state.sgprs[2] = source_value;

            const astraea::graphics::ShaderIrOperation
                operation =
                    astraea::graphics::
                        ShaderIrScalarMove32{
                            .destination =
                                astraea::graphics::
                                    ShaderIrSgpr{
                                        .index = 5,
                                    },
                            .source =
                                astraea::graphics::
                                    ShaderIrSgpr{
                                        .index = 2,
                                    },
                        };

            const auto execution =
                astraea::graphics::
                    execute_shader_scalar_operation(
                        operation,
                        state);
            REQUIRE(execution.has_value());

            auto event =
                astraea::trace::
                    trace_shader_scalar_execution_v0(
                        126,
                        execution.value());
            REQUIRE(event.has_value());
            return std::move(event).value();
        };

    auto diff =
        astraea::trace::diff_trace_v0(
            document(make_event(0x11111111U)),
            document(make_event(0x22222222U)));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "written_value0_bits"});
}


TEST_CASE(
    "branch decision Trace v0 exposes typed predicate outcome",
    "[trace][graphics][v0][shader-execution][branch]") {
    astraea::graphics::ShaderScalarState state{};
    state.exec = 0x100000000ULL;

    const auto decision =
        astraea::graphics::
            evaluate_shader_branch_condition(
                astraea::graphics::
                    ShaderIrBranchCondition::exec_nonzero,
                state);

    auto event =
        astraea::trace::
            trace_shader_branch_decision_v0(
                127,
                decision);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.execute");
    REQUIRE(
        event->type ==
        "conditional_branch_decision");
    REQUIRE_FALSE(event->guest.has_value());
    REQUIRE(event->stable.size() == 2);
    REQUIRE(event->stable[0].name == "condition");
    REQUIRE(
        std::get<std::string>(
            event->stable[0].value) ==
        "exec_nonzero");
    REQUIRE(event->stable[1].name == "taken");
    REQUIRE(
        std::get<bool>(
            event->stable[1].value));
    REQUIRE(event->diagnostics.empty());
}

TEST_CASE(
    "branch decision outcome participates in trace divergence",
    "[trace][graphics][v0][shader-execution][branch][diff]") {
    const auto make_event =
        [](std::uint64_t vcc) {
            astraea::graphics::ShaderScalarState state{};
            state.vcc = vcc;

            const auto decision =
                astraea::graphics::
                    evaluate_shader_branch_condition(
                        astraea::graphics::
                            ShaderIrBranchCondition::
                                vcc_zero,
                        state);

            auto event =
                astraea::trace::
                    trace_shader_branch_decision_v0(
                        128,
                        decision);
            REQUIRE(event.has_value());
            return std::move(event).value();
        };

    auto diff =
        astraea::trace::diff_trace_v0(
            document(make_event(0)),
            document(make_event(1)));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{"taken"});
}


TEST_CASE(
    "CFG successor selection Trace v0 exposes selected edge",
    "[trace][graphics][v0][shader-execution][cfg-successor]") {
    const astraea::graphics::ShaderCfgSuccessorSelection
        selection{
            .edge =
                astraea::graphics::ShaderCfgEdge{
                    .kind =
                        astraea::graphics::ShaderCfgEdgeKind::
                            conditional_branch_taken,
                    .target_block_index = 4,
                },
        };

    auto event =
        astraea::trace::
            trace_shader_cfg_successor_selection_v0(
                129,
                2,
                selection);

    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "shader.execute");
    REQUIRE(
        event->type ==
        "cfg_successor_selection");
    REQUIRE(event->stable.size() == 4);
    REQUIRE(event->stable[0].name == "source_block_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[0].value) == 2);
    REQUIRE(event->stable[1].name == "has_successor");
    REQUIRE(
        std::get<bool>(
            event->stable[1].value));
    REQUIRE(event->stable[2].name == "edge_kind");
    REQUIRE(
        std::get<std::string>(
            event->stable[2].value) ==
        "conditional_branch_taken");
    REQUIRE(
        event->stable[3].name ==
        "target_block_index");
    REQUIRE(
        std::get<std::uint64_t>(
            event->stable[3].value) == 4);
    REQUIRE(event->diagnostics.empty());
}

TEST_CASE(
    "terminal CFG successor selection Trace v0 has no edge fields",
    "[trace][graphics][v0][shader-execution][cfg-successor]") {
    const astraea::graphics::ShaderCfgSuccessorSelection
        selection{};

    auto event =
        astraea::trace::
            trace_shader_cfg_successor_selection_v0(
                130,
                3,
                selection);

    REQUIRE(event.has_value());
    REQUIRE(event->stable.size() == 2);
    REQUIRE(event->stable[1].name == "has_successor");
    REQUIRE_FALSE(
        std::get<bool>(
            event->stable[1].value));
}

TEST_CASE(
    "CFG successor target participates in trace divergence",
    "[trace][graphics][v0][shader-execution][cfg-successor][diff]") {
    const auto make_event =
        [](std::size_t target_block_index) {
            const astraea::graphics::
                ShaderCfgSuccessorSelection selection{
                    .edge =
                        astraea::graphics::ShaderCfgEdge{
                            .kind =
                                astraea::graphics::
                                    ShaderCfgEdgeKind::
                                        unconditional_branch,
                            .target_block_index =
                                target_block_index,
                        },
                };

            auto event =
                astraea::trace::
                    trace_shader_cfg_successor_selection_v0(
                        131,
                        1,
                        selection);
            REQUIRE(event.has_value());
            return std::move(event).value();
        };

    auto diff =
        astraea::trace::diff_trace_v0(
            document(make_event(2)),
            document(make_event(3)));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "target_block_index"});
}


TEST_CASE(
    "Graphics IR SET_SH_REG trace exposes stable range semantics",
    "[trace][graphics][v0][set-sh-reg]") {
    const std::vector<std::byte> bytes{
        std::byte{0x00}, std::byte{0x76},
        std::byte{0x02}, std::byte{0xc0},
        std::byte{0x10}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00},
        std::byte{0x44}, std::byte{0x33},
        std::byte{0x22}, std::byte{0x11},
        std::byte{0x88}, std::byte{0x77},
        std::byte{0x66}, std::byte{0x55},
    };

    const auto framed =
        astraea::graphics::
            frame_pm4_type3_stream(bytes);
    REQUIRE(framed.has_value());
    REQUIRE(framed->frames.size() == 1U);

    const auto lowered =
        astraea::graphics::
            lower_pm4_type3_frame_to_graphics_ir(
                framed->frames[0]);
    REQUIRE(lowered.has_value());

    auto event =
        astraea::trace::trace_graphics_ir_v0(
            25,
            lowered.value());
    REQUIRE(event.has_value());
    REQUIRE(event->subsystem == "graphics.ir");
    REQUIRE(
        event->type ==
        "shader_register_write_range");
    REQUIRE(event->stable.size() == 3U);
    REQUIRE(
        event->stable[0].name ==
        "start_offset");
    REQUIRE(
        event->stable[0].value ==
        astraea::trace::TraceValueV0{
            std::uint64_t{0x10U}});
    REQUIRE(
        event->stable[1].name ==
        "value_count");
    REQUIRE(
        event->stable[1].value ==
        astraea::trace::TraceValueV0{
            std::uint64_t{2U}});
    REQUIRE(
        event->stable[2].name ==
        "value_bits");
    REQUIRE(
        event->stable[2].value ==
        astraea::trace::TraceValueV0{
            std::vector<std::byte>{
                std::byte{0x44},
                std::byte{0x33},
                std::byte{0x22},
                std::byte{0x11},
                std::byte{0x88},
                std::byte{0x77},
                std::byte{0x66},
                std::byte{0x55}}});
    REQUIRE(event->diagnostics.size() == 3U);
}

TEST_CASE(
    "Graphics IR SET_SH_REG trace participates in first divergence",
    "[trace][graphics][v0][set-sh-reg][diff]") {
    auto make_ir =
        [](std::uint32_t value)
        -> astraea::graphics::GraphicsIrEmission {
        std::vector<std::byte> bytes{
            std::byte{0x00}, std::byte{0x76},
            std::byte{0x01}, std::byte{0xc0},
            std::byte{0x20}, std::byte{0x00},
            std::byte{0x00}, std::byte{0x00},
            std::byte{
                static_cast<unsigned char>(
                    value & 0xffU)},
            std::byte{
                static_cast<unsigned char>(
                    (value >> 8U) & 0xffU)},
            std::byte{
                static_cast<unsigned char>(
                    (value >> 16U) & 0xffU)},
            std::byte{
                static_cast<unsigned char>(
                    (value >> 24U) & 0xffU)},
        };

        auto framed =
            astraea::graphics::
                frame_pm4_type3_stream(bytes);
        REQUIRE(framed.has_value());
        auto lowered =
            astraea::graphics::
                lower_pm4_type3_frame_to_graphics_ir(
                    framed->frames[0]);
        REQUIRE(lowered.has_value());
        return std::move(lowered).value();
    };

    auto left =
        astraea::trace::trace_graphics_ir_v0(
            26,
            make_ir(0x11111111U));
    auto right =
        astraea::trace::trace_graphics_ir_v0(
            26,
            make_ir(0x22222222U));

    REQUIRE(left.has_value());
    REQUIRE(right.has_value());

    auto diff =
        astraea::trace::diff_trace_v0(
            document(std::move(left).value()),
            document(std::move(right).value()));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "value_bits"});
}
