#include <astraea/graphics/shader_register_state.hpp>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ShaderRegisterApplyError apply_error(
    ShaderRegisterApplyErrorCode code,
    std::uint16_t start_offset = 0,
    std::size_t value_count = 0,
    std::optional<GraphicsIrUnsupportedReason> unsupported_reason =
        std::nullopt) noexcept {
    return ShaderRegisterApplyError{
        .code = code,
        .start_offset = start_offset,
        .value_count = value_count,
        .unsupported_reason = unsupported_reason,
    };
}

enum class RawProgramAddressErrorCode {
    pgm_lo_uninitialized,
    pgm_hi_uninitialized,
    unsupported_pgm_hi_bits,
};

struct RawProgramAddressResult {
    bool valid = false;
    RawProgramAddressErrorCode error =
        RawProgramAddressErrorCode::pgm_lo_uninitialized;
    std::uint32_t pgm_lo = 0;
    std::uint32_t pgm_hi = 0;
    std::uint64_t address = 0;
};

[[nodiscard]] RawProgramAddressResult
resolve_raw_program_address(
    const ShaderRegisterState& state,
    std::uint16_t lo_offset,
    std::uint16_t hi_offset) noexcept {
    const auto lo_index =
        static_cast<std::size_t>(lo_offset);
    const auto hi_index =
        static_cast<std::size_t>(hi_offset);

    if (!state.initialized.test(lo_index)) {
        return RawProgramAddressResult{
            .valid = false,
            .error =
                RawProgramAddressErrorCode::
                    pgm_lo_uninitialized,
        };
    }

    const auto pgm_lo = state.values[lo_index];

    if (!state.initialized.test(hi_index)) {
        return RawProgramAddressResult{
            .valid = false,
            .error =
                RawProgramAddressErrorCode::
                    pgm_hi_uninitialized,
            .pgm_lo = pgm_lo,
        };
    }

    const auto pgm_hi = state.values[hi_index];
    if ((pgm_hi & 0xffffff00U) != 0U) {
        return RawProgramAddressResult{
            .valid = false,
            .error =
                RawProgramAddressErrorCode::
                    unsupported_pgm_hi_bits,
            .pgm_lo = pgm_lo,
            .pgm_hi = pgm_hi,
        };
    }

    return RawProgramAddressResult{
        .valid = true,
        .pgm_lo = pgm_lo,
        .pgm_hi = pgm_hi,
        .address =
            (static_cast<std::uint64_t>(pgm_lo)
             << 8U) |
            (static_cast<std::uint64_t>(
                 pgm_hi & 0xffU)
             << 40U),
    };
}

[[nodiscard]] PixelProgramAddressError
pixel_address_error(
    const RawProgramAddressResult& raw) noexcept {
    PixelProgramAddressErrorCode code =
        PixelProgramAddressErrorCode::
            pgm_lo_uninitialized;

    switch (raw.error) {
    case RawProgramAddressErrorCode::
        pgm_lo_uninitialized:
        code =
            PixelProgramAddressErrorCode::
                pgm_lo_uninitialized;
        break;
    case RawProgramAddressErrorCode::
        pgm_hi_uninitialized:
        code =
            PixelProgramAddressErrorCode::
                pgm_hi_uninitialized;
        break;
    case RawProgramAddressErrorCode::
        unsupported_pgm_hi_bits:
        code =
            PixelProgramAddressErrorCode::
                unsupported_pgm_hi_bits;
        break;
    }

    return PixelProgramAddressError{
        .code = code,
        .pgm_lo = raw.pgm_lo,
        .pgm_hi = raw.pgm_hi,
    };
}

[[nodiscard]] GeometryEsProgramAddressError
geometry_es_address_error(
    const RawProgramAddressResult& raw) noexcept {
    GeometryEsProgramAddressErrorCode code =
        GeometryEsProgramAddressErrorCode::
            pgm_lo_uninitialized;

    switch (raw.error) {
    case RawProgramAddressErrorCode::
        pgm_lo_uninitialized:
        code =
            GeometryEsProgramAddressErrorCode::
                pgm_lo_uninitialized;
        break;
    case RawProgramAddressErrorCode::
        pgm_hi_uninitialized:
        code =
            GeometryEsProgramAddressErrorCode::
                pgm_hi_uninitialized;
        break;
    case RawProgramAddressErrorCode::
        unsupported_pgm_hi_bits:
        code =
            GeometryEsProgramAddressErrorCode::
                unsupported_pgm_hi_bits;
        break;
    }

    return GeometryEsProgramAddressError{
        .code = code,
        .pgm_lo = raw.pgm_lo,
        .pgm_hi = raw.pgm_hi,
    };
}

}  // namespace

ShaderRegisterApplyResult
apply_shader_register_graphics_ir(
    const GraphicsIrEmission& emission,
    ShaderRegisterState& state) noexcept {
    if (const auto* unsupported =
            std::get_if<GraphicsIrUnsupported>(
                &emission.operation);
        unsupported != nullptr) {
        return ShaderRegisterApplyResult::failure(
            apply_error(
                ShaderRegisterApplyErrorCode::
                    unsupported_operation,
                0,
                0,
                unsupported->reason));
    }

    const auto* write =
        std::get_if<
            GraphicsIrShaderRegisterWriteRange>(
            &emission.operation);
    if (write == nullptr) {
        return ShaderRegisterApplyResult::failure(
            apply_error(
                ShaderRegisterApplyErrorCode::
                    unsupported_operation));
    }

    const auto start =
        static_cast<std::size_t>(
            write->start_offset);
    const auto count = write->values.size();

    if (start >= kShaderRegisterStateDwords ||
        count >
            kShaderRegisterStateDwords -
                start) {
        return ShaderRegisterApplyResult::failure(
            apply_error(
                ShaderRegisterApplyErrorCode::
                    register_range_out_of_bounds,
                write->start_offset,
                count));
    }

    for (std::size_t index = 0;
         index < count;
         ++index) {
        const auto destination =
            start + index;
        state.values[destination] =
            write->values[index];
        state.initialized.set(destination);
    }

    return ShaderRegisterApplyResult::success(
        ShaderRegisterWriteEffect{
            .start_offset = write->start_offset,
            .value_count = count,
        });
}

PixelProgramAddressResult
resolve_pixel_program_address(
    const ShaderRegisterState& state) noexcept {
    const auto raw =
        resolve_raw_program_address(
            state,
            kPixelProgramLoRegisterOffset,
            kPixelProgramHiRegisterOffset);

    if (!raw.valid) {
        return PixelProgramAddressResult::failure(
            pixel_address_error(raw));
    }

    return PixelProgramAddressResult::success(
        PixelProgramGpuAddress{
            .value = raw.address,
        });
}

GeometryEsProgramAddressResult
resolve_geometry_es_program_address(
    const ShaderRegisterState& state) noexcept {
    const auto raw =
        resolve_raw_program_address(
            state,
            kGeometryEsProgramLoRegisterOffset,
            kGeometryEsProgramHiRegisterOffset);

    if (!raw.valid) {
        return GeometryEsProgramAddressResult::failure(
            geometry_es_address_error(raw));
    }

    return GeometryEsProgramAddressResult::success(
        GeometryEsProgramGpuAddress{
            .value = raw.address,
        });
}

}  // namespace astraea::graphics
