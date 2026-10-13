#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/mapping.hpp>

namespace astraea::execution {

enum class ExecutionMemorySourceKind {
    mapping,
    initial_stack,
};

struct ExecutionMemorySource {
    ExecutionMemorySourceKind kind;
    std::size_t source_index;

    auto operator<=>(const ExecutionMemorySource&) const = default;
};

struct ExecutionMemoryRegion {
    astraea::memory::GuestRange range;
    astraea::memory::GuestPermissions permissions;
    std::vector<ExecutionMemorySource> sources;

    auto operator<=>(const ExecutionMemoryRegion&) const = default;
};

struct ExecutionMemoryPlan {
    std::uint64_t host_page_size;
    std::vector<ExecutionMemoryRegion> regions;

    auto operator<=>(const ExecutionMemoryPlan&) const = default;
};

struct ExecutionMemoryPlanRequest {
    std::span<const astraea::memory::MappingIntent> mappings;
    astraea::memory::GuestRange stack_storage;
    astraea::memory::GuestAddress entry_point;
    astraea::memory::GuestAddress stack_pointer;
    std::uint64_t host_page_size;
};

enum class ExecutionPlanErrorCode {
    invalid_host_page_size,
    host_page_arithmetic_overflow,
    mixed_write_execute_page,
    entry_point_unmapped,
    entry_point_not_executable,
    stack_pointer_unmapped,
    permission_construction_failure,
    host_allocation_failure,
};

struct ExecutionPlanError {
    ExecutionPlanErrorCode code;
    std::optional<astraea::memory::GuestAddress> guest_address;
};

using ExecutionMemoryPlanResult =
    astraea::core::Result<ExecutionMemoryPlan, ExecutionPlanError>;

// Internal nonzero identity for each successfully prepared native guest
// mapping *owner*. It is not a Sony runtime module ID, a guest VA, or a
// memory safety pin. An exhausted epoch source refuses new issuance.
[[nodiscard]] std::uint64_t next_prepared_mapping_epoch() noexcept;

[[nodiscard]] ExecutionMemoryPlanResult build_execution_memory_plan(
    const ExecutionMemoryPlanRequest& request);

// Research-facing, pure authorization for ONE prospective guest write.
// Requires both an exact writable guest mapping/stack range AND writable,
// non-executable host-page backing in a trusted prepared memory plan.
// Page-rounded permission alone never grants writes into guest-VA padding.
// No allocation, mutation, host pointer, ISA decode, or general sandbox.
[[nodiscard]] bool guest_write_owned_by_plan(
    const ExecutionMemoryPlan& plan,
    std::span<const astraea::memory::MappingIntent> guest_mappings,
    astraea::memory::GuestRange guest_stack,
    astraea::memory::GuestAddress target,
    std::size_t byte_count) noexcept;

}  // namespace astraea::execution
