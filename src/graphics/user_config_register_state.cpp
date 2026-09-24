#include <astraea/graphics/user_config_register_state.hpp>

#include <cstddef>
#include <variant>

namespace astraea::graphics {
namespace {

[[nodiscard]] UserConfigRegisterApplyError apply_error(
    UserConfigRegisterApplyErrorCode code,
    std::uint16_t start_offset = 0,
    std::size_t value_count = 0,
    std::optional<GraphicsIrUnsupportedReason>
        unsupported_reason = std::nullopt) noexcept {
    return UserConfigRegisterApplyError{
        .code = code,
        .start_offset = start_offset,
        .value_count = value_count,
        .unsupported_reason = unsupported_reason,
    };
}

}  // namespace

UserConfigRegisterApplyResult
apply_user_config_register_graphics_ir(
    const GraphicsIrEmission& emission,
    UserConfigRegisterState& state) noexcept {
    if (const auto* unsupported =
            std::get_if<GraphicsIrUnsupported>(
                &emission.operation);
        unsupported != nullptr) {
        return UserConfigRegisterApplyResult::failure(
            apply_error(
                UserConfigRegisterApplyErrorCode::
                    unsupported_operation,
                0,
                0,
                unsupported->reason));
    }

    const auto* write =
        std::get_if<
            GraphicsIrUserConfigRegisterWriteRange>(
            &emission.operation);
    if (write == nullptr) {
        return UserConfigRegisterApplyResult::failure(
            apply_error(
                UserConfigRegisterApplyErrorCode::
                    unsupported_operation));
    }

    const auto start =
        static_cast<std::size_t>(
            write->start_offset);
    const auto count = write->values.size();

    if (start >= kUserConfigRegisterStateDwords ||
        count >
            kUserConfigRegisterStateDwords -
                start) {
        return UserConfigRegisterApplyResult::failure(
            apply_error(
                UserConfigRegisterApplyErrorCode::
                    register_range_out_of_bounds,
                write->start_offset,
                count));
    }

    for (std::size_t index = 0;
         index < count;
         ++index) {
        const auto destination = start + index;
        state.values[destination] =
            write->values[index];
        state.initialized.set(destination);
    }

    return UserConfigRegisterApplyResult::success(
        UserConfigRegisterWriteEffect{
            .start_offset = write->start_offset,
            .value_count = count,
        });
}

}  // namespace astraea::graphics
