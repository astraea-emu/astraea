#include <astraea/graphics/shader_vector_execution.hpp>
#include <astraea/trace/diff_v0.hpp>
#include <astraea/trace/graphics_v0.hpp>
#include <astraea/trace/v0.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

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

astraea::trace::TraceEventV0 make_wave32_move_event(
    std::uint32_t lane0_value) {
    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec =
        (std::uint64_t{1} << 0U) |
        (std::uint64_t{1} << 2U);

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size =
        astraea::graphics::ShaderWaveSize::wave32;
    vector_state.vgprs[2][0] = lane0_value;
    vector_state.vgprs[2][2] = 0xaabbccddU;

    const astraea::graphics::ShaderIrOperation operation =
        astraea::graphics::ShaderIrVectorMove32{
            .destination =
                astraea::graphics::ShaderIrVgpr{
                    .index = 1,
                },
            .source =
                astraea::graphics::ShaderIrVgpr{
                    .index = 2,
                },
        };

    const auto execution =
        astraea::graphics::
            execute_shader_vector_move32_operation(
                operation,
                scalar_state,
                vector_state);
    REQUIRE(execution.has_value());

    auto event =
        astraea::trace::
            trace_shader_vector_move_execution_v0(
                130,
                execution.value());
    REQUIRE(event.has_value());
    return std::move(event).value();
}

}  // namespace

TEST_CASE(
    "V_MOV_B32 Trace v0 exposes wave and written-lane semantics",
    "[trace][graphics][v0][shader-execution][vector]") {
    auto event =
        make_wave32_move_event(0x01020304U);

    REQUIRE(event.subsystem == "shader.execute");
    REQUIRE(event.type == "vector_move_32");
    REQUIRE_FALSE(event.guest.has_value());
    REQUIRE(event.stable.size() == 5);

    REQUIRE(event.stable[0].name == "wave_size_lanes");
    REQUIRE(
        std::get<std::uint64_t>(
            event.stable[0].value) == 32);

    REQUIRE(event.stable[1].name == "destination_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event.stable[1].value) == 1);

    REQUIRE(event.stable[2].name == "source_vgpr");
    REQUIRE(
        std::get<std::uint64_t>(
            event.stable[2].value) == 2);

    REQUIRE(event.stable[3].name == "active_lane_mask");
    REQUIRE(
        std::get<std::uint64_t>(
            event.stable[3].value) ==
        ((std::uint64_t{1} << 0U) |
         (std::uint64_t{1} << 2U)));

    REQUIRE(
        event.stable[4].name ==
        "written_lane_bits_le");
    const auto& bytes =
        std::get<std::vector<std::byte>>(
            event.stable[4].value);
    REQUIRE(bytes.size() == 32U * 4U);
    REQUIRE(bytes[0] == std::byte{0x04});
    REQUIRE(bytes[1] == std::byte{0x03});
    REQUIRE(bytes[2] == std::byte{0x02});
    REQUIRE(bytes[3] == std::byte{0x01});
    REQUIRE(bytes[8] == std::byte{0xdd});
    REQUIRE(bytes[9] == std::byte{0xcc});
    REQUIRE(bytes[10] == std::byte{0xbb});
    REQUIRE(bytes[11] == std::byte{0xaa});
    REQUIRE(event.diagnostics.empty());
}

TEST_CASE(
    "V_MOV_B32 written lane value participates in trace divergence",
    "[trace][graphics][v0][shader-execution][vector][diff]") {
    auto diff =
        astraea::trace::diff_trace_v0(
            document(
                make_wave32_move_event(
                    0x11111111U)),
            document(
                make_wave32_move_event(
                    0x22222222U)));

    REQUIRE(diff.has_value());
    REQUIRE_FALSE(diff->equivalent);
    REQUIRE(
        diff->first_divergence->kind ==
        astraea::trace::TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        diff->first_divergence->field_name ==
        std::optional<std::string>{
            "written_lane_bits_le"});
}
