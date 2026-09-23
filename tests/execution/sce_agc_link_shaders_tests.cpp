#include <astraea/execution/sce_agc_link_shaders.hpp>
#include <astraea/execution/sce_import_binding.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::CreatedAgcShader;
using astraea::execution::CreatedAgcShaderHandleLookupErrorCode;
using astraea::execution::CreatedAgcShaderRegistry;
using astraea::execution::HleCall;
using astraea::execution::HleFunctionId;
using astraea::execution::SceAgcLinkShadersPlanErrorCode;
using astraea::execution::SceAgcLinkShadersShaderSlot;
using astraea::execution::SceAgcShaderPreparationProfile;
using astraea::graphics::AgcShaderStage;
using astraea::graphics::GpuVirtualAddress;
using astraea::memory::GuestAddress;

constexpr std::uint64_t kContextOutput = 0x00100000ULL;
constexpr std::uint64_t kUserConfigOutput = 0x00101000ULL;
constexpr std::uint64_t kGeometryHandle = 0x00200000ULL;
constexpr std::uint64_t kPixelHandle = 0x00300000ULL;
constexpr std::uint64_t kGeometryCode = 0x00400000ULL;
constexpr std::uint64_t kPixelCode = 0x00500000ULL;

CreatedAgcShader make_created(
    std::uint64_t handle,
    std::uint64_t code,
    AgcShaderStage stage,
    SceAgcShaderPreparationProfile profile) {
    return CreatedAgcShader{
        .code_address =
            GpuVirtualAddress{.value = code},
        .stage = stage,
        .preparation_profile = profile,
        .shader_handle = GuestAddress{handle},
        .shader_header_address = GuestAddress{handle},
        .shader_text_address = GuestAddress{code},
        .shader = {},
        .shader_ir = {},
    };
}

CreatedAgcShaderRegistry make_registry() {
    CreatedAgcShaderRegistry registry;

    auto geometry =
        make_created(
            kGeometryHandle,
            kGeometryCode,
            AgcShaderStage::geometry,
            SceAgcShaderPreparationProfile::
                v18_geometry_es_public_shape);
    auto pixel =
        make_created(
            kPixelHandle,
            kPixelCode,
            AgcShaderStage::pixel,
            SceAgcShaderPreparationProfile::
                v18_pixel_public_shape);

    REQUIRE(
        registry.register_shader(
            std::move(geometry))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(pixel))
            .has_value());

    return registry;
}

HleCall make_call(
    std::uint64_t context_output = kContextOutput,
    std::uint64_t user_config_output = kUserConfigOutput,
    std::uint64_t hull_handle = 0,
    std::uint64_t pre_raster_handle = kGeometryHandle,
    std::uint64_t pixel_handle = kPixelHandle,
    std::uint64_t primitive_type =
        astraea::execution::
            kSceAgcLinkShadersTriangleListPrimitiveType,
    HleFunctionId function_id =
        astraea::execution::kSceAgcLinkShadersHleId) {
    return HleCall{
        .function_id = function_id,
        .gate_slot = 7,
        .guest_rip = 0x11112222ULL,
        .guest_rsp = 0x33334444ULL,
        .arguments =
            {
                context_output,
                user_config_output,
                hull_handle,
                pre_raster_handle,
                pixel_handle,
                primitive_type,
            },
    };
}

}  // namespace

TEST_CASE(
    "sceAgcLinkShaders planner captures the exact owned request and stable shader identities",
    "[execution][agc][link-shaders][planner]") {
    const auto registry = make_registry();
    const auto before_size = registry.size();

    const auto result =
        astraea::execution::
            plan_sce_agc_link_shaders(
                make_call(),
                registry);

    REQUIRE(result.has_value());
    REQUIRE(
        result->context_output_address ==
        GuestAddress{kContextOutput});
    REQUIRE(
        result->context_output_range.base() ==
        GuestAddress{kContextOutput});
    REQUIRE(
        result->context_output_range.size().value() ==
        astraea::execution::
            kSceAgcLinkShadersContextOutputSize);
    REQUIRE(
        result->context_output_range.size().value() ==
        0x110U);
    REQUIRE(
        result->user_config_output_address ==
        GuestAddress{kUserConfigOutput});
    REQUIRE(
        result->user_config_output_range.base() ==
        GuestAddress{kUserConfigOutput});
    REQUIRE(
        result->user_config_output_range.size().value() ==
        astraea::execution::
            kSceAgcLinkShadersUserConfigOutputSize);
    REQUIRE(
        result->user_config_output_range.size().value() ==
        0x18U);
    REQUIRE(
        result->hull_shader_handle ==
        GuestAddress{0});
    REQUIRE(
        result->pre_raster_shader.shader_handle ==
        GuestAddress{kGeometryHandle});
    REQUIRE(
        result->pre_raster_shader.code_address ==
        GpuVirtualAddress{.value = kGeometryCode});
    REQUIRE(
        result->pre_raster_shader.stage ==
        AgcShaderStage::geometry);
    REQUIRE(
        result->pre_raster_shader.preparation_profile ==
        SceAgcShaderPreparationProfile::
            v18_geometry_es_public_shape);
    REQUIRE(
        result->pixel_shader.shader_handle ==
        GuestAddress{kPixelHandle});
    REQUIRE(
        result->pixel_shader.code_address ==
        GpuVirtualAddress{.value = kPixelCode});
    REQUIRE(
        result->pixel_shader.stage ==
        AgcShaderStage::pixel);
    REQUIRE(
        result->pixel_shader.preparation_profile ==
        SceAgcShaderPreparationProfile::
            v18_pixel_public_shape);
    REQUIRE(
        result->primitive_type ==
        astraea::execution::
            kSceAgcLinkShadersTriangleListPrimitiveType);

    REQUIRE(registry.size() == before_size);
    REQUIRE(registry.entry_at(0U) != nullptr);
    REQUIRE(registry.entry_at(1U) != nullptr);
    REQUIRE(
        registry.entry_at(0U)->shader_handle ==
        GuestAddress{kGeometryHandle});
    REQUIRE(
        registry.entry_at(1U)->shader_handle ==
        GuestAddress{kPixelHandle});
}

TEST_CASE(
    "sceAgcLinkShaders planner rejects unsupported call envelope before handle lookup",
    "[execution][agc][link-shaders][planner][negative]") {
    const auto registry = make_registry();

    SECTION("wrong function") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        kUserConfigOutput,
                        0,
                        kGeometryHandle,
                        kPixelHandle,
                        4,
                        HleFunctionId{99}),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                unexpected_function);
        REQUIRE(
            result.error().function_id ==
            std::optional<HleFunctionId>{
                HleFunctionId{99}});
    }

    SECTION("null context output") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(0),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                null_context_output);
        REQUIRE(
            result.error().guest_address ==
            std::optional<GuestAddress>{
                GuestAddress{0}});
    }

    SECTION("null user-config output") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        0),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                null_user_config_output);
    }

    SECTION("non-null hull") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        kUserConfigOutput,
                        0x77770000ULL),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                unsupported_hull_shader);
        REQUIRE(
            result.error().guest_address ==
            std::optional<GuestAddress>{
                GuestAddress{0x77770000ULL}});
    }

    SECTION("unsupported primitive") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        kUserConfigOutput,
                        0,
                        kGeometryHandle,
                        kPixelHandle,
                        3U),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                unsupported_primitive_type);
        REQUIRE(result.error().primitive_type == 3U);
    }
}

TEST_CASE(
    "sceAgcLinkShaders planner validates exact output ranges and disjointness",
    "[execution][agc][link-shaders][planner][range]") {
    const auto registry = make_registry();

    SECTION("context range overflow") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        std::numeric_limits<std::uint64_t>::max() - 0x80U,
                        kUserConfigOutput),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                output_range_overflow);
        REQUIRE(
            result.error().guest_address ==
            std::optional<GuestAddress>{
                GuestAddress{std::numeric_limits<std::uint64_t>::max() - 0x80U}});
    }

    SECTION("user-config range overflow") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        std::numeric_limits<std::uint64_t>::max() - 0x10U),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                output_range_overflow);
        REQUIRE(
            result.error().guest_address ==
            std::optional<GuestAddress>{
                GuestAddress{std::numeric_limits<std::uint64_t>::max() - 0x10U}});
    }

    SECTION("overlap") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        0x1000U,
                        0x1100U),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                output_ranges_overlap);
    }

    SECTION("adjacent ranges are accepted") {
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        0x1000U,
                        0x1110U),
                    registry);
        REQUIRE(result.has_value());
    }
}

TEST_CASE(
    "sceAgcLinkShaders planner preserves exact handle lookup provenance",
    "[execution][agc][link-shaders][planner][lookup]") {
    SECTION("missing pre-raster handle") {
        const auto registry = make_registry();
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        kUserConfigOutput,
                        0,
                        0xdead0000ULL),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                pre_raster_handle_lookup_failure);
        REQUIRE(
            result.error().shader_slot ==
            std::optional<
                SceAgcLinkShadersShaderSlot>{
                SceAgcLinkShadersShaderSlot::
                    pre_raster});
        REQUIRE(
            result.error().handle_lookup_error.has_value());
        REQUIRE(
            result.error().handle_lookup_error->code ==
            CreatedAgcShaderHandleLookupErrorCode::
                not_found);
        REQUIRE(
            result.error().
                handle_lookup_error->
                shader_handle ==
            GuestAddress{0xdead0000ULL});
        REQUIRE(
            result.error().
                handle_lookup_error->
                match_count == 0U);
    }

    SECTION("ambiguous pre-raster handle") {
        auto registry = make_registry();
        auto duplicate =
            make_created(
                kGeometryHandle,
                0x00600000ULL,
                AgcShaderStage::geometry,
                SceAgcShaderPreparationProfile::
                    v18_geometry_es_public_shape);
        REQUIRE(
            registry.register_shader(
                std::move(duplicate))
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                pre_raster_handle_lookup_failure);
        REQUIRE(
            result.error().handle_lookup_error.has_value());
        REQUIRE(
            result.error().handle_lookup_error->code ==
            CreatedAgcShaderHandleLookupErrorCode::
                ambiguous);
        REQUIRE(
            result.error().
                handle_lookup_error->
                match_count == 2U);
    }

    SECTION("missing pixel handle") {
        const auto registry = make_registry();
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        kUserConfigOutput,
                        0,
                        kGeometryHandle,
                        0xbeef0000ULL),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                pixel_handle_lookup_failure);
        REQUIRE(
            result.error().shader_slot ==
            std::optional<
                SceAgcLinkShadersShaderSlot>{
                SceAgcLinkShadersShaderSlot::pixel});
        REQUIRE(
            result.error().handle_lookup_error.has_value());
        REQUIRE(
            result.error().handle_lookup_error->code ==
            CreatedAgcShaderHandleLookupErrorCode::
                not_found);
    }

    SECTION("ambiguous pixel handle") {
        auto registry = make_registry();
        auto duplicate =
            make_created(
                kPixelHandle,
                0x00700000ULL,
                AgcShaderStage::pixel,
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape);
        REQUIRE(
            registry.register_shader(
                std::move(duplicate))
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                pixel_handle_lookup_failure);
        REQUIRE(
            result.error().handle_lookup_error.has_value());
        REQUIRE(
            result.error().handle_lookup_error->code ==
            CreatedAgcShaderHandleLookupErrorCode::
                ambiguous);
        REQUIRE(
            result.error().
                handle_lookup_error->
                match_count == 2U);
    }
}

TEST_CASE(
    "sceAgcLinkShaders planner enforces slot stage and preparation profile",
    "[execution][agc][link-shaders][planner][stage]") {
    SECTION("same non-null handle in both slots") {
        const auto registry = make_registry();
        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(
                        kContextOutput,
                        kUserConfigOutput,
                        0,
                        kGeometryHandle,
                        kGeometryHandle),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                same_shader_handle);
    }

    SECTION("pixel shader in pre-raster slot") {
        CreatedAgcShaderRegistry registry;
        auto wrong =
            make_created(
                kGeometryHandle,
                kGeometryCode,
                AgcShaderStage::pixel,
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape);
        auto pixel =
            make_created(
                kPixelHandle,
                kPixelCode,
                AgcShaderStage::pixel,
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape);
        REQUIRE(
            registry.register_shader(
                std::move(wrong))
                .has_value());
        REQUIRE(
            registry.register_shader(
                std::move(pixel))
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                unsupported_pre_raster_shader);
        REQUIRE(
            result.error().actual_stage ==
            std::optional<AgcShaderStage>{
                AgcShaderStage::pixel});
        REQUIRE(
            result.error().
                actual_preparation_profile ==
            std::optional<
                SceAgcShaderPreparationProfile>{
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape});
    }

    SECTION("Geometry profile mismatch in pre-raster slot") {
        CreatedAgcShaderRegistry registry;
        auto wrong =
            make_created(
                kGeometryHandle,
                kGeometryCode,
                AgcShaderStage::geometry,
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape);
        auto pixel =
            make_created(
                kPixelHandle,
                kPixelCode,
                AgcShaderStage::pixel,
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape);
        REQUIRE(
            registry.register_shader(
                std::move(wrong))
                .has_value());
        REQUIRE(
            registry.register_shader(
                std::move(pixel))
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                unsupported_pre_raster_shader);
    }

    SECTION("Geometry shader in pixel slot") {
        CreatedAgcShaderRegistry registry;
        auto geometry =
            make_created(
                kGeometryHandle,
                kGeometryCode,
                AgcShaderStage::geometry,
                SceAgcShaderPreparationProfile::
                    v18_geometry_es_public_shape);
        auto wrong =
            make_created(
                kPixelHandle,
                kPixelCode,
                AgcShaderStage::geometry,
                SceAgcShaderPreparationProfile::
                    v18_geometry_es_public_shape);
        REQUIRE(
            registry.register_shader(
                std::move(geometry))
                .has_value());
        REQUIRE(
            registry.register_shader(
                std::move(wrong))
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_link_shaders(
                    make_call(),
                    registry);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcLinkShadersPlanErrorCode::
                unsupported_pixel_shader);
        REQUIRE(
            result.error().actual_stage ==
            std::optional<AgcShaderStage>{
                AgcShaderStage::geometry});
    }
}

TEST_CASE(
    "sceAgcLinkShaders HLE descriptor and owned import identity bind exactly",
    "[execution][agc][link-shaders][import]") {
    auto hle =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::HleFunctionDescriptor>{
                {
                    .id =
                        astraea::execution::
                            kSceAgcLinkShadersHleId,
                    .canonical_name =
                        "sceAgcLinkShaders",
                    .argument_count = 6,
                },
            });
    REQUIRE(hle.has_value());

    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity =
                astraea::loader::SceSymbolIdentity{
                    .nid = "MqAdbRMdNz4",
                    .library_id = "A",
                    .module_id = "B",
                },
            .function_id =
                astraea::execution::
                    kSceAgcLinkShadersHleId,
        },
    };

    const auto imports =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle.value(),
                bindings);
    REQUIRE(imports.has_value());

    const auto exact =
        imports->resolve(
            astraea::loader::SceSymbolIdentity{
                .nid = "MqAdbRMdNz4",
                .library_id = "A",
                .module_id = "B",
            });
    REQUIRE(
        exact.kind ==
        astraea::execution::
            SceImportResolutionKind::resolved);
    REQUIRE(
        exact.function_id ==
        std::optional<HleFunctionId>{
            astraea::execution::
                kSceAgcLinkShadersHleId});

    for (const auto& wrong :
         std::array{
             astraea::loader::SceSymbolIdentity{
                 .nid = "MqAdbRMdNz5",
                 .library_id = "A",
                 .module_id = "B",
             },
             astraea::loader::SceSymbolIdentity{
                 .nid = "MqAdbRMdNz4",
                 .library_id = "C",
                 .module_id = "B",
             },
             astraea::loader::SceSymbolIdentity{
                 .nid = "MqAdbRMdNz4",
                 .library_id = "A",
                 .module_id = "C",
             },
         }) {
        const auto unresolved =
            imports->resolve(wrong);
        REQUIRE(
            unresolved.kind ==
            astraea::execution::
                SceImportResolutionKind::unresolved);
        REQUIRE_FALSE(
            unresolved.function_id.has_value());
    }
}


TEST_CASE(
    "sceAgcLinkShaders measured output contains only the pinned context records",
    "[execution][agc][link-shaders][measured-output]") {
    const auto registry = make_registry();
    const auto request =
        astraea::execution::
            plan_sce_agc_link_shaders(
                make_call(),
                registry);
    REQUIRE(request.has_value());

    const auto output =
        astraea::execution::
            materialize_sce_agc_link_shaders_measured_output(
                request.value());
    REQUIRE(output.has_value());

    REQUIRE(
        output->completeness ==
        astraea::execution::
            SceAgcLinkShadersOutputCompleteness::
                measured_partial);
    REQUIRE(
        output->interpolant_records.size() ==
        astraea::execution::
            kSceAgcLinkShadersMeasuredInterpolantRecordCount);
    REQUIRE(
        output->interpolant_records.front() ==
        astraea::execution::
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x191U,
                .value = 0U,
            });
    REQUIRE(
        output->interpolant_records[1] ==
        astraea::execution::
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x192U,
                .value = 1U,
            });
    REQUIRE(
        output->interpolant_records.back() ==
        astraea::execution::
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x1b0U,
                .value = 31U,
            });
    REQUIRE(
        output->routing_record ==
        astraea::execution::
            SceAgcLinkShadersRegisterRecord{
                .offset = 0x29bU,
                .value = 2U,
            });

    REQUIRE(
        output->interpolant_output_address ==
        GuestAddress{kContextOutput});
    REQUIRE(
        output->interpolant_output_range.size().value() ==
        0x100U);
    REQUIRE(
        output->routing_output_address ==
        GuestAddress{kContextOutput + 0x108U});
    REQUIRE(
        output->routing_output_range.size().value() ==
        8U);
    REQUIRE(
        output->preserved_user_config_address ==
        GuestAddress{kUserConfigOutput});

    // The measured patches intentionally leave exactly one eight-byte context
    // record at +0x100 untouched.
    REQUIRE(
        output->routing_output_address.value() -
            (output->interpolant_output_address.value() +
             output->interpolant_output_range.size().value()) ==
        8U);
}

TEST_CASE(
    "sceAgcLinkShaders measured output reports routing address overflow",
    "[execution][agc][link-shaders][measured-output][negative]") {
    const auto registry = make_registry();
    auto request =
        astraea::execution::
            plan_sce_agc_link_shaders(
                make_call(),
                registry);
    REQUIRE(request.has_value());

    request->context_output_address =
        GuestAddress{
            std::numeric_limits<std::uint64_t>::max() -
            0x100U};

    const auto output =
        astraea::execution::
            materialize_sce_agc_link_shaders_measured_output(
                request.value());
    REQUIRE_FALSE(output.has_value());
    REQUIRE(
        output.error().code ==
        astraea::execution::
            SceAgcLinkShadersMeasuredOutputPlanErrorCode::
                measured_output_address_overflow);
    REQUIRE(
        output.error().base_address ==
        std::optional<GuestAddress>{
            GuestAddress{
                std::numeric_limits<std::uint64_t>::max() -
                0x100U}});
    REQUIRE(
        output.error().byte_offset ==
        astraea::execution::
            kSceAgcLinkShadersMeasuredRoutingByteOffset);
}
