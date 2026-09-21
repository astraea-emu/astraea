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

[[nodiscard]] ExecutionMemoryPlanResult build_execution_memory_plan(
    const ExecutionMemoryPlanRequest& request);

}  // namespace astraea::execution
