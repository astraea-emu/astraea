#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// Astraea-internal service identity. The public PS5 NID/library identity is
// resolved separately and must not be confused with this numeric ID.
inline constexpr HleFunctionId kSceAgcDriverSubmitDcbHleId{4};

inline constexpr std::size_t kSceAgcDcbSubmitDescriptionSize = 16;
inline constexpr std::uint32_t
    kSceAgcDcbSupportedMaximumWordCount = 0x000fffffU;

struct SceAgcDcbSubmission {
    astraea::memory::GuestAddress submit_description_address;
    astraea::memory::GuestAddress command_words_address;
    std::uint32_t word_count = 0;
    std::uint8_t flag = 0;
    std::array<std::byte, kSceAgcDcbSubmitDescriptionSize>
        raw_submit_description{};
    std::array<std::byte, 3> opaque_padding{};
    std::vector<std::byte> command_buffer_bytes;

    auto operator<=>(const SceAgcDcbSubmission&) const = default;
};

enum class SceAgcDriverSubmitDcbPlanErrorCode {
    unexpected_function,
    null_submit_description,
    null_command_words,
    word_count_exceeds_supported_profile,
    host_size_unrepresentable,
    host_allocation_failure,
    guest_memory_failure,
};

struct SceAgcDriverSubmitDcbPlanError {
    SceAgcDriverSubmitDcbPlanErrorCode code =
        SceAgcDriverSubmitDcbPlanErrorCode::unexpected_function;
    std::optional<HleFunctionId> function_id;
    std::optional<astraea::memory::GuestAddress> guest_address;
    std::optional<GuestMemoryError> guest_memory_error;

    auto operator<=>(const SceAgcDriverSubmitDcbPlanError&) const =
        default;
};

using SceAgcDriverSubmitDcbPlanResult =
    astraea::core::Result<
        SceAgcDcbSubmission,
        SceAgcDriverSubmitDcbPlanError>;

// Captures only the immutable read/validate half of sceAgcDriverSubmitDcb.
// It never mutates guest memory, decodes PM4, dispatches host GPU work,
// returns a guest-visible native result, or resumes guest execution.
[[nodiscard]] SceAgcDriverSubmitDcbPlanResult
plan_sce_agc_driver_submit_dcb(
    const HleCall& call,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
