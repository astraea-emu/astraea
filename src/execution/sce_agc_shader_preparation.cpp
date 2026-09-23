#include <astraea/execution/sce_agc_shader_preparation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {
namespace {

constexpr std::uint32_t kSupportedHeaderVersion = 0x18U;
constexpr std::uint16_t kPixelPgmLoRegister = 0x0008U;
constexpr std::uint16_t kPixelPgmHiRegister = 0x0009U;
constexpr std::uint16_t kGeometryEsPgmLoRegister = 0x00c8U;
constexpr std::uint16_t kGeometryEsPgmHiRegister = 0x00c9U;

constexpr std::uint64_t kUserDataField = 0x08U;
constexpr std::uint64_t kCodeField = 0x10U;
constexpr std::uint64_t kContextRegistersField = 0x18U;
constexpr std::uint64_t kShaderRegistersField = 0x20U;
constexpr std::uint64_t kSpecialsField = 0x28U;
constexpr std::uint64_t kInputSemanticsField = 0x30U;
constexpr std::uint64_t kOutputSemanticsField = 0x38U;

constexpr std::uint64_t kUserDataPointerFieldCount = 5U;
constexpr std::uint64_t kPointerWidth = 8U;
constexpr std::uint64_t kRegisterRecordWidth = 8U;
constexpr std::uint64_t kRegisterValueOffset = 4U;
constexpr std::uint64_t kOutputHandleWidth = 8U;

struct ResolvedSelfRelativePointer {
    std::uint64_t field_offset = 0;
    std::uint64_t target_offset = 0;
    astraea::memory::GuestAddress field_address;
    astraea::memory::GuestAddress target_address;
};

using ResolvedPointerResult =
    astraea::core::Result<
        std::optional<ResolvedSelfRelativePointer>,
        SceAgcShaderPreparationError>;

[[nodiscard]] SceAgcShaderPreparationError error(
    SceAgcShaderPreparationErrorCode code,
    std::optional<std::uint64_t> header_byte_offset =
        std::nullopt,
    std::optional<astraea::memory::GuestAddress>
        guest_address = std::nullopt,
    std::optional<SceAgcShaderPatchKind> patch_kind =
        std::nullopt) noexcept {
    return SceAgcShaderPreparationError{
        .code = code,
        .header_byte_offset = header_byte_offset,
        .guest_address = guest_address,
        .patch_kind = patch_kind,
    };
}

template <typename T>
[[nodiscard]] T read_little_endian(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t index = 0;
         index < sizeof(T);
         ++index) {
        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] bool checked_add_u64(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left >
        std::numeric_limits<std::uint64_t>::max() -
            right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] astraea::core::Result<
    astraea::memory::GuestAddress,
    SceAgcShaderPreparationError>
guest_address_at(
    astraea::memory::GuestAddress base,
    std::uint64_t offset,
    std::optional<std::uint64_t> header_byte_offset =
        std::nullopt) noexcept {
    auto result =
        astraea::memory::GuestAddress::checked_add(
            base,
            astraea::memory::GuestSize{offset});
    if (!result.has_value()) {
        return astraea::core::Result<
            astraea::memory::GuestAddress,
            SceAgcShaderPreparationError>::failure(
                error(
                    SceAgcShaderPreparationErrorCode::
                        guest_address_overflow,
                    header_byte_offset,
                    base));
    }
    return astraea::core::Result<
        astraea::memory::GuestAddress,
        SceAgcShaderPreparationError>::success(
            result.value());
}

[[nodiscard]] ResolvedPointerResult
resolve_self_relative_pointer(
    std::span<const std::byte> header,
    astraea::memory::GuestAddress header_address,
    std::uint64_t field_offset) noexcept {
    if (field_offset >
            static_cast<std::uint64_t>(
                header.size()) ||
        static_cast<std::uint64_t>(
            header.size()) -
                field_offset <
            kPointerWidth) {
        return ResolvedPointerResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    self_relative_pointer_out_of_bounds,
                field_offset));
    }

    const auto raw =
        read_little_endian<std::uint64_t>(
            header,
            static_cast<std::size_t>(
                field_offset));
    if (raw == 0) {
        return ResolvedPointerResult::success(
            std::nullopt);
    }

    std::uint64_t target_offset = 0;
    if (!checked_add_u64(
            field_offset,
            raw,
            target_offset)) {
        return ResolvedPointerResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    self_relative_pointer_overflow,
                field_offset));
    }
    if (target_offset >
        static_cast<std::uint64_t>(
            header.size())) {
        return ResolvedPointerResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    self_relative_pointer_out_of_bounds,
                field_offset));
    }

    auto field_address =
        guest_address_at(
            header_address,
            field_offset,
            field_offset);
    if (!field_address.has_value()) {
        return ResolvedPointerResult::failure(
            field_address.error());
    }
    auto target_address =
        guest_address_at(
            header_address,
            target_offset,
            field_offset);
    if (!target_address.has_value()) {
        return ResolvedPointerResult::failure(
            target_address.error());
    }

    return ResolvedPointerResult::success(
        ResolvedSelfRelativePointer{
            .field_offset = field_offset,
            .target_offset = target_offset,
            .field_address = field_address.value(),
            .target_address = target_address.value(),
        });
}

template <typename T>
[[nodiscard]] std::vector<std::byte>
encode_little_endian(T value) {
    static_assert(std::is_unsigned_v<T>);
    std::vector<std::byte> bytes(sizeof(T));
    for (std::size_t index = 0;
         index < sizeof(T);
         ++index) {
        bytes[index] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (index * 8U)) &
                0xffU);
    }
    return bytes;
}

[[nodiscard]] bool ranges_overlap(
    astraea::memory::GuestAddress left_address,
    std::uint64_t left_size,
    astraea::memory::GuestAddress right_address,
    std::uint64_t right_size) noexcept {
    if (left_size == 0 || right_size == 0) {
        return false;
    }

    std::uint64_t left_end = 0;
    std::uint64_t right_end = 0;
    if (!checked_add_u64(
            left_address.value(),
            left_size,
            left_end) ||
        !checked_add_u64(
            right_address.value(),
            right_size,
            right_end)) {
        return true;
    }

    return left_address.value() < right_end &&
           right_address.value() < left_end;
}

[[nodiscard]] std::optional<
    SceAgcShaderPreparationError>
append_patch(
    std::vector<SceAgcShaderPatch>& patches,
    SceAgcShaderPatch patch) {
    for (const auto& existing : patches) {
        if (ranges_overlap(
                existing.address,
                existing.bytes.size(),
                patch.address,
                patch.bytes.size())) {
            return error(
                SceAgcShaderPreparationErrorCode::
                    patch_overlap,
                std::nullopt,
                patch.address,
                patch.kind);
        }
    }

    patches.push_back(std::move(patch));
    return std::nullopt;
}

[[nodiscard]] std::optional<
    SceAgcShaderPreparationError>
append_pointer_patch(
    std::vector<SceAgcShaderPatch>& patches,
    SceAgcShaderPatchKind kind,
    const ResolvedSelfRelativePointer& pointer) {
    return append_patch(
        patches,
        SceAgcShaderPatch{
            .kind = kind,
            .address = pointer.field_address,
            .bytes =
                encode_little_endian<std::uint64_t>(
                    pointer.target_address.value()),
        });
}

[[nodiscard]] SceAgcShaderApplyError apply_error(
    SceAgcShaderApplyErrorCode code,
    std::size_t patch_index,
    std::size_t patch_count,
    std::size_t applied_count,
    GuestMemoryError detail) noexcept {
    return SceAgcShaderApplyError{
        .code = code,
        .patch_index = patch_index,
        .patch_count = patch_count,
        .applied_count = applied_count,
        .guest_memory_error = detail,
    };
}

}  // namespace

SceAgcShaderPreparationResult
plan_sce_agc_shader_preparation(
    const SceAgcCreateShaderPlan& create_shader) {
    using Stage = astraea::graphics::AgcShaderStage;

    const auto& shader = create_shader.shader;
    const auto& request = create_shader.request;
    const auto& header = shader.shader_header_bytes;

    if (request.output_pointer_address.value() == 0) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    null_output_pointer,
                std::nullopt,
                request.output_pointer_address));
    }

    if (shader.header_version !=
        kSupportedHeaderVersion) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    unsupported_header_version,
                4));
    }

    SceAgcShaderPreparationProfile profile{};
    std::uint16_t pgm_lo_register = 0;
    std::uint16_t pgm_hi_register = 0;
    SceAgcShaderPatchKind pgm_lo_patch_kind =
        SceAgcShaderPatchKind::pixel_pgm_lo_value;
    SceAgcShaderPatchKind pgm_hi_patch_kind =
        SceAgcShaderPatchKind::pixel_pgm_hi_value;
    SceAgcShaderPreparationErrorCode pgm_pair_error =
        SceAgcShaderPreparationErrorCode::
            unsupported_pixel_program_register_pair;

    if (shader.program_type.known ==
            std::optional<Stage>{Stage::pixel} &&
        shader.program_type.raw == 1U) {
        profile =
            SceAgcShaderPreparationProfile::
                v18_pixel_public_shape;
        pgm_lo_register = kPixelPgmLoRegister;
        pgm_hi_register = kPixelPgmHiRegister;
    } else if (
        shader.program_type.known ==
            std::optional<Stage>{Stage::geometry} &&
        shader.program_type.raw == 2U) {
        profile =
            SceAgcShaderPreparationProfile::
                v18_geometry_es_public_shape;
        pgm_lo_register =
            kGeometryEsPgmLoRegister;
        pgm_hi_register =
            kGeometryEsPgmHiRegister;
        pgm_lo_patch_kind =
            SceAgcShaderPatchKind::
                geometry_es_pgm_lo_value;
        pgm_hi_patch_kind =
            SceAgcShaderPatchKind::
                geometry_es_pgm_hi_value;
        pgm_pair_error =
            SceAgcShaderPreparationErrorCode::
                unsupported_geometry_program_register_pair;
    } else {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    unsupported_shader_stage,
                0x5a));
    }

    const auto code_address =
        request.shader_text_address.value();
    if ((code_address & 0xffU) != 0U) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    shader_code_address_misaligned,
                std::nullopt,
                request.shader_text_address));
    }
    if ((code_address >> 48U) != 0U) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    shader_code_address_unrepresentable,
                std::nullopt,
                request.shader_text_address));
    }

    if (!shader.context_register_list_header_offset
             .has_value() ||
        !shader.shader_register_list_header_offset
             .has_value()) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    missing_register_list_provenance));
    }

    if (shader.shader_registers.size() < 2 ||
        shader.shader_registers[0].register_offset !=
            pgm_lo_register ||
        shader.shader_registers[1].register_offset !=
            pgm_hi_register) {
        return SceAgcShaderPreparationResult::failure(
            error(
                pgm_pair_error,
                *shader.shader_register_list_header_offset));
    }

    const auto output_size =
        static_cast<std::uint64_t>(
            kOutputHandleWidth);
    if (ranges_overlap(
            request.output_pointer_address,
            output_size,
            request.shader_header_address,
            shader.declared_header_size) ||
        ranges_overlap(
            request.output_pointer_address,
            output_size,
            request.shader_text_address,
            shader.declared_shader_text_size)) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    output_aliases_shader_input,
                std::nullopt,
                request.output_pointer_address));
    }

    try {
        std::vector<SceAgcShaderPatch> patches;
        patches.reserve(16);

        auto user_data =
            resolve_self_relative_pointer(
                header,
                request.shader_header_address,
                kUserDataField);
        if (!user_data.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                user_data.error());
        }

        auto context =
            resolve_self_relative_pointer(
                header,
                request.shader_header_address,
                kContextRegistersField);
        if (!context.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                context.error());
        }
        auto shader_registers =
            resolve_self_relative_pointer(
                header,
                request.shader_header_address,
                kShaderRegistersField);
        if (!shader_registers.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                shader_registers.error());
        }

        if (!context->has_value() ||
            !shader_registers->has_value() ||
            context->value().target_offset !=
                *shader
                     .context_register_list_header_offset ||
            shader_registers->value().target_offset !=
                *shader
                     .shader_register_list_header_offset) {
            return SceAgcShaderPreparationResult::failure(
                error(
                    SceAgcShaderPreparationErrorCode::
                        canonical_register_list_mismatch));
        }

        auto specials =
            resolve_self_relative_pointer(
                header,
                request.shader_header_address,
                kSpecialsField);
        if (!specials.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                specials.error());
        }
        auto input_semantics =
            resolve_self_relative_pointer(
                header,
                request.shader_header_address,
                kInputSemanticsField);
        if (!input_semantics.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                input_semantics.error());
        }
        auto output_semantics =
            resolve_self_relative_pointer(
                header,
                request.shader_header_address,
                kOutputSemanticsField);
        if (!output_semantics.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                output_semantics.error());
        }

        if (user_data->has_value()) {
            if (user_data->value().target_offset >
                    static_cast<std::uint64_t>(
                        header.size()) ||
                static_cast<std::uint64_t>(
                    header.size()) -
                        user_data->value().target_offset <
                    kUserDataPointerFieldCount *
                        kPointerWidth) {
                return SceAgcShaderPreparationResult::failure(
                    error(
                        SceAgcShaderPreparationErrorCode::
                            user_data_too_small,
                        user_data->value().target_offset));
            }
        }

        if (user_data->has_value()) {
            if (auto failure =
                    append_pointer_patch(
                        patches,
                        SceAgcShaderPatchKind::
                            user_data_pointer,
                        user_data->value());
                failure.has_value()) {
                return SceAgcShaderPreparationResult::failure(
                    failure.value());
            }
        }

        auto code_field_address =
            guest_address_at(
                request.shader_header_address,
                kCodeField,
                kCodeField);
        if (!code_field_address.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                code_field_address.error());
        }
        if (auto failure =
                append_patch(
                    patches,
                    SceAgcShaderPatch{
                        .kind =
                            SceAgcShaderPatchKind::
                                code_pointer,
                        .address =
                            code_field_address.value(),
                        .bytes =
                            encode_little_endian<
                                std::uint64_t>(
                                code_address),
                    });
            failure.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                failure.value());
        }

        if (auto failure =
                append_pointer_patch(
                    patches,
                    SceAgcShaderPatchKind::
                        context_register_pointer,
                    context->value());
            failure.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                failure.value());
        }
        if (auto failure =
                append_pointer_patch(
                    patches,
                    SceAgcShaderPatchKind::
                        shader_register_pointer,
                    shader_registers->value());
            failure.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                failure.value());
        }

        if (specials->has_value()) {
            if (auto failure =
                    append_pointer_patch(
                        patches,
                        SceAgcShaderPatchKind::
                            specials_pointer,
                        specials->value());
                failure.has_value()) {
                return SceAgcShaderPreparationResult::failure(
                    failure.value());
            }
        }
        if (input_semantics->has_value()) {
            if (auto failure =
                    append_pointer_patch(
                        patches,
                        SceAgcShaderPatchKind::
                            input_semantics_pointer,
                        input_semantics->value());
                failure.has_value()) {
                return SceAgcShaderPreparationResult::failure(
                    failure.value());
            }
        }
        if (output_semantics->has_value()) {
            if (auto failure =
                    append_pointer_patch(
                        patches,
                        SceAgcShaderPatchKind::
                            output_semantics_pointer,
                        output_semantics->value());
                failure.has_value()) {
                return SceAgcShaderPreparationResult::failure(
                    failure.value());
            }
        }

        if (user_data->has_value()) {
            static constexpr std::array<
                SceAgcShaderPatchKind,
                kUserDataPointerFieldCount>
                kNestedKinds{
                    SceAgcShaderPatchKind::
                        user_data_direct_resource_pointer,
                    SceAgcShaderPatchKind::
                        user_data_sharp_resource_pointer_0,
                    SceAgcShaderPatchKind::
                        user_data_sharp_resource_pointer_1,
                    SceAgcShaderPatchKind::
                        user_data_sharp_resource_pointer_2,
                    SceAgcShaderPatchKind::
                        user_data_sharp_resource_pointer_3,
                };

            for (std::size_t index = 0;
                 index < kNestedKinds.size();
                 ++index) {
                const auto nested_field_offset =
                    user_data->value().target_offset +
                    static_cast<std::uint64_t>(
                        index) *
                        kPointerWidth;
                auto nested =
                    resolve_self_relative_pointer(
                        header,
                        request.shader_header_address,
                        nested_field_offset);
                if (!nested.has_value()) {
                    return SceAgcShaderPreparationResult::
                        failure(
                            nested.error());
                }
                if (!nested->has_value()) {
                    continue;
                }

                if (auto failure =
                        append_pointer_patch(
                            patches,
                            kNestedKinds[index],
                            nested->value());
                    failure.has_value()) {
                    return SceAgcShaderPreparationResult::
                        failure(
                            failure.value());
                }
            }
        }

        const auto shader_list_offset =
            *shader.shader_register_list_header_offset;
        std::uint64_t pgm_lo_value_offset = 0;
        std::uint64_t second_record_offset = 0;
        std::uint64_t pgm_hi_value_offset = 0;
        if (!checked_add_u64(
                shader_list_offset,
                kRegisterValueOffset,
                pgm_lo_value_offset) ||
            !checked_add_u64(
                shader_list_offset,
                kRegisterRecordWidth,
                second_record_offset) ||
            !checked_add_u64(
                second_record_offset,
                kRegisterValueOffset,
                pgm_hi_value_offset)) {
            return SceAgcShaderPreparationResult::failure(
                error(
                    SceAgcShaderPreparationErrorCode::
                        guest_address_overflow,
                    shader_list_offset));
        }

        auto pgm_lo_address =
            guest_address_at(
                request.shader_header_address,
                pgm_lo_value_offset,
                pgm_lo_value_offset);
        if (!pgm_lo_address.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                pgm_lo_address.error());
        }
        auto pgm_hi_address =
            guest_address_at(
                request.shader_header_address,
                pgm_hi_value_offset,
                pgm_hi_value_offset);
        if (!pgm_hi_address.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                pgm_hi_address.error());
        }

        if (auto failure =
                append_patch(
                    patches,
                    SceAgcShaderPatch{
                        .kind = pgm_lo_patch_kind,
                        .address =
                            pgm_lo_address.value(),
                        .bytes =
                            encode_little_endian<
                                std::uint32_t>(
                                static_cast<
                                    std::uint32_t>(
                                    (code_address >>
                                     8U) &
                                    0xffffffffU)),
                    });
            failure.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                failure.value());
        }
        if (auto failure =
                append_patch(
                    patches,
                    SceAgcShaderPatch{
                        .kind = pgm_hi_patch_kind,
                        .address =
                            pgm_hi_address.value(),
                        .bytes =
                            encode_little_endian<
                                std::uint32_t>(
                                static_cast<
                                    std::uint32_t>(
                                    (code_address >>
                                     40U) &
                                    0xffU)),
                    });
            failure.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                failure.value());
        }

        // Publish the shader handle last so no caller can observe a prepared
        // object before every header mutation has been applied.
        if (auto failure =
                append_patch(
                    patches,
                    SceAgcShaderPatch{
                        .kind =
                            SceAgcShaderPatchKind::
                                output_handle,
                        .address =
                            request.output_pointer_address,
                        .bytes =
                            encode_little_endian<
                                std::uint64_t>(
                                request
                                    .shader_header_address
                                    .value()),
                    });
            failure.has_value()) {
            return SceAgcShaderPreparationResult::failure(
                failure.value());
        }

        return SceAgcShaderPreparationResult::success(
            SceAgcShaderPreparationPlan{
                .create_shader = create_shader,
                .profile = profile,
                .shader_handle =
                    request.shader_header_address,
                .patches = std::move(patches),
            });
    } catch (const std::bad_alloc&) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return SceAgcShaderPreparationResult::failure(
            error(
                SceAgcShaderPreparationErrorCode::
                    host_allocation_failure));
    }
}

SceAgcShaderApplyResult
apply_sce_agc_shader_preparation(
    const SceAgcShaderPreparationPlan& plan,
    const GuestMemoryAccess& guest_memory) {
    for (std::size_t index = 0;
         index < plan.patches.size();
         ++index) {
        const auto& patch = plan.patches[index];
        auto preflight =
            guest_memory.preflight_write(
                patch.address,
                patch.bytes.size());
        if (!preflight.has_value()) {
            return SceAgcShaderApplyResult::failure(
                apply_error(
                    SceAgcShaderApplyErrorCode::
                        guest_memory_preflight_failure,
                    index,
                    plan.patches.size(),
                    0,
                    preflight.error()));
        }
    }

    for (std::size_t index = 0;
         index < plan.patches.size();
         ++index) {
        const auto& patch = plan.patches[index];
        auto written =
            guest_memory.write(
                patch.address,
                patch.bytes);
        if (!written.has_value()) {
            return SceAgcShaderApplyResult::failure(
                apply_error(
                    SceAgcShaderApplyErrorCode::
                        guest_memory_write_failure,
                    index,
                    plan.patches.size(),
                    index,
                    written.error()));
        }
    }

    return SceAgcShaderApplyResult::success(
        SceAgcShaderApplyReport{
            .shader_handle = plan.shader_handle,
            .applied_patch_count =
                plan.patches.size(),
        });
}

}  // namespace astraea::execution
