#pragma once

// Research-only, backend-neutral representation of *reported and readback-
// verified* guest writes. This ledger does not execute guest code, authorize
// memory access, or prove that a write occurred merely because a hook fired.
//
// Unlike the current limited owned interpreter trace, x86 REP operations may
// commit a prefix of writes before a fault with RIP still at the instruction.
// Future backends must explicitly attest completed effects before recording.
// This is an opt-in evaluation contract, NOT a production memory mediator.
#include "owned_guest_address_space.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include <astraea/execution/context.hpp>

namespace astraea::research {

enum class OwnedEffectOutcome {
    retired,
    refused_before_effects,
    fault_after_effects,
};

enum class OwnedEffectFault {
    none,
    unsupported_instruction,
    invalid_instruction,
    memory_unmapped,
    memory_permission,
    other,
};

struct OwnedVerifiedGuestWrite {
    std::uint64_t guest_address = 0U;
    std::size_t size = 0U;
    std::array<std::byte, 64U> bytes{};

    auto operator<=>(const OwnedVerifiedGuestWrite&) const = default;
};

struct OwnedCpuEffectReport {
    static constexpr std::size_t kMaxEvents = 64U;
    std::uint64_t instruction_rip = 0U;
    astraea::execution::GuestCpuContext before_context{};
    astraea::execution::GuestCpuContext after_context{};
    OwnedEffectOutcome outcome = OwnedEffectOutcome::refused_before_effects;
    OwnedEffectFault fault = OwnedEffectFault::none;
    std::optional<std::uint64_t> fault_address;
    std::size_t write_count = 0U;
    std::array<OwnedVerifiedGuestWrite, kMaxEvents> writes{};
};

// One ledger instance is single-use. No effects may be silently truncated.
// Every append checks the guest virtual *read* permission and exact current
// readback bytes, in addition to requiring an explicit producer attestation
// that its guest write has already completed. Readback verifies current value,
// not causality (e.g. re-writing identical bytes), and neither this class nor
// a generic pre-write CPU hook establishes atomicity/containment.
// On any error, this instance is poisoned and cannot produce a report.
// This bounded fixed-storage design does not claim unbounded REP coverage.
class OwnedCpuEffectLedger {
public:
    static constexpr std::size_t kMaxEffectBytes = 64U;

    [[nodiscard]] bool begin(
        const astraea::execution::GuestCpuContext& before) noexcept {
        if (phase_ != Phase::unused) return false;
        report_.instruction_rip = before.rip;
        report_.before_context = before;
        phase_ = Phase::recording;
        return true;
    }

    [[nodiscard]] bool append_completed_write(
        std::uint64_t guest_address, std::span<const std::byte> committed,
        const OwnedGuestAddressSpace& memory) noexcept {
        if (phase_ != Phase::recording) return false;
        if (committed.empty() || committed.size() > kMaxEffectBytes ||
            report_.write_count >= report_.writes.size() ||
            committed.size() >
                std::numeric_limits<std::uint64_t>::max() - guest_address)
            return poison();

        std::array<std::byte, kMaxEffectBytes> readback{};
        const auto got = memory.read(
            guest_address, std::span<std::byte>(
                readback.data(), committed.size()));
        if (!got.has_value() || got.value() != committed.size() ||
            !std::equal(committed.begin(), committed.end(), readback.begin()))
            return poison();

        auto& effect = report_.writes[report_.write_count];
        effect.guest_address = guest_address;
        effect.size = committed.size();
        std::copy(committed.begin(), committed.end(), effect.bytes.begin());
        ++report_.write_count;
        return true;
    }

    [[nodiscard]] bool finish(
        OwnedEffectOutcome outcome,
        const astraea::execution::GuestCpuContext& after,
        OwnedEffectFault fault,
        std::optional<std::uint64_t> fault_address,
        OwnedCpuEffectReport& output) noexcept {
        if (phase_ != Phase::recording) return false;
        if (outcome == OwnedEffectOutcome::retired) {
            if (fault != OwnedEffectFault::none ||
                fault_address.has_value()) return poison();
        } else if (outcome == OwnedEffectOutcome::refused_before_effects) {
            if (fault == OwnedEffectFault::none ||
                report_.write_count != 0U ||
                after != report_.before_context)
                return poison();
        } else if (outcome == OwnedEffectOutcome::fault_after_effects) {
            if (fault == OwnedEffectFault::none ||
                report_.write_count == 0U ||
                after.rip != report_.instruction_rip)
                return poison();
        } else {
            return poison();
        }

        report_.outcome = outcome;
        report_.after_context = after;
        report_.fault = fault;
        report_.fault_address = fault_address;
        output = report_;
        phase_ = Phase::finished;
        return true;
    }

private:
    enum class Phase { unused, recording, poisoned, finished };
    [[nodiscard]] bool poison() noexcept {
        phase_ = Phase::poisoned;
        return false;
    }

    Phase phase_ = Phase::unused;
    OwnedCpuEffectReport report_{};
};

} // namespace astraea::research
