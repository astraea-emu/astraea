#include <astraea/execution/memory_plan.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::execution {
namespace {

using astraea::memory::GuestAddress;
using astraea::memory::GuestPermission;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::MappingIntent;

struct PageSpan {
    std::uint64_t first_page;
    std::uint64_t last_page;
    std::uint8_t permission_bits;
    ExecutionMemorySource source;
};

[[nodiscard]] bool is_power_of_two(std::uint64_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] ExecutionPlanError error(
    ExecutionPlanErrorCode code,
    std::optional<GuestAddress> address = std::nullopt) {
    return ExecutionPlanError{.code = code, .guest_address = address};
}

[[nodiscard]] astraea::core::Result<PageSpan, ExecutionPlanError> to_page_span(
    const GuestRange& range,
    std::uint8_t permission_bits,
    ExecutionMemorySource source,
    std::uint64_t page_size) {
    using Result = astraea::core::Result<PageSpan, ExecutionPlanError>;

    if (range.empty()) {
        return Result::failure(
            error(
                ExecutionPlanErrorCode::host_page_arithmetic_overflow,
                range.base()));
    }

    const auto base = range.base().value();
    const auto last = base + (range.size().value() - 1U);

    return Result::success(
        PageSpan{
            .first_page = base / page_size,
            .last_page = last / page_size,
            .permission_bits = permission_bits,
            .source = source,
        });
}

[[nodiscard]] bool span_covers(
    const PageSpan& span,
    std::uint64_t page_index) noexcept {
    return span.first_page <= page_index && page_index <= span.last_page;
}

[[nodiscard]] bool source_less(
    const ExecutionMemorySource& lhs,
    const ExecutionMemorySource& rhs) noexcept {
    if (lhs.kind != rhs.kind) {
        return lhs.kind < rhs.kind;
    }
    return lhs.source_index < rhs.source_index;
}

}  // namespace

ExecutionMemoryPlanResult build_execution_memory_plan(
    const ExecutionMemoryPlanRequest& request) {
    try {
        const auto page_size = request.host_page_size;
        if (!is_power_of_two(page_size)) {
            return ExecutionMemoryPlanResult::failure(
                error(ExecutionPlanErrorCode::invalid_host_page_size));
        }

        std::vector<PageSpan> spans;
        spans.reserve(request.mappings.size() + 1U);

        for (const auto& mapping : request.mappings) {
            if (mapping.range.empty()) {
                continue;
            }

            auto span = to_page_span(
                mapping.range,
                mapping.permissions.bits(),
                ExecutionMemorySource{
                    .kind = ExecutionMemorySourceKind::mapping,
                    .source_index = mapping.source_index,
                },
                page_size);
            if (!span.has_value()) {
                return ExecutionMemoryPlanResult::failure(span.error());
            }
            spans.push_back(span.value());
        }

        if (!request.stack_storage.empty()) {
            auto stack_permissions = GuestPermissions::checked_from_bits(
                static_cast<std::uint8_t>(GuestPermission::read) |
                static_cast<std::uint8_t>(GuestPermission::write));
            if (!stack_permissions.has_value()) {
                return ExecutionMemoryPlanResult::failure(
                    error(ExecutionPlanErrorCode::permission_construction_failure));
            }

            auto stack_span = to_page_span(
                request.stack_storage,
                stack_permissions->bits(),
                ExecutionMemorySource{
                    .kind = ExecutionMemorySourceKind::initial_stack,
                    .source_index = 0,
                },
                page_size);
            if (!stack_span.has_value()) {
                return ExecutionMemoryPlanResult::failure(stack_span.error());
            }
            spans.push_back(stack_span.value());
        }

        if (spans.size() >
            std::numeric_limits<std::size_t>::max() / 2U) {
            return ExecutionMemoryPlanResult::failure(
                error(ExecutionPlanErrorCode::host_allocation_failure));
        }

        std::vector<std::uint64_t> boundaries;
        boundaries.reserve(spans.size() * 2U);

        const auto max_page_index =
            std::numeric_limits<std::uint64_t>::max() / page_size;

        for (const auto& span : spans) {
            boundaries.push_back(span.first_page);
            if (span.last_page < max_page_index) {
                boundaries.push_back(span.last_page + 1U);
            }
        }

        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(
            std::unique(boundaries.begin(), boundaries.end()),
            boundaries.end());

        std::vector<ExecutionMemoryRegion> regions;
        regions.reserve(boundaries.size());

        for (std::size_t i = 0; i < boundaries.size(); ++i) {
            const auto first_page = boundaries[i];
            const auto last_page =
                (i + 1U < boundaries.size())
                    ? boundaries[i + 1U] - 1U
                    : max_page_index;

            std::uint8_t permission_bits = 0;
            std::vector<ExecutionMemorySource> sources;

            for (const auto& span : spans) {
                if (!span_covers(span, first_page)) {
                    continue;
                }

                permission_bits |= span.permission_bits;
                sources.push_back(span.source);
            }

            if (sources.empty()) {
                continue;
            }

            const auto write_bit =
                static_cast<std::uint8_t>(GuestPermission::write);
            const auto execute_bit =
                static_cast<std::uint8_t>(GuestPermission::execute);
            if ((permission_bits & write_bit) != 0 &&
                (permission_bits & execute_bit) != 0) {
                const auto address = first_page * page_size;
                return ExecutionMemoryPlanResult::failure(
                    error(
                        ExecutionPlanErrorCode::mixed_write_execute_page,
                        GuestAddress{address}));
            }

            const auto page_count = last_page - first_page + 1U;
            if (page_count >
                std::numeric_limits<std::uint64_t>::max() / page_size) {
                return ExecutionMemoryPlanResult::failure(
                    error(
                        ExecutionPlanErrorCode::host_page_arithmetic_overflow,
                        GuestAddress{first_page * page_size}));
            }

            const auto byte_size = page_count * page_size;
            const auto base = first_page * page_size;
            auto range = GuestRange::create(
                GuestAddress{base},
                GuestSize{byte_size});
            if (!range.has_value()) {
                return ExecutionMemoryPlanResult::failure(
                    error(
                        ExecutionPlanErrorCode::host_page_arithmetic_overflow,
                        GuestAddress{base}));
            }

            auto permissions =
                GuestPermissions::checked_from_bits(permission_bits);
            if (!permissions.has_value()) {
                return ExecutionMemoryPlanResult::failure(
                    error(
                        ExecutionPlanErrorCode::permission_construction_failure,
                        GuestAddress{base}));
            }

            std::sort(sources.begin(), sources.end(), source_less);
            sources.erase(
                std::unique(sources.begin(), sources.end()),
                sources.end());

            regions.push_back(
                ExecutionMemoryRegion{
                    .range = range.value(),
                    .permissions = permissions.value(),
                    .sources = std::move(sources),
                });
        }

        const auto entry = request.entry_point;
        bool entry_mapped = false;
        bool entry_executable = false;
        for (const auto& mapping : request.mappings) {
            if (!mapping.range.contains(entry)) {
                continue;
            }

            entry_mapped = true;
            if (mapping.permissions.has(GuestPermission::execute)) {
                entry_executable = true;
            }
        }

        if (!entry_mapped) {
            return ExecutionMemoryPlanResult::failure(
                error(
                    ExecutionPlanErrorCode::entry_point_unmapped,
                    entry));
        }
        if (!entry_executable) {
            return ExecutionMemoryPlanResult::failure(
                error(
                    ExecutionPlanErrorCode::entry_point_not_executable,
                    entry));
        }

        if (!request.stack_storage.contains(request.stack_pointer)) {
            return ExecutionMemoryPlanResult::failure(
                error(
                    ExecutionPlanErrorCode::stack_pointer_unmapped,
                    request.stack_pointer));
        }

        return ExecutionMemoryPlanResult::success(
            ExecutionMemoryPlan{
                .host_page_size = page_size,
                .regions = std::move(regions),
            });
    } catch (const std::bad_alloc&) {
        return ExecutionMemoryPlanResult::failure(
            error(ExecutionPlanErrorCode::host_allocation_failure));
    } catch (const std::length_error&) {
        return ExecutionMemoryPlanResult::failure(
            error(ExecutionPlanErrorCode::host_allocation_failure));
    }
}

}  // namespace astraea::execution
