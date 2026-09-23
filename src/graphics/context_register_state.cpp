#include <astraea/graphics/context_register_state.hpp>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] ContextRegisterApplyError apply_error(
    ContextRegisterApplyErrorCode code,
    std::uint16_t start_offset = 0,
    std::size_t value_count = 0,
    std::optional<GraphicsIrUnsupportedReason> unsupported_reason =
        std::nullopt) noexcept {
    return ContextRegisterApplyError{
        .code = code,
        .start_offset = start_offset,
        .value_count = value_count,
        .unsupported_reason = unsupported_reason,
    };
}

}  // namespace

ContextRegisterApplyResult
apply_context_register_graphics_ir(
    const GraphicsIrEmission& emission,
    ContextRegisterState& state) noexcept {
    if (const auto* unsupported =
            std::get_if<GraphicsIrUnsupported>(
                &emission.operation);
        unsupported != nullptr) {
        return ContextRegisterApplyResult::failure(
            apply_error(
                ContextRegisterApplyErrorCode::
                    unsupported_operation,
                0,
                0,
                unsupported->reason));
    }

    const auto* write =
        std::get_if<GraphicsIrContextRegisterWriteRange>(
            &emission.operation);
    if (write == nullptr) {
        return ContextRegisterApplyResult::failure(
            apply_error(
                ContextRegisterApplyErrorCode::
                    unsupported_operation));
    }

    const auto start =
        static_cast<std::size_t>(
            write->start_offset);
    const auto count = write->values.size();

    if (start >= kContextRegisterStateDwords ||
        count >
            kContextRegisterStateDwords -
                start) {
        return ContextRegisterApplyResult::failure(
            apply_error(
                ContextRegisterApplyErrorCode::
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

    return ContextRegisterApplyResult::success(
        ContextRegisterWriteEffect{
            .start_offset = write->start_offset,
            .value_count = count,
        });
}

}  // namespace astraea::graphics
