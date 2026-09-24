#include <astraea/execution/sce_agc_shader_registry.hpp>

#include <utility>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::execution::CreatedAgcShader shader(
    astraea::graphics::AgcShaderStage stage,
    std::uint64_t code,
    std::uint64_t handle) {
    const auto guest_handle =
        astraea::memory::GuestAddress{handle};

    return astraea::execution::CreatedAgcShader{
        .code_address =
            astraea::graphics::GpuVirtualAddress{
                .value = code,
            },
        .stage = stage,
        .preparation_profile =
            stage == astraea::graphics::AgcShaderStage::geometry
                ? astraea::execution::
                      SceAgcShaderPreparationProfile::
                          v18_geometry_es_public_shape
                : astraea::execution::
                      SceAgcShaderPreparationProfile::
                          v18_pixel_public_shape,
        .shader_handle = guest_handle,
        .shader_header_address = guest_handle,
        .shader_text_address =
            astraea::memory::GuestAddress{code},
        .shader = {},
        .shader_ir = {},
    };
}

}  // namespace

TEST_CASE(
    "stage-qualified shader lookup isolates identical Pixel and Geometry addresses",
    "[execution][agc][shader-registry][stage-lookup]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    REQUIRE(
        registry.register_shader(
                    shader(
                        astraea::graphics::AgcShaderStage::pixel,
                        0x00200000ULL,
                        0x00100000ULL))
            .has_value());
    REQUIRE(
        registry.register_shader(
                    shader(
                        astraea::graphics::AgcShaderStage::geometry,
                        0x00200000ULL,
                        0x00110000ULL))
            .has_value());

    const auto pixel =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00200000ULL},
            astraea::graphics::AgcShaderStage::pixel);
    const auto geometry =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00200000ULL},
            astraea::graphics::AgcShaderStage::geometry);

    REQUIRE(pixel.has_value());
    REQUIRE(geometry.has_value());
    REQUIRE(
        pixel.value().get().shader_handle ==
        astraea::memory::GuestAddress{0x00100000ULL});
    REQUIRE(
        geometry.value().get().shader_handle ==
        astraea::memory::GuestAddress{0x00110000ULL});
}

TEST_CASE(
    "stage-qualified shader lookup reports same-stage duplicates only",
    "[execution][agc][shader-registry][stage-lookup][negative]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    REQUIRE(
        registry.register_shader(
                    shader(
                        astraea::graphics::AgcShaderStage::geometry,
                        0x00300000ULL,
                        0x00110000ULL))
            .has_value());
    REQUIRE(
        registry.register_shader(
                    shader(
                        astraea::graphics::AgcShaderStage::geometry,
                        0x00300000ULL,
                        0x00120000ULL))
            .has_value());
    REQUIRE(
        registry.register_shader(
                    shader(
                        astraea::graphics::AgcShaderStage::pixel,
                        0x00300000ULL,
                        0x00130000ULL))
            .has_value());

    const auto result =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00300000ULL},
            astraea::graphics::AgcShaderStage::geometry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            CreatedAgcShaderStageLookupErrorCode::ambiguous);
    REQUIRE(result.error().match_count == 2U);

    const auto pixel =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00300000ULL},
            astraea::graphics::AgcShaderStage::pixel);
    REQUIRE(pixel.has_value());
}

TEST_CASE(
    "stage-qualified shader lookup preserves missing stage and address",
    "[execution][agc][shader-registry][stage-lookup][negative]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    const auto result =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00900000ULL},
            astraea::graphics::AgcShaderStage::geometry);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            CreatedAgcShaderStageLookupErrorCode::not_found);
    REQUIRE(
        result.error().program_address ==
        astraea::graphics::GpuVirtualAddress{
            .value = 0x00900000ULL});
    REQUIRE(
        result.error().stage ==
        astraea::graphics::AgcShaderStage::geometry);
    REQUIRE(result.error().match_count == 0U);
}
