#include <astraea/execution/sce_agc_shader_registry.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint32_t kSoppBase = 0xbf800000U;
constexpr std::uint32_t kVop1Base = 0x7e000000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
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

astraea::execution::SceAgcShaderPreparationPlan
make_preparation(
    std::uint64_t header_address,
    std::uint64_t text_address,
    std::uint64_t handle_address,
    std::vector<std::uint32_t> rdna2_words = {
        make_sopp(0, 0),
        make_sopp(1, 0),
    },
    std::uint8_t raw_program_type = 1U,
    std::optional<astraea::graphics::AgcShaderStage>
        known_stage =
            astraea::graphics::AgcShaderStage::pixel,
    astraea::execution::SceAgcShaderPreparationProfile
        profile =
            astraea::execution::
                SceAgcShaderPreparationProfile::
                    v18_pixel_public_shape) {
    astraea::graphics::AgcShaderBinary shader{
        .header_magic = 0x34333231U,
        .header_version = 0x18U,
        .declared_header_size = 0x160U,
        .declared_shader_text_size = 0x80U,
        .program_type =
            astraea::graphics::AgcShaderProgramType{
                .raw = raw_program_type,
                .known = known_stage,
            },
        .context_register_list_header_offset =
            0xc8U,
        .shader_register_list_header_offset =
            0x98U,
        .context_registers = {},
        .shader_registers = {},
        .program_byte_size =
            static_cast<std::uint32_t>(
                rdna2_words.size() * 4U),
        .trailer_sl00_byte_size = 0U,
        .shader_header_bytes = {},
        .shader_text_bytes = {},
        .rdna2_words = std::move(rdna2_words),
    };

    return astraea::execution::
        SceAgcShaderPreparationPlan{
            .create_shader =
                astraea::execution::
                    SceAgcCreateShaderPlan{
                        .request =
                            astraea::execution::
                                SceAgcCreateShaderRequest{
                                    .output_pointer_address =
                                        astraea::memory::
                                            GuestAddress{
                                                0x700000U},
                                    .shader_header_address =
                                        astraea::memory::
                                            GuestAddress{
                                                header_address},
                                    .shader_text_address =
                                        astraea::memory::
                                            GuestAddress{
                                                text_address},
                                },
                        .shader = std::move(shader),
                    },
            .profile = profile,
            .shader_handle =
                astraea::memory::GuestAddress{
                    handle_address},
            .patches = {},
        };
}

astraea::execution::CreatedAgcShader
require_created(
    std::uint64_t header_address,
    std::uint64_t text_address,
    std::uint64_t handle_address) {
    const auto preparation =
        make_preparation(
            header_address,
            text_address,
            handle_address);
    auto result =
        astraea::execution::
            materialize_created_agc_shader(
                preparation);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::execution::CreatedAgcShader
require_geometry_created(
    std::uint64_t header_address,
    std::uint64_t text_address,
    std::uint64_t handle_address) {
    const auto preparation =
        make_preparation(
            header_address,
            text_address,
            handle_address,
            {
                make_sopp(0, 0),
                make_sopp(1, 0),
            },
            2U,
            astraea::graphics::AgcShaderStage::geometry,
            astraea::execution::
                SceAgcShaderPreparationProfile::
                    v18_geometry_es_public_shape);
    auto result =
        astraea::execution::
            materialize_created_agc_shader(
                preparation);
    REQUIRE(result.has_value());
    return std::move(result).value();
}

}  // namespace

TEST_CASE(
    "created AGC shader materialization preserves provenance and lowers Shader IR",
    "[execution][agc][shader-registry]") {
    constexpr std::uint64_t kHeader = 0x00100000ULL;
    constexpr std::uint64_t kText = 0x00234500ULL;
    constexpr std::uint64_t kHandle = 0x00100000ULL;

    const auto preparation =
        make_preparation(
            kHeader,
            kText,
            kHandle);
    const auto result =
        astraea::execution::
            materialize_created_agc_shader(
                preparation);

    REQUIRE(result.has_value());
    REQUIRE(
        result->code_address ==
        astraea::graphics::
            GpuVirtualAddress{
                .value = kText});
    REQUIRE(
        result->stage ==
        astraea::graphics::AgcShaderStage::pixel);
    REQUIRE(
        result->preparation_profile ==
        astraea::execution::
            SceAgcShaderPreparationProfile::
                v18_pixel_public_shape);
    REQUIRE(
        result->shader_handle ==
        astraea::memory::GuestAddress{kHandle});
    REQUIRE(
        result->shader_header_address ==
        astraea::memory::GuestAddress{kHeader});
    REQUIRE(
        result->shader_text_address ==
        astraea::memory::GuestAddress{kText});
    REQUIRE(
        result->shader.rdna2_words ==
        preparation.create_shader.shader.rdna2_words);
    REQUIRE(result->shader_ir.source_word_count == 2U);
    REQUIRE(result->shader_ir.emissions.size() == 2U);
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrNop>(
            result->shader_ir.emissions[0].operation));
    REQUIRE(
        std::holds_alternative<
            astraea::graphics::ShaderIrEndProgram>(
            result->shader_ir.emissions[1].operation));
}

TEST_CASE(
    "created Geometry AGC shader materialization preserves stage profile and provenance",
    "[execution][agc][shader-registry][geometry]") {
    constexpr std::uint64_t kHeader = 0x00110000ULL;
    constexpr std::uint64_t kText = 0x00245600ULL;
    constexpr std::uint64_t kHandle = 0x00110000ULL;

    const auto preparation =
        make_preparation(
            kHeader,
            kText,
            kHandle,
            {
                make_sopp(0, 0),
                make_sopp(1, 0),
            },
            2U,
            astraea::graphics::AgcShaderStage::geometry,
            astraea::execution::
                SceAgcShaderPreparationProfile::
                    v18_geometry_es_public_shape);
    const auto result =
        astraea::execution::
            materialize_created_agc_shader(
                preparation);

    REQUIRE(result.has_value());
    REQUIRE(
        result->code_address ==
        astraea::graphics::
            GpuVirtualAddress{
                .value = kText});
    REQUIRE(
        result->stage ==
        astraea::graphics::AgcShaderStage::geometry);
    REQUIRE(
        result->preparation_profile ==
        astraea::execution::
            SceAgcShaderPreparationProfile::
                v18_geometry_es_public_shape);
    REQUIRE(
        result->shader_handle ==
        astraea::memory::GuestAddress{kHandle});
    REQUIRE(
        result->shader_header_address ==
        astraea::memory::GuestAddress{kHeader});
    REQUIRE(
        result->shader_text_address ==
        astraea::memory::GuestAddress{kText});
    REQUIRE(
        result->shader ==
        preparation.create_shader.shader);
    REQUIRE(result->shader_ir.source_word_count == 2U);
    REQUIRE(result->shader_ir.emissions.size() == 2U);
}

TEST_CASE(
    "created AGC shader materialization preserves nested RDNA2 lowering failure",
    "[execution][agc][shader-registry][negative]") {
    const auto preparation =
        make_preparation(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL,
            {
                make_sopp(0, 0),
                make_vop1(1, 5, 255),
            });

    const auto result =
        astraea::execution::
            materialize_created_agc_shader(
                preparation);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            CreatedAgcShaderMaterializationErrorCode::
                shader_ir_lowering_failure);
    REQUIRE(result.error().shader_ir_error.has_value());
    REQUIRE(
        result.error().shader_ir_error->code ==
        astraea::graphics::
            ShaderIrProgramErrorCode::
                decode_failure);
    REQUIRE(
        result.error().shader_ir_error->word_index ==
        1U);
    REQUIRE(
        result.error().
            shader_ir_error->
            lowered_instruction_count ==
        1U);
    REQUIRE(
        result.error().shader_ir_error->decode_error
            .has_value());
    REQUIRE(
        result.error().
            shader_ir_error->
            decode_error->
            code ==
        astraea::graphics::
            Rdna2DecodeErrorCode::
                instruction_out_of_bounds);
}

TEST_CASE(
    "created AGC shader materialization rejects profile stage mismatches",
    "[execution][agc][shader-registry][negative][stage]") {
    SECTION("pixel profile with Geometry binary") {
        auto preparation =
            make_preparation(
                0x00100000ULL,
                0x00200000ULL,
                0x00100000ULL);
        preparation.create_shader.shader.program_type =
            astraea::graphics::AgcShaderProgramType{
                .raw = 2U,
                .known =
                    astraea::graphics::AgcShaderStage::
                        geometry,
            };

        const auto result =
            astraea::execution::
                materialize_created_agc_shader(
                    preparation);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                CreatedAgcShaderMaterializationErrorCode::
                    preparation_stage_mismatch);
    }

    SECTION("Geometry profile with pixel binary") {
        auto preparation =
            make_preparation(
                0x00100000ULL,
                0x00200000ULL,
                0x00100000ULL);
        preparation.profile =
            astraea::execution::
                SceAgcShaderPreparationProfile::
                    v18_geometry_es_public_shape;

        const auto result =
            astraea::execution::
                materialize_created_agc_shader(
                    preparation);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                CreatedAgcShaderMaterializationErrorCode::
                    preparation_stage_mismatch);
    }
}

TEST_CASE(
    "created AGC shader materialization rejects unsupported preparation profile",
    "[execution][agc][shader-registry][negative]") {
    auto preparation =
        make_preparation(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    preparation.profile =
        static_cast<
            astraea::execution::
                SceAgcShaderPreparationProfile>(
            0xffU);

    const auto result =
        astraea::execution::
            materialize_created_agc_shader(
                preparation);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            CreatedAgcShaderMaterializationErrorCode::
                unsupported_preparation_profile);
    REQUIRE_FALSE(
        result.error().shader_ir_error.has_value());
}

TEST_CASE(
    "created AGC shader registry preserves deterministic insertion order",
    "[execution][agc][shader-registry]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto first =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto second =
        require_geometry_created(
            0x00110000ULL,
            0x00300000ULL,
            0x00110000ULL);

    const auto first_index =
        registry.register_shader(
            std::move(first));
    REQUIRE(first_index.has_value());
    REQUIRE(first_index.value() == 0U);

    const auto second_index =
        registry.register_shader(
            std::move(second));
    REQUIRE(second_index.has_value());
    REQUIRE(second_index.value() == 1U);

    REQUIRE(registry.size() == 2U);
    REQUIRE(registry.entry_at(0U) != nullptr);
    REQUIRE(registry.entry_at(1U) != nullptr);
    REQUIRE(registry.entry_at(2U) == nullptr);
    REQUIRE(
        registry.entry_at(0U)->shader_header_address ==
        astraea::memory::GuestAddress{
            0x00100000ULL});
    REQUIRE(
        registry.entry_at(1U)->shader_header_address ==
        astraea::memory::GuestAddress{
            0x00110000ULL});
}

TEST_CASE(
    "created AGC shader registry unique lookup returns the exact record",
    "[execution][agc][shader-registry][lookup]") {
    astraea::execution::CreatedAgcShaderRegistry registry;
    auto shader =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(shader))
            .has_value());

    const auto result =
        registry.lookup_unique(
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value = 0x00200000ULL});

    REQUIRE(result.has_value());
    const auto& found = result.value().get();
    REQUIRE(
        found.shader_handle ==
        astraea::memory::GuestAddress{
            0x00100000ULL});
    REQUIRE(
        found.shader_text_address ==
        astraea::memory::GuestAddress{
            0x00200000ULL});
    REQUIRE(found.shader_ir.emissions.size() == 2U);
}

TEST_CASE(
    "created AGC shader registry missing lookup is explicit",
    "[execution][agc][shader-registry][lookup][negative]") {
    astraea::execution::CreatedAgcShaderRegistry registry;
    auto shader =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    REQUIRE(
        registry.register_shader(
            std::move(shader))
            .has_value());

    const auto result =
        registry.lookup_unique(
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value = 0x00900000ULL});

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            CreatedAgcShaderLookupErrorCode::
                not_found);
    REQUIRE(
        result.error().program_address ==
        astraea::graphics::
            PixelProgramGpuAddress{
                .value = 0x00900000ULL});
    REQUIRE(result.error().match_count == 0U);
}

TEST_CASE(
    "created AGC shader registry reports duplicate program address as ambiguous",
    "[execution][agc][shader-registry][lookup][duplicate]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto first =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto second =
        require_created(
            0x00110000ULL,
            0x00200000ULL,
            0x00110000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(first))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(second))
            .has_value());

    REQUIRE(registry.size() == 2U);
    REQUIRE(
        registry.entry_at(0U)->code_address ==
        registry.entry_at(1U)->code_address);
    REQUIRE(
        registry.entry_at(0U)->shader_handle !=
        registry.entry_at(1U)->shader_handle);

    const auto result =
        registry.lookup_unique(
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value = 0x00200000ULL});

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            CreatedAgcShaderLookupErrorCode::
                ambiguous);
    REQUIRE(result.error().match_count == 2U);
}

TEST_CASE(
    "pixel lookup ignores Geometry record with the same numeric code address",
    "[execution][agc][shader-registry][lookup][mixed-stage]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto pixel =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto geometry =
        require_geometry_created(
            0x00110000ULL,
            0x00200000ULL,
            0x00110000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(pixel))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(geometry))
            .has_value());

    const auto result =
        registry.lookup_unique(
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value = 0x00200000ULL});

    REQUIRE(result.has_value());
    REQUIRE(
        result.value().get().stage ==
        astraea::graphics::AgcShaderStage::pixel);
    REQUIRE(
        result.value().get().shader_handle ==
        astraea::memory::GuestAddress{
            0x00100000ULL});
}

TEST_CASE(
    "created AGC shader handle lookup is stage independent and duplicate safe",
    "[execution][agc][shader-registry][handle-lookup]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto pixel =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto geometry =
        require_geometry_created(
            0x00110000ULL,
            0x00300000ULL,
            0x00110000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(pixel))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(geometry))
            .has_value());

    const auto geometry_lookup =
        registry.lookup_unique_by_handle(
            astraea::memory::GuestAddress{
                0x00110000ULL});
    REQUIRE(geometry_lookup.has_value());
    REQUIRE(
        geometry_lookup.value().get().stage ==
        astraea::graphics::AgcShaderStage::geometry);
    REQUIRE(
        geometry_lookup.value().get().code_address ==
        astraea::graphics::GpuVirtualAddress{
            .value = 0x00300000ULL});

    const auto missing =
        registry.lookup_unique_by_handle(
            astraea::memory::GuestAddress{
                0x00900000ULL});
    REQUIRE_FALSE(missing.has_value());
    REQUIRE(
        missing.error().code ==
        astraea::execution::
            CreatedAgcShaderHandleLookupErrorCode::
                not_found);
    REQUIRE(
        missing.error().shader_handle ==
        astraea::memory::GuestAddress{
            0x00900000ULL});
    REQUIRE(missing.error().match_count == 0U);

    auto duplicate =
        require_geometry_created(
            0x00120000ULL,
            0x00400000ULL,
            0x00110000ULL);
    REQUIRE(
        registry.register_shader(
            std::move(duplicate))
            .has_value());

    const auto ambiguous =
        registry.lookup_unique_by_handle(
            astraea::memory::GuestAddress{
                0x00110000ULL});
    REQUIRE_FALSE(ambiguous.has_value());
    REQUIRE(
        ambiguous.error().code ==
        astraea::execution::
            CreatedAgcShaderHandleLookupErrorCode::
                ambiguous);
    REQUIRE(ambiguous.error().match_count == 2U);
}

TEST_CASE(
    "created AGC shader registry keeps different program addresses independently retrievable",
    "[execution][agc][shader-registry][lookup]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto first =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto second =
        require_created(
            0x00110000ULL,
            0x00300000ULL,
            0x00110000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(first))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(second))
            .has_value());

    const auto first_lookup =
        registry.lookup_unique(
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value = 0x00200000ULL});
    const auto second_lookup =
        registry.lookup_unique(
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value = 0x00300000ULL});

    REQUIRE(first_lookup.has_value());
    REQUIRE(second_lookup.has_value());
    REQUIRE(
        first_lookup.value().get().shader_header_address ==
        astraea::memory::GuestAddress{
            0x00100000ULL});
    REQUIRE(
        second_lookup.value().get().shader_header_address ==
        astraea::memory::GuestAddress{
            0x00110000ULL});
}


TEST_CASE(
    "created AGC shader registry rolls back only the exact last registration",
    "[execution][agc][shader-registry][rollback]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto first =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto second =
        require_geometry_created(
            0x00110000ULL,
            0x00300000ULL,
            0x00110000ULL);

    const auto first_index =
        registry.register_shader(
            std::move(first));
    const auto second_index =
        registry.register_shader(
            std::move(second));
    REQUIRE(first_index.has_value());
    REQUIRE(second_index.has_value());
    REQUIRE(first_index.value() == 0U);
    REQUIRE(second_index.value() == 1U);
    REQUIRE(registry.size() == 2U);

    REQUIRE_FALSE(
        registry.rollback_last_registration(
            first_index.value()));
    REQUIRE_FALSE(
        registry.rollback_last_registration(
            99U));
    REQUIRE(registry.size() == 2U);
    REQUIRE(
        registry.entry_at(0U)->shader_header_address ==
        astraea::memory::GuestAddress{
            0x00100000ULL});
    REQUIRE(
        registry.entry_at(1U)->shader_header_address ==
        astraea::memory::GuestAddress{
            0x00110000ULL});

    REQUIRE(
        registry.rollback_last_registration(
            second_index.value()));
    REQUIRE(registry.size() == 1U);
    REQUIRE(registry.entry_at(0U) != nullptr);
    REQUIRE(registry.entry_at(1U) == nullptr);

    REQUIRE(
        registry.rollback_last_registration(
            first_index.value()));
    REQUIRE(registry.size() == 0U);
    REQUIRE_FALSE(
        registry.rollback_last_registration(0U));
}


TEST_CASE(
    "stage-qualified shader lookup isolates Pixel and Geometry at the same code address",
    "[execution][agc][shader-registry][stage-lookup]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto pixel =
        require_created(
            0x00100000ULL,
            0x00200000ULL,
            0x00100000ULL);
    auto geometry =
        require_geometry_created(
            0x00110000ULL,
            0x00200000ULL,
            0x00110000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(pixel))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(geometry))
            .has_value());

    const auto pixel_lookup =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00200000ULL},
            astraea::graphics::AgcShaderStage::pixel);
    const auto geometry_lookup =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00200000ULL},
            astraea::graphics::AgcShaderStage::geometry);

    REQUIRE(pixel_lookup.has_value());
    REQUIRE(geometry_lookup.has_value());
    REQUIRE(
        pixel_lookup.value().get().shader_handle ==
        astraea::memory::GuestAddress{
            0x00100000ULL});
    REQUIRE(
        geometry_lookup.value().get().shader_handle ==
        astraea::memory::GuestAddress{
            0x00110000ULL});
}

TEST_CASE(
    "stage-qualified shader lookup reports only same-stage duplicates",
    "[execution][agc][shader-registry][stage-lookup][duplicate]") {
    astraea::execution::CreatedAgcShaderRegistry registry;

    auto first =
        require_geometry_created(
            0x00110000ULL,
            0x00300000ULL,
            0x00110000ULL);
    auto second =
        require_geometry_created(
            0x00120000ULL,
            0x00300000ULL,
            0x00120000ULL);
    auto pixel =
        require_created(
            0x00130000ULL,
            0x00300000ULL,
            0x00130000ULL);

    REQUIRE(
        registry.register_shader(
            std::move(first))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(second))
            .has_value());
    REQUIRE(
        registry.register_shader(
            std::move(pixel))
            .has_value());

    const auto geometry_lookup =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00300000ULL},
            astraea::graphics::AgcShaderStage::geometry);
    REQUIRE_FALSE(geometry_lookup.has_value());
    REQUIRE(
        geometry_lookup.error().code ==
        astraea::execution::
            CreatedAgcShaderStageLookupErrorCode::
                ambiguous);
    REQUIRE(
        geometry_lookup.error().program_address ==
        astraea::graphics::GpuVirtualAddress{
            .value = 0x00300000ULL});
    REQUIRE(
        geometry_lookup.error().stage ==
        astraea::graphics::AgcShaderStage::geometry);
    REQUIRE(
        geometry_lookup.error().match_count == 2U);

    const auto pixel_lookup =
        registry.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x00300000ULL},
            astraea::graphics::AgcShaderStage::pixel);
    REQUIRE(pixel_lookup.has_value());
}

TEST_CASE(
    "stage-qualified shader lookup preserves requested stage on missing result",
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
            CreatedAgcShaderStageLookupErrorCode::
                not_found);
    REQUIRE(
        result.error().program_address ==
        astraea::graphics::GpuVirtualAddress{
            .value = 0x00900000ULL});
    REQUIRE(
        result.error().stage ==
        astraea::graphics::AgcShaderStage::geometry);
    REQUIRE(result.error().match_count == 0U);
}
