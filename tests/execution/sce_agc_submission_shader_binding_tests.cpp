#include <astraea/execution/sce_agc_submission_shader_binding.hpp>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count,
    std::uint8_t low_control_bits = 0) {
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
    for (std::size_t index = 0;
         index < 4U;
         ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) &
                    0xffU)});
    }
}

std::vector<std::byte> make_stream(
    std::initializer_list<std::uint32_t> words) {
    std::vector<std::byte> bytes;
    bytes.reserve(words.size() * 4U);
    for (const auto word : words) {
        append_word(bytes, word);
    }
    return bytes;
}

astraea::execution::SceAgcDcbSubmission make_submission(
    std::initializer_list<std::uint32_t> words,
    std::uint8_t flag = 0U) {
    astraea::execution::SceAgcDcbSubmission submission{};
    submission.submit_description_address =
        astraea::memory::GuestAddress{0x00100000ULL};
    submission.command_words_address =
        astraea::memory::GuestAddress{0x00200000ULL};
    submission.word_count =
        static_cast<std::uint32_t>(words.size());
    submission.flag = flag;
    submission.command_buffer_bytes =
        make_stream(words);
    return submission;
}

constexpr std::uint32_t pgm_lo(
    std::uint64_t address) {
    return static_cast<std::uint32_t>(
        address >> 8U);
}

constexpr std::uint32_t pgm_hi(
    std::uint64_t address) {
    return static_cast<std::uint32_t>(
        (address >> 40U) & 0xffU);
}

void register_shader(
    astraea::execution::CreatedAgcShaderRegistry& registry,
    std::uint64_t program_address,
    std::uint64_t handle) {
    auto result =
        registry.register_shader(
            astraea::execution::CreatedAgcShader{
                .program_address =
                    astraea::graphics::
                        PixelProgramGpuAddress{
                            .value = program_address,
                        },
                .shader_handle =
                    astraea::memory::GuestAddress{handle},
                .shader_header_address =
                    astraea::memory::GuestAddress{
                        handle + 0x1000U},
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
    "submission binding resolves one created pixel shader and exact PGM provenance",
    "[execution][agc][submission-binding]") {
    constexpr std::uint64_t kProgram =
        0x0000123456789a00ULL;
    constexpr std::uint64_t kHandle =
        0x0000000000100000ULL;

    astraea::execution::CreatedAgcShaderRegistry registry;
    register_shader(registry, kProgram, kHandle);

    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kProgram),
            pgm_hi(kProgram),
        });

    const auto result =
        astraea::execution::
            plan_sce_agc_submitted_pixel_shader_binding(
                submission,
                registry);

    REQUIRE(result.has_value());
    REQUIRE(
        result->program_address ==
        astraea::graphics::PixelProgramGpuAddress{
            .value = kProgram});
    REQUIRE(result->shader != nullptr);
    REQUIRE(
        result->shader->shader_handle ==
        astraea::memory::GuestAddress{kHandle});

    REQUIRE(
        result->shader_register_state.initialized.test(
            astraea::graphics::
                kPixelProgramLoRegisterOffset));
    REQUIRE(
        result->shader_register_state.initialized.test(
            astraea::graphics::
                kPixelProgramHiRegisterOffset));
    REQUIRE(
        result->shader_register_state.values[
            astraea::graphics::
                kPixelProgramLoRegisterOffset] ==
        pgm_lo(kProgram));
    REQUIRE(
        result->shader_register_state.values[
            astraea::graphics::
                kPixelProgramHiRegisterOffset] ==
        pgm_hi(kProgram));

    REQUIRE(
        result->pgm_lo_source ==
        astraea::execution::
            SubmittedPixelProgramRegisterSource{
                .frame_index = 0U,
                .packet_word_offset = 0U,
                .value_word_offset = 2U,
            });
    REQUIRE(
        result->pgm_hi_source ==
        astraea::execution::
            SubmittedPixelProgramRegisterSource{
                .frame_index = 0U,
                .packet_word_offset = 0U,
                .value_word_offset = 3U,
            });
    REQUIRE(registry.size() == 1U);
}

TEST_CASE(
    "submission binding tracks split and overwritten PGM register provenance",
    "[execution][agc][submission-binding][provenance]") {
    constexpr std::uint64_t kProgram =
        0x0000123456789a00ULL;
    constexpr std::uint64_t kHandle =
        0x0000000000200000ULL;

    astraea::execution::CreatedAgcShaderRegistry registry;
    register_shader(registry, kProgram, kHandle);

    const auto stale_lo =
        pgm_lo(kProgram) ^ 0x00000100U;
    const auto submission =
        make_submission({
            // Frame 0: stale LO, words 0..2.
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                1U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            stale_lo,

            // Frame 1: final HI, words 3..5.
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                1U),
            astraea::graphics::
                kPixelProgramHiRegisterOffset,
            pgm_hi(kProgram),

            // Frame 2: final LO overwrites frame 0, words 6..8.
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                1U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kProgram),
        });

    const auto result =
        astraea::execution::
            plan_sce_agc_submitted_pixel_shader_binding(
                submission,
                registry);

    REQUIRE(result.has_value());
    REQUIRE(
        result->program_address.value ==
        kProgram);
    REQUIRE(
        result->pgm_lo_source ==
        astraea::execution::
            SubmittedPixelProgramRegisterSource{
                .frame_index = 2U,
                .packet_word_offset = 6U,
                .value_word_offset = 8U,
            });
    REQUIRE(
        result->pgm_hi_source ==
        astraea::execution::
            SubmittedPixelProgramRegisterSource{
                .frame_index = 1U,
                .packet_word_offset = 3U,
                .value_word_offset = 5U,
            });
    REQUIRE(registry.size() == 1U);
}

TEST_CASE(
    "submission binding rejects unsupported submit flag before PM4 semantics",
    "[execution][agc][submission-binding][profile]") {
    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto submission =
        make_submission({}, 0x7fU);

    const auto result =
        astraea::execution::
            plan_sce_agc_submitted_pixel_shader_binding(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceAgcSubmittedPixelShaderBindingErrorCode::
                unsupported_submit_flag);
    REQUIRE(result.error().submit_flag == 0x7fU);
    REQUIRE_FALSE(result.error().frame_index.has_value());
    REQUIRE_FALSE(
        result.error().pm4_framing_error.has_value());
    REQUIRE(registry.size() == 0U);
}

TEST_CASE(
    "submission binding rejects nonzero Type3 low control bits",
    "[execution][agc][submission-binding][profile]") {
    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U,
                0x01U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            0U,
            0U,
        });

    const auto result =
        astraea::execution::
            plan_sce_agc_submitted_pixel_shader_binding(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceAgcSubmittedPixelShaderBindingErrorCode::
                unsupported_type3_header_control_bits);
    REQUIRE(
        result.error().frame_index ==
        std::optional<std::size_t>{0U});
    REQUIRE(
        result.error().word_offset ==
        std::optional<std::size_t>{0U});
    REQUIRE(
        result.error().type3_header_control_bits ==
        0x01U);
}

TEST_CASE(
    "submission binding fails explicitly on unsupported packet semantics",
    "[execution][agc][submission-binding][unsupported]") {
    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto submission =
        make_submission({
            make_type3_header(0x75U, 1U),
            0U,
            0x12345678U,
        });

    const auto result =
        astraea::execution::
            plan_sce_agc_submitted_pixel_shader_binding(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceAgcSubmittedPixelShaderBindingErrorCode::
                shader_register_apply_failure);
    REQUIRE(
        result.error().frame_index ==
        std::optional<std::size_t>{0U});
    REQUIRE(
        result.error().word_offset ==
        std::optional<std::size_t>{0U});
    REQUIRE(
        result.error().
            shader_register_apply_error.has_value());
    REQUIRE(
        result.error().
            shader_register_apply_error->code ==
        astraea::graphics::
            ShaderRegisterApplyErrorCode::
                unsupported_operation);
    REQUIRE(
        result.error().
            shader_register_apply_error->
            unsupported_reason ==
        std::optional<
            astraea::graphics::
                GraphicsIrUnsupportedReason>{
            astraea::graphics::
                GraphicsIrUnsupportedReason::
                    packet_semantics_unknown});
}

TEST_CASE(
    "submission binding preserves SET_SH_REG lowering failure",
    "[execution][agc][submission-binding][negative]") {
    astraea::execution::CreatedAgcShaderRegistry registry;
    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                0U),
            0U,
        });

    const auto result =
        astraea::execution::
            plan_sce_agc_submitted_pixel_shader_binding(
                submission,
                registry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceAgcSubmittedPixelShaderBindingErrorCode::
                pm4_lowering_failure);
    REQUIRE(
        result.error().frame_index ==
        std::optional<std::size_t>{0U});
    REQUIRE(
        result.error().word_offset ==
        std::optional<std::size_t>{0U});
    REQUIRE(
        result.error().pm4_lowering_error.has_value());
    REQUIRE(
        result.error().pm4_lowering_error->code ==
        astraea::graphics::
            Pm4SetShRegLowerErrorCode::
                malformed_set_sh_reg);
}

TEST_CASE(
    "submission binding preserves pixel program address failures",
    "[execution][agc][submission-binding][address]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    SECTION("missing PGM_LO") {
        const auto submission =
            make_submission({
                make_type3_header(
                    astraea::graphics::kPm4SetShRegOpcode,
                    1U),
                astraea::graphics::
                    kPixelProgramHiRegisterOffset,
                0U,
            });

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pixel_program_address_failure);
        REQUIRE(
            result.error().
                pixel_program_address_error.has_value());
        REQUIRE(
            result.error().
                pixel_program_address_error->code ==
            astraea::graphics::
                PixelProgramAddressErrorCode::
                    pgm_lo_uninitialized);
    }

    SECTION("missing PGM_HI") {
        const auto submission =
            make_submission({
                make_type3_header(
                    astraea::graphics::kPm4SetShRegOpcode,
                    1U),
                astraea::graphics::
                    kPixelProgramLoRegisterOffset,
                0U,
            });

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pixel_program_address_failure);
        REQUIRE(
            result.error().
                pixel_program_address_error.has_value());
        REQUIRE(
            result.error().
                pixel_program_address_error->code ==
            astraea::graphics::
                PixelProgramAddressErrorCode::
                    pgm_hi_uninitialized);
    }

    SECTION("unsupported PGM_HI upper bits") {
        const auto submission =
            make_submission({
                make_type3_header(
                    astraea::graphics::kPm4SetShRegOpcode,
                    2U),
                astraea::graphics::
                    kPixelProgramLoRegisterOffset,
                0U,
                0x00000100U,
            });

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pixel_program_address_failure);
        REQUIRE(
            result.error().
                pixel_program_address_error.has_value());
        REQUIRE(
            result.error().
                pixel_program_address_error->code ==
            astraea::graphics::
                PixelProgramAddressErrorCode::
                    unsupported_pgm_hi_bits);
    }
}

TEST_CASE(
    "submission binding preserves created shader lookup failures",
    "[execution][agc][submission-binding][lookup]") {
    constexpr std::uint64_t kProgram =
        0x0000123456789a00ULL;

    const auto submission =
        make_submission({
            make_type3_header(
                astraea::graphics::kPm4SetShRegOpcode,
                2U),
            astraea::graphics::
                kPixelProgramLoRegisterOffset,
            pgm_lo(kProgram),
            pgm_hi(kProgram),
        });

    SECTION("not found") {
        astraea::execution::CreatedAgcShaderRegistry registry;

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    created_shader_lookup_failure);
        REQUIRE(
            result.error().
                created_shader_lookup_error.has_value());
        REQUIRE(
            result.error().
                created_shader_lookup_error->code ==
            astraea::execution::
                CreatedAgcShaderLookupErrorCode::
                    not_found);
        REQUIRE(registry.size() == 0U);
    }

    SECTION("ambiguous") {
        astraea::execution::CreatedAgcShaderRegistry registry;
        register_shader(
            registry,
            kProgram,
            0x00100000ULL);
        register_shader(
            registry,
            kProgram,
            0x00200000ULL);
        REQUIRE(registry.size() == 2U);

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    created_shader_lookup_failure);
        REQUIRE(
            result.error().
                created_shader_lookup_error.has_value());
        REQUIRE(
            result.error().
                created_shader_lookup_error->code ==
            astraea::execution::
                CreatedAgcShaderLookupErrorCode::
                    ambiguous);
        REQUIRE(
            result.error().
                created_shader_lookup_error->match_count ==
            2U);
        REQUIRE(registry.size() == 2U);
    }
}

TEST_CASE(
    "submission binding preserves PM4 framing failures",
    "[execution][agc][submission-binding][framing]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    SECTION("command bytes are not word aligned") {
        astraea::execution::SceAgcDcbSubmission submission{};
        submission.flag = 0U;
        submission.command_buffer_bytes = {
            std::byte{0x01},
            std::byte{0x02},
            std::byte{0x03},
        };

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pm4_framing_failure);
        REQUIRE(
            result.error().pm4_framing_error.has_value());
        REQUIRE(
            result.error().pm4_framing_error->code ==
            astraea::graphics::
                Pm4Type3FrameErrorCode::
                    command_buffer_not_word_aligned);
    }

    SECTION("packet type is not Type3") {
        auto submission =
            make_submission({
                0x00000000U,
            });

        const auto result =
            astraea::execution::
                plan_sce_agc_submitted_pixel_shader_binding(
                    submission,
                    registry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                SceAgcSubmittedPixelShaderBindingErrorCode::
                    pm4_framing_failure);
        REQUIRE(
            result.error().pm4_framing_error.has_value());
        REQUIRE(
            result.error().pm4_framing_error->code ==
            astraea::graphics::
                Pm4Type3FrameErrorCode::
                    unsupported_packet_type);
    }
}
