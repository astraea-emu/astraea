#include <astraea/execution/windows_memory.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(_M_X64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace astraea::execution {
namespace {

[[nodiscard]] NativeBackendError backend_error(
    NativeBackendErrorCode code,
    bool has_guest_address = false,
    std::uint64_t guest_address = 0,
    bool has_host_code = false,
    std::uint64_t host_code = 0) noexcept {
    return NativeBackendError{
        .code = code,
        .has_guest_address = has_guest_address,
        .guest_address = guest_address,
        .has_host_code = has_host_code,
        .host_code = host_code,
    };
}

#if defined(_WIN32) && defined(_M_X64)

[[nodiscard]] NativeBackendError plan_error(
    const ExecutionPlanError& error) noexcept {
    NativeBackendErrorCode code =
        NativeBackendErrorCode::internal_transition_failure;
    switch (error.code) {
    case ExecutionPlanErrorCode::invalid_host_page_size:
    case ExecutionPlanErrorCode::host_page_arithmetic_overflow:
        code =
            NativeBackendErrorCode::host_page_arithmetic_overflow;
        break;
    case ExecutionPlanErrorCode::mixed_write_execute_page:
        code =
            NativeBackendErrorCode::mixed_write_execute_page;
        break;
    case ExecutionPlanErrorCode::entry_point_unmapped:
        code =
            NativeBackendErrorCode::entry_point_unmapped;
        break;
    case ExecutionPlanErrorCode::entry_point_not_executable:
        code =
            NativeBackendErrorCode::entry_point_not_executable;
        break;
    case ExecutionPlanErrorCode::stack_pointer_unmapped:
        code =
            NativeBackendErrorCode::stack_pointer_unmapped;
        break;
    case ExecutionPlanErrorCode::permission_construction_failure:
    case ExecutionPlanErrorCode::host_allocation_failure:
        code =
            NativeBackendErrorCode::internal_transition_failure;
        break;
    }

    return backend_error(
        code,
        error.guest_address.has_value(),
        error.guest_address.has_value()
            ? error.guest_address->value()
            : 0);
}

#endif

#if defined(_WIN32) && defined(_M_X64)

[[nodiscard]] bool to_host_size(
    astraea::memory::GuestSize size,
    std::size_t& output) noexcept {
    if constexpr (
        sizeof(std::size_t) <
        sizeof(std::uint64_t)) {
        if (size.value() >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return false;
        }
    }

    output =
        static_cast<std::size_t>(
            size.value());
    return true;
}

[[nodiscard]] void* guest_pointer(
    astraea::memory::GuestAddress address) noexcept {
    static_assert(
        sizeof(std::uintptr_t) >=
        sizeof(std::uint64_t));
    return reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(
            address.value()));
}

[[nodiscard]] DWORD final_protection(
    astraea::memory::GuestPermissions permissions) noexcept {
    const bool read =
        permissions.has(
            astraea::memory::GuestPermission::read);
    const bool write =
        permissions.has(
            astraea::memory::GuestPermission::write);
    const bool execute =
        permissions.has(
            astraea::memory::GuestPermission::execute);

    if (execute) {
        return read
            ? PAGE_EXECUTE_READ
            : PAGE_EXECUTE;
    }
    if (write) {
        return PAGE_READWRITE;
    }
    if (read) {
        return PAGE_READONLY;
    }
    return PAGE_NOACCESS;
}

[[nodiscard]] WindowsPreparedMemoryResult fail_with_cleanup(
    WindowsPreparedMemory prepared,
    NativeBackendError error) {
    static_cast<void>(prepared);
    return WindowsPreparedMemoryResult::failure(
        error);
}

[[nodiscard]] astraea::core::Result<
    std::vector<astraea::memory::GuestRange>,
    NativeBackendError>
build_reservation_ranges(
    const ExecutionMemoryPlan& plan,
    std::uint64_t allocation_granularity) {
    using Result =
        astraea::core::Result<
            std::vector<astraea::memory::GuestRange>,
            NativeBackendError>;

    if (allocation_granularity == 0 ||
        (allocation_granularity &
         (allocation_granularity - 1U)) != 0) {
        return Result::failure(
            backend_error(
                NativeBackendErrorCode::
                    host_page_arithmetic_overflow));
    }

    try {
        std::vector<astraea::memory::GuestRange>
            ranges;
        ranges.reserve(plan.regions.size());

        for (const auto& region : plan.regions) {
            if (region.range.empty()) {
                continue;
            }

            const std::uint64_t base =
                region.range.base().value();
            const std::uint64_t size =
                region.range.size().value();
            const std::uint64_t last =
                base + (size - 1U);
            const std::uint64_t reserve_base =
                (base / allocation_granularity) *
                allocation_granularity;
            const std::uint64_t last_block =
                last / allocation_granularity;

            if (last_block >
                std::numeric_limits<std::uint64_t>::max() /
                    allocation_granularity) {
                return Result::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        base));
            }

            const std::uint64_t reserve_last_base =
                last_block *
                allocation_granularity;
            if (reserve_last_base >
                std::numeric_limits<std::uint64_t>::max() -
                    allocation_granularity) {
                return Result::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        base));
            }

            const std::uint64_t reserve_end =
                reserve_last_base +
                allocation_granularity;
            const std::uint64_t reserve_size =
                reserve_end - reserve_base;

            auto reserve_range =
                astraea::memory::GuestRange::create(
                    astraea::memory::GuestAddress{
                        reserve_base},
                    astraea::memory::GuestSize{
                        reserve_size});
            if (!reserve_range.has_value()) {
                return Result::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        reserve_base));
            }

            ranges.push_back(
                reserve_range.value());
        }

        std::sort(
            ranges.begin(),
            ranges.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.base().value() <
                    rhs.base().value();
            });

        std::vector<astraea::memory::GuestRange>
            merged;
        for (const auto& range : ranges) {
            if (merged.empty()) {
                merged.push_back(range);
                continue;
            }

            auto& previous = merged.back();
            const std::uint64_t previous_end =
                previous.base().value() +
                previous.size().value();
            const std::uint64_t range_end =
                range.base().value() +
                range.size().value();

            if (range.base().value() >
                previous_end) {
                merged.push_back(range);
                continue;
            }

            const std::uint64_t merged_end =
                std::max(
                    previous_end,
                    range_end);
            auto merged_range =
                astraea::memory::GuestRange::create(
                    previous.base(),
                    astraea::memory::GuestSize{
                        merged_end -
                        previous.base().value()});
            if (!merged_range.has_value()) {
                return Result::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        previous.base().value()));
            }
            previous = merged_range.value();
        }

        return Result::success(
            std::move(merged));
    } catch (const std::bad_alloc&) {
        return Result::failure(
            backend_error(
                NativeBackendErrorCode::
                    host_mapping_failure));
    } catch (const std::length_error&) {
        return Result::failure(
            backend_error(
                NativeBackendErrorCode::
                    host_page_arithmetic_overflow));
    }
}

#endif

}  // namespace

bool windows_native_memory_backend_available() noexcept {
#if defined(_WIN32) && defined(_M_X64)
    return true;
#else
    return false;
#endif
}

WindowsPreparedMemory::~WindowsPreparedMemory() {
    reset();
}

WindowsPreparedMemory::WindowsPreparedMemory(
    WindowsPreparedMemory&& other) noexcept
    : plan_(std::move(other.plan_)),
      reservation_ranges_(
          std::move(
              other.reservation_ranges_)) {
    other.reservation_ranges_.clear();
    other.plan_ = ExecutionMemoryPlan{
        .host_page_size = 0,
        .regions = {},
    };
}

WindowsPreparedMemory& WindowsPreparedMemory::operator=(
    WindowsPreparedMemory&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    reset();
    plan_ = std::move(other.plan_);
    reservation_ranges_ =
        std::move(
            other.reservation_ranges_);
    other.reservation_ranges_.clear();
    other.plan_ = ExecutionMemoryPlan{
        .host_page_size = 0,
        .regions = {},
    };
    return *this;
}

void WindowsPreparedMemory::reset() noexcept {
#if defined(_WIN32) && defined(_M_X64)
    for (auto it =
             reservation_ranges_.rbegin();
         it != reservation_ranges_.rend();
         ++it) {
        static_cast<void>(
            ::VirtualFree(
                guest_pointer(it->base()),
                0,
                MEM_RELEASE));
    }
#endif

    reservation_ranges_.clear();
    plan_ = ExecutionMemoryPlan{
        .host_page_size = 0,
        .regions = {},
    };
}

WindowsPreparedMemoryResult prepare_windows_guest_memory(
    const astraea::loader::GuestImage& image) {
#if !(defined(_WIN32) && defined(_M_X64))
    static_cast<void>(image);
    return WindowsPreparedMemoryResult::failure(
        backend_error(
            NativeBackendErrorCode::
                backend_unavailable));
#else
    try {
        SYSTEM_INFO system_info{};
        ::GetSystemInfo(&system_info);

        const auto page_size =
            static_cast<std::uint64_t>(
                system_info.dwPageSize);
        const auto allocation_granularity =
            static_cast<std::uint64_t>(
                system_info.dwAllocationGranularity);

        auto plan =
            build_execution_memory_plan(
                ExecutionMemoryPlanRequest{
                    .mappings = image.mappings,
                    .stack_storage =
                        image.initial_stack.storage,
                    .entry_point =
                        astraea::memory::GuestAddress{
                            image.elf.header.entry},
                    .stack_pointer =
                        image.initial_stack.rsp,
                    .host_page_size =
                        page_size,
                });
        if (!plan.has_value()) {
            return WindowsPreparedMemoryResult::failure(
                plan_error(
                    plan.error()));
        }

        auto reservations =
            build_reservation_ranges(
                plan.value(),
                allocation_granularity);
        if (!reservations.has_value()) {
            return WindowsPreparedMemoryResult::failure(
                reservations.error());
        }

        WindowsPreparedMemory prepared;
        prepared.plan_ =
            std::move(plan.value());
        prepared.reservation_ranges_.reserve(
            reservations->size());

        for (const auto& reservation :
             reservations.value()) {
            std::size_t size = 0;
            if (!to_host_size(
                    reservation.size(),
                    size) ||
                size == 0) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        reservation.base().value()));
            }

            void* const requested =
                guest_pointer(
                    reservation.base());
            ::SetLastError(ERROR_SUCCESS);
            void* const reserved =
                ::VirtualAlloc(
                    requested,
                    size,
                    MEM_RESERVE,
                    PAGE_NOACCESS);
            if (reserved == nullptr) {
                const DWORD host_error =
                    ::GetLastError();
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        host_error ==
                                ERROR_INVALID_ADDRESS
                            ? NativeBackendErrorCode::
                                  guest_address_unavailable
                            : NativeBackendErrorCode::
                                  host_mapping_failure,
                        true,
                        reservation.base().value(),
                        true,
                        host_error));
            }

            if (reserved != requested) {
                static_cast<void>(
                    ::VirtualFree(
                        reserved,
                        0,
                        MEM_RELEASE));
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            guest_address_unavailable,
                        true,
                        reservation.base().value()));
            }

            prepared.reservation_ranges_.push_back(
                reservation);
        }

        for (const auto& region :
             prepared.plan_.regions) {
            std::size_t size = 0;
            if (!to_host_size(
                    region.range.size(),
                    size) ||
                size == 0) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        region.range.base().value()));
            }

            void* const requested =
                guest_pointer(
                    region.range.base());
            ::SetLastError(ERROR_SUCCESS);
            void* const committed =
                ::VirtualAlloc(
                    requested,
                    size,
                    MEM_COMMIT,
                    PAGE_READWRITE);
            if (committed == nullptr) {
                const DWORD host_error =
                    ::GetLastError();
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            host_mapping_failure,
                        true,
                        region.range.base().value(),
                        true,
                        host_error));
            }

            if (committed != requested) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            guest_address_unavailable,
                        true,
                        region.range.base().value()));
            }
        }

        auto initialized_view =
            image.initialized_image_view();
        if (!initialized_view.has_value()) {
            return fail_with_cleanup(
                std::move(prepared),
                backend_error(
                    NativeBackendErrorCode::
                        internal_transition_failure));
        }

        for (const auto& mapping :
             image.mappings) {
            if (mapping.range.empty()) {
                continue;
            }

            std::size_t size = 0;
            if (!to_host_size(
                    mapping.range.size(),
                    size)) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        mapping.range.base().value()));
            }

            auto* const destination =
                static_cast<std::byte*>(
                    guest_pointer(
                        mapping.range.base()));
            auto copied =
                initialized_view->copy_bytes(
                    mapping.range,
                    std::span<std::byte>{
                        destination,
                        size});
            if (!copied.has_value()) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            internal_transition_failure,
                        copied.error().
                            guest_address.has_value(),
                        copied.error().
                                guest_address.has_value()
                            ? copied.error().
                                  guest_address->value()
                            : 0));
            }
        }

        const auto stack_byte_count =
            static_cast<std::uint64_t>(
                image.initial_stack.bytes.size());
        if (image.initial_stack.used_range.size().value() !=
            stack_byte_count) {
            return fail_with_cleanup(
                std::move(prepared),
                backend_error(
                    NativeBackendErrorCode::
                        stack_mapping_failure,
                    true,
                    image.initial_stack.
                        used_range.base().value()));
        }

        if (stack_byte_count != 0) {
            const auto used_base =
                image.initial_stack.
                    used_range.base();
            auto used_last =
                astraea::memory::GuestAddress::
                    checked_add(
                        used_base,
                        astraea::memory::GuestSize{
                            stack_byte_count - 1U});
            if (!used_last.has_value() ||
                !image.initial_stack.storage.contains(
                    used_base) ||
                !image.initial_stack.storage.contains(
                    used_last.value())) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            stack_mapping_failure,
                        true,
                        used_base.value()));
            }

            std::memcpy(
                guest_pointer(used_base),
                image.initial_stack.bytes.data(),
                image.initial_stack.bytes.size());
        }

        for (const auto& region :
             prepared.plan_.regions) {
            std::size_t size = 0;
            if (!to_host_size(
                    region.range.size(),
                    size) ||
                size == 0) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        region.range.base().value()));
            }

            DWORD old_protection = 0;
            void* const address =
                guest_pointer(
                    region.range.base());
            ::SetLastError(ERROR_SUCCESS);
            if (::VirtualProtect(
                    address,
                    size,
                    final_protection(
                        region.permissions),
                    &old_protection) == 0) {
                const DWORD host_error =
                    ::GetLastError();
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::
                            host_protection_failure,
                        true,
                        region.range.base().value(),
                        true,
                        host_error));
            }

            if (region.permissions.has(
                    astraea::memory::
                        GuestPermission::execute)) {
                ::SetLastError(ERROR_SUCCESS);
                if (::FlushInstructionCache(
                        ::GetCurrentProcess(),
                        address,
                        size) == 0) {
                    const DWORD host_error =
                        ::GetLastError();
                    return fail_with_cleanup(
                        std::move(prepared),
                        backend_error(
                            NativeBackendErrorCode::
                                instruction_cache_sync_failure,
                            true,
                            region.range.base().value(),
                            true,
                            host_error));
                }
            }
        }

        return WindowsPreparedMemoryResult::success(
            std::move(prepared));
    } catch (const std::bad_alloc&) {
        return WindowsPreparedMemoryResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    host_mapping_failure));
    } catch (const std::length_error&) {
        return WindowsPreparedMemoryResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    host_page_arithmetic_overflow));
    }
#endif
}

}  // namespace astraea::execution
