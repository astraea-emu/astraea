#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <astraea/execution/linux_execution.hpp>

#include <array>
#include <bit>
#include <climits>
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

#if defined(__linux__) && defined(__x86_64__)
#include <cerrno>
#include <csignal>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
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

struct LinuxThreadExecutionOutcome {
    ExecutionStop stop;
    bool has_seccomp_syscall_trap = false;
    LinuxSeccompSyscallTrap seccomp_syscall_trap;
};

using LinuxThreadExecutionResult =
    astraea::core::Result<
        LinuxThreadExecutionOutcome,
        NativeBackendError>;

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

extern "C" [[noreturn]] void astraea_linux_enter_guest_context(
    const GuestCpuContext* context) noexcept;

static_assert(offsetof(GuestCpuContext, rax) == 0);
static_assert(offsetof(GuestCpuContext, rbx) == 8);
static_assert(offsetof(GuestCpuContext, rcx) == 16);
static_assert(offsetof(GuestCpuContext, rdx) == 24);
static_assert(offsetof(GuestCpuContext, rsi) == 32);
static_assert(offsetof(GuestCpuContext, rdi) == 40);
static_assert(offsetof(GuestCpuContext, rbp) == 48);
static_assert(offsetof(GuestCpuContext, rsp) == 56);
static_assert(offsetof(GuestCpuContext, r8) == 64);
static_assert(offsetof(GuestCpuContext, r9) == 72);
static_assert(offsetof(GuestCpuContext, r10) == 80);
static_assert(offsetof(GuestCpuContext, r11) == 88);
static_assert(offsetof(GuestCpuContext, r12) == 96);
static_assert(offsetof(GuestCpuContext, r13) == 104);
static_assert(offsetof(GuestCpuContext, r14) == 112);
static_assert(offsetof(GuestCpuContext, r15) == 120);
static_assert(offsetof(GuestCpuContext, rip) == 128);
static_assert(offsetof(GuestCpuContext, rflags) == 136);

constexpr std::array<int, 5> kGuestSignals{
    SIGSEGV,
    SIGBUS,
    SIGILL,
    SIGFPE,
    SIGSYS,
};

constexpr std::size_t kMinimumAlternateSignalStackSize = 64U * 1024U;

constexpr std::uint64_t kRflagsTrap = 1ULL << 8U;
constexpr std::uint64_t kRflagsDirection = 1ULL << 10U;

// Linux UAPI asm-generic/siginfo.h defines SYS_SECCOMP as 1. Some libc
// header combinations used by CI do not expose that macro even with
// SA_SIGINFO support, so keep the kernel ABI value local and verify it when
// the libc does expose the symbolic constant.
constexpr int kLinuxSysSeccompSignalCode = 1;
#if defined(SYS_SECCOMP)
static_assert(SYS_SECCOMP == kLinuxSysSeccompSignalCode);
#endif

struct SignalRange {
    std::uint64_t base = 0;
    std::uint64_t size = 0;
};

struct RawLinuxSeccompSyscallTrap {
    GuestCpuContext context;
    std::uint64_t kernel_instruction_pointer = 0;
    std::int32_t syscall_number = 0;
    std::uint32_t audit_arch = 0;
};

struct SignalFrame {
    sigjmp_buf jump_buffer;
    const SignalRange* executable_ranges = nullptr;
    std::size_t executable_range_count = 0;
    const SignalRange* seccomp_instruction_ranges = nullptr;
    std::size_t seccomp_ip_range_count = 0;
    std::uint64_t gate_base = 0;
    std::uint32_t gate_slot_count = 0;
    const RegisteredSyscallTrapSite*
        registered_syscall_traps = nullptr;
    std::size_t registered_syscall_trap_count = 0;
    bool has_seccomp_syscall_trap = false;
    RawLinuxSeccompSyscallTrap raw_seccomp_syscall_trap;
    ExecutionStop stop;
};

thread_local SignalFrame* g_active_frame = nullptr;
std::mutex g_signal_mutex;
std::array<struct sigaction, kGuestSignals.size()> g_previous_actions{};

[[nodiscard]] std::optional<std::size_t> signal_index(
    int signal_number) noexcept {
    for (std::size_t i = 0; i < kGuestSignals.size(); ++i) {
        if (kGuestSignals[i] == signal_number) {
            return i;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool range_contains(
    const SignalRange& range,
    std::uint64_t address) noexcept {
    if (range.size == 0 || address < range.base) {
        return false;
    }
    return address - range.base < range.size;
}

[[nodiscard]] bool frame_owns_guest_seccomp_ip(
    const SignalFrame& frame,
    std::uint64_t instruction_pointer) noexcept {
    for (std::size_t i = 0;
         i < frame.seccomp_ip_range_count;
         ++i) {
        if (range_contains(
                frame.seccomp_instruction_ranges[i],
                instruction_pointer)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool frame_owns_guest_rip(
    const SignalFrame& frame,
    std::uint64_t rip) noexcept {
    for (std::size_t i = 0; i < frame.executable_range_count; ++i) {
        if (range_contains(frame.executable_ranges[i], rip)) {
            return true;
        }
    }

    const std::uint64_t gate_size =
        static_cast<std::uint64_t>(frame.gate_slot_count) *
        kSyntheticGateStride;
    return range_contains(
        SignalRange{
            .base = frame.gate_base,
            .size = gate_size,
        },
        rip);
}

[[nodiscard]] bool recognize_gate_slot(
    const SignalFrame& frame,
    std::uint64_t rip,
    std::uint32_t& slot) noexcept {
    if (rip < frame.gate_base) {
        return false;
    }

    const std::uint64_t offset = rip - frame.gate_base;
    if ((offset % kSyntheticGateStride) != 0) {
        return false;
    }

    const std::uint64_t candidate =
        offset / kSyntheticGateStride;
    if (candidate >= frame.gate_slot_count) {
        return false;
    }

    slot = static_cast<std::uint32_t>(candidate);
    return true;
}

[[nodiscard]] bool
recognize_registered_syscall_trap(
    const SignalFrame& frame,
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

[[nodiscard]] GuestFaultKind fault_kind_for_signal(
    int signal_number) noexcept {
    switch (signal_number) {
    case SIGSEGV:
    case SIGBUS:
        return GuestFaultKind::access_violation;
    case SIGILL:
        return GuestFaultKind::illegal_instruction;
    case SIGFPE:
        return GuestFaultKind::arithmetic;
    default:
        return GuestFaultKind::unknown;
    }
}

[[nodiscard]] std::uint64_t host_signal_code(
    int signal_number,
    const siginfo_t* info) noexcept {
    const auto signal_bits =
        static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(signal_number));
    const auto code_bits =
        static_cast<std::uint64_t>(
            static_cast<std::uint32_t>(
                info != nullptr ? info->si_code : 0));
    return (signal_bits << 32U) | code_bits;
}

void capture_guest_context(
    const ucontext_t& host_context,
    GuestCpuContext& guest_context) noexcept {
    const auto& gregs = host_context.uc_mcontext.gregs;

    guest_context.rax = static_cast<std::uint64_t>(gregs[REG_RAX]);
    guest_context.rbx = static_cast<std::uint64_t>(gregs[REG_RBX]);
    guest_context.rcx = static_cast<std::uint64_t>(gregs[REG_RCX]);
    guest_context.rdx = static_cast<std::uint64_t>(gregs[REG_RDX]);
    guest_context.rsi = static_cast<std::uint64_t>(gregs[REG_RSI]);
    guest_context.rdi = static_cast<std::uint64_t>(gregs[REG_RDI]);
    guest_context.rbp = static_cast<std::uint64_t>(gregs[REG_RBP]);
    guest_context.rsp = static_cast<std::uint64_t>(gregs[REG_RSP]);
    guest_context.r8 = static_cast<std::uint64_t>(gregs[REG_R8]);
    guest_context.r9 = static_cast<std::uint64_t>(gregs[REG_R9]);
    guest_context.r10 = static_cast<std::uint64_t>(gregs[REG_R10]);
    guest_context.r11 = static_cast<std::uint64_t>(gregs[REG_R11]);
    guest_context.r12 = static_cast<std::uint64_t>(gregs[REG_R12]);
    guest_context.r13 = static_cast<std::uint64_t>(gregs[REG_R13]);
    guest_context.r14 = static_cast<std::uint64_t>(gregs[REG_R14]);
    guest_context.r15 = static_cast<std::uint64_t>(gregs[REG_R15]);
    guest_context.rip = static_cast<std::uint64_t>(gregs[REG_RIP]);
    guest_context.rflags = static_cast<std::uint64_t>(gregs[REG_EFL]);

    // M2 v0 does not install guest FS/GS bases. Synthetic probes must
    // keep these portable fields zero and must not depend on host TLS.
    guest_context.fs_base = 0;
    guest_context.gs_base = 0;
}

void chain_previous_signal(
    int signal_number,
    siginfo_t* info,
    void* opaque_context) noexcept {
    const auto index = signal_index(signal_number);
    if (!index.has_value()) {
        ::_exit(128 + signal_number);
    }

    const auto& previous = g_previous_actions[index.value()];

    if (previous.sa_handler == SIG_IGN) {
        return;
    }

    if (previous.sa_handler == SIG_DFL) {
        static_cast<void>(
            ::sigaction(signal_number, &previous, nullptr));
        static_cast<void>(::raise(signal_number));
        ::_exit(128 + signal_number);
    }

    if ((previous.sa_flags & SA_SIGINFO) != 0) {
        previous.sa_sigaction(signal_number, info, opaque_context);
        return;
    }

    previous.sa_handler(signal_number);
}

void guest_signal_handler(
    int signal_number,
    siginfo_t* info,
    void* opaque_context) noexcept {
    auto* const frame = g_active_frame;
    if (frame == nullptr || opaque_context == nullptr) {
        chain_previous_signal(
            signal_number,
            info,
            opaque_context);
        return;
    }

    auto* const host_context =
        static_cast<ucontext_t*>(opaque_context);
    const std::uint64_t rip =
        static_cast<std::uint64_t>(
            host_context->uc_mcontext.gregs[REG_RIP]);

    if (signal_number == SIGSYS &&
        info != nullptr &&
        info->si_code == kLinuxSysSeccompSignalCode) {
        const auto kernel_instruction_pointer =
            static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(
                    info->si_call_addr));

        // For SECCOMP_RET_TRAP, Linux reports si_call_addr as the exact
        // system-call instruction address while the saved ucontext RIP is
        // already post-instruction. The handler only establishes ownership
        // and captures bounded metadata; ordinary code validates the opcode
        // and the architecture-specific post-instruction relationship.
        if (!frame_owns_guest_seccomp_ip(
                *frame,
                kernel_instruction_pointer)) {
            chain_previous_signal(
                signal_number,
                info,
                opaque_context);
            return;
        }

        capture_guest_context(
            *host_context,
            frame->raw_seccomp_syscall_trap.context);
        frame->raw_seccomp_syscall_trap.
            kernel_instruction_pointer =
                kernel_instruction_pointer;
        frame->raw_seccomp_syscall_trap.syscall_number =
            static_cast<std::int32_t>(
                info->si_syscall);
        frame->raw_seccomp_syscall_trap.audit_arch =
            static_cast<std::uint32_t>(
                info->si_arch);
        frame->has_seccomp_syscall_trap = true;

        siglongjmp(
            frame->jump_buffer,
            1);
    }

    if (!frame_owns_guest_rip(*frame, rip)) {
        chain_previous_signal(
            signal_number,
            info,
            opaque_context);
        return;
    }

    capture_guest_context(
        *host_context,
        frame->stop.context);

    std::uint32_t gate_slot = 0;
    if (signal_number == SIGILL &&
        recognize_gate_slot(*frame, rip, gate_slot)) {
        frame->stop.reason = ExecutionStopReason::host_gate;
        frame->stop.has_gate_slot = true;
        frame->stop.gate_slot = gate_slot;
        frame->stop.has_fault = false;
        siglongjmp(frame->jump_buffer, 1);
    }

    if (signal_number == SIGILL &&
        recognize_registered_syscall_trap(
            *frame,
            rip)) {
        frame->stop.reason =
            ExecutionStopReason::
                registered_syscall_trap;
        frame->stop.has_gate_slot = false;
        frame->stop.gate_slot = 0;
        frame->stop.has_fault = false;
        siglongjmp(frame->jump_buffer, 1);
    }

    frame->stop.reason = ExecutionStopReason::guest_fault;
    frame->stop.has_gate_slot = false;
    frame->stop.gate_slot = 0;
    frame->stop.has_fault = true;
    frame->stop.fault = GuestFault{
        .kind = fault_kind_for_signal(signal_number),
        .instruction_pointer = rip,
        .stack_pointer =
            static_cast<std::uint64_t>(
                host_context->uc_mcontext.gregs[REG_RSP]),
        .has_fault_address =
            info != nullptr &&
            (signal_number == SIGSEGV ||
             signal_number == SIGBUS),
        .fault_address =
            info != nullptr &&
                    (signal_number == SIGSEGV ||
                     signal_number == SIGBUS)
                ? static_cast<std::uint64_t>(
                      reinterpret_cast<std::uintptr_t>(
                          info->si_addr))
                : 0,
        .host_code = host_signal_code(signal_number, info),
    };

    siglongjmp(frame->jump_buffer, 1);
}

[[nodiscard]] bool exact_executable_contains(
    const astraea::loader::GuestImage& image,
    std::uint64_t address) noexcept {
    for (const auto& mapping : image.mappings) {
        if (mapping.permissions.has(
                astraea::memory::GuestPermission::execute) &&
            mapping.range.contains(
                astraea::memory::GuestAddress{address})) {
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

[[nodiscard]] LinuxExecutionResult validate_context(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const GuestCpuContext& context) {
    if (prepared_memory.empty() ||
        prepared_memory.plan().host_page_size == 0) {
        return LinuxExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::invalid_guest_context));
    }

    if (image.tls.has_value()) {
        return LinuxExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::tls_runtime_layout_unsupported));
    }

    if (context.fs_base != 0 ||
        context.gs_base != 0 ||
        (context.rflags & 0x2U) == 0 ||
        (context.rflags & kRflagsDirection) != 0 ||
        (context.rflags & kRflagsTrap) != 0) {
        return LinuxExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::invalid_guest_context));
    }

    if (!exact_executable_contains(image, context.rip)) {
        return LinuxExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::invalid_guest_context,
                true,
                context.rip));
    }

    if (!image.initial_stack.storage.contains(
            astraea::memory::GuestAddress{context.rsp})) {
        return LinuxExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::invalid_guest_context,
                true,
                context.rsp));
    }

    return LinuxExecutionResult::success(
        ExecutionStop{});
}

class GateMapping {
public:
    GateMapping() = default;

    GateMapping(const GateMapping&) = delete;
    GateMapping& operator=(const GateMapping&) = delete;

    GateMapping(GateMapping&& other) noexcept
        : address_(other.address_),
          size_(other.size_) {
        other.address_ = nullptr;
        other.size_ = 0;
    }

    GateMapping& operator=(GateMapping&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        reset();
        address_ = other.address_;
        size_ = other.size_;
        other.address_ = nullptr;
        other.size_ = 0;
        return *this;
    }

    ~GateMapping() {
        reset();
    }

    [[nodiscard]] static astraea::core::Result<
        GateMapping,
        NativeBackendError>
    prepare(
        const LinuxPreparedMemory& prepared_memory,
        const SyntheticGateRegion& gate_region) {
        const std::uint64_t page_size =
            prepared_memory.plan().host_page_size;
        const auto exact_range = gate_region.range();

        if (page_size == 0 ||
            exact_range.empty() ||
            (exact_range.base().value() % page_size) != 0) {
            return astraea::core::Result<
                GateMapping,
                NativeBackendError>::failure(
                    backend_error(
                        NativeBackendErrorCode::invalid_guest_context,
                        true,
                        exact_range.base().value()));
        }

        const std::uint64_t exact_size =
            exact_range.size().value();
        const std::uint64_t remainder =
            exact_size % page_size;
        const std::uint64_t padding =
            remainder == 0 ? 0 : page_size - remainder;
        if (exact_size >
            std::numeric_limits<std::uint64_t>::max() -
                padding) {
            return astraea::core::Result<
                GateMapping,
                NativeBackendError>::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        exact_range.base().value()));
        }

        const std::uint64_t mapped_size =
            exact_size + padding;
        auto mapped_range =
            astraea::memory::GuestRange::create(
                exact_range.base(),
                astraea::memory::GuestSize{mapped_size});
        if (!mapped_range.has_value()) {
            return astraea::core::Result<
                GateMapping,
                NativeBackendError>::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_page_arithmetic_overflow,
                        true,
                        exact_range.base().value()));
        }

        for (const auto& region :
             prepared_memory.plan().regions) {
            if (mapped_range->overlaps(region.range)) {
                return astraea::core::Result<
                    GateMapping,
                    NativeBackendError>::failure(
                        backend_error(
                            NativeBackendErrorCode::
                                guest_address_unavailable,
                            true,
                            exact_range.base().value()));
            }
        }

        if constexpr (
            sizeof(std::size_t) < sizeof(std::uint64_t)) {
            if (mapped_size >
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
                return astraea::core::Result<
                    GateMapping,
                    NativeBackendError>::failure(
                        backend_error(
                            NativeBackendErrorCode::
                                host_page_arithmetic_overflow,
                            true,
                            exact_range.base().value()));
            }
        }

        const auto host_size =
            static_cast<std::size_t>(mapped_size);
        void* const requested =
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(
                    exact_range.base().value()));

        errno = 0;
        void* const mapped = ::mmap(
            requested,
            host_size,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE |
                MAP_ANONYMOUS |
                MAP_FIXED_NOREPLACE,
            -1,
            0);
        if (mapped == MAP_FAILED) {
            const int host_error = errno;
            return astraea::core::Result<
                GateMapping,
                NativeBackendError>::failure(
                    backend_error(
                        host_error == EEXIST
                            ? NativeBackendErrorCode::
                                  guest_address_unavailable
                            : NativeBackendErrorCode::
                                  host_mapping_failure,
                        true,
                        exact_range.base().value(),
                        true,
                        static_cast<std::uint64_t>(
                            host_error)));
        }

        if (mapped != requested) {
            static_cast<void>(
                ::munmap(mapped, host_size));
            return astraea::core::Result<
                GateMapping,
                NativeBackendError>::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            guest_address_unavailable,
                        true,
                        exact_range.base().value()));
        }

        std::memcpy(
            mapped,
            gate_region.bytes().data(),
            gate_region.bytes().size());

        errno = 0;
        if (::mprotect(
                mapped,
                host_size,
                PROT_READ | PROT_EXEC) != 0) {
            const int host_error = errno;
            static_cast<void>(
                ::munmap(mapped, host_size));
            return astraea::core::Result<
                GateMapping,
                NativeBackendError>::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            host_protection_failure,
                        true,
                        exact_range.base().value(),
                        true,
                        static_cast<std::uint64_t>(
                            host_error)));
        }

#if defined(__GNUC__) || defined(__clang__)
        auto* const begin =
            static_cast<char*>(mapped);
        auto* const end = begin + host_size;
        __builtin___clear_cache(begin, end);
#endif

        GateMapping result;
        result.address_ = mapped;
        result.size_ = host_size;
        return astraea::core::Result<
            GateMapping,
            NativeBackendError>::success(
                std::move(result));
    }

private:
    void reset() noexcept {
        if (address_ != nullptr && size_ != 0) {
            static_cast<void>(
                ::munmap(address_, size_));
        }
        address_ = nullptr;
        size_ = 0;
    }

    void* address_ = nullptr;
    std::size_t size_ = 0;
};

[[nodiscard]] std::vector<SignalRange>
build_executable_ranges(
    const astraea::loader::GuestImage& image) {
    std::vector<SignalRange> ranges;
    for (const auto& mapping : image.mappings) {
        if (!mapping.permissions.has(
                astraea::memory::GuestPermission::execute) ||
            mapping.range.empty()) {
            continue;
        }
        ranges.push_back(
            SignalRange{
                .base = mapping.range.base().value(),
                .size = mapping.range.size().value(),
            });
    }
    return ranges;
}

using SeccompIpRangeBuildResult =
    astraea::core::Result<
        std::vector<SignalRange>,
        NativeBackendError>;

[[nodiscard]] SeccompIpRangeBuildResult
build_seccomp_instruction_pointer_ranges(
    std::span<const SignalRange> executable_ranges) {
    try {
        std::vector<SignalRange> ranges;
        ranges.reserve(executable_ranges.size());

        for (const auto& executable :
             executable_ranges) {
            // On x86 the saved user RIP after SYSCALL/INT 0x80 is two bytes
            // past the call site. Some kernel paths expose that post-call
            // value to seccomp's instruction-pointer check. Extend the
            // half-open ownership envelope by exactly one byte so a valid
            // two-byte call ending at the final executable byte still traps.
            //
            // Ordinary-code normalization later requires the actual two-byte
            // syscall opcode to lie completely inside the exact executable
            // mapping, so this one-byte filter envelope cannot admit a
            // non-guest call site as a valid guest syscall event.
            if (executable.size ==
                    std::numeric_limits<
                        std::uint64_t>::max() ||
                executable.base >
                    std::numeric_limits<
                        std::uint64_t>::max() -
                        executable.size) {
                return SeccompIpRangeBuildResult::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            syscall_interception_setup_failure,
                        true,
                        executable.base));
            }

            ranges.push_back(
                SignalRange{
                    .base = executable.base,
                    .size = executable.size + 1U,
                });
        }

        return SeccompIpRangeBuildResult::success(
            std::move(ranges));
    } catch (const std::bad_alloc&) {
        return SeccompIpRangeBuildResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_setup_failure));
    } catch (const std::length_error&) {
        return SeccompIpRangeBuildResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_setup_failure,
                false,
                0,
                true,
                E2BIG));
    }
}

using SeccompTrapNormalizeResult =
    astraea::core::Result<
        LinuxSeccompSyscallTrap,
        NativeBackendError>;

[[nodiscard]] SeccompTrapNormalizeResult
normalize_seccomp_syscall_trap(
    const astraea::loader::GuestImage& image,
    const RawLinuxSeccompSyscallTrap& raw) noexcept {
    constexpr std::uint64_t kX86SyscallInstructionLength = 2U;
    constexpr std::array<std::byte, 2> kX86Int80Bytes{
        std::byte{0xcd},
        std::byte{0x80},
    };

    // Linux documents si_call_addr/seccomp_data.instruction_pointer as the
    // system-call instruction address, while the saved processor RIP is
    // post-instruction. In practice, supported x86 kernels/environments may
    // expose the post-instruction value through the seccomp metadata path.
    //
    // Do not trust either spelling on its own. Reconstruct the only admitted
    // call site from the captured post-instruction RIP, verify the literal
    // guest opcode there, and require the kernel-reported IP to agree with
    // either the verified call site or that post-instruction RIP.
    if (raw.context.rip <
        kX86SyscallInstructionLength) {
        return SeccompTrapNormalizeResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_metadata_failure,
                true,
                raw.context.rip));
    }

    const auto guest_rip =
        raw.context.rip -
        kX86SyscallInstructionLength;

    if (!exact_executable_contains(
            image,
            guest_rip) ||
        !exact_executable_contains(
            image,
            guest_rip + 1U) ||
        guest_rip >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uintptr_t>::max()) ||
        (raw.kernel_instruction_pointer != guest_rip &&
         raw.kernel_instruction_pointer !=
             raw.context.rip)) {
        return SeccompTrapNormalizeResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_metadata_failure,
                true,
                guest_rip));
    }

    std::array<std::byte, 2> mapped_bytes{};
    std::memcpy(
        mapped_bytes.data(),
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(
                guest_rip)),
        mapped_bytes.size());

    bool recognized = false;
    switch (raw.audit_arch) {
    case AUDIT_ARCH_X86_64:
        recognized =
            mapped_bytes ==
            kX86SyscallBytes;
        break;
    case AUDIT_ARCH_I386:
        recognized =
            mapped_bytes ==
            kX86Int80Bytes;
        break;
    default:
        break;
    }

    if (!recognized) {
        return SeccompTrapNormalizeResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_metadata_failure,
                true,
                guest_rip));
    }

    return SeccompTrapNormalizeResult::success(
        LinuxSeccompSyscallTrap{
            .context = raw.context,
            .guest_rip =
                astraea::memory::GuestAddress{
                    guest_rip},
            .syscall_number =
                raw.syscall_number,
            .audit_arch =
                raw.audit_arch,
        });
}

[[nodiscard]] sock_filter bpf_statement(
    std::uint16_t code,
    std::uint32_t value) noexcept {
    return sock_filter{
        .code = code,
        .jt = 0U,
        .jf = 0U,
        .k = value,
    };
}

[[nodiscard]] sock_filter bpf_jump(
    std::uint16_t code,
    std::uint32_t value,
    std::uint8_t jump_true,
    std::uint8_t jump_false) noexcept {
    return sock_filter{
        .code = code,
        .jt = jump_true,
        .jf = jump_false,
        .k = value,
    };
}

using SeccompInstallResult =
    astraea::core::Result<
        bool,
        NativeBackendError>;

[[nodiscard]] SeccompInstallResult
install_guest_executable_syscall_filter(
    std::span<const SignalRange> seccomp_instruction_ranges) {
    if (seccomp_instruction_ranges.empty()) {
        return SeccompInstallResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_setup_failure));
    }

    static_assert(
        std::endian::native ==
        std::endian::little);

    constexpr std::size_t kInstructionsPerRange = 11U;
    constexpr std::size_t kTrailingInstructions = 1U;
    constexpr auto kMaxProgramLength =
        static_cast<std::size_t>(
            std::numeric_limits<
                unsigned short>::max());

    if (seccomp_instruction_ranges.size() >
        (kMaxProgramLength -
         kTrailingInstructions) /
            kInstructionsPerRange) {
        return SeccompInstallResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_setup_failure,
                false,
                0,
                true,
                E2BIG));
    }

    try {
        std::vector<sock_filter> program;
        program.reserve(
            seccomp_instruction_ranges.size() *
                kInstructionsPerRange +
            kTrailingInstructions);

        constexpr auto kLoadAbsoluteWord =
            static_cast<std::uint16_t>(
                BPF_LD | BPF_W | BPF_ABS);
        constexpr auto kJumpGreater =
            static_cast<std::uint16_t>(
                BPF_JMP | BPF_JGT | BPF_K);
        constexpr auto kJumpEqual =
            static_cast<std::uint16_t>(
                BPF_JMP | BPF_JEQ | BPF_K);
        constexpr auto kJumpGreaterEqual =
            static_cast<std::uint16_t>(
                BPF_JMP | BPF_JGE | BPF_K);
        constexpr auto kReturnConstant =
            static_cast<std::uint16_t>(
                BPF_RET | BPF_K);

        constexpr auto kIpLowOffset =
            static_cast<std::uint32_t>(
                offsetof(
                    seccomp_data,
                    instruction_pointer));
        constexpr auto kIpHighOffset =
            kIpLowOffset +
            static_cast<std::uint32_t>(
                sizeof(std::uint32_t));

        for (const auto& range :
             seccomp_instruction_ranges) {
            if (range.size == 0U ||
                range.base >
                    std::numeric_limits<
                        std::uint64_t>::max() -
                        (range.size - 1U)) {
                return SeccompInstallResult::failure(
                    backend_error(
                        NativeBackendErrorCode::
                            syscall_interception_setup_failure,
                        true,
                        range.base));
            }

            const auto last =
                range.base +
                (range.size - 1U);

            const auto base_high =
                static_cast<std::uint32_t>(
                    range.base >> 32U);
            const auto base_low =
                static_cast<std::uint32_t>(
                    range.base & 0xffffffffULL);
            const auto last_high =
                static_cast<std::uint32_t>(
                    last >> 32U);
            const auto last_low =
                static_cast<std::uint32_t>(
                    last & 0xffffffffULL);

            // 64-bit lexicographic check:
            //
            //   exact guest executable range contains instruction_pointer
            //
            // A failed bound jumps to the next 11-instruction range block.
            // A successful match returns TRAP immediately.
            program.push_back(
                bpf_statement(
                    kLoadAbsoluteWord,
                    kIpHighOffset));
            program.push_back(
                bpf_jump(
                    kJumpGreater,
                    base_high,
                    3U,
                    0U));
            program.push_back(
                bpf_jump(
                    kJumpEqual,
                    base_high,
                    0U,
                    8U));
            program.push_back(
                bpf_statement(
                    kLoadAbsoluteWord,
                    kIpLowOffset));
            program.push_back(
                bpf_jump(
                    kJumpGreaterEqual,
                    base_low,
                    0U,
                    6U));

            program.push_back(
                bpf_statement(
                    kLoadAbsoluteWord,
                    kIpHighOffset));
            program.push_back(
                bpf_jump(
                    kJumpGreater,
                    last_high,
                    4U,
                    0U));
            program.push_back(
                bpf_jump(
                    kJumpEqual,
                    last_high,
                    0U,
                    2U));
            program.push_back(
                bpf_statement(
                    kLoadAbsoluteWord,
                    kIpLowOffset));
            program.push_back(
                bpf_jump(
                    kJumpGreater,
                    last_low,
                    1U,
                    0U));
            program.push_back(
                bpf_statement(
                    kReturnConstant,
                    SECCOMP_RET_TRAP));
        }

        program.push_back(
            bpf_statement(
                kReturnConstant,
                SECCOMP_RET_ALLOW));

        if (::prctl(
                PR_SET_NO_NEW_PRIVS,
                1UL,
                0UL,
                0UL,
                0UL) != 0) {
            return SeccompInstallResult::failure(
                backend_error(
                    NativeBackendErrorCode::
                        syscall_interception_setup_failure,
                    false,
                    0,
                    true,
                    static_cast<std::uint64_t>(
                        errno)));
        }

        sock_fprog filter_program{
            .len =
                static_cast<unsigned short>(
                    program.size()),
            .filter = program.data(),
        };

        errno = 0;
        if (::syscall(
                SYS_seccomp,
                SECCOMP_SET_MODE_FILTER,
                0U,
                &filter_program) != 0) {
            return SeccompInstallResult::failure(
                backend_error(
                    NativeBackendErrorCode::
                        syscall_interception_setup_failure,
                    false,
                    0,
                    true,
                    static_cast<std::uint64_t>(
                        errno)));
        }

        return SeccompInstallResult::success(true);
    } catch (const std::bad_alloc&) {
        return SeccompInstallResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_setup_failure));
    } catch (const std::length_error&) {
        return SeccompInstallResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    syscall_interception_setup_failure,
                false,
                0,
                true,
                E2BIG));
    }
}

[[nodiscard]] NativeBackendError recovery_error(
    int host_error) noexcept {
    return backend_error(
        NativeBackendErrorCode::recovery_setup_failure,
        false,
        0,
        true,
        static_cast<std::uint64_t>(host_error));
}

[[nodiscard]] std::optional<NativeBackendError>
restore_signal_environment(
    const stack_t& previous_stack,
    std::size_t installed_count) noexcept {
    int first_error = 0;

    for (std::size_t i = installed_count; i > 0; --i) {
        if (::sigaction(
                kGuestSignals[i - 1U],
                &g_previous_actions[i - 1U],
                nullptr) != 0 &&
            first_error == 0) {
            first_error = errno;
        }
    }

    if (::sigaltstack(
            &previous_stack,
            nullptr) != 0 &&
        first_error == 0) {
        first_error = errno;
    }

    if (first_error != 0) {
        return recovery_error(first_error);
    }
    return std::nullopt;
}

[[nodiscard]] LinuxThreadExecutionResult
run_linux_guest_thread(
    const astraea::loader::GuestImage& image,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps,
    bool enable_seccomp_syscall_trap) {
    std::vector<SignalRange> executable_ranges;
    std::vector<SignalRange> seccomp_instruction_ranges;
    std::vector<std::byte> alternate_stack;
    try {
        executable_ranges =
            build_executable_ranges(image);
        if (enable_seccomp_syscall_trap) {
            auto built_seccomp_ranges =
                build_seccomp_instruction_pointer_ranges(
                    executable_ranges);
            if (!built_seccomp_ranges.has_value()) {
                return LinuxThreadExecutionResult::failure(
                    built_seccomp_ranges.error());
            }
            seccomp_instruction_ranges =
                std::move(
                    built_seccomp_ranges.value());
        }
        const std::size_t alternate_size =
            static_cast<std::size_t>(SIGSTKSZ) >
                    kMinimumAlternateSignalStackSize
                ? static_cast<std::size_t>(SIGSTKSZ)
                : kMinimumAlternateSignalStackSize;
        alternate_stack.resize(alternate_size);
    } catch (const std::bad_alloc&) {
        return LinuxThreadExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure));
    } catch (const std::length_error&) {
        return LinuxThreadExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure));
    }

    stack_t previous_stack{};
    stack_t requested_stack{
        .ss_sp = alternate_stack.data(),
        .ss_flags = 0,
        .ss_size = alternate_stack.size(),
    };
    errno = 0;
    if (::sigaltstack(
            &requested_stack,
            &previous_stack) != 0) {
        return LinuxThreadExecutionResult::failure(
            recovery_error(errno));
    }

    for (std::size_t i = 0; i < kGuestSignals.size(); ++i) {
        errno = 0;
        if (::sigaction(
                kGuestSignals[i],
                nullptr,
                &g_previous_actions[i]) != 0) {
            const int host_error = errno;
            static_cast<void>(
                ::sigaltstack(
                    &previous_stack,
                    nullptr));
            return LinuxThreadExecutionResult::failure(
                recovery_error(host_error));
        }
    }

    struct sigaction action {};
    action.sa_sigaction = &guest_signal_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    static_cast<void>(::sigemptyset(&action.sa_mask));
    for (const int signal_number : kGuestSignals) {
        static_cast<void>(
            ::sigaddset(
                &action.sa_mask,
                signal_number));
    }

    std::size_t installed_count = 0;
    for (std::size_t i = 0; i < kGuestSignals.size(); ++i) {
        errno = 0;
        if (::sigaction(
                kGuestSignals[i],
                &action,
                nullptr) != 0) {
            const int host_error = errno;
            const auto cleanup_error =
                restore_signal_environment(
                    previous_stack,
                    installed_count);
            static_cast<void>(cleanup_error);
            return LinuxThreadExecutionResult::failure(
                recovery_error(host_error));
        }
        ++installed_count;
    }

    SignalFrame frame{};

    frame.executable_ranges =
        executable_ranges.data();
    frame.executable_range_count =
        executable_ranges.size();
    frame.seccomp_instruction_ranges =
        seccomp_instruction_ranges.data();
    frame.seccomp_ip_range_count =
        seccomp_instruction_ranges.size();
    frame.gate_base =
        gate_region.range().base().value();
    frame.gate_slot_count =
        gate_region.slot_count();
    frame.registered_syscall_traps =
        registered_syscall_traps.data();
    frame.registered_syscall_trap_count =
        registered_syscall_traps.size();

    const int jump_result =
        sigsetjmp(frame.jump_buffer, 1);

    std::optional<NativeBackendError>
        interception_setup_error;
    if (jump_result == 0 &&
        enable_seccomp_syscall_trap) {
        const auto installed =
            install_guest_executable_syscall_filter(
                seccomp_instruction_ranges);
        if (!installed.has_value()) {
            interception_setup_error =
                installed.error();
        }
    }

    if (jump_result == 0 &&
        !interception_setup_error.has_value()) {
        g_active_frame = &frame;
        astraea_linux_enter_guest_context(
            &context);
    }

    g_active_frame = nullptr;
    const auto cleanup_error =
        restore_signal_environment(
            previous_stack,
            installed_count);
    if (cleanup_error.has_value()) {
        return LinuxThreadExecutionResult::failure(
            cleanup_error.value());
    }

    if (interception_setup_error.has_value()) {
        return LinuxThreadExecutionResult::failure(
            interception_setup_error.value());
    }

    LinuxSeccompSyscallTrap normalized_seccomp_trap{};
    if (frame.has_seccomp_syscall_trap) {
        const auto normalized =
            normalize_seccomp_syscall_trap(
                image,
                frame.raw_seccomp_syscall_trap);
        if (!normalized.has_value()) {
            return LinuxThreadExecutionResult::failure(
                normalized.error());
        }
        normalized_seccomp_trap =
            normalized.value();
    }

    return LinuxThreadExecutionResult::success(
        LinuxThreadExecutionOutcome{
            .stop = frame.stop,
            .has_seccomp_syscall_trap =
                frame.has_seccomp_syscall_trap,
            .seccomp_syscall_trap =
                normalized_seccomp_trap,
        });
}

#endif

}  // namespace

bool linux_native_execution_backend_available() noexcept {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    return true;
#else
    return false;
#endif
}

bool
linux_guest_syscall_seccomp_available() noexcept {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE) && defined(SYS_seccomp)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] static LinuxThreadExecutionResult
enter_linux_guest_internal(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps,
    bool enable_seccomp_syscall_trap) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    static_cast<void>(image);
    static_cast<void>(prepared_memory);
    static_cast<void>(gate_region);
    static_cast<void>(context);
    static_cast<void>(registered_syscall_traps);
    static_cast<void>(enable_seccomp_syscall_trap);
    return LinuxThreadExecutionResult::failure(
        backend_error(
            NativeBackendErrorCode::backend_unavailable));
#else
    auto valid =
        validate_context(
            image,
            prepared_memory,
            context);
    if (!valid.has_value()) {
        return LinuxThreadExecutionResult::failure(
            valid.error());
    }

    if (!registered_traps_are_valid(
            image,
            registered_syscall_traps)) {
        return LinuxThreadExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    invalid_registered_syscall_trap));
    }

    std::unique_lock<std::mutex> execution_lock(
        g_signal_mutex,
        std::try_to_lock);
    if (!execution_lock.owns_lock()) {
        return LinuxThreadExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    nested_execution_unsupported));
    }

    auto gate_mapping =
        GateMapping::prepare(
            prepared_memory,
            gate_region);
    if (!gate_mapping.has_value()) {
        return LinuxThreadExecutionResult::failure(
            gate_mapping.error());
    }

    try {
        std::optional<LinuxThreadExecutionResult>
            thread_result;
        std::thread worker(
            [&]() {
                try {
                    thread_result.emplace(
                        run_linux_guest_thread(
                            image,
                            gate_region,
                            context,
                            registered_syscall_traps,
                            enable_seccomp_syscall_trap));
                } catch (const std::bad_alloc&) {
                    thread_result.emplace(
                        LinuxThreadExecutionResult::failure(
                            backend_error(
                                NativeBackendErrorCode::
                                    recovery_setup_failure)));
                } catch (...) {
                    thread_result.emplace(
                        LinuxThreadExecutionResult::failure(
                            backend_error(
                                NativeBackendErrorCode::
                                    internal_transition_failure)));
                }
            });
        worker.join();

        if (!thread_result.has_value()) {
            return LinuxThreadExecutionResult::failure(
                backend_error(
                    NativeBackendErrorCode::
                        internal_transition_failure));
        }

        return std::move(
            thread_result.value());
    } catch (const std::system_error& error) {
        return LinuxThreadExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure,
                false,
                0,
                true,
                static_cast<std::uint64_t>(
                    error.code().value())));
    } catch (const std::bad_alloc&) {
        return LinuxThreadExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    recovery_setup_failure));
    }
#endif
}

LinuxExecutionResult enter_linux_guest(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context,
    std::span<const RegisteredSyscallTrapSite>
        registered_syscall_traps) {
    auto result =
        enter_linux_guest_internal(
            image,
            prepared_memory,
            gate_region,
            context,
            registered_syscall_traps,
            false);
    if (!result.has_value()) {
        return LinuxExecutionResult::failure(
            result.error());
    }

    if (result->has_seccomp_syscall_trap) {
        return LinuxExecutionResult::failure(
            backend_error(
                NativeBackendErrorCode::
                    internal_transition_failure));
    }

    return LinuxExecutionResult::success(
        result->stop);
}

LinuxSeccompExecutionResult
enter_linux_guest_with_seccomp_syscall_trap(
    const astraea::loader::GuestImage& image,
    const LinuxPreparedMemory& prepared_memory,
    const SyntheticGateRegion& gate_region,
    GuestCpuContext context) {
    auto result =
        enter_linux_guest_internal(
            image,
            prepared_memory,
            gate_region,
            context,
            {},
            true);
    if (!result.has_value()) {
        return LinuxSeccompExecutionResult::failure(
            result.error());
    }

    if (result->has_seccomp_syscall_trap) {
        return LinuxSeccompExecutionResult::success(
            LinuxSeccompExecutionEvent{
                result->seccomp_syscall_trap});
    }

    return LinuxSeccompExecutionResult::success(
        LinuxSeccompExecutionEvent{
            result->stop});
}

}  // namespace astraea::execution
