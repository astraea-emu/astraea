#include <astraea/research/v3_first_submission.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count,
    std::uint8_t low_control_bits = 0U) {
    return
        (static_cast<std::uint32_t>(
             astraea::graphics::kPm4Type3PacketType)
         << 30U) |
        ((static_cast<std::uint32_t>(encoded_count) &
          0x3fffU)
         << 16U) |
        (static_cast<std::uint32_t>(opcode) << 8U) |
        static_cast<std::uint32_t>(low_control_bits);
}

void append_word(
    std::vector<std::byte>& bytes,
    std::uint32_t word) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) & 0xffU)});
    }
}

astraea::execution::SceAgcDcbSubmission make_submission(
    std::initializer_list<std::uint32_t> words,
    std::uint8_t flag = 0U) {
    astraea::execution::SceAgcDcbSubmission submission{
        .submit_description_address =
            astraea::memory::GuestAddress{0x00100000ULL},
        .command_words_address =
            astraea::memory::GuestAddress{0x00200000ULL},
        .word_count =
            static_cast<std::uint32_t>(words.size()),
        .flag = flag,
        .raw_submit_description = {},
        .opaque_padding = {},
        .command_buffer_bytes = {},
    };

    submission.command_buffer_bytes.reserve(
        words.size() * 4U);
    for (const auto word : words) {
        append_word(
            submission.command_buffer_bytes,
            word);
    }
    return submission;
}

constexpr std::uint32_t pgm_lo(
    std::uint64_t address) noexcept {
    return static_cast<std::uint32_t>(
        address >> 8U);
}

constexpr std::uint32_t pgm_hi(
    std::uint64_t address) noexcept {
    return static_cast<std::uint32_t>(
        (address >> 40U) & 0xffU);
}

void register_shader(
    astraea::execution::CreatedAgcShaderRegistry& registry,
    astraea::graphics::AgcShaderStage stage,
    std::uint64_t program_address,
    std::uint64_t handle) {
    const auto profile =
        stage ==
                astraea::graphics::AgcShaderStage::pixel
            ? astraea::execution::
                  SceAgcShaderPreparationProfile::
                      v18_pixel_public_shape
            : astraea::execution::
                  SceAgcShaderPreparationProfile::
                      v18_geometry_es_public_shape;

    const auto result =
        registry.register_shader(
            astraea::execution::CreatedAgcShader{
                .code_address =
                    astraea::graphics::GpuVirtualAddress{
                        .value = program_address,
                    },
                .stage = stage,
                .preparation_profile = profile,
                .shader_handle =
                    astraea::memory::GuestAddress{handle},
                .shader_header_address =
                    astraea::memory::GuestAddress{handle},
                .shader_text_address =
                    astraea::memory::GuestAddress{
                        program_address},
                .shader = {},
                .shader_ir = {},
            });
    REQUIRE(result.has_value());
}

}  // namespace

TEST_CASE(
    "first V3 submission reduces SH context UCONFIG and draw domains at draw time",
    "[research][v3][submission]") {
    constexpr std::uint64_t kGeometryProgram =
        0x0000123456000000ULL;
    constexpr std::uint64_t kPixelProgram =
        0x0000234567000000ULL;

    astraea::execution::CreatedAgcShaderRegistry registry;
    register_shader(
        registry,
        astraea::graphics::AgcShaderStage::geometry,
        kGeometryProgram,
        0x00300000ULL);
    register_shader(
        registry,
        astraea::graphics::AgcShaderStage::pixel,
        kPixelProgram,
        0x00400000ULL);

    const auto submission =
        make_submission({
            // Frame 0, words 0..3: Geometry/ES program.
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kGeometryEsProgramLoRegisterOffset,
            pgm_lo(kGeometryProgram),
            pgm_hi(kGeometryProgram),

            // Frame 1, words 4..7: Pixel program.
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kPixelProgram),
            pgm_hi(kPixelProgram),

            // Frame 2, words 8..10: generic draw-time context state.
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U),
            0x0010U,
            0x11111111U,

            // Frame 3, words 11..13: primitive type 4.
            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U),
            astraea::research::
                kV3FirstDrawPrimitiveTypeUconfigOffset,
            astraea::research::
                kV3FirstDrawTriangleListPrimitiveType,

            // Frame 4, words 14..15.
            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,

            // Frame 5, words 16..18: the one accepted draw.
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,

            // Frames after the draw prove snapshot semantics.
            make_type3_header(
                astraea::graphics::
                    kPm4SetContextRegOpcode,
                1U),
            0x0010U,
            0x22222222U,

            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U),
            astraea::research::
                kV3FirstDrawPrimitiveTypeUconfigOffset,
            6U,
        });

    const auto result =
        astraea::research::
            plan_v3_first_submission(
                submission,
                registry);

    REQUIRE(result.has_value());
    REQUIRE(
        result->geometry_program_address.value ==
        kGeometryProgram);
    REQUIRE(
        result->pixel_program_address.value ==
        kPixelProgram);
    REQUIRE(result->geometry_shader != nullptr);
    REQUIRE(result->pixel_shader != nullptr);
    REQUIRE(
        result->geometry_shader->stage ==
        astraea::graphics::AgcShaderStage::geometry);
    REQUIRE(
        result->pixel_shader->stage ==
        astraea::graphics::AgcShaderStage::pixel);

    REQUIRE(
        result->context_register_state.values[0x10U] ==
        0x11111111U);
    REQUIRE(
        result->user_config_register_state.values[
            astraea::research::
                kV3FirstDrawPrimitiveTypeUconfigOffset] ==
        4U);

    REQUIRE(
        result->draw ==
        astraea::research::V3FirstDrawPlan{
            .primitive_type = 4U,
            .instance_count = 1U,
            .index_count = 3U,
            .initiator = 2U,
        });

    REQUIRE(
        result->geometry_pgm_lo_source ==
        astraea::research::
            V3FirstSubmissionPacketSource{
                .frame_index = 0U,
                .packet_word_offset = 0U,
                .value_word_offset = 2U,
            });
    REQUIRE(
        result->geometry_pgm_hi_source.value_word_offset ==
        3U);
    REQUIRE(
        result->pixel_pgm_lo_source ==
        astraea::research::
            V3FirstSubmissionPacketSource{
                .frame_index = 1U,
                .packet_word_offset = 4U,
                .value_word_offset = 6U,
            });
    REQUIRE(
        result->pixel_pgm_hi_source.value_word_offset ==
        7U);
    REQUIRE(
        result->instances_source ==
        astraea::research::
            V3FirstSubmissionPacketSource{
                .frame_index = 4U,
                .packet_word_offset = 14U,
                .value_word_offset = 15U,
            });
    REQUIRE(
        result->draw_source ==
        astraea::research::
            V3FirstSubmissionPacketSource{
                .frame_index = 5U,
                .packet_word_offset = 16U,
                .value_word_offset = 17U,
            });
}

TEST_CASE(
    "first V3 submission requires NUM_INSTANCES before draw",
    "[research][v3][submission][negative][ordering]") {
    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto result =
        astraea::research::
            plan_v3_first_submission(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::research::
            V3FirstSubmissionErrorCode::
                missing_instance_count_before_draw);
    REQUIRE(result.error().frame_index == 0U);
    REQUIRE(result.error().word_offset == 0U);
}

TEST_CASE(
    "first V3 submission rejects a second draw explicitly",
    "[research][v3][submission][negative][multiple-draws]") {
    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto result =
        astraea::research::
            plan_v3_first_submission(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::research::
            V3FirstSubmissionErrorCode::
                multiple_draws);
    REQUIRE(result.error().frame_index == 2U);
    REQUIRE(result.error().word_offset == 5U);
}

TEST_CASE(
    "first V3 submission rejects unknown packet semantics instead of skipping them",
    "[research][v3][submission][negative][opcode]") {
    const auto submission =
        make_submission({
            make_type3_header(0x75U, 1U),
            0U,
            0x12345678U,
        });

    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto result =
        astraea::research::
            plan_v3_first_submission(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::research::
            V3FirstSubmissionErrorCode::
                unsupported_opcode);
    REQUIRE(result.error().opcode == 0x75U);
}

TEST_CASE(
    "first V3 submission preserves draw-plan primitive rejection",
    "[research][v3][submission][negative][draw-plan]") {
    constexpr std::uint64_t kGeometryProgram =
        0x0000123456000000ULL;
    constexpr std::uint64_t kPixelProgram =
        0x0000234567000000ULL;

    astraea::execution::CreatedAgcShaderRegistry registry;
    register_shader(
        registry,
        astraea::graphics::AgcShaderStage::geometry,
        kGeometryProgram,
        0x00300000ULL);
    register_shader(
        registry,
        astraea::graphics::AgcShaderStage::pixel,
        kPixelProgram,
        0x00400000ULL);

    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kGeometryEsProgramLoRegisterOffset,
            pgm_lo(kGeometryProgram),
            pgm_hi(kGeometryProgram),

            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kPixelProgram),
            pgm_hi(kPixelProgram),

            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U),
            astraea::research::
                kV3FirstDrawPrimitiveTypeUconfigOffset,
            6U,

            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,

            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

    const auto result =
        astraea::research::
            plan_v3_first_submission(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::research::
            V3FirstSubmissionErrorCode::
                draw_plan_failure);
    REQUIRE(result.error().draw_plan_error.has_value());
    REQUIRE(
        result.error().draw_plan_error->code ==
        astraea::research::
            V3FirstDrawPlanErrorCode::
                unsupported_primitive_type);
    REQUIRE(
        result.error().draw_plan_error->actual_value ==
        6U);
}

TEST_CASE(
    "first V3 submission keeps Geometry and Pixel lookup failures stage-qualified",
    "[research][v3][submission][negative][lookup]") {
    constexpr std::uint64_t kGeometryProgram =
        0x0000123456000000ULL;
    constexpr std::uint64_t kPixelProgram =
        0x0000234567000000ULL;

    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kGeometryEsProgramLoRegisterOffset,
            pgm_lo(kGeometryProgram),
            pgm_hi(kGeometryProgram),

            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kPixelProgram),
            pgm_hi(kPixelProgram),

            make_type3_header(
                astraea::graphics::
                    kPm4SetUconfigRegOpcode,
                1U),
            astraea::research::
                kV3FirstDrawPrimitiveTypeUconfigOffset,
            4U,

            make_type3_header(
                astraea::graphics::
                    kPm4NumInstancesOpcode,
                0U),
            1U,

            make_type3_header(
                astraea::graphics::
                    kPm4DrawIndexAutoOpcode,
                1U),
            3U,
            2U,
        });

    SECTION("Geometry missing") {
        astraea::execution::CreatedAgcShaderRegistry registry;
        register_shader(
            registry,
            astraea::graphics::AgcShaderStage::pixel,
            kPixelProgram,
            0x00400000ULL);

        const auto result =
            astraea::research::
                plan_v3_first_submission(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstSubmissionErrorCode::
                    geometry_shader_lookup_failure);
        REQUIRE(
            result.error().
                created_shader_lookup_error.has_value());
        REQUIRE(
            result.error().
                created_shader_lookup_error->stage ==
            astraea::graphics::AgcShaderStage::geometry);
    }

    SECTION("Pixel missing") {
        astraea::execution::CreatedAgcShaderRegistry registry;
        register_shader(
            registry,
            astraea::graphics::AgcShaderStage::geometry,
            kGeometryProgram,
            0x00300000ULL);

        const auto result =
            astraea::research::
                plan_v3_first_submission(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::research::
                V3FirstSubmissionErrorCode::
                    pixel_shader_lookup_failure);
        REQUIRE(
            result.error().
                created_shader_lookup_error.has_value());
        REQUIRE(
            result.error().
                created_shader_lookup_error->stage ==
            astraea::graphics::AgcShaderStage::pixel);
    }
}
