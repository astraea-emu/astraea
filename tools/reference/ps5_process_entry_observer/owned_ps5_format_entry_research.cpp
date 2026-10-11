// C1 research-only, exact-source PS5-format ELF first-instruction proof.
// This binary is built only with ASTRAEA_BUILD_C1_RESEARCH=ON (Linux x86-64).
// It accepts ONE independently authored, pinned SHA-256 image. It does not
// implement or enable production retail entry or Sony process ABI semantics.
#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include "owned_pair_sha256.hpp"

#include <astraea/execution/context.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/guest_worker_artifact.hpp>
#include <astraea/execution/guest_worker_fault_projection.hpp>
#include <astraea/execution/guest_worker_process_session.hpp>
#include <astraea/execution/guest_worker_wire.hpp>
#include <astraea/execution/linux_execution.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/owned_relative_apply.hpp>
#include <astraea/execution/owned_relative_patch.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/guest_image.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <csignal>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
namespace e = astraea::execution;
namespace l = astraea::loader;
namespace m = astraea::memory;

constexpr std::size_t kMaxTitleBytes = 16U * 1024U * 1024U;
constexpr std::uint64_t kReserveBytes = 32U * 1024U * 1024U;
constexpr std::uint64_t kStackOffset = 24U * 1024U * 1024U;
constexpr std::uint64_t kMaxOriginalAddress = 16U * 1024U * 1024U;
constexpr std::string_view kTitleSha256 =
    "45920de847b2d33896c186654bbf22a728f1910fad84accb15204aa2da50c678";
constexpr e::GuestWorkerId kWorkerId{1U};
constexpr e::GuestThreadId kThreadId{1U};

// A single bounded, versioned leaf-to-controller observation. The record is
// data only: the controller performs no action based on a leaf-supplied RIP,
// identity, or service request. All fields, including unused fields, are
// checked against this source-owned fixture's exact expected stop.
using LeafStopRecord = std::array<std::uint64_t, 8U>;
constexpr std::size_t kLeafStopBytes = sizeof(LeafStopRecord);
constexpr std::uint64_t kLeafStopMagic = 0x31434c4541465453ULL;
constexpr std::uint64_t kLeafStopVersion = 1U;
constexpr std::uint64_t kLeafIllegalInstruction = 1U;
constexpr std::uint64_t kLeafGuestFault = 1U;
constexpr int kLeafStopFd = 4;
constexpr LeafStopRecord kExpectedLeafStop{
    kLeafStopMagic,
    kLeafStopVersion,
    kLeafIllegalInstruction,
    0x10U, // Original ELF entry, normalized across host ASLR.
    8U,    // Exactly eight applied RELATIVE patches.
    kWorkerId.value,
    kThreadId.value,
    kLeafGuestFault,
};
static_assert(kLeafStopBytes == 64U);

[[nodiscard]] bool valid_leaf_stop(
    const LeafStopRecord& record) noexcept {
    return record == kExpectedLeafStop;
}

[[nodiscard]] bool exact_pinned_title(std::span<const std::byte> bytes) {
    return !bytes.empty() && bytes.size() <= kMaxTitleBytes &&
        astraea::test::owned_pair_sha256_hex(bytes) == kTitleSha256;
}

[[nodiscard]] bool read_exact(std::span<std::byte> bytes) {
    if (bytes.empty()) return true;
    std::cin.read(reinterpret_cast<char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    return std::cin.gcount() == static_cast<std::streamsize>(bytes.size());
}

[[nodiscard]] std::optional<e::GuestWorkerWireMessage> receive() {
    std::array<std::byte, e::kGuestWorkerWireHeaderSize> header{};
    if (!read_exact(header)) return std::nullopt;
    auto decoded_header = e::decode_guest_worker_wire_header(header);
    if (!decoded_header.has_value()) return std::nullopt;
    std::vector<std::byte> frame(decoded_header->frame_size);
    std::copy(header.begin(), header.end(), frame.begin());
    if (decoded_header->payload_size > 0U) {
        const auto body = std::span<std::byte>{
            frame.data() + e::kGuestWorkerWireHeaderSize,
            decoded_header->payload_size};
        if (!read_exact(body)) return std::nullopt;
    }
    auto decoded = e::decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) return std::nullopt;
    return std::move(decoded.value());
}

[[nodiscard]] bool send(const e::GuestWorkerWireMessage& value) {
    auto frame = e::encode_guest_worker_wire_message(value);
    if (!frame.has_value()) return false;
    std::cout.write(reinterpret_cast<const char*>(frame->data()),
                    static_cast<std::streamsize>(frame->size()));
    std::cout.flush();
    return std::cout.good();
}

[[nodiscard]] bool bind_controller() {
    const char* expected = std::getenv("ASTRAEA_CONTROLLER_PID");
    if (expected == nullptr) return false;
    pid_t pid = 0;
    const std::string_view value{expected};
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), pid);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() || pid <= 1) return false;
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0) return false;
    return ::getppid() == pid;
}

[[nodiscard]] std::optional<std::uint64_t> choose_free_base() {
    if (::sysconf(_SC_PAGESIZE) != 4096) return std::nullopt;
    void* allocation = ::mmap(
        nullptr, static_cast<std::size_t>(kReserveBytes), PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (allocation == MAP_FAILED) return std::nullopt;
    const auto address = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(allocation));
    if (::munmap(allocation, static_cast<std::size_t>(kReserveBytes)) != 0 ||
        address == 0U || address % 4096U != 0U ||
        address > std::numeric_limits<std::uint64_t>::max() - kReserveBytes)
        return std::nullopt;
    // Exact MAP_FIXED_NOREPLACE mapping in the existing backend still
    // fails safely if another mapping claims this VMA in the meantime.
    return address;
}

// This is a narrow research-only process policy for the ONE pinned UD2
// workload. The leaf is a new exec-created process, is never ASan-linked,
// has no controller socket or effectful reporting descriptor, and cannot
// perform write/open/socket/exec after the filter is installed. This still
// permits thread creation and virtual memory operations for Astraea's
// actual native backend; it is not hostile-code memory containment.
[[nodiscard]] bool install_pinned_leaf_policy() noexcept {
    constexpr std::array<int, 22U> kHostRuntimeSyscalls{
        SYS_brk, SYS_clone, SYS_clone3, SYS_exit, SYS_exit_group,
        SYS_futex, SYS_getpid, SYS_gettid, SYS_madvise,
        SYS_mmap, SYS_mprotect, SYS_munmap, SYS_prctl,
        SYS_rt_sigaction, SYS_rt_sigprocmask, SYS_rt_sigreturn,
        SYS_rseq, SYS_seccomp, SYS_set_robust_list, SYS_sigaltstack,
        SYS_clock_gettime, SYS_sched_getaffinity,
    };
    constexpr auto kLoad = static_cast<std::uint16_t>(BPF_LD | BPF_W | BPF_ABS);
    constexpr auto kEq = static_cast<std::uint16_t>(BPF_JMP | BPF_JEQ | BPF_K);
    constexpr auto kGe = static_cast<std::uint16_t>(BPF_JMP | BPF_JGE | BPF_K);
    constexpr auto kReturn = static_cast<std::uint16_t>(BPF_RET | BPF_K);
    constexpr auto kDenied = SECCOMP_RET_ERRNO |
        static_cast<std::uint32_t>(EPERM);
    std::array<sock_filter, 7U + 2U * kHostRuntimeSyscalls.size()> rules{};
    std::size_t i = 0U;
    const auto statement = [&](std::uint16_t code, std::uint32_t value) {
        rules[i++] = sock_filter{.code = code, .jt = 0U, .jf = 0U, .k = value};
    };
    const auto jump = [&](std::uint16_t code, std::uint32_t value,
                          std::uint8_t yes, std::uint8_t no) {
        rules[i++] = sock_filter{.code = code, .jt = yes, .jf = no, .k = value};
    };
    statement(kLoad, static_cast<std::uint32_t>(offsetof(seccomp_data, arch)));
    jump(kEq, AUDIT_ARCH_X86_64, 1U, 0U);
    statement(kReturn, SECCOMP_RET_KILL_PROCESS);
    statement(kLoad, static_cast<std::uint32_t>(offsetof(seccomp_data, nr)));
    jump(kGe, 0x40000000U, 0U, 1U); // Reject x32 and high-number aliases.
    statement(kReturn, kDenied);
    for (const int number : kHostRuntimeSyscalls) {
        jump(kEq, static_cast<std::uint32_t>(number), 0U, 1U);
        statement(kReturn, SECCOMP_RET_ALLOW);
    }
    statement(kReturn, kDenied);
    if (i != rules.size() ||
        ::prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL) != 0)
        return false;
    sock_fprog program{
        .len = static_cast<unsigned short>(rules.size()),
        .filter = rules.data(),
    };
    return ::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0U, &program) == 0;
}

struct Terminal {
    e::GuestWorkerFault fault;
    e::GuestWorkerStop stop;
    std::uint64_t load_base = 0U;
};

[[nodiscard]] std::optional<Terminal> execute_pinned_owned_title(
    std::span<const std::byte> source,
    std::uint64_t& stage,
    bool restricted_leaf = false) {
    stage = 1U;
    if (!exact_pinned_title(source) ||
        !e::linux_guest_syscall_seccomp_available()) return std::nullopt;
    stage = 2U;
    auto base = choose_free_base();
    if (!base.has_value()) return std::nullopt;
    const auto stack_range = m::GuestRange::create(
        m::GuestAddress{base.value() + kStackOffset}, m::GuestSize{4096U});
    if (!stack_range.has_value()) return std::nullopt;

    stage = 3U;
    auto original = l::build_guest_image(l::GuestImageRequest{
        .image_bytes = std::vector<std::byte>(source.begin(), source.end()),
        .initial_stack = l::InitialStackRequest{
            .storage = stack_range.value(),
            .arguments = {"astraea-source-owned-ps5-format"},
            .environment = {},
            .auxiliary_vector = {},
        },
        .elf_profile = l::ElfParseProfile::ps5_sce,
    });
    if (!original.has_value() || original->elf.header.type != 0xfe10U ||
        original->elf.header.entry != 0x10U ||
        !original->dynamic_symbols.has_value() ||
        !original->general_relocations.rela.has_value() ||
        original->general_relocations.rela->count != 8U ||
        (original->general_relocations.rel.has_value() &&
         original->general_relocations.rel->count != 0U) ||
        (original->plt_relocations.has_value() &&
         original->plt_relocations->count != 0U) ||
        (original->dynamic_strings.has_value() &&
         !original->dynamic_strings->needed.empty()) ||
        (original->tls.has_value() &&
         original->tls->total_size.value() != 0U))
        return std::nullopt;

    stage = 4U;
    auto initial_view = original->initialized_image_view();
    if (!initial_view.has_value()) return std::nullopt;
    // Verify the exact first instruction through the source-backed image
    // view, which does not imply the eventual guest RX mapping grants READ.
    const auto first_range = m::GuestRange::create(
        m::GuestAddress{original->elf.header.entry}, m::GuestSize{2U});
    std::array<std::byte, 2U> verified_entry{};
    if (!first_range.has_value() ||
        !initial_view->copy_bytes(first_range.value(), verified_entry).has_value() ||
        verified_entry != std::array<std::byte, 2U>{
            std::byte{0x0f}, std::byte{0x0b}})
        return std::nullopt;
    std::vector<e::OwnedRelativePatch> patches;
    patches.reserve(8U);
    for (std::uint64_t i = 0U; i < 8U; ++i) {
        auto record = l::parse_dynamic_relocation(
            *original->general_relocations.rela, i,
            *original->dynamic_symbols, initial_view.value());
        if (!record.has_value() ||
            record->target.value() >= kMaxOriginalAddress) return std::nullopt;
        const auto shifted = m::GuestAddress::checked_add(
            m::GuestAddress{base.value()},
            m::GuestSize{record->target.value()});
        if (!shifted.has_value()) return std::nullopt;
        record->target = shifted.value();
        auto patch = e::build_owned_x86_64_relative_patch(
            record.value(), m::GuestAddress{base.value()});
        if (!patch.has_value()) return std::nullopt;
        patches.push_back(patch.value());
    }

    // This narrow test-only rebase modifies in-memory mapping ownership and
    // entry addresses, not the pinned ELF bytes or unverified Sony metadata.
    // The original dynamic relocation descriptors were parsed above.
    stage = 5U;
    auto image = std::move(original.value());
    for (auto& mapping : image.mappings) {
        const auto old_base = mapping.range.base().value();
        if (old_base >= kMaxOriginalAddress ||
            mapping.range.size().value() > kMaxOriginalAddress - old_base)
            return std::nullopt;
        auto shifted = m::GuestRange::create(
            m::GuestAddress{base.value() + old_base}, mapping.range.size());
        if (!shifted.has_value()) return std::nullopt;
        mapping.range = shifted.value();
    }
    image.elf.header.entry = base.value() + 0x10U;
    // The pinned source-built converter may retain a zero-length PT_TLS.
    // Native entry rejects any TLS descriptor. In this deliberately owned
    // first-UD2 experiment, only an empty descriptor may be dropped from
    // the transient image. Non-empty TLS and retail entry remain blocked.
    if (image.tls.has_value()) {
        if (image.tls->total_size.value() != 0U ||
            image.tls->initialized_size.value() != 0U)
            return std::nullopt;
        image.tls.reset();
    }
    stage = 6U;
    auto prepared = e::prepare_linux_guest_memory(image);
    if (!prepared.has_value()) return std::nullopt;
    e::GuestMemoryAccess memory{image, prepared.value()};

    stage = 7U;
    const auto applied = e::apply_owned_relative_batch(patches, memory);
    if (!applied.has_value()) {
        const auto& reason = applied.error();
        // Precise numeric failure code and index for research CI only.
        // Do not log guest bytes, pointer contents or a host pathname.
        stage = 7000U +
            100U * static_cast<std::uint64_t>(reason.code) +
            static_cast<std::uint64_t>(reason.patch_index);
        return std::nullopt;
    }
    if (applied->size() != 8U) {
        stage = 7999U;
        return std::nullopt;
    }
    // The RX entry segment need not grant guest READ. Its exact source
    // bytes were checked before mapping; a native SIGILL at that RIP is the
    // end-to-end instruction-fetch observation (no synthetic fault event).

    // Explicitly synthetic entry context: UD2 consumes no PS5 process
    // parameters. This NEVER promotes an initial-process ABI contract.
    stage = 8U;
    if (restricted_leaf) {
        // No effectful controller service channel exists in this leaf.
        // Its fixed untrusted observation mapping conveys data only after
        // child exit. The filter is installed before the backend starts its
        // guest thread, which inherits this policy; the existing IP-scoped
        // guest syscall trap remains an additional layer.
        if (!install_pinned_leaf_policy()) return std::nullopt;
        errno = 0;
        if (::syscall(SYS_write, STDOUT_FILENO, "!", 1U) != -1 ||
            errno != EPERM) return std::nullopt;
        errno = 0;
        if (::syscall(SYS_socket, 2, 1, 0) != -1 ||
            errno != EPERM) return std::nullopt;
    }
    auto run = e::enter_linux_guest_with_seccomp_syscall_trap(
        image, prepared.value(), e::make_synthetic_initial_context(image));
    if (!run.has_value()) {
        stage = 8000U + static_cast<std::uint64_t>(run.error().code);
        return std::nullopt;
    }
    stage = 9U;
    const auto* stop = std::get_if<e::ExecutionStop>(&run.value());
    if (stop == nullptr ||
        stop->reason != e::ExecutionStopReason::guest_fault ||
        !stop->has_fault ||
        stop->fault.kind != e::GuestFaultKind::illegal_instruction ||
        stop->fault.instruction_pointer != image.elf.header.entry)
        return std::nullopt;
    stage = 10U;
    auto projected =
        e::project_guest_worker_fault(*stop, kWorkerId, kThreadId);
    if (!projected.has_value() ||
        projected->kind != e::GuestWorkerFaultKind::illegal_instruction ||
        projected->guest_rip.value() != image.elf.header.entry)
        return std::nullopt;
    stage = 0U;
    return Terminal{
        .fault = projected.value(),
        .stop = e::GuestWorkerStop{
            .worker_id = kWorkerId,
            .thread_id = kThreadId,
            .reason = e::GuestWorkerStopReason::guest_fault,
            .guest_rip = m::GuestAddress{image.elf.header.entry},
        },
        .load_base = base.value(),
    };
}

[[nodiscard]] int worker_main() {
    if (!bind_controller()) return 10;
    // A sealed anonymous fd, never a host pathname, is the execution input.
    auto artifact = e::read_linux_sealed_worker_artifact(kMaxTitleBytes);
    const auto hello = receive();
    if (!hello.has_value() ||
        std::get_if<e::GuestWorkerHello>(&hello.value()) == nullptr)
        return 11;
    if (!send(e::GuestWorkerReady{
            .protocol_version = e::kGuestWorkerProtocolVersion,
            .worker_id = kWorkerId})) return 12;
    const auto request = receive();
    if (!request.has_value()) return 13;
    const auto* run = std::get_if<e::GuestWorkerRunRequest>(&request.value());
    if (run == nullptr ||
        !e::validate_guest_worker_run_request(*run).has_value()) return 14;

    const auto refuse = [&](e::GuestWorkerDiagnosticKind reason,
                            std::uint64_t failed_stage = 0U) {
        const m::GuestAddress zero{0U};
        if (!send(e::GuestWorkerDiagnostic{
                .worker_id = kWorkerId,
                .thread_id = kThreadId,
                .kind = reason,
                .guest_rip = zero,
                .detail0 = failed_stage,
                .detail1 = 0U,
            }) ||
            !send(e::GuestWorkerStop{
                .worker_id = kWorkerId,
                .thread_id = kThreadId,
                .reason = e::GuestWorkerStopReason::diagnostic_boundary,
                .guest_rip = zero,
            })) return 15;
        const auto terminate = receive();
        const auto* msg = terminate.has_value()
            ? std::get_if<e::GuestWorkerTerminate>(&terminate.value())
            : nullptr;
        return msg != nullptr &&
            msg->reason == e::GuestWorkerTerminationReason::
                unsupported_guest_behavior ? 0 : 16;
    };

    if (!artifact.has_value() ||
        !exact_pinned_title(artifact.value()))
        return refuse(e::GuestWorkerDiagnosticKind::loader_rejected);
    std::uint64_t stage = 0U;
    auto terminal = execute_pinned_owned_title(artifact.value(), stage);
    if (!terminal.has_value())
        return refuse(e::GuestWorkerDiagnosticKind::native_backend_error, stage);
    if (!send(terminal->fault) || !send(terminal->stop)) return 17;
    const auto terminate = receive();
    const auto* message = terminate.has_value()
        ? std::get_if<e::GuestWorkerTerminate>(&terminate.value())
        : nullptr;
    return message != nullptr &&
        message->reason ==
            e::GuestWorkerTerminationReason::fatal_guest_fault ? 0 : 18;
}

[[nodiscard]] std::optional<std::vector<std::byte>>
read_local_exact_title(std::string_view path);

// A bounded, untrusted *exit verdict*, not guest-controlled protocol IPC.
// The trusted controller never services a syscall request from this leaf.
[[nodiscard]] int pinned_leaf_main() {
    if (!bind_controller()) return 70;
    // RLIMITs predate guest entry; no filesystem, network or output handle
    // is inherited except /dev/null stdio and the sealed input fd3.
    const rlimit core_limit{0U, 0U};
    const rlimit cpu_limit{5U, 5U};
    const rlimit as_limit{4ULL * 1024ULL * 1024ULL * 1024ULL,
                          4ULL * 1024ULL * 1024ULL * 1024ULL};
    if (::setrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        ::setrlimit(RLIMIT_FSIZE, &core_limit) != 0 ||
        ::setrlimit(RLIMIT_CPU, &cpu_limit) != 0 ||
        ::setrlimit(RLIMIT_AS, &as_limit) != 0) return 71;
    auto bytes = e::read_linux_sealed_worker_artifact(kMaxTitleBytes);
    if (!bytes.has_value() || !exact_pinned_title(bytes.value()))
        return 72;
    // The parent owns a fixed-size anonymous observation memfd. It is
    // neither a controller socket nor a syscall broker. Close the fd
    // *before* guest entry; only a single writable shared-memory mapping
    // remains. The controller reads it only after reaping the child.
    struct stat report_info{};
    constexpr int kObservationSeals =
        F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
    const int seals = ::fcntl(kLeafStopFd, F_GET_SEALS);
    if (seals < 0 || (seals & kObservationSeals) != kObservationSeals ||
        ::fstat(kLeafStopFd, &report_info) != 0 ||
        !S_ISREG(report_info.st_mode) ||
        report_info.st_size != static_cast<off_t>(kLeafStopBytes))
        return 74;
    void* observation = ::mmap(
        nullptr, kLeafStopBytes, PROT_READ | PROT_WRITE,
        MAP_SHARED, kLeafStopFd, 0);
    if (observation == MAP_FAILED) return 74;
    if (::close(kLeafStopFd) != 0) return 74;
    std::uint64_t stage = 0U;
    auto result = execute_pinned_owned_title(bytes.value(), stage, true);
    if (!result.has_value() || stage != 0U ||
        result->fault.kind != e::GuestWorkerFaultKind::illegal_instruction ||
        result->stop.reason != e::GuestWorkerStopReason::guest_fault ||
        result->stop.guest_rip != result->fault.guest_rip ||
        result->stop.guest_rip.value() < result->load_base)
        return 73;
    // This record is constructed only after observing the actual native
    // stop, not inferred from the source-level expected UD2 instruction.
    const LeafStopRecord record{
        kLeafStopMagic,
        kLeafStopVersion,
        kLeafIllegalInstruction,
        result->stop.guest_rip.value() - result->load_base,
        8U,
        result->fault.worker_id.value,
        result->fault.thread_id.value,
        kLeafGuestFault,
    };
    std::memcpy(observation, record.data(), kLeafStopBytes);
    // No write/pwrite/msync syscall or effectful controller callback.
    return 0;
}

struct SourceFd {
    int fd = -1;
    ~SourceFd() noexcept { if (fd >= 0) (void)::close(fd); }
    SourceFd() = default;
    explicit SourceFd(int value) : fd(value) {}
    SourceFd(const SourceFd&) = delete;
    SourceFd& operator=(const SourceFd&) = delete;
};

// The controller owns this memfd and keeps the source independently pinned.
// The child independently verifies required seals and source SHA-256.
[[nodiscard]] bool prepare_sealed_source(
    std::span<const std::byte> bytes, SourceFd& source) {
    source.fd = static_cast<int>(::syscall(
        SYS_memfd_create, "astraea-c1-fixed-ud2",
        MFD_CLOEXEC | MFD_ALLOW_SEALING));
    if (source.fd < 0) return false;
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(
            source.fd, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    constexpr int seals = F_SEAL_WRITE | F_SEAL_GROW |
        F_SEAL_SHRINK | F_SEAL_SEAL;
    return ::fcntl(source.fd, F_ADD_SEALS, seals) == 0 &&
           ::fcntl(source.fd, F_GET_SEALS) == seals;
}

[[nodiscard]] std::optional<int> run_isolated_leaf(
    const std::string& executable,
    std::span<const std::byte> source_bytes,
    LeafStopRecord* observation_out = nullptr) {
    SourceFd artifact;
    if (!prepare_sealed_source(source_bytes, artifact)) return std::nullopt;
    // Reserve fd3 for immutable source and fd4 for the strictly fixed-size
    // one-way observation memory. Both are closed in the leaf before entry.
    SourceFd report{static_cast<int>(::syscall(
        SYS_memfd_create, "astraea-c1-typed-stop",
        MFD_CLOEXEC | MFD_ALLOW_SEALING))};
    if (report.fd < 0 ||
        ::ftruncate(report.fd, static_cast<off_t>(kLeafStopBytes)) != 0)
        return std::nullopt;
    constexpr int kObservationSeals =
        F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL;
    if (::fcntl(report.fd, F_ADD_SEALS, kObservationSeals) != 0)
        return std::nullopt;
    SourceFd transport{::fcntl(artifact.fd, F_DUPFD_CLOEXEC, 5)};
    SourceFd report_transport{::fcntl(report.fd, F_DUPFD_CLOEXEC, 6)};
    if (transport.fd < 5 || report_transport.fd < 6)
        return std::nullopt;
    posix_spawn_file_actions_t actions{};
    if (::posix_spawn_file_actions_init(&actions) != 0)
        return std::nullopt;
    struct ActionsGuard {
        posix_spawn_file_actions_t& value;
        ~ActionsGuard() noexcept {
            (void)::posix_spawn_file_actions_destroy(&value);
        }
    } cleanup{actions};
    if (::posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0) != 0 ||
        ::posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
        ::posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0) != 0 ||
        ::posix_spawn_file_actions_adddup2(
            &actions, transport.fd, e::kLinuxWorkerArtifactFd) != 0 ||
        ::posix_spawn_file_actions_adddup2(
            &actions, report_transport.fd, kLeafStopFd) != 0 ||
        ::posix_spawn_file_actions_addclosefrom_np(&actions, 5) != 0)
        return std::nullopt;

    posix_spawnattr_t attributes{};
    if (::posix_spawnattr_init(&attributes) != 0)
        return std::nullopt;
    struct AttrGuard {
        posix_spawnattr_t& value;
        ~AttrGuard() noexcept { (void)::posix_spawnattr_destroy(&value); }
    } attr_cleanup{attributes};
    if (::posix_spawnattr_setflags(
            &attributes, POSIX_SPAWN_SETPGROUP) != 0 ||
        ::posix_spawnattr_setpgroup(&attributes, 0) != 0)
        return std::nullopt;
    std::string expected_pid = "ASTRAEA_CONTROLLER_PID=" +
        std::to_string(::getpid());
    char locale[] = "LC_ALL=C";
    char* envp[]{expected_pid.data(), locale, nullptr};
    char* argv[]{const_cast<char*>(executable.c_str()),
                 const_cast<char*>("--isolated-leaf"), nullptr};
    pid_t pid = -1;
    if (::posix_spawn(&pid, executable.c_str(), &actions, &attributes,
                      argv, envp) != 0 || pid <= 0) return std::nullopt;
    // Bound every child, including descendants in its distinct process group.
    int status = 0;
    pid_t waited = 0;
    for (unsigned n = 0; n < 500U; ++n) {
        waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0 && errno != EINTR) break;
        (void)::usleep(10000);
    }
    if (waited != pid) {
        (void)::kill(-pid, SIGKILL);
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, &status, 0);
        return std::nullopt;
    }
    if (!WIFEXITED(status)) return std::nullopt;
    LeafStopRecord observed{};
    // The child has exited, so even a compromised guest can no longer
    // change this record. No controller-side effects occur while it runs.
    const auto bytes_read = ::pread(
        report.fd, observed.data(), kLeafStopBytes, 0);
    if (bytes_read != static_cast<ssize_t>(kLeafStopBytes))
        return std::nullopt;
    if (observation_out != nullptr)
        *observation_out = observed;
    return WEXITSTATUS(status);
}

[[nodiscard]] int isolated_controller_main(std::string_view title_path) {
    auto input = read_local_exact_title(title_path);
    if (!input.has_value()) return 2;
    const std::string executable =
        std::filesystem::read_symlink("/proc/self/exe").string();
    LeafStopRecord first{};
    for (unsigned run = 0; run < 2U; ++run) {
        LeafStopRecord observed{};
        auto status = run_isolated_leaf(executable, input.value(), &observed);
        if (!status.has_value() || status.value() != 0 ||
            !valid_leaf_stop(observed) ||
            (run == 1U && observed != first)) {
            std::cerr << "Pinned isolated leaf typed stop failed on run "
                      << run << " child="
                      << (status.has_value() ? status.value() : -1) << "\n";
            return 3;
        }
        if (run == 0U) first = observed;
    }
    // Eight independently corrupted fields, including the exact RIP,
    // version, identity and stop type, must all be refused by the parent.
    for (std::size_t field = 0U; field < first.size(); ++field) {
        auto forged = first;
        forged[field] ^= 1U;
        if (valid_leaf_stop(forged)) return 5;
    }
    // Check actual leaf admission, not only controller-side byte hashing.
    auto tampered = input.value();
    tampered[0] ^= std::byte{1U};
    LeafStopRecord rejected_observation{};
    const auto denied = run_isolated_leaf(
        executable, tampered, &rejected_observation);
    if (!denied.has_value() || denied.value() != 72 ||
        rejected_observation != LeafStopRecord{}) {
        std::cerr << "Tampered isolated leaf artifact was not refused\n";
        return 4;
    }
    std::cout << "PINNED OWNED PS5-FORMAT ISOLATED LEAF PASS\n"
              << "source_sha256=" << kTitleSha256 << "\n"
              << "isolated_actual_guestimage_ud2_runs=2\n"
              << "relocations_per_run=8\n"
              << "leaf_report=bounded_untrusted_typed_stop_v1\n"
              << "observed_stop=guest_fault_illegal_instruction\n"
              << "normalized_guest_rip=0x10\n"
              << "malformed_report_fields=8_refused\n"
              << "repeat_normalized_reports=identical\n"
              << "tampered_sealed_input=refused_by_leaf\n"
              << "effectful_controller_ipc=not_inherited\n"
              << "process_entry_profile=synthetic_research_only\n"
              << "hostile_code_containment=not_established\n"
              << "commercial_title_execution=not_attempted\n";
    return 0;
}

[[nodiscard]] std::optional<std::vector<std::byte>>
read_local_exact_title(std::string_view path) {
    std::ifstream file(std::string{path}, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto length = file.tellg();
    if (length <= 0 ||
        length > static_cast<std::streamoff>(kMaxTitleBytes))
        return std::nullopt;
    std::vector<std::byte> result(static_cast<std::size_t>(length));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(result.data()),
              static_cast<std::streamsize>(result.size()));
    if (file.gcount() != static_cast<std::streamsize>(result.size()))
        return std::nullopt;
    if (!exact_pinned_title(result)) return std::nullopt;
    return result;
}

[[nodiscard]] int controller_main(std::string_view title_path) {
    auto input = read_local_exact_title(title_path);
    if (!input.has_value()) {
        std::cerr << "Pinned source identity or input read rejected\n";
        return 2;
    }
    const auto executable =
        std::filesystem::read_symlink("/proc/self/exe").string();
    e::GuestWorkerProcessSessionConfig config{
        .worker_executable = executable,
        .worker_arguments = {"--sealed-worker"},
        .run_budget_microseconds = 2'000'000U,
        .timeout_milliseconds = 15'000U,
        .syscall_service = {},
        .max_syscall_requests = 0U,
        .resource_policy = e::GuestWorkerResourcePolicy{
            .process_memory_limit_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL,
            .process_cpu_time_seconds = 5U,
            .linux_max_open_files = 32U,
            .linux_disable_core_dumps = true,
            .linux_disable_file_growth = true,
        },
        .linux_artifact_bytes = input.value(),
    };

    for (std::size_t i = 0U; i < 2U; ++i) {
        auto run = e::run_guest_worker_process_session(config);
        if (!run.has_value() || run->child_exit_code != 0 ||
            run->terminal_diagnostic.has_value() ||
            !run->terminal_fault.has_value() ||
            run->terminal_fault->kind !=
                e::GuestWorkerFaultKind::illegal_instruction ||
            run->stop.reason != e::GuestWorkerStopReason::guest_fault ||
            run->stop.guest_rip != run->terminal_fault->guest_rip ||
            run->stop.guest_rip.value() == 0U ||
            run->syscall_request_count != 0U) {
            std::cerr << "Controlled first-instruction worker run failed: "
                      << i;
            if (!run.has_value()) {
                std::cerr << " session_error="
                          << static_cast<unsigned>(run.error().code);
            } else {
                std::cerr << " child=" << run->child_exit_code;
                if (run->terminal_diagnostic.has_value())
                    std::cerr << " diagnostic="
                              << static_cast<unsigned>(run->terminal_diagnostic->kind)
                              << " stage=" << run->terminal_diagnostic->detail0;
                if (run->terminal_fault.has_value())
                    std::cerr << " fault="
                              << static_cast<unsigned>(run->terminal_fault->kind);
            }
            std::cerr << "\n";
            return 3;
        }
    }

    // Same controller/sealed-fd path must refuse a tampered source before
    // guest entry. Never allow an arbitrary SCE ELF via this test harness.
    auto modified = config;
    modified.linux_artifact_bytes->at(0U) ^= std::byte{1U};
    auto denied = e::run_guest_worker_process_session(modified);
    if (!denied.has_value() || denied->child_exit_code != 0 ||
        !denied->terminal_diagnostic.has_value() ||
        denied->terminal_diagnostic->kind !=
            e::GuestWorkerDiagnosticKind::loader_rejected ||
        denied->terminal_fault.has_value() ||
        denied->stop.reason !=
            e::GuestWorkerStopReason::diagnostic_boundary) {
        std::cerr << "Tampered source was not refused before entry\n";
        return 4;
    }

    std::cout << "PINNED OWNED PS5-FORMAT FIRST INSTRUCTION PASS\n"
              << "source_sha256=" << kTitleSha256 << "\n"
              << "relocations=8\n"
              << "supervised_repeated_guest_faults=2\n"
              << "first_opcode=UD2\n"
              << "tampered_source=refused_before_entry\n"
              << "process_entry_profile=synthetic_research_only\n"
              << "commercial_title_execution=not_attempted\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) return 1;
        if (std::string_view{argv[1]} == "--sealed-worker")
            return worker_main();
        if (std::string_view{argv[1]} == "--isolated-leaf")
            return pinned_leaf_main();
        if (argc == 3 && std::string_view{argv[1]} == "--isolated")
            return isolated_controller_main(argv[2]);
        if (argc != 2) return 1;
        return controller_main(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "Research probe infrastructure error: "
                  << error.what() << "\n";
        return 99;
    }
}
