#include <astraea/graphics/shader_register_state.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::graphics::GraphicsIrEmission make_emission(
    astraea::graphics::GraphicsIrOperation operation) {
    return astraea::graphics::GraphicsIrEmission{
        .operation = std::move(operation),
        .provenance =
            astraea::graphics::GraphicsIrProvenance{
                .source_packet = {},
            },
    };
}

astraea::graphics::GraphicsIrEmission make_write(
    std::uint16_t start_offset,
    std::vector<std::uint32_t> values) {
    return make_emission(
        astraea::graphics::
            GraphicsIrShaderRegisterWriteRange{
                .start_offset = start_offset,
                .values = std::move(values),
            });
}

void write_pixel_program_address(
    astraea::graphics::ShaderRegisterState& state,
    std::uint64_t gpu_address) {
    const auto pgm_lo =
        static_cast<std::uint32_t>(
            (gpu_address >> 8U) &
            0xffffffffULL);
    const auto pgm_hi =
        static_cast<std::uint32_t>(
            (gpu_address >> 40U) &
            0xffULL);

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    astraea::graphics::
                        kPixelProgramLoRegisterOffset,
                    {
                        pgm_lo,
                        pgm_hi,
                    }),
                state);
    REQUIRE(result.has_value());
}

}  // namespace

TEST_CASE(
    "shader register state applies one value and marks it initialized",
    "[graphics][shader-register-state]") {
    astraea::graphics::ShaderRegisterState state{};

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x20U,
                    {0x12345678U}),
                state);

    REQUIRE(result.has_value());
    REQUIRE(result->start_offset == 0x20U);
    REQUIRE(result->value_count == 1U);
    REQUIRE(state.values[0x20U] == 0x12345678U);
    REQUIRE(state.initialized.test(0x20U));
    REQUIRE_FALSE(state.initialized.test(0x21U));
}

TEST_CASE(
    "shader register state applies consecutive multi-value range",
    "[graphics][shader-register-state]") {
    astraea::graphics::ShaderRegisterState state{};

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x30U,
                    {
                        0x11111111U,
                        0x22222222U,
                        0x33333333U,
                    }),
                state);

    REQUIRE(result.has_value());
    REQUIRE(state.values[0x30U] == 0x11111111U);
    REQUIRE(state.values[0x31U] == 0x22222222U);
    REQUIRE(state.values[0x32U] == 0x33333333U);
    REQUIRE(state.initialized.test(0x30U));
    REQUIRE(state.initialized.test(0x31U));
    REQUIRE(state.initialized.test(0x32U));
}

TEST_CASE(
    "explicit zero remains distinguishable from uninitialized register",
    "[graphics][shader-register-state]") {
    astraea::graphics::ShaderRegisterState state{};

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x40U,
                    {0U}),
                state);

    REQUIRE(result.has_value());
    REQUIRE(state.values[0x40U] == 0U);
    REQUIRE(state.initialized.test(0x40U));
    REQUIRE(state.values[0x41U] == 0U);
    REQUIRE_FALSE(state.initialized.test(0x41U));
}

TEST_CASE(
    "later shader register range deterministically overwrites overlap",
    "[graphics][shader-register-state]") {
    astraea::graphics::ShaderRegisterState state{};

    REQUIRE(
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x10U,
                    {
                        1U,
                        2U,
                        3U,
                    }),
                state)
            .has_value());

    REQUIRE(
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x11U,
                    {
                        20U,
                        30U,
                    }),
                state)
            .has_value());

    REQUIRE(state.values[0x10U] == 1U);
    REQUIRE(state.values[0x11U] == 20U);
    REQUIRE(state.values[0x12U] == 30U);
}

TEST_CASE(
    "shader register range preserves unrelated initialized state",
    "[graphics][shader-register-state]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[0x05U] = 0xabcdef01U;
    state.initialized.set(0x05U);

    REQUIRE(
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x100U,
                    {
                        0x01020304U,
                        0x05060708U,
                    }),
                state)
            .has_value());

    REQUIRE(state.values[0x05U] == 0xabcdef01U);
    REQUIRE(state.initialized.test(0x05U));
}

TEST_CASE(
    "out-of-bounds shader register range fails atomically",
    "[graphics][shader-register-state][negative]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[0x3ffU] = 0xaabbccddU;
    state.initialized.set(0x3ffU);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x3ffU,
                    {
                        0x11111111U,
                        0x22222222U,
                    }),
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderRegisterApplyErrorCode::
                register_range_out_of_bounds);
    REQUIRE(result.error().start_offset == 0x3ffU);
    REQUIRE(result.error().value_count == 2U);
    REQUIRE(state == before);
}

TEST_CASE(
    "out-of-window shader register start fails atomically",
    "[graphics][shader-register-state][negative]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[1U] = 9U;
    state.initialized.set(1U);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    0x400U,
                    {}),
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderRegisterApplyErrorCode::
                register_range_out_of_bounds);
    REQUIRE(state == before);
}

TEST_CASE(
    "unsupported Graphics IR fails without shader state mutation",
    "[graphics][shader-register-state][negative]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[7U] = 0x76543210U;
    state.initialized.set(7U);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_emission(
                    astraea::graphics::
                        GraphicsIrUnsupported{
                            .reason =
                                astraea::graphics::
                                    GraphicsIrUnsupportedReason::
                                        packet_semantics_unknown,
                        }),
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderRegisterApplyErrorCode::
                unsupported_operation);
    REQUIRE(
        result.error().unsupported_reason ==
        std::optional<
            astraea::graphics::
                GraphicsIrUnsupportedReason>{
            astraea::graphics::
                GraphicsIrUnsupportedReason::
                    packet_semantics_unknown});
    REQUIRE(state == before);
}

TEST_CASE(
    "pixel program resolver requires explicit PGM_LO",
    "[graphics][shader-register-state][pixel-program]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[
        astraea::graphics::
            kPixelProgramHiRegisterOffset] = 0U;
    state.initialized.set(
        astraea::graphics::
            kPixelProgramHiRegisterOffset);

    const auto result =
        astraea::graphics::
            resolve_pixel_program_address(state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            PixelProgramAddressErrorCode::
                pgm_lo_uninitialized);
}

TEST_CASE(
    "pixel program resolver requires explicit PGM_HI",
    "[graphics][shader-register-state][pixel-program]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[
        astraea::graphics::
            kPixelProgramLoRegisterOffset] = 0x12345678U;
    state.initialized.set(
        astraea::graphics::
            kPixelProgramLoRegisterOffset);

    const auto result =
        astraea::graphics::
            resolve_pixel_program_address(state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            PixelProgramAddressErrorCode::
                pgm_hi_uninitialized);
    REQUIRE(result.error().pgm_lo == 0x12345678U);
}

TEST_CASE(
    "pixel program resolver rejects unsupported PGM_HI upper bits",
    "[graphics][shader-register-state][pixel-program][negative]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[
        astraea::graphics::
            kPixelProgramLoRegisterOffset] = 0x12345678U;
    state.values[
        astraea::graphics::
            kPixelProgramHiRegisterOffset] = 0x0000019aU;
    state.initialized.set(
        astraea::graphics::
            kPixelProgramLoRegisterOffset);
    state.initialized.set(
        astraea::graphics::
            kPixelProgramHiRegisterOffset);

    const auto result =
        astraea::graphics::
            resolve_pixel_program_address(state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            PixelProgramAddressErrorCode::
                unsupported_pgm_hi_bits);
    REQUIRE(result.error().pgm_hi == 0x0000019aU);
}

TEST_CASE(
    "pixel program resolver round-trips representative aligned GPU addresses",
    "[graphics][shader-register-state][pixel-program]") {
    for (const auto address : {
             std::uint64_t{0x0000000000000100ULL},
             std::uint64_t{0x0000123456789a00ULL},
             std::uint64_t{0x0000ffffffffff00ULL},
         }) {
        astraea::graphics::ShaderRegisterState state{};
        write_pixel_program_address(state, address);

        const auto result =
            astraea::graphics::
                resolve_pixel_program_address(state);

        REQUIRE(result.has_value());
        REQUIRE(result->value == address);
    }
}

TEST_CASE(
    "explicitly written zero pixel program registers resolve to GPU VA zero",
    "[graphics][shader-register-state][pixel-program]") {
    astraea::graphics::ShaderRegisterState state{};

    REQUIRE(
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_write(
                    astraea::graphics::
                        kPixelProgramLoRegisterOffset,
                    {
                        0U,
                        0U,
                    }),
                state)
            .has_value());

    const auto result =
        astraea::graphics::
            resolve_pixel_program_address(state);

    REQUIRE(result.has_value());
    REQUIRE(result->value == 0U);
}

TEST_CASE(
    "shader register state equality includes initialization state",
    "[graphics][shader-register-state][equality]") {
    astraea::graphics::ShaderRegisterState left{};
    astraea::graphics::ShaderRegisterState right{};

    REQUIRE(left == right);

    right.initialized.set(0U);
    REQUIRE_FALSE(left == right);

    left.initialized.set(0U);
    REQUIRE(left == right);

    right.values[0U] = 1U;
    REQUIRE_FALSE(left == right);
}


TEST_CASE(
    "context register Graphics IR does not mutate shader state",
    "[graphics][shader-register-state][domain]") {
    astraea::graphics::ShaderRegisterState state{};
    state.values[2U] = 0x12345678U;
    state.initialized.set(2U);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_shader_register_graphics_ir(
                make_emission(
                    astraea::graphics::
                        GraphicsIrContextRegisterWriteRange{
                            .start_offset = 2U,
                            .values =
                                std::vector<std::uint32_t>{
                                    0xdeadbeefU},
                        }),
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ShaderRegisterApplyErrorCode::
                unsupported_operation);
    REQUIRE(state == before);
}
