#include <astraea/execution/linux_memory.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

#if defined(__linux__) && defined(__x86_64__)
#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>
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

#if defined(__linux__) && defined(__x86_64__)

[[nodiscard]] bool to_host_size(
    astraea::memory::GuestSize size,
    std::size_t& output) noexcept {
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (size.value() >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            return false;
        }
    }

    output = static_cast<std::size_t>(size.value());
    return true;
}

[[nodiscard]] void* guest_pointer(
    astraea::memory::GuestAddress address) noexcept {
    static_assert(sizeof(std::uintptr_t) >= sizeof(std::uint64_t));
    return reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(address.value()));
}

[[nodiscard]] int final_protection(
    astraea::memory::GuestPermissions permissions) noexcept {
    int protection = PROT_NONE;
    if (permissions.has(astraea::memory::GuestPermission::read)) {
        protection |= PROT_READ;
    }
    if (permissions.has(astraea::memory::GuestPermission::write)) {
        protection |= PROT_WRITE;
    }
    if (permissions.has(astraea::memory::GuestPermission::execute)) {
        protection |= PROT_EXEC;
    }
    return protection;
}

[[nodiscard]] LinuxPreparedMemoryResult fail_with_cleanup(
    LinuxPreparedMemory prepared,
    NativeBackendError error) {
    static_cast<void>(prepared);
    return LinuxPreparedMemoryResult::failure(error);
}

#endif

}  // namespace

bool linux_native_memory_backend_available() noexcept {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    return true;
#else
    return false;
#endif
}

LinuxPreparedMemory::~LinuxPreparedMemory() {
    reset();
}

LinuxPreparedMemory::LinuxPreparedMemory(
    LinuxPreparedMemory&& other) noexcept
    : plan_(std::move(other.plan_)),
      mapped_regions_(std::move(other.mapped_regions_)) {
    other.mapped_regions_.clear();
    other.plan_ = ExecutionMemoryPlan{
        .host_page_size = 0,
        .regions = {},
    };
}

LinuxPreparedMemory& LinuxPreparedMemory::operator=(
    LinuxPreparedMemory&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    reset();
    plan_ = std::move(other.plan_);
    mapped_regions_ = std::move(other.mapped_regions_);
    other.mapped_regions_.clear();
    other.plan_ = ExecutionMemoryPlan{
        .host_page_size = 0,
        .regions = {},
    };
    return *this;
}

void LinuxPreparedMemory::reset() noexcept {
#if defined(__linux__) && defined(__x86_64__)
    for (auto it = mapped_regions_.rbegin();
         it != mapped_regions_.rend();
         ++it) {
        std::size_t size = 0;
        if (!to_host_size(it->size(), size) || size == 0) {
            continue;
        }
        static_cast<void>(
            ::munmap(guest_pointer(it->base()), size));
    }
#endif

    mapped_regions_.clear();
    plan_ = ExecutionMemoryPlan{
        .host_page_size = 0,
        .regions = {},
    };
}

LinuxPreparedMemoryResult prepare_linux_guest_memory(
    const astraea::loader::GuestImage& image) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    static_cast<void>(image);
    return LinuxPreparedMemoryResult::failure(
        backend_error(
            NativeBackendErrorCode::backend_unavailable));
#else
    try {
        const long raw_page_size = ::sysconf(_SC_PAGESIZE);
        if (raw_page_size <= 0) {
            return LinuxPreparedMemoryResult::failure(
                backend_error(
                    NativeBackendErrorCode::internal_transition_failure,
                    false,
                    0,
                    true,
                    static_cast<std::uint64_t>(errno)));
        }

        const auto page_size =
            static_cast<std::uint64_t>(raw_page_size);

        auto plan = build_execution_memory_plan(
            ExecutionMemoryPlanRequest{
                .mappings = image.mappings,
                .stack_storage = image.initial_stack.storage,
                .entry_point =
                    astraea::memory::GuestAddress{
                        image.elf.header.entry},
                .stack_pointer = image.initial_stack.rsp,
                .host_page_size = page_size,
            });
        if (!plan.has_value()) {
            NativeBackendErrorCode code =
                NativeBackendErrorCode::internal_transition_failure;
            switch (plan.error().code) {
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

            return LinuxPreparedMemoryResult::failure(
                backend_error(
                    code,
                    plan.error().guest_address.has_value(),
                    plan.error().guest_address.has_value()
                        ? plan.error().guest_address->value()
                        : 0));
        }

        LinuxPreparedMemory prepared;
        prepared.plan_ = std::move(plan.value());
        prepared.mapped_regions_.reserve(
            prepared.plan_.regions.size());

        for (const auto& region : prepared.plan_.regions) {
            std::size_t size = 0;
            if (!to_host_size(region.range.size(), size) ||
                size == 0) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::host_page_arithmetic_overflow,
                        true,
                        region.range.base().value()));
            }

            void* const requested =
                guest_pointer(region.range.base());
            errno = 0;
            void* const mapped = ::mmap(
                requested,
                size,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE |
                    MAP_ANONYMOUS |
                    MAP_FIXED_NOREPLACE,
                -1,
                0);

            if (mapped == MAP_FAILED) {
                const int host_error = errno;
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        host_error == EEXIST
                            ? NativeBackendErrorCode::guest_address_unavailable
                            : NativeBackendErrorCode::host_mapping_failure,
                        true,
                        region.range.base().value(),
                        true,
                        static_cast<std::uint64_t>(host_error)));
            }

            if (mapped != requested) {
                static_cast<void>(::munmap(mapped, size));
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::guest_address_unavailable,
                        true,
                        region.range.base().value()));
            }

            prepared.mapped_regions_.push_back(
                region.range);
        }

        auto initialized_view =
            image.initialized_image_view();
        if (!initialized_view.has_value()) {
            return fail_with_cleanup(
                std::move(prepared),
                backend_error(
                    NativeBackendErrorCode::internal_transition_failure));
        }

        for (const auto& mapping : image.mappings) {
            if (mapping.range.empty()) {
                continue;
            }

            std::size_t size = 0;
            if (!to_host_size(mapping.range.size(), size)) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::host_page_arithmetic_overflow,
                        true,
                        mapping.range.base().value()));
            }

            auto* const destination =
                static_cast<std::byte*>(
                    guest_pointer(mapping.range.base()));
            auto copied = initialized_view->copy_bytes(
                mapping.range,
                std::span<std::byte>{destination, size});
            if (!copied.has_value()) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::internal_transition_failure,
                        copied.error().guest_address.has_value(),
                        copied.error().guest_address.has_value()
                            ? copied.error().guest_address->value()
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
                    NativeBackendErrorCode::stack_mapping_failure,
                    true,
                    image.initial_stack.used_range.base().value()));
        }

        if (stack_byte_count > 0) {
            const auto used_base =
                image.initial_stack.used_range.base();
            auto used_last =
                astraea::memory::GuestAddress::checked_add(
                    used_base,
                    astraea::memory::GuestSize{
                        stack_byte_count - 1U});
            if (!used_last.has_value() ||
                !image.initial_stack.storage.contains(used_base) ||
                !image.initial_stack.storage.contains(
                    used_last.value())) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::stack_mapping_failure,
                        true,
                        used_base.value()));
            }

            auto* const stack_destination =
                static_cast<std::byte*>(
                    guest_pointer(used_base));
            std::memcpy(
                stack_destination,
                image.initial_stack.bytes.data(),
                image.initial_stack.bytes.size());
        }

        for (const auto& region : prepared.plan_.regions) {
            std::size_t size = 0;
            if (!to_host_size(region.range.size(), size) ||
                size == 0) {
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::host_page_arithmetic_overflow,
                        true,
                        region.range.base().value()));
            }

            void* const address =
                guest_pointer(region.range.base());
            const int protection =
                final_protection(region.permissions);

            errno = 0;
            if (::mprotect(address, size, protection) != 0) {
                const int host_error = errno;
                return fail_with_cleanup(
                    std::move(prepared),
                    backend_error(
                        NativeBackendErrorCode::host_protection_failure,
                        true,
                        region.range.base().value(),
                        true,
                        static_cast<std::uint64_t>(host_error)));
            }

#if defined(__GNUC__) || defined(__clang__)
            if (region.permissions.has(
                    astraea::memory::GuestPermission::execute)) {
                auto* const begin =
                    static_cast<char*>(address);
                auto* const end = begin + size;
                __builtin___clear_cache(begin, end);
            }
#endif
        }

        return LinuxPreparedMemoryResult::success(
            std::move(prepared));
    } catch (const std::bad_alloc&) {
        return LinuxPreparedMemoryResult::failure(
            backend_error(
                NativeBackendErrorCode::host_mapping_failure));
    } catch (const std::length_error&) {
        return LinuxPreparedMemoryResult::failure(
            backend_error(
                NativeBackendErrorCode::host_page_arithmetic_overflow));
    }
#endif
}

}  // namespace astraea::execution
