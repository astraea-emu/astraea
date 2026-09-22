#include <astraea/graphics/shader_vector_add_execution.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace astraea::graphics {
namespace {

struct NormalF32Parts {
    bool negative = false;
    int exponent = 0;
    std::uint32_t significand = 0;
};

struct ExactNormalF32Add {
    bool supported = false;
    std::uint32_t bits = 0;
    ShaderVectorAddF32UnsupportedReason reason =
        ShaderVectorAddF32UnsupportedReason::
            non_normal_input;
};

[[nodiscard]] ShaderVectorAddF32ExecutionError add_error(
    ShaderVectorAddF32ExecutionErrorCode code,
    std::optional<std::size_t> lane_index = std::nullopt,
    std::optional<ShaderVectorAddF32UnsupportedReason>
        unsupported_reason = std::nullopt) noexcept {
    return ShaderVectorAddF32ExecutionError{
        .code = code,
        .lane_index = lane_index,
        .unsupported_reason = unsupported_reason,
    };
}

[[nodiscard]] std::optional<NormalF32Parts>
decode_normal_f32(std::uint32_t bits) noexcept {
    const auto exponent_field =
        static_cast<std::uint8_t>(
            (bits >> 23U) & 0xffU);
    if (exponent_field == 0U ||
        exponent_field == 0xffU) {
        return std::nullopt;
    }

    return NormalF32Parts{
        .negative = (bits & 0x80000000U) != 0U,
        .exponent =
            static_cast<int>(exponent_field) - 127,
        .significand =
            0x00800000U |
            (bits & 0x007fffffU),
    };
}

[[nodiscard]] ExactNormalF32Add
exact_normal_f32_add(
    std::uint32_t source0_bits,
    std::uint32_t source1_bits) noexcept {
    const auto source0 =
        decode_normal_f32(source0_bits);
    const auto source1 =
        decode_normal_f32(source1_bits);
    if (!source0.has_value() ||
        !source1.has_value()) {
        return ExactNormalF32Add{
            .supported = false,
            .bits = 0,
            .reason =
                ShaderVectorAddF32UnsupportedReason::
                    non_normal_input,
        };
    }

    const auto minimum_exponent =
        std::min(
            source0->exponent,
            source1->exponent);
    const auto source0_shift =
        source0->exponent - minimum_exponent;
    const auto source1_shift =
        source1->exponent - minimum_exponent;

    // With two nonzero 24-bit normal significands, an exponent gap greater
    // than 24 necessarily leaves significant low bits outside a binary32
    // result. Such a sum therefore requires rounding.
    if (source0_shift > 24 ||
        source1_shift > 24) {
        return ExactNormalF32Add{
            .supported = false,
            .bits = 0,
            .reason =
                ShaderVectorAddF32UnsupportedReason::
                    inexact_result,
        };
    }

    const auto signed_aligned =
        [](const NormalF32Parts& value,
           int shift) noexcept {
            const auto magnitude =
                static_cast<std::int64_t>(
                    static_cast<std::uint64_t>(
                        value.significand)
                    << static_cast<unsigned int>(
                        shift));
            return value.negative
                       ? -magnitude
                       : magnitude;
        };

    const auto exact_sum =
        signed_aligned(
            source0.value(),
            source0_shift) +
        signed_aligned(
            source1.value(),
            source1_shift);

    if (exact_sum == 0) {
        return ExactNormalF32Add{
            .supported = false,
            .bits = 0,
            .reason =
                ShaderVectorAddF32UnsupportedReason::
                    zero_result,
        };
    }

    const auto negative = exact_sum < 0;
    const auto magnitude =
        static_cast<std::uint64_t>(
            negative ? -exact_sum : exact_sum);
    const auto bit_count =
        std::bit_width(magnitude);

    const auto result_exponent =
        minimum_exponent - 23 +
        static_cast<int>(bit_count) - 1;
    if (result_exponent < -126 ||
        result_exponent > 127) {
        return ExactNormalF32Add{
            .supported = false,
            .bits = 0,
            .reason =
                ShaderVectorAddF32UnsupportedReason::
                    non_normal_result,
        };
    }

    std::uint64_t normalized_significand = 0;
    if (bit_count > 24U) {
        const auto discarded_bit_count =
            bit_count - 24U;
        const auto discarded_mask =
            (std::uint64_t{1}
             << discarded_bit_count) -
            1U;
        if ((magnitude & discarded_mask) != 0U) {
            return ExactNormalF32Add{
                .supported = false,
                .bits = 0,
                .reason =
                    ShaderVectorAddF32UnsupportedReason::
                        inexact_result,
            };
        }
        normalized_significand =
            magnitude >> discarded_bit_count;
    } else {
        normalized_significand =
            magnitude << (24U - bit_count);
    }

    const auto exponent_field =
        static_cast<std::uint32_t>(
            result_exponent + 127);
    const auto fraction =
        static_cast<std::uint32_t>(
            normalized_significand) &
        0x007fffffU;
    const auto sign_bit =
        negative ? 0x80000000U : 0U;

    return ExactNormalF32Add{
        .supported = true,
        .bits =
            sign_bit |
            (exponent_field << 23U) |
            fraction,
        .reason =
            ShaderVectorAddF32UnsupportedReason::
                inexact_result,
    };
}

}  // namespace

ShaderVectorAddF32ExecutionResult
execute_shader_vector_add_f32_exact_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state) noexcept {
    const auto* add =
        std::get_if<ShaderIrVectorAddF32>(
            &operation);
    if (add == nullptr) {
        return ShaderVectorAddF32ExecutionResult::failure(
            add_error(
                ShaderVectorAddF32ExecutionErrorCode::
                    unsupported_operation));
    }

    std::size_t lane_count = 0;
    std::uint64_t active_lane_mask = 0;
    switch (vector_state.wave_size) {
    case ShaderWaveSize::wave32:
        lane_count = 32;
        active_lane_mask =
            scalar_state.exec & 0xffffffffULL;
        break;
    case ShaderWaveSize::wave64:
        lane_count = 64;
        active_lane_mask = scalar_state.exec;
        break;
    case ShaderWaveSize::unspecified:
        return ShaderVectorAddF32ExecutionResult::failure(
            add_error(
                ShaderVectorAddF32ExecutionErrorCode::
                    invalid_wave_size));
    }

    const auto source0_index =
        static_cast<std::size_t>(
            add->source0.index);
    const auto source1_index =
        static_cast<std::size_t>(
            add->source1.index);
    const auto destination_index =
        static_cast<std::size_t>(
            add->destination.index);

    std::array<
        std::uint32_t,
        kShaderMaxWaveLaneCount>
        written_values{};

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) == 0U) {
            continue;
        }

        const auto lane_result =
            exact_normal_f32_add(
                vector_state.vgprs[
                    source0_index][lane],
                vector_state.vgprs[
                    source1_index][lane]);
        if (!lane_result.supported) {
            return ShaderVectorAddF32ExecutionResult::failure(
                add_error(
                    ShaderVectorAddF32ExecutionErrorCode::
                        unsupported_f32_case,
                    lane,
                    lane_result.reason));
        }

        written_values[lane] =
            lane_result.bits;
    }

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) != 0U) {
            vector_state.vgprs[
                destination_index][lane] =
                written_values[lane];
        }
    }

    return ShaderVectorAddF32ExecutionResult::success(
        ShaderVectorAddF32Effect{
            .wave_size = vector_state.wave_size,
            .destination_vgpr =
                add->destination.index,
            .source0_vgpr = add->source0.index,
            .source1_vgpr = add->source1.index,
            .active_lane_mask = active_lane_mask,
            .written_values = written_values,
        });
}

}  // namespace astraea::graphics
