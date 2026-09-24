#include <astraea/execution/sce_agc_first_raster_submission.hpp>
#include <astraea/execution/sce_agc_shader_registry.hpp>
#include <astraea/graphics/gpu_image.hpp>
#include <astraea/graphics/shader_export_probe_compiler.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/vulkan_raster_probe_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint64_t kGeometryProgram =
    0x0000123456000000ULL;
constexpr std::uint64_t kPixelProgram =
    0x0000234567000000ULL;
constexpr std::uint64_t kColorTargetBase =
    0x0000000000001200ULL;
constexpr std::uint32_t kExpBase = 0xf8000000U;
constexpr std::uint32_t kEndPgm = 0xbf810000U;

constexpr std::uint32_t make_type3_header(
    std::uint8_t opcode,
    std::uint16_t encoded_count) {
    return
        (static_cast<std::uint32_t>(
             astraea::graphics::kPm4Type3PacketType)
         << 30U) |
        ((static_cast<std::uint32_t>(encoded_count) &
          0x3fffU)
         << 16U) |
        (static_cast<std::uint32_t>(opcode) << 8U);
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

void append_word(
    std::vector<std::byte>& bytes,
    std::uint32_t word) {
    for (std::size_t index = 0U;
         index < 4U;
         ++index) {
        bytes.push_back(
            std::byte{
                static_cast<unsigned char>(
                    (word >> (index * 8U)) &
                    0xffU)});
    }
}

class SubmissionBuilder {
public:
    void add(
        std::initializer_list<std::uint32_t> packet) {
        words_.insert(
            words_.end(),
            packet.begin(),
            packet.end());
    }

    [[nodiscard]] astraea::execution::SceAgcDcbSubmission
    build() const {
        astraea::execution::SceAgcDcbSubmission submission{
            .submit_description_address =
                astraea::memory::GuestAddress{
                    0x00100000ULL},
            .command_words_address =
                astraea::memory::GuestAddress{
                    0x00200000ULL},
            .word_count =
                static_cast<std::uint32_t>(
                    words_.size()),
            .flag = 0U,
            .raw_submit_description = {},
            .opaque_padding = {},
            .command_buffer_bytes = {},
        };

        submission.command_buffer_bytes.reserve(
            words_.size() * 4U);
        for (const auto word : words_) {
            append_word(
                submission.command_buffer_bytes,
                word);
        }
        return submission;
    }

private:
    std::vector<std::uint32_t> words_;
};

[[nodiscard]] astraea::execution::SceAgcDcbSubmission
first_raster_submission() {
    SubmissionBuilder builder;

    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetShRegOpcode,
            2U),
        astraea::graphics::
            kGeometryEsProgramLoRegisterOffset,
        pgm_lo(kGeometryProgram),
        pgm_hi(kGeometryProgram),
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetShRegOpcode,
            2U),
        astraea::graphics::
            kPixelProgramLoRegisterOffset,
        pgm_lo(kPixelProgram),
        pgm_hi(kPixelProgram),
    });

    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetContextRegOpcode,
            1U),
        astraea::graphics::
            kColorTargetMaskContextOffset,
        0x0fU,
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetContextRegOpcode,
            1U),
        astraea::graphics::
            kColorTarget0BaseContextOffset,
        static_cast<std::uint32_t>(
            kColorTargetBase >> 8U),
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetContextRegOpcode,
            1U),
        astraea::graphics::
            kColorTarget0InfoContextOffset,
        astraea::graphics::
            kGfx10ColorFormatR8G8B8A8 << 2U,
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetContextRegOpcode,
            1U),
        astraea::graphics::
            kColorTarget0BaseExtContextOffset,
        static_cast<std::uint32_t>(
            kColorTargetBase >> 40U),
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetContextRegOpcode,
            1U),
        astraea::graphics::
            kColorTarget0Attrib2ContextOffset,
        (3U << 14U) | 3U,
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetContextRegOpcode,
            1U),
        astraea::graphics::
            kColorTarget0Attrib3ContextOffset,
        static_cast<std::uint32_t>(
            astraea::graphics::
                kGfx10ResourceType2d)
            << 24U,
    });

    builder.add({
        make_type3_header(
            astraea::graphics::kPm4SetUconfigRegOpcode,
            1U),
        astraea::graphics::
            kFirstRasterPrimitiveTypeUconfigOffset,
        astraea::graphics::
            kFirstRasterPrimitiveTypeRaw,
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4NumInstancesOpcode,
            0U),
        astraea::graphics::
            kFirstRasterInstanceCount,
    });
    builder.add({
        make_type3_header(
            astraea::graphics::kPm4DrawIndexAutoOpcode,
            1U),
        astraea::graphics::
            kFirstRasterIndexCount,
        astraea::graphics::
            kFirstRasterAutoIndexInitiatorRaw,
    });

    return builder.build();
}

constexpr std::uint32_t make_exp_word0(
    std::uint8_t target) {
    return kExpBase |
           0x0fU |
           ((static_cast<std::uint32_t>(target) & 0x3fU) << 4U) |
           (1U << 11U);
}

constexpr std::uint32_t make_exp_sources() {
    return
        0U |
        (1U << 8U) |
        (2U << 16U) |
        (3U << 24U);
}

[[nodiscard]] astraea::graphics::ShaderIrProgram
owned_export_program(std::uint8_t target) {
    const std::array<std::uint32_t, 3> words{
        make_exp_word0(target),
        make_exp_sources(),
        kEndPgm,
    };
    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    return program.value();
}

[[nodiscard]] astraea::execution::CreatedAgcShader
created_shader(
    astraea::graphics::AgcShaderStage stage,
    std::uint64_t code_address,
    astraea::graphics::ShaderIrProgram shader_ir) {
    return astraea::execution::CreatedAgcShader{
        .code_address =
            astraea::graphics::GpuVirtualAddress{
                .value = code_address,
            },
        .stage = stage,
        .preparation_profile =
            stage ==
                    astraea::graphics::
                        AgcShaderStage::geometry
                ? astraea::execution::
                      SceAgcShaderPreparationProfile::
                          v18_geometry_es_public_shape
                : astraea::execution::
                      SceAgcShaderPreparationProfile::
                          v18_pixel_public_shape,
        .shader_handle =
            astraea::memory::GuestAddress{
                code_address + 0x100000ULL},
        .shader_header_address =
            astraea::memory::GuestAddress{
                code_address + 0x200000ULL},
        .shader_text_address =
            astraea::memory::GuestAddress{
                code_address},
        .shader = {},
        .shader_ir = std::move(shader_ir),
    };
}

[[nodiscard]] bool live_vulkan_required() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0U;
    if (_dupenv_s(
            &value,
            &value_size,
            "ASTRAEA_REQUIRE_VULKAN_PROBE") != 0 ||
        value == nullptr) {
        return false;
    }

    const bool required =
        std::string_view{value} == "1";
    std::free(value);
    return required;
#else
    const auto* value =
        std::getenv("ASTRAEA_REQUIRE_VULKAN_PROBE");
    return value != nullptr &&
           std::string_view{value} == "1";
#endif
}

}  // namespace

TEST_CASE(
    "submitted PM4 selects registered guest shaders and drives exact Vulkan pixels",
    "[execution][graphics][v3][submitted-raster][vulkan][live]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan raster proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    const auto submission_plan =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                first_raster_submission());
    REQUIRE(submission_plan.has_value());

    astraea::execution::CreatedAgcShaderRegistry
        shaders;
    REQUIRE(
        shaders.register_shader(
                   created_shader(
                       astraea::graphics::
                           AgcShaderStage::geometry,
                       kGeometryProgram,
                       owned_export_program(0x0cU)))
            .has_value());
    REQUIRE(
        shaders.register_shader(
                   created_shader(
                       astraea::graphics::
                           AgcShaderStage::pixel,
                       kPixelProgram,
                       owned_export_program(0x00U)))
            .has_value());

    const auto geometry =
        shaders.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value =
                    submission_plan->
                        geometry_program_address.value,
            },
            astraea::graphics::
                AgcShaderStage::geometry);
    const auto pixel =
        shaders.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value =
                    submission_plan->
                        pixel_program_address.value,
            },
            astraea::graphics::
                AgcShaderStage::pixel);
    REQUIRE(geometry.has_value());
    REQUIRE(pixel.has_value());

    const auto vertex_ir =
        astraea::graphics::
            compile_shader_export_probe(
                geometry.value().get().shader_ir,
                astraea::graphics::
                    make_fullscreen_position_probe_launch_abi());
    const auto fragment_ir =
        astraea::graphics::
            compile_shader_export_probe(
                pixel.value().get().shader_ir,
                astraea::graphics::
                    make_magenta_fragment_probe_launch_abi());
    REQUIRE(vertex_ir.has_value());
    REQUIRE(fragment_ir.has_value());

    const auto vertex =
        astraea::graphics::
            lower_shader_export_probe_to_spirv(
                vertex_ir.value());
    const auto fragment =
        astraea::graphics::
            lower_shader_export_probe_to_spirv(
                fragment_ir.value());
    REQUIRE(vertex.has_value());
    REQUIRE(fragment.has_value());

    astraea::graphics::GuestGpuAllocationAddressSpace
        allocations;
    const auto allocation =
        allocations.register_allocation(
            astraea::graphics::GpuVirtualAddress{
                .value = 0x1000U},
            0x2000U);
    REQUIRE(allocation.has_value());

    astraea::graphics::GuestGpuImageRegistry images;
    const auto image_id =
        images.register_view(
            submission_plan->color_target_image,
            allocations);
    REQUIRE(image_id.has_value());

    const auto* image =
        images.entry_at(image_id.value());
    REQUIRE(image != nullptr);
    REQUIRE(
        image->allocation_id ==
        allocation.value());

    const auto executed =
        astraea::graphics::
            execute_vulkan_fullscreen_triangle_spirv(
                *image,
                vertex->words,
                fragment->words);

    REQUIRE(executed.has_value());
    REQUIRE(
        executed->image_id ==
        image_id.value());
    REQUIRE(executed->vertex_count == 3U);
    REQUIRE(executed->logical_pixels.size() == 64U);

    for (std::size_t pixel_index = 0U;
         pixel_index < 16U;
         ++pixel_index) {
        const auto base = pixel_index * 4U;
        REQUIRE(
            executed->logical_pixels[base + 0U] ==
            std::byte{0xff});
        REQUIRE(
            executed->logical_pixels[base + 1U] ==
            std::byte{0x00});
        REQUIRE(
            executed->logical_pixels[base + 2U] ==
            std::byte{0xff});
        REQUIRE(
            executed->logical_pixels[base + 3U] ==
            std::byte{0xff});
    }
}

TEST_CASE(
    "submitted raster shader selection remains stage-qualified and exact",
    "[execution][graphics][v3][submitted-raster][negative][shader]") {
    const auto submission_plan =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                first_raster_submission());
    REQUIRE(submission_plan.has_value());

    SECTION("missing Geometry stage") {
        astraea::execution::CreatedAgcShaderRegistry
            shaders;
        REQUIRE(
            shaders.register_shader(
                       created_shader(
                           astraea::graphics::
                               AgcShaderStage::pixel,
                           kGeometryProgram,
                           owned_export_program(0x00U)))
                .has_value());

        const auto result =
            shaders.lookup_unique_by_stage_and_code(
                astraea::graphics::GpuVirtualAddress{
                    .value =
                        submission_plan->
                            geometry_program_address.value,
                },
                astraea::graphics::
                    AgcShaderStage::geometry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                CreatedAgcShaderStageLookupErrorCode::
                    not_found);
    }

    SECTION("ambiguous Geometry stage") {
        astraea::execution::CreatedAgcShaderRegistry
            shaders;
        REQUIRE(
            shaders.register_shader(
                       created_shader(
                           astraea::graphics::
                               AgcShaderStage::geometry,
                           kGeometryProgram,
                           owned_export_program(0x0cU)))
                .has_value());
        REQUIRE(
            shaders.register_shader(
                       created_shader(
                           astraea::graphics::
                               AgcShaderStage::geometry,
                           kGeometryProgram,
                           owned_export_program(0x0cU)))
                .has_value());

        const auto result =
            shaders.lookup_unique_by_stage_and_code(
                astraea::graphics::GpuVirtualAddress{
                    .value =
                        submission_plan->
                            geometry_program_address.value,
                },
                astraea::graphics::
                    AgcShaderStage::geometry);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                CreatedAgcShaderStageLookupErrorCode::
                    ambiguous);
        REQUIRE(result.error().match_count == 2U);
    }
}

TEST_CASE(
    "selected shader IR must satisfy the bounded compiler profile",
    "[execution][graphics][v3][submitted-raster][negative][compiler]") {
    const auto submission_plan =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                first_raster_submission());
    REQUIRE(submission_plan.has_value());

    astraea::execution::CreatedAgcShaderRegistry
        shaders;
    // Deliberately put an MRT0 semantic program behind the submitted Geometry
    // program identity. The registry lookup still succeeds; the compiler must
    // reject the selected semantic program for the pre-raster role.
    REQUIRE(
        shaders.register_shader(
                   created_shader(
                       astraea::graphics::
                           AgcShaderStage::geometry,
                       kGeometryProgram,
                       owned_export_program(0x00U)))
            .has_value());

    const auto geometry =
        shaders.lookup_unique_by_stage_and_code(
            astraea::graphics::GpuVirtualAddress{
                .value =
                    submission_plan->
                        geometry_program_address.value,
            },
            astraea::graphics::
                AgcShaderStage::geometry);
    REQUIRE(geometry.has_value());

    const auto compiled =
        astraea::graphics::
            compile_shader_export_probe(
                geometry.value().get().shader_ir,
                astraea::graphics::
                    make_fullscreen_position_probe_launch_abi());

    REQUIRE_FALSE(compiled.has_value());
    REQUIRE(
        compiled.error().code ==
        astraea::graphics::
            ShaderExportProbeCompilerErrorCode::
                unsupported_export_target);
}

TEST_CASE(
    "submitted color target requires complete physical guest backing",
    "[execution][graphics][v3][submitted-raster][negative][backing]") {
    const auto submission_plan =
        astraea::execution::
            plan_sce_agc_first_raster_submission(
                first_raster_submission());
    REQUIRE(submission_plan.has_value());
    REQUIRE(
        submission_plan->
            color_target_image.logical_byte_count ==
        64U);
    REQUIRE(
        submission_plan->
            color_target_image.layout.surface_byte_count ==
        1024U);

    astraea::graphics::GuestGpuAllocationAddressSpace
        allocations;
    REQUIRE(
        allocations.register_allocation(
            submission_plan->
                color_target_image.base_address,
            submission_plan->
                color_target_image.logical_byte_count)
            .has_value());

    astraea::graphics::GuestGpuImageRegistry images;
    const auto image =
        images.register_view(
            submission_plan->color_target_image,
            allocations);

    REQUIRE_FALSE(image.has_value());
    REQUIRE(
        image.error().code ==
        astraea::graphics::
            GuestGpuImageRegistrationErrorCode::
                allocation_resolution_failure);
    REQUIRE(
        image.error().
            allocation_resolution_error.has_value());
    REQUIRE(
        image.error().
            allocation_resolution_error->code ==
        astraea::graphics::
            GuestGpuAllocationResolutionErrorCode::
                partially_mapped_range);
}
