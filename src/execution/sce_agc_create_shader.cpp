#include <astraea/execution/sce_agc_create_shader.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::execution {
namespace {

constexpr std::size_t kAgcHeaderMinimumSize = 96;
constexpr std::size_t kAgcHeaderSizeOffset = 0x40;
constexpr std::size_t kAgcShaderTextSizeOffset = 0x44;
constexpr std::size_t kAgcShaderTextTrailerSize = 0x30;

[[nodiscard]] std::uint32_t read_little_endian_u32(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |=
            static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

[[nodiscard]] bool fits_host_size(
    std::uint32_t value) noexcept {
    if constexpr (
        sizeof(std::size_t) <
        sizeof(std::uint32_t)) {
        return
            static_cast<std::uint64_t>(value) <=
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max());
    }
    return true;
}

[[nodiscard]] SceAgcCreateShaderPlanError plan_error(
    SceAgcCreateShaderPlanErrorCode code,
    std::optional<HleFunctionId> function_id = std::nullopt,
    std::optional<astraea::memory::GuestAddress> guest_address =
        std::nullopt) noexcept {
    return SceAgcCreateShaderPlanError{
        .code = code,
        .function_id = function_id,
        .guest_address = guest_address,
        .guest_memory_error = std::nullopt,
        .shader_binary_error = std::nullopt,
    };
}

[[nodiscard]] SceAgcCreateShaderPlanError memory_error(
    GuestMemoryError detail) noexcept {
    std::optional<astraea::memory::GuestAddress> address;
    if (detail.has_guest_address) {
        address =
            astraea::memory::GuestAddress{
                detail.guest_address};
    }

    return SceAgcCreateShaderPlanError{
        .code =
            SceAgcCreateShaderPlanErrorCode::
                guest_memory_failure,
        .function_id = kSceAgcCreateShaderHleId,
        .guest_address = address,
        .guest_memory_error = detail,
        .shader_binary_error = std::nullopt,
    };
}

[[nodiscard]] SceAgcCreateShaderPlanError shader_error(
    astraea::graphics::AgcShaderBinaryError detail) noexcept {
    return SceAgcCreateShaderPlanError{
        .code =
            SceAgcCreateShaderPlanErrorCode::
                invalid_shader_binary,
        .function_id = kSceAgcCreateShaderHleId,
        .guest_address = std::nullopt,
        .guest_memory_error = std::nullopt,
        .shader_binary_error = detail,
    };
}

}  // namespace

SceAgcCreateShaderPlanResult
plan_sce_agc_create_shader(
    const HleCall& call,
    const GuestMemoryAccess& guest_memory) {
    if (call.function_id != kSceAgcCreateShaderHleId) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    unexpected_function,
                call.function_id));
    }

    const SceAgcCreateShaderRequest request{
        .output_pointer_address =
            astraea::memory::GuestAddress{
                call.arguments[0]},
        .shader_header_address =
            astraea::memory::GuestAddress{
                call.arguments[1]},
        .shader_text_address =
            astraea::memory::GuestAddress{
                call.arguments[2]},
    };

    if (request.shader_header_address.value() == 0) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    null_shader_header,
                call.function_id,
                request.shader_header_address));
    }
    if (request.shader_text_address.value() == 0) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    null_shader_text,
                call.function_id,
                request.shader_text_address));
    }

    std::array<std::byte, kAgcHeaderMinimumSize>
        header_prefix{};
    auto prefix_read =
        guest_memory.read(
            request.shader_header_address,
            header_prefix);
    if (!prefix_read.has_value()) {
        return SceAgcCreateShaderPlanResult::failure(
            memory_error(prefix_read.error()));
    }

    const auto declared_header_size =
        read_little_endian_u32(
            header_prefix,
            kAgcHeaderSizeOffset);
    const auto declared_shader_text_size =
        read_little_endian_u32(
            header_prefix,
            kAgcShaderTextSizeOffset);

    if (declared_header_size < kAgcHeaderMinimumSize) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    declared_header_size_too_small,
                call.function_id,
                request.shader_header_address));
    }
    if (declared_shader_text_size <
        kAgcShaderTextTrailerSize) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    declared_shader_text_size_too_small,
                call.function_id,
                request.shader_text_address));
    }

    if (!fits_host_size(declared_header_size) ||
        !fits_host_size(declared_shader_text_size)) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    host_size_unrepresentable,
                call.function_id));
    }

    const auto header_size =
        static_cast<std::size_t>(
            declared_header_size);
    const auto text_size =
        static_cast<std::size_t>(
            declared_shader_text_size);

    try {
        std::vector<std::byte> shader_header(
            header_size);
        std::vector<std::byte> shader_text(
            text_size);

        auto header_read =
            guest_memory.read(
                request.shader_header_address,
                shader_header);
        if (!header_read.has_value()) {
            return SceAgcCreateShaderPlanResult::failure(
                memory_error(header_read.error()));
        }

        auto text_read =
            guest_memory.read(
                request.shader_text_address,
                shader_text);
        if (!text_read.has_value()) {
            return SceAgcCreateShaderPlanResult::failure(
                memory_error(text_read.error()));
        }

        auto shader =
            astraea::graphics::
                parse_agc_shader_binary(
                    shader_header,
                    shader_text);
        if (!shader.has_value()) {
            return SceAgcCreateShaderPlanResult::failure(
                shader_error(shader.error()));
        }

        return SceAgcCreateShaderPlanResult::success(
            SceAgcCreateShaderPlan{
                .request = request,
                .shader =
                    std::move(shader).value(),
            });
    } catch (const std::bad_alloc&) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    host_allocation_failure,
                call.function_id));
    } catch (const std::length_error&) {
        return SceAgcCreateShaderPlanResult::failure(
            plan_error(
                SceAgcCreateShaderPlanErrorCode::
                    host_size_unrepresentable,
                call.function_id));
    }
}

}  // namespace astraea::execution
