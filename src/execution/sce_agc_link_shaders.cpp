#include <astraea/execution/sce_agc_link_shaders.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace astraea::execution {
namespace {

[[nodiscard]] SceAgcLinkShadersPlanError error(
    SceAgcLinkShadersPlanErrorCode code,
    std::optional<HleFunctionId> function_id = std::nullopt,
    std::optional<astraea::memory::GuestAddress>
        guest_address = std::nullopt,
    std::optional<SceAgcLinkShadersShaderSlot>
        shader_slot = std::nullopt,
    std::optional<CreatedAgcShaderHandleLookupError>
        handle_lookup_error = std::nullopt,
    std::optional<astraea::graphics::AgcShaderStage>
        actual_stage = std::nullopt,
    std::optional<SceAgcShaderPreparationProfile>
        actual_preparation_profile = std::nullopt,
    std::uint32_t primitive_type = 0) noexcept {
    return SceAgcLinkShadersPlanError{
        .code = code,
        .function_id = function_id,
        .guest_address = guest_address,
        .shader_slot = shader_slot,
        .handle_lookup_error = handle_lookup_error,
        .actual_stage = actual_stage,
        .actual_preparation_profile =
            actual_preparation_profile,
        .primitive_type = primitive_type,
    };
}

[[nodiscard]] SceAgcLinkedShaderIdentity identity(
    const CreatedAgcShader& shader) noexcept {
    return SceAgcLinkedShaderIdentity{
        .shader_handle = shader.shader_handle,
        .code_address = shader.code_address,
        .stage = shader.stage,
        .preparation_profile =
            shader.preparation_profile,
    };
}

}  // namespace

SceAgcLinkShadersPlanResult
plan_sce_agc_link_shaders(
    const HleCall& call,
    const CreatedAgcShaderRegistry& shader_registry) noexcept {
    if (call.function_id != kSceAgcLinkShadersHleId) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    unexpected_function,
                call.function_id));
    }

    const auto context_output =
        astraea::memory::GuestAddress{
            call.arguments[0]};
    if (context_output.value() == 0U) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    null_context_output,
                call.function_id,
                context_output));
    }

    const auto user_config_output =
        astraea::memory::GuestAddress{
            call.arguments[1]};
    if (user_config_output.value() == 0U) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    null_user_config_output,
                call.function_id,
                user_config_output));
    }

    const auto hull_shader =
        astraea::memory::GuestAddress{
            call.arguments[2]};
    if (hull_shader.value() != 0U) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    unsupported_hull_shader,
                call.function_id,
                hull_shader));
    }

    const auto primitive_type =
        static_cast<std::uint32_t>(
            call.arguments[5] & 0xffffffffULL);
    if (call.arguments[5] !=
            static_cast<std::uint64_t>(
                primitive_type) ||
        primitive_type !=
            kSceAgcLinkShadersTriangleListPrimitiveType) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    unsupported_primitive_type,
                call.function_id,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                std::nullopt,
                primitive_type));
    }

    auto context_range =
        astraea::memory::GuestRange::create(
            context_output,
            astraea::memory::GuestSize{
                kSceAgcLinkShadersContextOutputSize});
    if (!context_range.has_value()) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    output_range_overflow,
                call.function_id,
                context_output));
    }

    auto user_config_range =
        astraea::memory::GuestRange::create(
            user_config_output,
            astraea::memory::GuestSize{
                kSceAgcLinkShadersUserConfigOutputSize});
    if (!user_config_range.has_value()) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    output_range_overflow,
                call.function_id,
                user_config_output));
    }

    if (context_range->overlaps(
            user_config_range.value())) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    output_ranges_overlap,
                call.function_id,
                user_config_output));
    }

    const auto pre_raster_handle =
        astraea::memory::GuestAddress{
            call.arguments[3]};
    const auto pixel_handle =
        astraea::memory::GuestAddress{
            call.arguments[4]};

    if (pre_raster_handle.value() != 0U &&
        pre_raster_handle == pixel_handle) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    same_shader_handle,
                call.function_id,
                pre_raster_handle));
    }

    const auto pre_raster_lookup =
        shader_registry.lookup_unique_by_handle(
            pre_raster_handle);
    if (!pre_raster_lookup.has_value()) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    pre_raster_handle_lookup_failure,
                call.function_id,
                pre_raster_handle,
                SceAgcLinkShadersShaderSlot::
                    pre_raster,
                pre_raster_lookup.error()));
    }

    const auto& pre_raster =
        pre_raster_lookup.value().get();
    if (pre_raster.stage !=
            astraea::graphics::AgcShaderStage::
                geometry ||
        pre_raster.preparation_profile !=
            SceAgcShaderPreparationProfile::
                v18_geometry_es_public_shape) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    unsupported_pre_raster_shader,
                call.function_id,
                pre_raster_handle,
                SceAgcLinkShadersShaderSlot::
                    pre_raster,
                std::nullopt,
                pre_raster.stage,
                pre_raster.preparation_profile));
    }

    const auto pixel_lookup =
        shader_registry.lookup_unique_by_handle(
            pixel_handle);
    if (!pixel_lookup.has_value()) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    pixel_handle_lookup_failure,
                call.function_id,
                pixel_handle,
                SceAgcLinkShadersShaderSlot::pixel,
                pixel_lookup.error()));
    }

    const auto& pixel =
        pixel_lookup.value().get();
    if (pixel.stage !=
            astraea::graphics::AgcShaderStage::pixel ||
        pixel.preparation_profile !=
            SceAgcShaderPreparationProfile::
                v18_pixel_public_shape) {
        return SceAgcLinkShadersPlanResult::failure(
            error(
                SceAgcLinkShadersPlanErrorCode::
                    unsupported_pixel_shader,
                call.function_id,
                pixel_handle,
                SceAgcLinkShadersShaderSlot::pixel,
                std::nullopt,
                pixel.stage,
                pixel.preparation_profile));
    }

    return SceAgcLinkShadersPlanResult::success(
        SceAgcLinkShadersPlan{
            .context_output_address =
                context_output,
            .context_output_range =
                context_range.value(),
            .user_config_output_address =
                user_config_output,
            .user_config_output_range =
                user_config_range.value(),
            .hull_shader_handle =
                hull_shader,
            .pre_raster_shader =
                identity(pre_raster),
            .pixel_shader = identity(pixel),
            .primitive_type = primitive_type,
        });
}

namespace {

[[nodiscard]] SceAgcLinkShadersMeasuredOutputPlanError
measured_plan_error(
    astraea::memory::GuestAddress base_address,
    std::uint64_t byte_offset) noexcept {
    return SceAgcLinkShadersMeasuredOutputPlanError{
        .code =
            SceAgcLinkShadersMeasuredOutputPlanErrorCode::
                measured_output_address_overflow,
        .base_address = base_address,
        .byte_offset = byte_offset,
    };
}

void serialize_record(
    const SceAgcLinkShadersRegisterRecord& record,
    std::span<std::byte, kSceAgcLinkShadersRegisterRecordSize>
        bytes) noexcept {
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[index] =
            std::byte{
                static_cast<unsigned char>(
                    (record.offset >> (index * 8U)) &
                    0xffU)};
        bytes[4U + index] =
            std::byte{
                static_cast<unsigned char>(
                    (record.value >> (index * 8U)) &
                    0xffU)};
    }
}

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N * kSceAgcLinkShadersRegisterRecordSize>
serialize_records(
    const std::array<SceAgcLinkShadersRegisterRecord, N>& records)
    noexcept {
    std::array<
        std::byte,
        N * kSceAgcLinkShadersRegisterRecordSize>
        bytes{};
    for (std::size_t index = 0; index < N; ++index) {
        std::span<
            std::byte,
            kSceAgcLinkShadersRegisterRecordSize>
            destination{
                bytes.data() +
                    index *
                        kSceAgcLinkShadersRegisterRecordSize,
                kSceAgcLinkShadersRegisterRecordSize};
        serialize_record(
            records[index],
            destination);
    }
    return bytes;
}

[[nodiscard]] SceAgcLinkShadersMeasuredApplyError
apply_error(
    SceAgcLinkShadersMeasuredApplyErrorCode code,
    SceAgcLinkShadersMeasuredPatchKind patch_kind,
    std::size_t patch_index,
    std::size_t applied_patch_count,
    std::size_t applied_record_count,
    GuestMemoryError detail) noexcept {
    return SceAgcLinkShadersMeasuredApplyError{
        .code = code,
        .patch_kind = patch_kind,
        .patch_index = patch_index,
        .patch_count = 2U,
        .applied_patch_count =
            applied_patch_count,
        .applied_record_count =
            applied_record_count,
        .guest_memory_error = detail,
    };
}

}  // namespace

SceAgcLinkShadersMeasuredOutputPlanResult
materialize_sce_agc_link_shaders_measured_output(
    const SceAgcLinkShadersPlan& plan) noexcept {
    const auto routing_address =
        astraea::memory::GuestAddress::checked_add(
            plan.context_output_address,
            astraea::memory::GuestSize{
                kSceAgcLinkShadersMeasuredRoutingByteOffset});
    if (!routing_address.has_value()) {
        return SceAgcLinkShadersMeasuredOutputPlanResult::failure(
            measured_plan_error(
                plan.context_output_address,
                kSceAgcLinkShadersMeasuredRoutingByteOffset));
    }

    const auto interpolant_range =
        astraea::memory::GuestRange::create(
            plan.context_output_address,
            astraea::memory::GuestSize{
                kSceAgcLinkShadersMeasuredInterpolantByteCount});
    if (!interpolant_range.has_value()) {
        return SceAgcLinkShadersMeasuredOutputPlanResult::failure(
            measured_plan_error(
                plan.context_output_address,
                0U));
    }

    const auto routing_range =
        astraea::memory::GuestRange::create(
            routing_address.value(),
            astraea::memory::GuestSize{
                kSceAgcLinkShadersRegisterRecordSize});
    if (!routing_range.has_value()) {
        return SceAgcLinkShadersMeasuredOutputPlanResult::failure(
            measured_plan_error(
                plan.context_output_address,
                kSceAgcLinkShadersMeasuredRoutingByteOffset));
    }

    std::array<
        SceAgcLinkShadersRegisterRecord,
        kSceAgcLinkShadersMeasuredInterpolantRecordCount>
        interpolants{};
    for (std::size_t index = 0;
         index <
         kSceAgcLinkShadersMeasuredInterpolantRecordCount;
         ++index) {
        interpolants[index] =
            SceAgcLinkShadersRegisterRecord{
                .offset =
                    0x191U +
                    static_cast<std::uint32_t>(index),
                .value =
                    static_cast<std::uint32_t>(index),
            };
    }

    return SceAgcLinkShadersMeasuredOutputPlanResult::success(
        SceAgcLinkShadersMeasuredOutputPlan{
            .completeness =
                SceAgcLinkShadersOutputCompleteness::
                    measured_partial,
            .interpolant_output_address =
                plan.context_output_address,
            .interpolant_output_range =
                interpolant_range.value(),
            .routing_output_address =
                routing_address.value(),
            .routing_output_range =
                routing_range.value(),
            .preserved_user_config_address =
                plan.user_config_output_address,
            .interpolant_records = interpolants,
            .routing_record =
                SceAgcLinkShadersRegisterRecord{
                    .offset = 0x29bU,
                    .value = 2U,
                },
        });
}

SceAgcLinkShadersMeasuredApplyResult
apply_sce_agc_link_shaders_measured_output(
    const SceAgcLinkShadersMeasuredOutputPlan& plan,
    const GuestMemoryAccess& guest_memory) noexcept {
    auto interpolant_preflight =
        guest_memory.preflight_write(
            plan.interpolant_output_address,
            kSceAgcLinkShadersMeasuredInterpolantByteCount);
    if (!interpolant_preflight.has_value()) {
        return SceAgcLinkShadersMeasuredApplyResult::failure(
            apply_error(
                SceAgcLinkShadersMeasuredApplyErrorCode::
                    guest_memory_preflight_failure,
                SceAgcLinkShadersMeasuredPatchKind::
                    interpolant_table,
                0U,
                0U,
                0U,
                interpolant_preflight.error()));
    }

    auto routing_preflight =
        guest_memory.preflight_write(
            plan.routing_output_address,
            kSceAgcLinkShadersRegisterRecordSize);
    if (!routing_preflight.has_value()) {
        return SceAgcLinkShadersMeasuredApplyResult::failure(
            apply_error(
                SceAgcLinkShadersMeasuredApplyErrorCode::
                    guest_memory_preflight_failure,
                SceAgcLinkShadersMeasuredPatchKind::
                    gs_out_primitive_type,
                1U,
                0U,
                0U,
                routing_preflight.error()));
    }

    const auto interpolant_bytes =
        serialize_records(plan.interpolant_records);
    auto interpolant_write =
        guest_memory.write(
            plan.interpolant_output_address,
            interpolant_bytes);
    if (!interpolant_write.has_value()) {
        return SceAgcLinkShadersMeasuredApplyResult::failure(
            apply_error(
                SceAgcLinkShadersMeasuredApplyErrorCode::
                    guest_memory_write_failure,
                SceAgcLinkShadersMeasuredPatchKind::
                    interpolant_table,
                0U,
                0U,
                0U,
                interpolant_write.error()));
    }

    std::array<
        std::byte,
        kSceAgcLinkShadersRegisterRecordSize>
        routing_bytes{};
    serialize_record(
        plan.routing_record,
        std::span<
            std::byte,
            kSceAgcLinkShadersRegisterRecordSize>{
                routing_bytes});
    auto routing_write =
        guest_memory.write(
            plan.routing_output_address,
            routing_bytes);
    if (!routing_write.has_value()) {
        return SceAgcLinkShadersMeasuredApplyResult::failure(
            apply_error(
                SceAgcLinkShadersMeasuredApplyErrorCode::
                    guest_memory_write_failure,
                SceAgcLinkShadersMeasuredPatchKind::
                    gs_out_primitive_type,
                1U,
                1U,
                kSceAgcLinkShadersMeasuredInterpolantRecordCount,
                routing_write.error()));
    }

    return SceAgcLinkShadersMeasuredApplyResult::success(
        SceAgcLinkShadersMeasuredApplyReport{
            .completeness =
                SceAgcLinkShadersOutputCompleteness::
                    measured_partial,
            .applied_patch_count = 2U,
            .applied_context_record_count =
                kSceAgcLinkShadersMeasuredInterpolantRecordCount +
                1U,
            .applied_user_config_record_count = 0U,
        });
}

}  // namespace astraea::execution
