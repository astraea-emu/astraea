#include <astraea/graphics/shader_scalar_execution.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderScalarExecutionError error(
    ShaderScalarExecutionErrorCode code,
    ShaderScalarExecutionOperandRole role =
        ShaderScalarExecutionOperandRole::none,
    std::uint16_t sgpr_index = 0) noexcept {
    return ShaderScalarExecutionError{
        .code = code,
        .role = role,
        .sgpr_index = sgpr_index,
    };
}

[[nodiscard]] bool valid_sgpr(
    std::uint8_t index) noexcept {
    return static_cast<std::size_t>(index) <
           kShaderScalarGprCount;
}

[[nodiscard]] bool valid_sgpr_pair(
    std::uint8_t first_index) noexcept {
    const auto first =
        static_cast<std::size_t>(first_index);
    return (first % 2U) == 0U &&
           first + 1U < kShaderScalarGprCount;
}

using ScalarReadResult =
    astraea::core::Result<
        std::uint32_t,
        ShaderScalarExecutionError>;

[[nodiscard]] ScalarReadResult read_scalar_source32(
    const ShaderIrScalarSource32& source,
    const ShaderScalarState& state) noexcept {
    return std::visit(
        [&state](const auto& typed_source)
            -> ScalarReadResult {
            using Source =
                std::decay_t<decltype(typed_source)>;

            if constexpr (
                std::is_same_v<Source, ShaderIrSgpr>) {
                if (!valid_sgpr(typed_source.index)) {
                    return ScalarReadResult::failure(
                        error(
                            ShaderScalarExecutionErrorCode::
                                invalid_sgpr_index,
                            ShaderScalarExecutionOperandRole::
                                source,
                            typed_source.index));
                }

                return ScalarReadResult::success(
                    state.sgprs[typed_source.index]);
            } else if constexpr (
                std::is_same_v<
                    Source,
                    ShaderIrSpecialScalarSource32>) {
                switch (typed_source.kind) {
                case ShaderIrSpecialScalarSourceKind32::vcc_lo:
                    return ScalarReadResult::success(
                        static_cast<std::uint32_t>(
                            state.vcc & 0xffffffffULL));
                case ShaderIrSpecialScalarSourceKind32::vcc_hi:
                    return ScalarReadResult::success(
                        static_cast<std::uint32_t>(
                            state.vcc >> 32U));
                case ShaderIrSpecialScalarSourceKind32::m0:
                    return ScalarReadResult::success(
                        state.m0);
                case ShaderIrSpecialScalarSourceKind32::
                    null_register:
                    return ScalarReadResult::success(0U);
                case ShaderIrSpecialScalarSourceKind32::exec_lo:
                    return ScalarReadResult::success(
                        static_cast<std::uint32_t>(
                            state.exec & 0xffffffffULL));
                case ShaderIrSpecialScalarSourceKind32::exec_hi:
                    return ScalarReadResult::success(
                        static_cast<std::uint32_t>(
                            state.exec >> 32U));
                }

                return ScalarReadResult::success(0U);
            } else if constexpr (
                std::is_same_v<
                    Source,
                    ShaderIrInlineInteger32>) {
                return ScalarReadResult::success(
                    static_cast<std::uint32_t>(
                        typed_source.value));
            } else {
                return ScalarReadResult::success(
                    typed_source.bits);
            }
        },
        source);
}

}  // namespace

ShaderScalarExecutionResult
execute_shader_scalar_operation(
    const ShaderIrOperation& operation,
    ShaderScalarState& state) noexcept {
    if (const auto* move32 =
            std::get_if<ShaderIrScalarMove32>(
                &operation);
        move32 != nullptr) {
        if (!valid_sgpr(move32->destination.index)) {
            return ShaderScalarExecutionResult::failure(
                error(
                    ShaderScalarExecutionErrorCode::
                        invalid_sgpr_index,
                    ShaderScalarExecutionOperandRole::
                        destination,
                    move32->destination.index));
        }

        const auto source =
            read_scalar_source32(
                move32->source,
                state);
        if (!source.has_value()) {
            return ShaderScalarExecutionResult::failure(
                source.error());
        }

        const auto value = source.value();
        state.sgprs[move32->destination.index] = value;

        return ShaderScalarExecutionResult::success(
            ShaderScalarExecutionEffect{
                .width = ShaderScalarWriteWidth::bits32,
                .first_destination_sgpr =
                    move32->destination.index,
                .written_values = {value, 0U},
            });
    }

    if (const auto* move64 =
            std::get_if<ShaderIrScalarMove64>(
                &operation);
        move64 != nullptr) {
        if (!valid_sgpr_pair(
                move64->destination.first_index)) {
            return ShaderScalarExecutionResult::failure(
                error(
                    ShaderScalarExecutionErrorCode::
                        invalid_sgpr_pair,
                    ShaderScalarExecutionOperandRole::
                        destination,
                    move64->destination.first_index));
        }
        if (!valid_sgpr_pair(
                move64->source.first_index)) {
            return ShaderScalarExecutionResult::failure(
                error(
                    ShaderScalarExecutionErrorCode::
                        invalid_sgpr_pair,
                    ShaderScalarExecutionOperandRole::
                        source,
                    move64->source.first_index));
        }

        const auto source_first =
            static_cast<std::size_t>(
                move64->source.first_index);
        const auto destination_first =
            static_cast<std::size_t>(
                move64->destination.first_index);

        // Capture both source dwords before either destination write so the
        // operation remains correct even for direct-constructed overlapping
        // state representations.
        const auto low = state.sgprs[source_first];
        const auto high = state.sgprs[source_first + 1U];

        state.sgprs[destination_first] = low;
        state.sgprs[destination_first + 1U] = high;

        return ShaderScalarExecutionResult::success(
            ShaderScalarExecutionEffect{
                .width = ShaderScalarWriteWidth::bits64,
                .first_destination_sgpr =
                    move64->destination.first_index,
                .written_values = {low, high},
            });
    }

    return ShaderScalarExecutionResult::failure(
        error(
            ShaderScalarExecutionErrorCode::
                unsupported_operation));
}

}  // namespace astraea::graphics
