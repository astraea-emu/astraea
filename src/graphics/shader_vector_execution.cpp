#include <astraea/graphics/shader_vector_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderVectorExecutionError vector_error(
    ShaderVectorExecutionErrorCode code,
    std::optional<std::size_t> lane_index =
        std::nullopt) noexcept {
    return ShaderVectorExecutionError{
        .code = code,
        .lane_index = lane_index,
    };
}

struct NormalF32 {
    bool negative = false;
    std::uint8_t exponent = 0;
    std::uint32_t significand = 0;
};

[[nodiscard]] bool decode_normal_f32(
    std::uint32_t bits,
    NormalF32& output) noexcept {
    const auto exponent =
        static_cast<std::uint8_t>(
            (bits >> 23U) & 0xffU);
    if (exponent == 0 || exponent == 0xffU) {
        return false;
    }

    output = NormalF32{
        .negative = (bits & 0x80000000U) != 0,
        .exponent = exponent,
        .significand =
            0x00800000U |
            (bits & 0x007fffffU),
    };
    return true;
}

[[nodiscard]] std::size_t bit_length(
    std::uint64_t value) noexcept {
    std::size_t length = 0;
    while (value != 0) {
        ++length;
        value >>= 1U;
    }
    return length;
}

[[nodiscard]] bool exact_normal_f32_sum(
    std::uint32_t left_bits,
    std::uint32_t right_bits,
    std::uint32_t& result_bits) noexcept {
    NormalF32 left{};
    NormalF32 right{};
    if (!decode_normal_f32(left_bits, left) ||
        !decode_normal_f32(right_bits, right)) {
        return false;
    }

    const auto minimum_exponent =
        left.exponent < right.exponent
            ? left.exponent
            : right.exponent;
    const auto left_shift =
        static_cast<unsigned>(
            left.exponent - minimum_exponent);
    const auto right_shift =
        static_cast<unsigned>(
            right.exponent - minimum_exponent);

    // For two nonzero 24-bit normal significands, an exponent separation
    // greater than 24 cannot produce an exactly representable binary32 sum.
    if (left_shift > 24U || right_shift > 24U) {
        return false;
    }

    const auto left_magnitude =
        static_cast<std::int64_t>(
            static_cast<std::uint64_t>(
                left.significand)
            << left_shift);
    const auto right_magnitude =
        static_cast<std::int64_t>(
            static_cast<std::uint64_t>(
                right.significand)
            << right_shift);

    const auto signed_left =
        left.negative
            ? -left_magnitude
            : left_magnitude;
    const auto signed_right =
        right.negative
            ? -right_magnitude
            : right_magnitude;
    const auto signed_sum =
        signed_left + signed_right;

    // Exact cancellation requires signed-zero semantics, which are deferred
    // until explicit RDNA2 floating-point mode behavior is represented.
    if (signed_sum == 0) {
        return false;
    }

    const auto negative = signed_sum < 0;
    const auto magnitude =
        static_cast<std::uint64_t>(
            negative ? -signed_sum : signed_sum);
    const auto length = bit_length(magnitude);
    if (length == 0) {
        return false;
    }

    std::uint64_t normalized_significand = 0;
    std::int32_t result_exponent =
        static_cast<std::int32_t>(
            minimum_exponent);

    if (length > 24U) {
        const auto discard_count =
            static_cast<unsigned>(
                length - 24U);
        const auto discarded_mask =
            (std::uint64_t{1} << discard_count) - 1U;
        if ((magnitude & discarded_mask) != 0) {
            return false;
        }

        normalized_significand =
            magnitude >> discard_count;
        result_exponent +=
            static_cast<std::int32_t>(
                discard_count);
    } else {
        const auto left_normalize =
            static_cast<unsigned>(
                24U - length);
        normalized_significand =
            magnitude << left_normalize;
        result_exponent -=
            static_cast<std::int32_t>(
                left_normalize);
    }

    // Subnormal/zero and overflow/infinity outcomes are intentionally deferred.
    if (result_exponent <= 0 ||
        result_exponent >= 0xff) {
        return false;
    }

    if (normalized_significand < 0x00800000ULL ||
        normalized_significand > 0x00ffffffULL) {
        return false;
    }

    result_bits =
        (negative ? 0x80000000U : 0U) |
        (static_cast<std::uint32_t>(
             result_exponent)
         << 23U) |
        (static_cast<std::uint32_t>(
             normalized_significand) &
         0x007fffffU);
    return true;
}

}  // namespace

ShaderVectorMove32ExecutionResult
execute_shader_vector_move32_operation(
    const ShaderIrOperation& operation,
    const ShaderScalarState& scalar_state,
    ShaderVectorState& vector_state) noexcept {
    const auto* move =
        std::get_if<ShaderIrVectorMove32>(
            &operation);
    if (move == nullptr) {
        return ShaderVectorMove32ExecutionResult::failure(
            vector_error(
                ShaderVectorExecutionErrorCode::
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
        return ShaderVectorMove32ExecutionResult::failure(
            vector_error(
                ShaderVectorExecutionErrorCode::
                    invalid_wave_size));
    }

    std::array<
        std::uint32_t,
        kShaderMaxWaveLaneCount>
        written_values{};

    const auto source_index =
        static_cast<std::size_t>(
            move->source.index);
    const auto destination_index =
        static_cast<std::size_t>(
            move->destination.index);

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) != 0) {
            written_values[lane] =
                vector_state.vgprs[source_index][lane];
        }
    }

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) != 0) {
            vector_state.vgprs[destination_index][lane] =
                written_values[lane];
        }
    }

    return ShaderVectorMove32ExecutionResult::success(
        ShaderVectorMove32Effect{
            .wave_size = vector_state.wave_size,
            .destination_vgpr =
                move->destination.index,
            .source_vgpr = move->source.index,
            .active_lane_mask = active_lane_mask,
            .written_values = written_values,
        });
}

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
            vector_error(
                ShaderVectorExecutionErrorCode::
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
            vector_error(
                ShaderVectorExecutionErrorCode::
                    invalid_wave_size));
    }

    std::array<
        std::uint32_t,
        kShaderMaxWaveLaneCount>
        written_values{};

    const auto source0_index =
        static_cast<std::size_t>(
            add->source0.index);
    const auto source1_index =
        static_cast<std::size_t>(
            add->source1.index);
    const auto destination_index =
        static_cast<std::size_t>(
            add->destination.index);

    // Precompute every active lane before committing any destination write.
    // This also makes destination/source aliasing deterministic.
    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) == 0) {
            continue;
        }

        if (!exact_normal_f32_sum(
                vector_state.vgprs[
                    source0_index][lane],
                vector_state.vgprs[
                    source1_index][lane],
                written_values[lane])) {
            return ShaderVectorAddF32ExecutionResult::failure(
                vector_error(
                    ShaderVectorExecutionErrorCode::
                        unsupported_f32_case,
                    lane));
        }
    }

    for (std::size_t lane = 0;
         lane < lane_count;
         ++lane) {
        const auto lane_bit =
            std::uint64_t{1} << lane;
        if ((active_lane_mask & lane_bit) != 0) {
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
