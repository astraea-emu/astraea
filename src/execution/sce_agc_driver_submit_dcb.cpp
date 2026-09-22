#include <astraea/execution/sce_agc_driver_submit_dcb.hpp>

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

constexpr std::size_t kWordsPointerOffset = 0x00;
constexpr std::size_t kWordCountOffset = 0x08;
constexpr std::size_t kFlagOffset = 0x0c;
constexpr std::size_t kPaddingOffset = 0x0d;
constexpr std::size_t kCommandWordBytes = 4;

[[nodiscard]] std::uint64_t read_little_endian_u64(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

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

[[nodiscard]] SceAgcDriverSubmitDcbPlanError plan_error(
    SceAgcDriverSubmitDcbPlanErrorCode code,
    std::optional<HleFunctionId> function_id = std::nullopt,
    std::optional<astraea::memory::GuestAddress> guest_address =
        std::nullopt) noexcept {
    return SceAgcDriverSubmitDcbPlanError{
        .code = code,
        .function_id = function_id,
        .guest_address = guest_address,
        .guest_memory_error = std::nullopt,
    };
}

[[nodiscard]] SceAgcDriverSubmitDcbPlanError memory_error(
    GuestMemoryError detail) noexcept {
    std::optional<astraea::memory::GuestAddress> address;
    if (detail.has_guest_address) {
        address =
            astraea::memory::GuestAddress{
                detail.guest_address};
    }

    return SceAgcDriverSubmitDcbPlanError{
        .code =
            SceAgcDriverSubmitDcbPlanErrorCode::
                guest_memory_failure,
        .function_id = kSceAgcDriverSubmitDcbHleId,
        .guest_address = address,
        .guest_memory_error = detail,
    };
}

[[nodiscard]] bool command_byte_count(
    std::uint32_t word_count,
    std::size_t& byte_count) noexcept {
    constexpr auto kWordBytes64 =
        static_cast<std::uint64_t>(kCommandWordBytes);
    const auto raw =
        static_cast<std::uint64_t>(word_count);

    if (raw >
        std::numeric_limits<std::uint64_t>::max() /
            kWordBytes64) {
        return false;
    }
    const auto bytes = raw * kWordBytes64;

    if constexpr (
        sizeof(std::size_t) <
        sizeof(std::uint64_t)) {
        if (bytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return false;
        }
    }

    byte_count = static_cast<std::size_t>(bytes);
    return true;
}

}  // namespace

SceAgcDriverSubmitDcbPlanResult
plan_sce_agc_driver_submit_dcb(
    const HleCall& call,
    const GuestMemoryAccess& guest_memory) {
    if (call.function_id !=
        kSceAgcDriverSubmitDcbHleId) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    unexpected_function,
                call.function_id));
    }

    const auto description_address =
        astraea::memory::GuestAddress{
            call.arguments[0]};
    if (description_address.value() == 0U) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    null_submit_description,
                call.function_id,
                description_address));
    }

    std::array<
        std::byte,
        kSceAgcDcbSubmitDescriptionSize>
        descriptor{};
    auto descriptor_read =
        guest_memory.read(
            description_address,
            descriptor);
    if (!descriptor_read.has_value()) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            memory_error(descriptor_read.error()));
    }

    const auto words_address =
        astraea::memory::GuestAddress{
            read_little_endian_u64(
                descriptor,
                kWordsPointerOffset)};
    const auto word_count =
        read_little_endian_u32(
            descriptor,
            kWordCountOffset);
    const auto flag =
        std::to_integer<std::uint8_t>(
            descriptor[kFlagOffset]);

    std::array<std::byte, 3> padding{
        descriptor[kPaddingOffset],
        descriptor[kPaddingOffset + 1U],
        descriptor[kPaddingOffset + 2U],
    };

    if (word_count >
        kSceAgcDcbSupportedMaximumWordCount) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    word_count_exceeds_supported_profile,
                call.function_id,
                words_address));
    }

    std::size_t byte_count = 0;
    if (!command_byte_count(
            word_count,
            byte_count)) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    host_size_unrepresentable,
                call.function_id,
                words_address));
    }

    if (word_count != 0U &&
        words_address.value() == 0U) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    null_command_words,
                call.function_id,
                words_address));
    }

    try {
        std::vector<std::byte> command_bytes(
            byte_count);

        if (byte_count != 0U) {
            auto command_read =
                guest_memory.read(
                    words_address,
                    command_bytes);
            if (!command_read.has_value()) {
                return SceAgcDriverSubmitDcbPlanResult::failure(
                    memory_error(command_read.error()));
            }
        }

        return SceAgcDriverSubmitDcbPlanResult::success(
            SceAgcDcbSubmission{
                .submit_description_address =
                    description_address,
                .command_words_address =
                    words_address,
                .word_count = word_count,
                .flag = flag,
                .raw_submit_description =
                    descriptor,
                .opaque_padding = padding,
                .command_buffer_bytes =
                    std::move(command_bytes),
            });
    } catch (const std::bad_alloc&) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    host_allocation_failure,
                call.function_id,
                words_address));
    } catch (const std::length_error&) {
        return SceAgcDriverSubmitDcbPlanResult::failure(
            plan_error(
                SceAgcDriverSubmitDcbPlanErrorCode::
                    host_size_unrepresentable,
                call.function_id,
                words_address));
    }
}

}  // namespace astraea::execution
