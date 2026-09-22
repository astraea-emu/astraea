#include <astraea/graphics/graphics_ir.hpp>
#include <astraea/graphics/packet.hpp>
#include <astraea/graphics/rdna2_decoder.hpp>
#include <astraea/graphics/shader_ir.hpp>
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
            decode_one(0x01234567U));
    const auto right_ir =
        astraea::graphics::lower_rdna2_to_shader_ir(
            decode_one(0x07654321U));

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
