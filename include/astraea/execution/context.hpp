#pragma once

#include <compare>
#include <cstdint>

#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

struct GuestCpuContext {
    std::uint64_t rax = 0;
    std::uint64_t rbx = 0;
    std::uint64_t rcx = 0;
    std::uint64_t rdx = 0;
    std::uint64_t rsi = 0;
    std::uint64_t rdi = 0;
    std::uint64_t rbp = 0;
    std::uint64_t rsp = 0;
    std::uint64_t r8 = 0;
    std::uint64_t r9 = 0;
    std::uint64_t r10 = 0;
    std::uint64_t r11 = 0;
    std::uint64_t r12 = 0;
    std::uint64_t r13 = 0;
    std::uint64_t r14 = 0;
    std::uint64_t r15 = 0;
    std::uint64_t rip = 0;
    std::uint64_t rflags = 0;
    std::uint64_t fs_base = 0;
    std::uint64_t gs_base = 0;

    auto operator<=>(const GuestCpuContext&) const = default;
};

inline constexpr std::uint64_t kSyntheticInitialRflags = 0x2U;

[[nodiscard]] GuestCpuContext make_synthetic_initial_context(
    std::uint64_t entry_point,
    astraea::memory::GuestAddress stack_pointer) noexcept;

[[nodiscard]] GuestCpuContext make_synthetic_initial_context(
    const astraea::loader::GuestImage& image) noexcept;

enum class GuestFaultKind {
    access_violation,
    illegal_instruction,
    arithmetic,
    breakpoint_or_trap,
    unknown,
};

struct GuestFault {
    GuestFaultKind kind = GuestFaultKind::unknown;
    std::uint64_t instruction_pointer = 0;
    std::uint64_t stack_pointer = 0;
    bool has_fault_address = false;
    std::uint64_t fault_address = 0;
    std::uint64_t host_code = 0;

    auto operator<=>(const GuestFault&) const = default;
};

enum class ExecutionStopReason {
    host_gate,
    guest_fault,
    backend_error,
};

struct ExecutionStop {
    ExecutionStopReason reason = ExecutionStopReason::backend_error;
    GuestCpuContext context;
    bool has_fault = false;
    GuestFault fault;

    auto operator<=>(const ExecutionStop&) const = default;
};

}  // namespace astraea::execution
