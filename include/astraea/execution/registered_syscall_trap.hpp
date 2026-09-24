#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <span>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::array<std::byte, 2>
    kX86SyscallBytes{
        std::byte{0x0f},
        std::byte{0x05},
    };

inline constexpr std::array<std::byte, 2>
    kX86Ud2Bytes{
        std::byte{0x0f},
        std::byte{0x0b},
    };

struct RegisteredSyscallTrapSite {
    astraea::memory::GuestAddress guest_rip;
    std::size_t code_byte_offset = 0;
    std::array<std::byte, 2> original_bytes =
        kX86SyscallBytes;
    std::array<std::byte, 2> trap_bytes =
        kX86Ud2Bytes;

    auto operator<=>(const RegisteredSyscallTrapSite&) const = default;
};

enum class RegisteredSyscallTrapErrorCode {
    code_range_overflow,
    site_before_code_range,
    site_not_fully_in_code_range,
    unexpected_original_bytes,
    unexpected_trap_bytes,
};

struct RegisteredSyscallTrapError {
    RegisteredSyscallTrapErrorCode code =
        RegisteredSyscallTrapErrorCode::
            site_not_fully_in_code_range;
    std::size_t code_byte_offset = 0;

    auto operator<=>(const RegisteredSyscallTrapError&) const = default;
};

using RegisteredSyscallTrapPlanResult =
    astraea::core::Result<
        RegisteredSyscallTrapSite,
        RegisteredSyscallTrapError>;

using RegisteredSyscallTrapMutationResult =
    astraea::core::Result<
        bool,
        RegisteredSyscallTrapError>;

// Plans exactly one explicitly registered x86-64 SYSCALL -> UD2 patch.
// No byte-pattern scanning is performed. x86 instruction addresses are
// byte-addressable; no artificial alignment constraint is imposed.
[[nodiscard]] RegisteredSyscallTrapPlanResult
plan_registered_syscall_trap(
    astraea::memory::GuestAddress code_base,
    std::span<const std::byte> code,
    astraea::memory::GuestAddress guest_rip) noexcept;

// Applies the two-byte patch only after the complete target span and expected
// original bytes have been validated. Failure leaves the span unchanged.
[[nodiscard]] RegisteredSyscallTrapMutationResult
apply_registered_syscall_trap(
    std::span<std::byte> code,
    const RegisteredSyscallTrapSite& site) noexcept;

// Restores exactly the registered original bytes only when the expected UD2
// trap bytes are currently present. Failure leaves the span unchanged.
[[nodiscard]] RegisteredSyscallTrapMutationResult
restore_registered_syscall_trap(
    std::span<std::byte> code,
    const RegisteredSyscallTrapSite& site) noexcept;

[[nodiscard]] constexpr bool
registered_syscall_trap_matches_rip(
    const RegisteredSyscallTrapSite& site,
    astraea::memory::GuestAddress guest_rip) noexcept {
    return site.guest_rip == guest_rip;
}

}  // namespace astraea::execution
