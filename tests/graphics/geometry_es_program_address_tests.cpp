#include <astraea/graphics/shader_register_state.hpp>

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

namespace {

void set_register(
    astraea::graphics::ShaderRegisterState& state,
    std::uint16_t offset,
    std::uint32_t value) {
    state.values[offset] = value;
    state.initialized.set(offset);
}

void write_geometry_program(
    astraea::graphics::ShaderRegisterState& state,
    std::uint64_t address) {
    set_register(
        state,
        astraea::graphics::kGeometryEsProgramLoRegisterOffset,
        static_cast<std::uint32_t>(
            (address >> 8U) & 0xffffffffULL));
    set_register(
        state,
        astraea::graphics::kGeometryEsProgramHiRegisterOffset,
        static_cast<std::uint32_t>(
            (address >> 40U) & 0xffULL));
}

}  // namespace

TEST_CASE(
    "Geometry ES program address round-trips the verified register encoding",
    "[graphics][shader-register-state][geometry-es]") {
    for (const auto address : {
             std::uint64_t{0x0000000000000100ULL},
             std::uint64_t{0x0000123456789a00ULL},
             std::uint64_t{0x0000ffffffffff00ULL},
         }) {
        astraea::graphics::ShaderRegisterState state{};
        write_geometry_program(state, address);

        const auto result =
            astraea::graphics::
                resolve_geometry_es_program_address(state);

        REQUIRE(result.has_value());
        REQUIRE(result->value == address);
    }
}

TEST_CASE(
    "Geometry ES program address requires both registers",
    "[graphics][shader-register-state][geometry-es][negative]") {
    SECTION("missing PGM_LO") {
        astraea::graphics::ShaderRegisterState state{};
        set_register(
            state,
            astraea::graphics::kGeometryEsProgramHiRegisterOffset,
            0U);

        const auto result =
            astraea::graphics::
                resolve_geometry_es_program_address(state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GeometryEsProgramAddressErrorCode::
                    pgm_lo_uninitialized);
    }

    SECTION("missing PGM_HI") {
        astraea::graphics::ShaderRegisterState state{};
        set_register(
            state,
            astraea::graphics::kGeometryEsProgramLoRegisterOffset,
            0x12345678U);

        const auto result =
            astraea::graphics::
                resolve_geometry_es_program_address(state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::graphics::
                GeometryEsProgramAddressErrorCode::
                    pgm_hi_uninitialized);
        REQUIRE(result.error().pgm_lo == 0x12345678U);
    }
}

TEST_CASE(
    "Geometry ES program address rejects unsupported high bits",
    "[graphics][shader-register-state][geometry-es][negative]") {
    astraea::graphics::ShaderRegisterState state{};
    set_register(
        state,
        astraea::graphics::kGeometryEsProgramLoRegisterOffset,
        0x12345678U);
    set_register(
        state,
        astraea::graphics::kGeometryEsProgramHiRegisterOffset,
        0x0000019aU);

    const auto result =
        astraea::graphics::
            resolve_geometry_es_program_address(state);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::graphics::
            GeometryEsProgramAddressErrorCode::
                unsupported_pgm_hi_bits);
    REQUIRE(result.error().pgm_hi == 0x0000019aU);
}
