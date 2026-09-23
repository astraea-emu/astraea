#include <astraea/graphics/context_register_state.hpp>

#include <cstdint>
#include <optional>
#include <utility>
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

astraea::graphics::GraphicsIrEmission make_context_write(
    std::uint16_t start_offset,
    std::vector<std::uint32_t> values) {
    return make_emission(
        astraea::graphics::
            GraphicsIrContextRegisterWriteRange{
                .start_offset = start_offset,
                .values = std::move(values),
            });
}

}  // namespace

TEST_CASE(
    "context register state applies one value and marks it initialized",
    "[graphics][context-register-state]") {
    astraea::graphics::ContextRegisterState state{};

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
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
    "context register state applies consecutive multi-value range",
    "[graphics][context-register-state]") {
    astraea::graphics::ContextRegisterState state{};

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
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
    "explicit zero remains distinguishable from uninitialized context register",
    "[graphics][context-register-state]") {
    astraea::graphics::ContextRegisterState state{};

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
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
    "later context register range deterministically overwrites overlap",
    "[graphics][context-register-state]") {
    astraea::graphics::ContextRegisterState state{};

    REQUIRE(
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
                    0x10U,
                    {1U, 2U, 3U}),
                state)
            .has_value());

    REQUIRE(
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
                    0x11U,
                    {20U, 30U}),
                state)
            .has_value());

    REQUIRE(state.values[0x10U] == 1U);
    REQUIRE(state.values[0x11U] == 20U);
    REQUIRE(state.values[0x12U] == 30U);
}

TEST_CASE(
    "context register range preserves unrelated initialized state",
    "[graphics][context-register-state]") {
    astraea::graphics::ContextRegisterState state{};
    state.values[0x05U] = 0xabcdef01U;
    state.initialized.set(0x05U);

    REQUIRE(
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
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
    "out-of-bounds context register range fails atomically",
    "[graphics][context-register-state][negative]") {
    astraea::graphics::ContextRegisterState state{};
    state.values[0x3ffU] = 0xaabbccddU;
    state.initialized.set(0x3ffU);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
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
            ContextRegisterApplyErrorCode::
                register_range_out_of_bounds);
    REQUIRE(result.error().start_offset == 0x3ffU);
    REQUIRE(result.error().value_count == 2U);
    REQUIRE(state == before);
}

TEST_CASE(
    "out-of-window context register start fails atomically",
    "[graphics][context-register-state][negative]") {
    astraea::graphics::ContextRegisterState state{};
    state.values[1U] = 9U;
    state.initialized.set(1U);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_context_write(
                    0x400U,
                    {}),
                state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            ContextRegisterApplyErrorCode::
                register_range_out_of_bounds);
    REQUIRE(state == before);
}

TEST_CASE(
    "unsupported Graphics IR fails without context state mutation",
    "[graphics][context-register-state][negative]") {
    astraea::graphics::ContextRegisterState state{};
    state.values[7U] = 0x76543210U;
    state.initialized.set(7U);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
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
            ContextRegisterApplyErrorCode::
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
    "shader register Graphics IR does not mutate context state",
    "[graphics][context-register-state][domain]") {
    astraea::graphics::ContextRegisterState state{};
    state.values[2U] = 0x12345678U;
    state.initialized.set(2U);
    const auto before = state;

    const auto result =
        astraea::graphics::
            apply_context_register_graphics_ir(
                make_emission(
                    astraea::graphics::
                        GraphicsIrShaderRegisterWriteRange{
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
            ContextRegisterApplyErrorCode::
                unsupported_operation);
    REQUIRE(state == before);
}

TEST_CASE(
    "context register state equality includes initialization state",
    "[graphics][context-register-state][equality]") {
    astraea::graphics::ContextRegisterState left{};
    astraea::graphics::ContextRegisterState right{};

    REQUIRE(left == right);

    right.initialized.set(0U);
    REQUIRE_FALSE(left == right);

    left.initialized.set(0U);
    REQUIRE(left == right);

    right.values[0U] = 1U;
    REQUIRE_FALSE(left == right);
}
