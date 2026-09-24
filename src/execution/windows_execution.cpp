#include <astraea/execution/windows_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>
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

constexpr std::uint64_t kRflagsTrap =
    1ULL << 8U;
constexpr std::uint64_t kRflagsDirection =
    1ULL << 10U;
constexpr std::uint64_t kMinimumWindowsExceptionHeadroom =
    64ULL * 1024ULL;

struct ExecutableRange {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

struct alignas(16) WindowsTransitionFrame {
    std::uint64_t host_rsp = 0;
    std::uint64_t host_rbx = 0;
    std::uint64_t host_rbp = 0;
    std::uint64_t host_rsi = 0;
    std::uint64_t host_rdi = 0;
    std::uint64_t host_r12 = 0;
    std::uint64_t host_r13 = 0;
    std::uint64_t host_r14 = 0;
    std::uint64_t host_r15 = 0;
    std::array<std::byte, 160> xmm6_to_xmm15{};
    std::uint32_t host_mxcsr = 0;
    std::uint16_t host_x87_control = 0;
    std::uint16_t reserved = 0;
};

static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_rsp) == 0);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_rbx) == 8);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_rbp) == 16);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_rsi) == 24);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_rdi) == 32);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_r12) == 40);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_r13) == 48);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_r14) == 56);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_r15) == 64);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        xmm6_to_xmm15) == 72);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_mxcsr) == 232);
static_assert(
    offsetof(
        WindowsTransitionFrame,
        host_x87_control) == 236);

static_assert(
    offsetof(GuestCpuContext, rax) == 0);
static_assert(
    offsetof(GuestCpuContext, rbx) == 8);
static_assert(
    offsetof(GuestCpuContext, rcx) == 16);
static_assert(
    offsetof(GuestCpuContext, rdx) == 24);
static_assert(
    offsetof(GuestCpuContext, rsi) == 32);
static_assert(
    offsetof(GuestCpuContext, rdi) == 40);
static_assert(
    offsetof(GuestCpuContext, rbp) == 48);
static_assert(
    offsetof(GuestCpuContext, rsp) == 56);
static_assert(
    offsetof(GuestCpuContext, r8) == 64);
static_assert(
    offsetof(GuestCpuContext, r9) == 72);
static_assert(
    offsetof(GuestCpuContext, r10) == 80);
static_assert(
    offsetof(GuestCpuContext, r11) == 88);
static_assert(
    offsetof(GuestCpuContext, r12) == 96);
static_assert(
    offsetof(GuestCpuContext, r13) == 104);
static_assert(
    offsetof(GuestCpuContext, r14) == 112);
static_assert(
    offsetof(GuestCpuContext, r15) == 120);
static_assert(
    offsetof(GuestCpuContext, rip) == 128);
static_assert(
    offsetof(GuestCpuContext, rflags) == 136);

extern "C" void astraea_windows_enter_guest_context(
    const GuestCpuContext* context,
    WindowsTransitionFrame* frame) noexcept;

extern "C" void astraea_windows_recover_guest_context() noexcept;

struct WindowsExceptionFrame {
    const ExecutableRange* executable_ranges = nullptr;
    std::size_t executable_range_count = 0;
    std::uint64_t gate_base = 0;
    std::uint32_t gate_slot_count = 0;
    std::uint32_t reserved = 0;
    const RegisteredSyscallTrapSite*
        registered_syscall_traps = nullptr;
    std::size_t registered_syscall_trap_count = 0;
    WindowsTransitionFrame* transition = nullptr;
    ExecutionStop stop;
};

thread_local WindowsExceptionFrame*
    g_active_frame = nullptr;
std::mutex g_windows_execution_mutex;

[[nodiscard]] bool range_contains(
    const ExecutableRange& range,
    std::uint64_t address) noexcept {
    if (range.size == 0 ||
        address < range.base) {
        return false;
    }

    return address - range.base <
        range.size;
}

[[nodiscard]] bool frame_owns_guest_rip(
    const WindowsExceptionFrame& frame,
    std::uint64_t rip) noexcept {
    for (std::size_t i = 0;
         i < frame.executable_range_count;
         ++i) {
        if (range_contains(
                frame.executable_ranges[i],
                rip)) {
            return true;
        }
    }

    const std::uint64_t gate_size =
        static_cast<std::uint64_t>(
            frame.gate_slot_count) *
        kSyntheticGateStride;
    return range_contains(
        ExecutableRange{
            .base = frame.gate_base,
            .size = gate_size,
        },
        rip);
}

[[nodiscard]] bool recognize_gate_slot(
    const WindowsExceptionFrame& frame,
    std::uint64_t rip,
    std::uint32_t& slot) noexcept {
    if (rip < frame.gate_base) {
        return false;
    }

    const std::uint64_t offset =
        rip - frame.gate_base;
    if ((offset %
         kSyntheticGateStride) != 0) {
        return false;
    }

    const std::uint64_t candidate =
        offset /
        kSyntheticGateStride;
    if (candidate >=
        frame.gate_slot_count) {
        return false;
    }

    slot =
        static_cast<std::uint32_t>(
            candidate);
    return true;
}

[[nodiscard]] bool
recognize_registered_syscall_trap(
    const WindowsExceptionFrame& frame,
    std::uint64_t rip) noexcept {
    for (std::size_t i = 0;
         i < frame.registered_syscall_trap_count;
         ++i) {
        if (registered_syscall_trap_matches_rip(
                frame.registered_syscall_traps[i],
                astraea::memory::GuestAddress{rip})) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] GuestFaultKind fault_kind(
    DWORD exception_code) noexcept {
    switch (exception_code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
        return GuestFaultKind::
            access_violation;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return GuestFaultKind::
            illegal_instruction;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
        return GuestFaultKind::arithmetic;
    case EXCEPTION_BREAKPOINT:
    case EXCEPTION_SINGLE_STEP:
        return GuestFaultKind::
            breakpoint_or_trap;
    default:
        return GuestFaultKind::unknown;
    }
}

void capture_guest_context(
    const CONTEXT& context,
    GuestCpuContext& guest) noexcept {
    guest.rax = context.Rax;
    guest.rbx = context.Rbx;
    guest.rcx = context.Rcx;
    guest.rdx = context.Rdx;
    guest.rsi = context.Rsi;
    guest.rdi = context.Rdi;
    guest.rbp = context.Rbp;
    guest.rsp = context.Rsp;
    guest.r8 = context.R8;
    guest.r9 = context.R9;
    guest.r10 = context.R10;
    guest.r11 = context.R11;
    guest.r12 = context.R12;
    guest.r13 = context.R13;
    guest.r14 = context.R14;
    guest.r15 = context.R15;
    guest.rip = context.Rip;
    guest.rflags =
        static_cast<std::uint64_t>(
            context.EFlags);

    // M2 v0 does not install guest FS/GS bases.
    guest.fs_base = 0;
    guest.gs_base = 0;
}

[[nodiscard]] bool has_fault_address(
    const EXCEPTION_RECORD& record) noexcept {
    return
        (record.ExceptionCode ==
             EXCEPTION_ACCESS_VIOLATION ||
         record.ExceptionCode ==
             EXCEPTION_IN_PAGE_ERROR) &&
        record.NumberParameters >= 2;
}

[[nodiscard]] LONG CALLBACK
windows_guest_exception_handler(
    EXCEPTION_POINTERS* pointers) noexcept {
    auto* const frame =
        g_active_frame;
    if (frame == nullptr ||
        pointers == nullptr ||
        pointers->ExceptionRecord ==
            nullptr ||
        pointers->ContextRecord ==
            nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto& context =
        *pointers->ContextRecord;
    const auto& record =
        *pointers->ExceptionRecord;
    const std::uint64_t rip =
        context.Rip;

    if (!frame_owns_guest_rip(
            *frame,
            rip)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    capture_guest_context(
        context,
        frame->stop.context);

    std::uint32_t gate_slot = 0;
    if (record.ExceptionCode ==
            EXCEPTION_ILLEGAL_INSTRUCTION &&
        recognize_gate_slot(
            *frame,
            rip,
            gate_slot)) {
        frame->stop.reason =
            ExecutionStopReason::host_gate;
        frame->stop.has_gate_slot = true;
        frame->stop.gate_slot =
            gate_slot;
        frame->stop.has_fault = false;
    } else if (
        record.ExceptionCode ==
            EXCEPTION_ILLEGAL_INSTRUCTION &&
        recognize_registered_syscall_trap(
            *frame,
            rip)) {
        frame->stop.reason =
            ExecutionStopReason::
                registered_syscall_trap;
        frame->stop.has_gate_slot = false;
        frame->stop.gate_slot = 0;
        frame->stop.has_fault = false;
    } else {
        frame->stop.reason =
            ExecutionStopReason::guest_fault;
        frame->stop.has_gate_slot = false;
        frame->stop.gate_slot = 0;
        frame->stop.has_fault = true;

        const bool address_present =
            has_fault_address(record);
        frame->stop.fault =
            GuestFault{
                .kind =
                    fault_kind(
                        record.ExceptionCode),
                .instruction_pointer = rip,
                .stack_pointer =
                    context.Rsp,
                .has_fault_address =
                    address_present,
                .fault_address =
                    address_present
                        ? static_cast<
                              std::uint64_t>(
                              record.
                                  ExceptionInformation[1])
                        : 0,
                .host_code =
                    static_cast<std::uint64_t>(
                        record.ExceptionCode),
            };
    }

    context.Rip =
        static_cast<DWORD64>(
            reinterpret_cast<
                std::uintptr_t>(
                &astraea_windows_recover_guest_context));
    context.Rsp =
        frame->transition->host_rsp;
    context.Rcx =
        static_cast<DWORD64>(
            reinterpret_cast<
                std::uintptr_t>(
                frame->transition));

    return EXCEPTION_CONTINUE_EXECUTION;
}

[[nodiscard]] bool exact_executable_contains(
    const astraea::loader::GuestImage& image,
    std::uint64_t address) noexcept {
    for (const auto& mapping :
         image.mappings) {
        if (mapping.permissions.has(
                astraea::memory::
                    GuestPermission::execute) &&
            mapping.range.contains(
                astraea::memory::GuestAddress{
                    address})) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool registered_traps_are_valid(
    const astraea::loader::GuestImage& image,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps) noexcept {
    for (std::size_t i = 0;
         i < registered_syscall_traps.size();
         ++i) {
        const auto& site =
            registered_syscall_traps[i];
        if (site.original_bytes !=
                kX86SyscallBytes ||
            site.trap_bytes !=
                kX86Ud2Bytes) {
            return false;
        }

        const auto rip = site.guest_rip.value();
        if (rip ==
                std::numeric_limits<
                    std::uint64_t>::max() ||
            !exact_executable_contains(
                image,
                rip) ||
            !exact_executable_contains(
                image,
                rip + 1U)) {
            return false;
        }

        if (rip >
            static_cast<std::uint64_t>(
                std::numeric_limits<
                    std::uintptr_t>::max())) {
            return false;
        }

        std::array<std::byte, 2> mapped_bytes{};
        std::memcpy(
            mapped_bytes.data(),
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(
                    rip)),
            mapped_bytes.size());
        if (mapped_bytes !=
            kX86Ud2Bytes) {
            return false;
        }

        for (std::size_t j = i + 1U;
             j < registered_syscall_traps.size();
             ++j) {
            if (registered_syscall_traps[j].
                    guest_rip ==
                site.guest_rip) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] WindowsExecutionResult
validate_context(
    const astraea::loader::GuestImage& image,
    const WindowsPreparedMemory& prepared_memory,
    const GuestCpuContext& context) {
    if (prepared_memory.empty() ||
        prepared_memory.plan().
            host_page_size == 0) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_guest_context));
    }

    if (image.tls.has_value()) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    tls_runtime_layout_unsupported));
    }

    if (context.fs_base != 0 ||
        context.gs_base != 0 ||
        (context.rflags & 0x2U) == 0 ||
        (context.rflags &
         kRflagsDirection) != 0 ||
        (context.rflags &
         kRflagsTrap) != 0) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_guest_context));
    }

    if (!exact_executable_contains(
            image,
            context.rip)) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_guest_context,
                true,
                context.rip));
    }

    if (!image.initial_stack.storage.contains(
            astraea::memory::GuestAddress{
                context.rsp})) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_guest_context,
                true,
                context.rsp));
    }

    const std::uint64_t stack_base =
        image.initial_stack.storage.
            base().value();
    const std::uint64_t exception_headroom =
        context.rsp - stack_base;
    if (exception_headroom <
        kMinimumWindowsExceptionHeadroom) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_guest_context,
                true,
                context.rsp));
    }

    return WindowsExecutionResult::success(
        ExecutionStop{});
}

[[nodiscard]] bool overlaps_any(
    astraea::memory::GuestRange range,
    std::span<
        const astraea::memory::GuestRange>
        others) noexcept {
    for (const auto& other : others) {
        if (range.overlaps(other)) {
            return true;
        }
    }
    return false;
}

class WindowsGateMapping {
public:
    WindowsGateMapping() = default;
    WindowsGateMapping(
        const WindowsGateMapping&) = delete;
    WindowsGateMapping& operator=(
        const WindowsGateMapping&) = delete;

    WindowsGateMapping(
        WindowsGateMapping&& other) noexcept
        : reservation_base_(
              other.reservation_base_) {
        other.reservation_base_ =
            nullptr;
    }

    WindowsGateMapping& operator=(
        WindowsGateMapping&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        reservation_base_ =
            other.reservation_base_;
        other.reservation_base_ =
            nullptr;
        return *this;
    }

    ~WindowsGateMapping() {
        reset();
    }

    using Result =
        astraea::core::Result<
            WindowsGateMapping,
            NativeBackendError>;

    [[nodiscard]] static Result prepare(
        const WindowsPreparedMemory&
            prepared_memory,
        const SyntheticGateRegion&
            gate_region) {
        SYSTEM_INFO system_info{};
        ::GetSystemInfo(&system_info);

        const std::uint64_t page_size =
            static_cast<std::uint64_t>(
                system_info.dwPageSize);
        const std::uint64_t granularity =
            static_cast<std::uint64_t>(
                system_info.
                    dwAllocationGranularity);
        const auto exact =
            gate_region.range();

        if (page_size == 0 ||
            granularity == 0 ||
            exact.empty() ||
            (exact.base().value() %
             page_size) != 0) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        invalid_guest_context,
                    true,
                    exact.base().value()));
        }

        const std::uint64_t exact_size =
            exact.size().value();
        const std::uint64_t page_remainder =
            exact_size % page_size;
        const std::uint64_t page_padding =
            page_remainder == 0
                ? 0
                : page_size -
                    page_remainder;
        if (exact_size >
            std::numeric_limits<
                std::uint64_t>::max() -
                page_padding) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_page_arithmetic_overflow,
                    true,
                    exact.base().value()));
        }
        const std::uint64_t mapped_size =
            exact_size + page_padding;

        const std::uint64_t reserve_base =
            (exact.base().value() /
             granularity) *
            granularity;
        if (exact.base().value() >
            std::numeric_limits<
                std::uint64_t>::max() -
                mapped_size) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_page_arithmetic_overflow,
                    true,
                    exact.base().value()));
        }
        const std::uint64_t mapped_end =
            exact.base().value() +
            mapped_size;
        if (mapped_end == 0) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_page_arithmetic_overflow,
                    true,
                    exact.base().value()));
        }
        const std::uint64_t last =
            mapped_end - 1U;
        const std::uint64_t last_block =
            last / granularity;
        if (last_block >
            std::numeric_limits<
                std::uint64_t>::max() /
                granularity) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_page_arithmetic_overflow,
                    true,
                    exact.base().value()));
        }
        const std::uint64_t reserve_last =
            last_block * granularity;
        if (reserve_last >
            std::numeric_limits<
                std::uint64_t>::max() -
                granularity) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_page_arithmetic_overflow,
                    true,
                    exact.base().value()));
        }
        const std::uint64_t reserve_end =
            reserve_last +
            granularity;

        auto reserve_range =
            astraea::memory::GuestRange::create(
                astraea::memory::GuestAddress{
                    reserve_base},
                astraea::memory::GuestSize{
                    reserve_end -
                    reserve_base});
        if (!reserve_range.has_value()) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_page_arithmetic_overflow,
                    true,
                    reserve_base));
        }

        if (overlaps_any(
                reserve_range.value(),
                prepared_memory.
                    reservation_ranges())) {
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        guest_address_unavailable,
                    true,
                    exact.base().value()));
        }

        if constexpr (
            sizeof(std::size_t) <
            sizeof(std::uint64_t)) {
            if (reserve_range->size().value() >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<
                            std::size_t>::max()) ||
                mapped_size >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<
                            std::size_t>::max())) {
                return Result::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        exact.base().value()));
            }
        }

        void* const reserve_request =
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(
                    reserve_base));
        const auto reserve_size =
            static_cast<std::size_t>(
                reserve_range->
                    size().value());

        ::SetLastError(ERROR_SUCCESS);
        void* const reserved =
            ::VirtualAlloc(
                reserve_request,
                reserve_size,
                MEM_RESERVE,
                PAGE_NOACCESS);
        if (reserved == nullptr) {
            const DWORD host_error =
                ::GetLastError();
            return Result::failure(
                backend_error(
                    host_error ==
                            ERROR_INVALID_ADDRESS
                        ? NativeBackendErrorCode::
                              guest_address_unavailable
                        : NativeBackendErrorCode::
                              host_mapping_failure,
                    true,
                    exact.base().value(),
                    true,
                    host_error));
        }

        if (reserved != reserve_request) {
            static_cast<void>(
                ::VirtualFree(
                    reserved,
                    0,
                    MEM_RELEASE));
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        guest_address_unavailable,
                    true,
                    exact.base().value()));
        }

        void* const exact_address =
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(
                    exact.base().value()));
        const auto host_mapped_size =
            static_cast<std::size_t>(
                mapped_size);
        ::SetLastError(ERROR_SUCCESS);
        void* const committed =
            ::VirtualAlloc(
                exact_address,
                host_mapped_size,
                MEM_COMMIT,
                PAGE_READWRITE);
        if (committed == nullptr ||
            committed != exact_address) {
            const DWORD host_error =
                ::GetLastError();
            static_cast<void>(
                ::VirtualFree(
                    reserved,
                    0,
                    MEM_RELEASE));
            return Result::failure(
                backend_error(
                    committed == nullptr
                        ? NativeBackendErrorCode::
                              host_mapping_failure
                        : NativeBackendErrorCode::
                              guest_address_unavailable,
                    true,
                    exact.base().value(),
                    true,
                    host_error));
        }

        std::memcpy(
            committed,
            gate_region.bytes().data(),
            gate_region.bytes().size());

        DWORD old_protection = 0;
        ::SetLastError(ERROR_SUCCESS);
        if (::VirtualProtect(
                committed,
                host_mapped_size,
                PAGE_EXECUTE_READ,
                &old_protection) == 0) {
            const DWORD host_error =
                ::GetLastError();
            static_cast<void>(
                ::VirtualFree(
                    reserved,
                    0,
                    MEM_RELEASE));
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        host_protection_failure,
                    true,
                    exact.base().value(),
                    true,
                    host_error));
        }

        ::SetLastError(ERROR_SUCCESS);
        if (::FlushInstructionCache(
                ::GetCurrentProcess(),
                committed,
                host_mapped_size) == 0) {
            const DWORD host_error =
                ::GetLastError();
            static_cast<void>(
                ::VirtualFree(
                    reserved,
                    0,
                    MEM_RELEASE));
            return Result::failure(
                backend_error(
                    NativeBackendErrorCode::
                        instruction_cache_sync_failure,
                    true,
                    exact.base().value(),
                    true,
                    host_error));
        }

        WindowsGateMapping result;
        result.reservation_base_ =
            reserved;
        return Result::success(
            std::move(result));
    }

private:
    void reset() noexcept {
        if (reservation_base_ != nullptr) {
            static_cast<void>(
                ::VirtualFree(
                    reservation_base_,
                    0,
                    MEM_RELEASE));
        }
        reservation_base_ = nullptr;
    }

    void* reservation_base_ = nullptr;
};

[[nodiscard]] std::vector<ExecutableRange>
build_executable_ranges(
    const astraea::loader::GuestImage& image) {
    std::vector<ExecutableRange> ranges;
    for (const auto& mapping :
         image.mappings) {
        if (!mapping.permissions.has(
                astraea::memory::
                    GuestPermission::execute) ||
            mapping.range.empty()) {
            continue;
        }

        ranges.push_back(
            ExecutableRange{
                .base =
                    mapping.range.base().value(),
                .size =
                    mapping.range.size().value(),
            });
    }
    return ranges;
}

[[nodiscard]] WindowsExecutionResult
run_windows_guest_thread(
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    const std::vector<ExecutableRange>&
        executable_ranges,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps) {
    WindowsTransitionFrame transition{};
    WindowsExceptionFrame frame{};
    frame.executable_ranges =
        executable_ranges.data();
    frame.executable_range_count =
        executable_ranges.size();
    frame.gate_base =
        gate_region.range().
            base().value();
    frame.gate_slot_count =
        gate_region.slot_count();
    frame.registered_syscall_traps =
        registered_syscall_traps.data();
    frame.registered_syscall_trap_count =
        registered_syscall_traps.size();
    frame.transition = &transition;

    ::SetLastError(ERROR_SUCCESS);
    void* const handler =
        ::AddVectoredExceptionHandler(
            1,
            &windows_guest_exception_handler);
    if (handler == nullptr) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure,
                false,
                0,
                true,
                ::GetLastError()));
    }

    g_active_frame = &frame;
    astraea_windows_enter_guest_context(
        &context,
        &transition);
    g_active_frame = nullptr;

    if (::RemoveVectoredExceptionHandler(
            handler) == 0) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure,
                false,
                0,
                true,
                ::GetLastError()));
    }

    return WindowsExecutionResult::success(
        frame.stop);
}

#endif

}  // namespace

bool windows_native_execution_backend_available() noexcept {
#if defined(_WIN32) && defined(_M_X64)
    return true;
#else
    return false;
#endif
}

WindowsExecutionResult enter_windows_guest(
    const astraea::loader::GuestImage& image,
    const WindowsPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps) {
#if !(defined(_WIN32) && defined(_M_X64))
    static_cast<void>(image);
    static_cast<void>(prepared_memory);
    static_cast<void>(gate_region);
    static_cast<void>(context);
    static_cast<void>(registered_syscall_traps);
    return WindowsExecutionResult::failure(
        backend_error(
            NativeBackendErrorCode::
                backend_unavailable));
#else
    auto valid =
        validate_context(
            image,
            prepared_memory,
            context);
    if (!valid.has_value()) {
        return WindowsExecutionResult::failure(
            valid.error());
    }

    if (!registered_traps_are_valid(
            image,
            registered_syscall_traps)) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_registered_syscall_trap));
    }

    std::unique_lock<std::mutex> execution_lock(
        g_windows_execution_mutex,
        std::try_to_lock);
    if (!execution_lock.owns_lock()) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    nested_execution_unsupported));
    }

    auto gate_mapping =
        WindowsGateMapping::prepare(
            prepared_memory,
            gate_region);
    if (!gate_mapping.has_value()) {
        return WindowsExecutionResult::failure(
            gate_mapping.error());
    }

    try {
        auto executable_ranges =
            build_executable_ranges(image);

        std::optional<
            WindowsExecutionResult>
            thread_result;
        std::thread worker(
            [&]() {
                try {
                    thread_result.emplace(
                        run_windows_guest_thread(
                            gate_region,
                            context,
                            executable_ranges,
                            registered_syscall_traps));
                } catch (
                    const std::bad_alloc&) {
                    thread_result.emplace(
                        WindowsExecutionResult::failure(
                            backend_error(
                                NativeBackendErrorCode::
                                    recovery_setup_failure)));
                } catch (...) {
                    thread_result.emplace(
                        WindowsExecutionResult::failure(
                            backend_error(
                                NativeBackendErrorCode::
                                    internal_transition_failure)));
                }
            });
        worker.join();

        if (!thread_result.has_value()) {
            return WindowsExecutionResult::failure(
                backend_error(
                    NativeBackendErrorCode::
                        internal_transition_failure));
        }

        return std::move(
            thread_result.value());
    } catch (const std::system_error& error) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure,
                false,
                0,
                true,
                static_cast<std::uint64_t>(
                    error.code().value())));
    } catch (const std::bad_alloc&) {
        return WindowsExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure));
    }
#endif
}

}  // namespace astraea::execution
