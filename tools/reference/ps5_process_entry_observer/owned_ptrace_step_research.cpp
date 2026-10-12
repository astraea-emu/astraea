// C1 Linux x86-64 security research: independent tracer-owned instruction
// observations. Executes ONLY fixed Astraea-authored instruction
// sequences below, never an external ELF or an arbitrary guest instruction.
// This is not a hostile-code sandbox or a PS5 ABI implementation.
#include "owned_pair_sha256.hpp"

#include <astraea/execution/owned_relative_patch.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>
#include <linux/memfd.h>
#include <array>
#include <span>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <string_view>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/prctl.h>

namespace {
constexpr std::uint64_t kGuestBase = 0x0000000100000000ULL;
constexpr std::uint64_t kHostSentinelBase = 0x0000000100010000ULL;
constexpr std::uint64_t kCanaryBase = 0x0000000100020000ULL;
constexpr std::uint64_t kNoAccessBase = 0x0000000100030000ULL;
constexpr std::uint64_t kControllerOnlyBase = 0x0000000100040000ULL;
constexpr std::size_t kPage = 4096U;
constexpr std::size_t kMaxSteps = 8U;

enum class Mode { fault, syscall, escape, write_rx, write_none, write_rw, write_rw_guarded, write_controller_only };

[[nodiscard]] bool is_write_mode(Mode mode) noexcept {
    return mode == Mode::write_rx || mode == Mode::write_none ||
        mode == Mode::write_rw || mode == Mode::write_rw_guarded ||
        mode == Mode::write_controller_only;
}
[[nodiscard]] std::uint64_t write_target_for(Mode mode) noexcept {
    switch (mode) {
    case Mode::write_rx: return kHostSentinelBase;
    case Mode::write_none: return kNoAccessBase;
    case Mode::write_rw:
    case Mode::write_rw_guarded: return kCanaryBase;
    case Mode::write_controller_only: return kControllerOnlyBase;
    default: return 0U;
    }
}

[[nodiscard]] std::optional<Mode> parse_mode(std::string_view name) {
    if (name == "fault") return Mode::fault;
    if (name == "syscall") return Mode::syscall;
    if (name == "escape") return Mode::escape;
    if (name == "write-rx") return Mode::write_rx;
    if (name == "write-none") return Mode::write_none;
    if (name == "write-rw") return Mode::write_rw;
    if (name == "write-rw-guarded") return Mode::write_rw_guarded;
    if (name == "write-controller-only") return Mode::write_controller_only;
    return std::nullopt;
}
[[nodiscard]] const char* mode_name(Mode mode) {
    switch (mode) {
    case Mode::fault: return "fault";
    case Mode::syscall: return "syscall";
    case Mode::escape: return "escape";
    case Mode::write_rx: return "write-rx";
    case Mode::write_none: return "write-none";
    case Mode::write_rw: return "write-rw";
    case Mode::write_rw_guarded: return "write-rw-guarded";
    case Mode::write_controller_only: return "write-controller-only";
    }
    return "invalid";
}

// Complete instruction bytes are Astraea-owned, fixed at compile time.
// fault: mov eax,42; ud2
// syscall: mov eax,60; xor edi,edi; syscall; ud2
// escape: movabs rax,kHostSentinelBase; jmp rax; ud2
// writes: movabs rax,exact address; mov byte ptr [rax],0x7f; ud2
struct Probe {
    std::array<unsigned char, 16U> bytes{};
    std::size_t length = 0U;
    std::uint64_t terminal_rip = 0U;
    std::uint64_t write_target = 0U;
};
[[nodiscard]] Probe probe_for(Mode mode) {
    Probe p{};
    if (mode == Mode::fault) {
        p.bytes = {0xb8, 0x2a, 0, 0, 0, 0x0f, 0x0b};
        p.length = 7U;
        p.terminal_rip = kGuestBase + 5U;
    } else if (mode == Mode::syscall) {
        p.bytes = {0xb8, 0x3c, 0, 0, 0, 0x31, 0xff, 0x0f, 0x05, 0x0f, 0x0b};
        p.length = 11U;
        p.terminal_rip = kGuestBase + 7U;
    } else if (mode == Mode::escape) {
        p.bytes = {0x48, 0xb8, 0, 0, 1, 0, 1, 0, 0, 0, 0xff, 0xe0, 0x0f, 0x0b};
        p.length = 14U;
        p.terminal_rip = kHostSentinelBase;
    } else if (is_write_mode(mode)) {
        p.write_target = write_target_for(mode);
        p.bytes[0] = 0x48U; // movabs rax,imm64
        p.bytes[1] = 0xb8U;
        for (std::size_t i = 0U; i < 8U; ++i)
            p.bytes[i + 2U] = static_cast<unsigned char>(
                (p.write_target >> (8U * i)) & 0xffU);
        p.bytes[10] = 0xc6U; // mov byte ptr [rax],0x7f
        p.bytes[11] = 0x00U;
        p.bytes[12] = 0x7fU;
        p.bytes[13] = 0x0fU; // UD2, never reached in negative cases
        p.bytes[14] = 0x0bU;
        p.length = 15U;
        p.terminal_rip = kGuestBase + 10U;
    }
    return p;
}

// If the escaped-to host page were ever executed, this code would write
// 0x7f to the child-owned canary. The tracer must refuse *before* its first
// instruction; neither the sentinel nor the canary belongs to the guest.
constexpr std::array<unsigned char, 15U> kHostSentinel{
    0x48, 0xb8, 0, 0, 2, 0, 1, 0, 0, 0, // movabs rax,kCanaryBase
    0xc6, 0x00, 0x7f,                 // mov byte ptr [rax],0x7f
    0x0f, 0x0b                        // ud2
};

[[nodiscard]] bool map_exact(std::uint64_t address, int protection,
                             std::span<const unsigned char> bytes) {
    void* const requested = reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(address));
    void* const mapped = ::mmap(requested, kPage, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mapped == MAP_FAILED) return false;
    if (mapped != requested) {
        (void)::munmap(mapped, kPage);
        return false;
    }
    if (!bytes.empty()) std::memcpy(mapped, bytes.data(), bytes.size());
    return ::mprotect(mapped, kPage, protection) == 0;
}

[[nodiscard]] bool controller_identity() {
    const char* const value = ::getenv("ASTRAEA_PTRACE_PARENT");
    if (value == nullptr || *value == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    return errno == 0 && end != value && *end == '\0' &&
        parsed > 1UL && parsed <= static_cast<unsigned long>(
            std::numeric_limits<pid_t>::max()) &&
        ::getppid() == static_cast<pid_t>(parsed);
}

[[nodiscard]] int leaf(Mode mode) {
    // Trusted bootstrap finishes before PTRACE_TRACEME and SIGSTOP.
    // Controller sets RIP directly to the inspected guest page on that stop.
    // The bootstrap's host continuation is never intentionally resumed.
    if (!controller_identity() ||
        ::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 ||
        !controller_identity()) return 91;

    const rlimit no_core{0, 0};
    if (::setrlimit(RLIMIT_CORE, &no_core) != 0) return 92;
    const int nullfd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd < 0) return 93;
    if (::dup2(nullfd, STDIN_FILENO) < 0 ||
        ::dup2(nullfd, STDOUT_FILENO) < 0 ||
        ::dup2(nullfd, STDERR_FILENO) < 0)
        return 94;
    // Linux-only fail-closed descriptor cleanup; no effectful controller
    // pipe/file/socket is intentionally inherited into the tracee.
    if (::syscall(SYS_close_range, 3U, ~0U, 0U) != 0) return 95;

    const Probe probe = probe_for(mode);
    if (!map_exact(kGuestBase, PROT_READ | PROT_EXEC,
                   {probe.bytes.data(), probe.length}) ||
        !map_exact(kHostSentinelBase, PROT_READ | PROT_EXEC,
                   kHostSentinel) ||
        !map_exact(kCanaryBase, PROT_READ | PROT_WRITE, {}) ||
        !map_exact(kNoAccessBase, PROT_NONE, {}))
        return 96;
    if (::ptrace(PTRACE_TRACEME, 0, nullptr, nullptr) != 0) return 97;
    if (::raise(SIGSTOP) != 0) return 98;
    // If this host instruction runs, the tracer mistakenly resumed the
    // bootstrap instead of applying the guest RIP boundary.
    return 99;
}

struct ChildGuard {
    pid_t pid = -1;
    ~ChildGuard() {
        if (pid <= 0) return;
        (void)::kill(pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
};

// Map AFTER fork, only in the independent controller address space.
// The fixed low guest-like VA intentionally matches the tracee's attempted
// absolute pointer, but the exec-separated tracee must not map it.
struct ControllerCanary {
    unsigned char* bytes = nullptr;
    ~ControllerCanary() noexcept {
        if (bytes != nullptr) (void)::munmap(bytes, kPage);
    }
    ControllerCanary(const ControllerCanary&) = delete;
    ControllerCanary& operator=(const ControllerCanary&) = delete;
    ControllerCanary() = default;
    [[nodiscard]] bool establish() noexcept {
        auto* requested = reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(kControllerOnlyBase));
        void* mapped = ::mmap(
            requested, kPage, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (mapped == MAP_FAILED) return false;
        if (mapped != requested) {
            (void)::munmap(mapped, kPage);
            return false;
        }
        bytes = static_cast<unsigned char*>(mapped);
        bytes[0] = 0x5aU;
        return true;
    }
    [[nodiscard]] bool intact() const noexcept {
        return bytes != nullptr && bytes[0] == 0x5aU;
    }
};

[[nodiscard]] bool wait_stop(pid_t child, int& signal) {
    int status = 0;
    pid_t got = -1;
    do {
        got = ::waitpid(child, &status, 0);
    } while (got < 0 && errno == EINTR);
    if (got != child || !WIFSTOPPED(status)) return false;
    signal = WSTOPSIG(status);
    return true;
}

[[nodiscard]] std::optional<user_regs_struct> registers(pid_t child) {
    user_regs_struct r{};
    if (::ptrace(PTRACE_GETREGS, child, nullptr, &r) != 0)
        return std::nullopt;
    return r;
}

[[nodiscard]] bool inspect_bytes(pid_t child, std::uint64_t address,
                                 std::span<const unsigned char> expected) {
    std::size_t index = 0U;
    while (index < expected.size()) {
        errno = 0;
        const long word = ::ptrace(
            PTRACE_PEEKDATA, child,
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                address + index)), nullptr);
        if (word == -1L && errno != 0) return false;
        const auto chunk = std::min(expected.size() - index, sizeof(word));
        if (std::memcmp(&word, expected.data() + index, chunk) != 0)
            return false;
        index += chunk;
    }
    return true;
}

[[nodiscard]] bool canary_untouched(pid_t child) {
    constexpr std::array<unsigned char, sizeof(long)> zeros{};
    return inspect_bytes(child, kCanaryBase, zeros);
}

struct Evidence {
    Mode mode;
    std::size_t executed = 0U;
    std::uint64_t terminal_rip = 0U;
    bool stopped = false;
    bool syscall_prevented = false;
    bool host_escape_denied = false;
    bool host_canary_unchanged = false;
    bool only_tracer_observations = false;
    bool memory_protection_fault = false;
    bool writable_non_guest_memory_modified = false;
    bool non_guest_write_denied_before_step = false;
    bool controller_only_canary_unchanged = false;
    bool controller_mapping_absent_in_tracee = false;
};

[[nodiscard]] std::optional<Evidence> run_once(Mode mode) {
    // SIGALRM terminates a stuck tracer; PTRACE_O_EXITKILL and
    // PR_SET_PDEATHSIG terminate the tracee if this controller dies.
    ::alarm(10U);
    const pid_t child = ::fork();
    if (child < 0) return std::nullopt;
    if (child == 0) {
        char pid_text[32]{};
        if (std::snprintf(pid_text, sizeof(pid_text), "%ld",
                          static_cast<long>(::getppid())) <= 0 ||
            ::setenv("ASTRAEA_PTRACE_PARENT", pid_text, 1) != 0)
            ::_exit(120);
        ::execl("/proc/self/exe", "astraea_c1_ptrace_step_research",
                "--leaf", mode_name(mode), static_cast<char*>(nullptr));
        ::_exit(121);
    }
    ChildGuard guard{child};
    ControllerCanary controller_canary{};
    if (mode == Mode::write_controller_only &&
        !controller_canary.establish())
        return std::nullopt;
    int signal = 0;
    if (!wait_stop(child, signal) || signal != SIGSTOP ||
        ::ptrace(PTRACE_SETOPTIONS, child, nullptr,
                 PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL |
                 PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                 PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC) != 0)
        return std::nullopt;

    const Probe probe = probe_for(mode);
    if (mode == Mode::write_controller_only) {
        // Independent ptrace read must fail before guest entry: the page
        // exists in the tracer, not in the exec-separated tracee.
        errno = 0;
        const long mapped = ::ptrace(
            PTRACE_PEEKDATA, child,
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(
                kControllerOnlyBase)), nullptr);
        if (mapped != -1L || errno == 0 ||
            !controller_canary.intact())
            return std::nullopt;
    }
    if (!inspect_bytes(child, kGuestBase,
                       {probe.bytes.data(), probe.length}) ||
        !inspect_bytes(child, kHostSentinelBase, kHostSentinel) ||
        !canary_untouched(child))
        return std::nullopt;

    auto regs = registers(child);
    if (!regs.has_value()) return std::nullopt;
    regs->rip = kGuestBase;
    regs->rax = 0U;
    if (::ptrace(PTRACE_SETREGS, child, nullptr, &regs.value()) != 0)
        return std::nullopt;

    Evidence evidence{.mode = mode};
    evidence.controller_mapping_absent_in_tracee =
        mode == Mode::write_controller_only;
    for (std::size_t steps = 0; steps < kMaxSteps; ++steps) {
        const auto before = registers(child);
        if (!before.has_value()) return std::nullopt;
        // Never resume even a single host instruction once guest RIP
        // escapes the *exact* reviewed code-byte range.
        if (before->rip < kGuestBase ||
            before->rip >= kGuestBase + probe.length) {
            if (mode != Mode::escape ||
                before->rip != kHostSentinelBase ||
                evidence.executed != 2U)
                return std::nullopt;
            evidence.terminal_rip = before->rip;
            evidence.host_escape_denied = true;
            evidence.host_canary_unchanged = canary_untouched(child);
            evidence.stopped = evidence.host_canary_unchanged;
            evidence.only_tracer_observations = true;
            ::alarm(0U);
            return evidence.stopped ? std::optional{evidence} : std::nullopt;
        }

        // Positive control (write_rw) demonstrates that ptrace single-
        // stepping alone DOES NOT isolate guest writes. This separate,
        // source-owned guarded case refuses the exact reviewed store before
        // asking the kernel to retire it. The canary is ordinary writable
        // host memory, so the absence of a write is a tracer decision,
        // not a hardware page-permission accident.
        if (mode == Mode::write_rw_guarded &&
            before->rip == kGuestBase + 10U) {
            constexpr std::array<unsigned char, 3U> store{
                0xc6U, 0x00U, 0x7fU,
            };
            if (evidence.executed != 1U ||
                before->rax != kCanaryBase ||
                !inspect_bytes(child, kGuestBase + 10U, store) ||
                !canary_untouched(child))
                return std::nullopt;
            evidence.terminal_rip = before->rip;
            evidence.non_guest_write_denied_before_step = true;
            evidence.host_canary_unchanged = true;
            evidence.stopped = true;
            evidence.only_tracer_observations = true;
            ::alarm(0U);
            return evidence;
        }
        if (::ptrace(PTRACE_SYSEMU_SINGLESTEP, child, nullptr, nullptr) != 0 ||
            !wait_stop(child, signal))
            return std::nullopt;
        const auto after = registers(child);
        if (!after.has_value()) return std::nullopt;

        // A syscall-stop occurs *before* host-kernel side effects. Never
        // issue a second resume at this syscall entry. Checking orig_rax
        // makes a fake ordinary SIGTRAP insufficient for this proof.
        if (mode == Mode::syscall &&
            before->rip == probe.terminal_rip &&
            signal == (SIGTRAP | 0x80) &&
            after->orig_rax == static_cast<std::uint64_t>(SYS_exit) &&
            (after->rip == before->rip ||
             after->rip == before->rip + 2U)) {
            evidence.terminal_rip = before->rip;
            evidence.syscall_prevented = true;
            evidence.host_canary_unchanged = canary_untouched(child);
            evidence.stopped = evidence.host_canary_unchanged;
            evidence.only_tracer_observations = true;
            ::alarm(0U);
            return evidence.stopped ? std::optional{evidence} : std::nullopt;
        }

        if (mode == Mode::fault &&
            before->rip == probe.terminal_rip &&
            signal == SIGILL && after->rip == before->rip &&
            after->rax == 42U && evidence.executed == 1U) {
            evidence.terminal_rip = after->rip;
            evidence.stopped = true;
            evidence.host_canary_unchanged = canary_untouched(child);
            evidence.only_tracer_observations = true;
            ::alarm(0U);
            return evidence.host_canary_unchanged
                ? std::optional{evidence} : std::nullopt;
        }

        // The test-only memory probes explicitly distinguish faults from
        // already-committed writes. A tracer cannot undo the effects of an
        // instruction that was permitted to write native host memory.
        if (is_write_mode(mode) && before->rip == probe.terminal_rip &&
            evidence.executed == 1U &&
            before->rax == probe.write_target &&
            after->rax == probe.write_target) {
            if (mode == Mode::write_rw) {
                constexpr std::array<unsigned char, sizeof(long)> changed{
                    0x7fU,
                };
                if (signal != SIGTRAP ||
                    after->rip != kGuestBase + 13U ||
                    !inspect_bytes(child, kCanaryBase, changed) ||
                    !inspect_bytes(child, kHostSentinelBase, kHostSentinel))
                    return std::nullopt;
                ++evidence.executed;
                evidence.writable_non_guest_memory_modified = true;
                evidence.host_canary_unchanged = false;
            } else {
                siginfo_t info{};
                if (signal != SIGSEGV ||
                    after->rip != before->rip ||
                    ::ptrace(PTRACE_GETSIGINFO, child, nullptr, &info) != 0 ||
                    info.si_signo != SIGSEGV ||
                    info.si_code !=
                        (mode == Mode::write_controller_only
                             ? SEGV_MAPERR : SEGV_ACCERR) ||
                    reinterpret_cast<std::uintptr_t>(info.si_addr) !=
                        probe.write_target ||
                    !canary_untouched(child) ||
                    !inspect_bytes(child, kHostSentinelBase, kHostSentinel))
                    return std::nullopt;
                if (mode == Mode::write_controller_only) {
                    if (!controller_canary.intact())
                        return std::nullopt;
                    evidence.controller_only_canary_unchanged = true;
                }
                evidence.memory_protection_fault = true;
                evidence.host_canary_unchanged = true;
            }
            evidence.terminal_rip = before->rip;
            evidence.stopped = true;
            evidence.only_tracer_observations = true;
            ::alarm(0U);
            return evidence;
        }

        // All other steps must be ordinary instruction single-step traps
        // with an expected next RIP; no signal handler or worker message
        // is trusted to describe the transition.
        if (signal != SIGTRAP) return std::nullopt;
        ++evidence.executed;
        if (mode == Mode::fault) {
            if (evidence.executed != 1U ||
                after->rip != kGuestBase + 5U || after->rax != 42U)
                return std::nullopt;
        } else if (mode == Mode::syscall) {
            if (evidence.executed == 1U &&
                after->rip != kGuestBase + 5U)
                return std::nullopt;
            if (evidence.executed == 2U &&
                after->rip != kGuestBase + 7U)
                return std::nullopt;
            if (evidence.executed > 2U) return std::nullopt;
        } else if (mode == Mode::escape) {
            if (evidence.executed == 1U &&
                after->rip != kGuestBase + 10U)
                return std::nullopt;
            if (evidence.executed == 2U &&
                after->rip != kHostSentinelBase)
                return std::nullopt;
            if (evidence.executed > 2U) return std::nullopt;
        } else {
            if (evidence.executed != 1U ||
                after->rip != kGuestBase + 10U ||
                after->rax != probe.write_target)
                return std::nullopt;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool equal_evidence(const Evidence& a, const Evidence& b) {
    return a.mode == b.mode && a.executed == b.executed &&
        a.terminal_rip == b.terminal_rip && a.stopped == b.stopped &&
        a.syscall_prevented == b.syscall_prevented &&
        a.host_escape_denied == b.host_escape_denied &&
        a.host_canary_unchanged == b.host_canary_unchanged &&
        a.only_tracer_observations == b.only_tracer_observations &&
        a.memory_protection_fault == b.memory_protection_fault &&
        a.writable_non_guest_memory_modified ==
            b.writable_non_guest_memory_modified &&
        a.non_guest_write_denied_before_step ==
            b.non_guest_write_denied_before_step &&
        a.controller_only_canary_unchanged ==
            b.controller_only_canary_unchanged &&
        a.controller_mapping_absent_in_tracee ==
            b.controller_mapping_absent_in_tracee;
}

// The actual pinned minimal PS5-format SCE ELF from the separate native
// observer workflow. Hash authority is checked by BOTH parent and leaf;
// only the existing owned research tool builds/relocates its GuestImage.
constexpr std::string_view kPinnedOwnedSha =
    "45920de847b2d33896c186654bbf22a728f1910fad84accb15204aa2da50c678";
constexpr std::uint64_t kPinnedEntry = kGuestBase + 0x10U;
constexpr std::uint64_t kPinnedStackOffset = 24ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxPinnedSize = 16U * 1024U * 1024U;

struct SourceFd {
    int fd = -1;
    ~SourceFd() { if (fd >= 0) (void)::close(fd); }
    SourceFd() = default;
    explicit SourceFd(int value) : fd(value) {}
    SourceFd(const SourceFd&) = delete;
    SourceFd& operator=(const SourceFd&) = delete;
};

[[nodiscard]] std::optional<std::vector<std::byte>>
read_exact_owned_source(std::string_view name) {
    std::ifstream file(std::string{name}, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto end = file.tellg();
    if (end <= 0 || end > static_cast<std::streamoff>(kMaxPinnedSize))
        return std::nullopt;
    std::vector<std::byte> source(static_cast<std::size_t>(end));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(source.data()),
              static_cast<std::streamsize>(source.size()));
    if (file.gcount() != static_cast<std::streamsize>(source.size()) ||
        astraea::test::owned_pair_sha256_hex(source) != kPinnedOwnedSha)
        return std::nullopt;
    return source;
}

[[nodiscard]] bool seal_owned_source(
    std::span<const std::byte> bytes, SourceFd& artifact) {
    artifact.fd = static_cast<int>(::syscall(SYS_memfd_create,
        "astraea-c1-verified-owned-trace",
        MFD_ALLOW_SEALING | MFD_CLOEXEC));
    if (artifact.fd < 0) return false;
    std::size_t at = 0U;
    while (at < bytes.size()) {
        const ssize_t written = ::write(
            artifact.fd, bytes.data() + at, bytes.size() - at);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        at += static_cast<std::size_t>(written);
    }
    constexpr int seals = F_SEAL_WRITE | F_SEAL_GROW |
        F_SEAL_SHRINK | F_SEAL_SEAL;
    return ::fcntl(artifact.fd, F_ADD_SEALS, seals) == 0 &&
        ::fcntl(artifact.fd, F_GET_SEALS) == seals;
}

// This check is independently performed by the tracer using the original
// immutable source, production loader and relocation planner, and real
// PTRACE_PEEKDATA into the stopped child (NOT its reported result).
[[nodiscard]] bool inspect_pinned_guestimage(
    pid_t pid, std::span<const std::byte> source) {
    namespace e = astraea::execution;
    namespace l = astraea::loader;
    namespace m = astraea::memory;
    const auto stack = m::GuestRange::create(
        m::GuestAddress{kGuestBase + kPinnedStackOffset},
        m::GuestSize{kPage});
    if (!stack.has_value()) return false;
    auto original = l::build_guest_image(l::GuestImageRequest{
        .image_bytes = std::vector<std::byte>(source.begin(), source.end()),
        .initial_stack = l::InitialStackRequest{
            .storage = stack.value(),
            .arguments = {"astraea-source-owned-ps5-format"},
            .environment = {},
            .auxiliary_vector = {},
        },
        .elf_profile = l::ElfParseProfile::ps5_sce,
    });
    if (!original.has_value() || original->elf.header.type != 0xfe10U ||
        original->elf.header.entry != 0x10U ||
        !original->general_relocations.rela.has_value() ||
        original->general_relocations.rela->count != 8U ||
        !original->dynamic_symbols.has_value())
        return false;
    const auto view = original->initialized_image_view();
    if (!view.has_value()) return false;
    const std::array<unsigned char, 2U> ud2{0x0fU, 0x0bU};
    if (!inspect_bytes(pid, kPinnedEntry, ud2)) return false;
    std::array<bool, 8U> verified{};
    std::array<std::uint64_t, 8U> targets{};
    for (std::uint64_t i = 0U; i < verified.size(); ++i) {
        const auto relocation = l::parse_dynamic_relocation(
            *original->general_relocations.rela, i,
            *original->dynamic_symbols, view.value());
        if (!relocation.has_value() ||
            relocation->relocation_type != e::kOwnedX86_64RelativeType ||
            relocation->symbol_index != 0U ||
            relocation->target.value() >= kMaxPinnedSize)
            return false;
        auto expected = e::build_owned_x86_64_relative_patch(
            relocation.value(), m::GuestAddress{kGuestBase});
        if (!expected.has_value()) return false;
        const auto guest_target = kGuestBase + expected->target.value();
        if (guest_target >= kGuestBase + kMaxPinnedSize ||
            guest_target + 8U > kGuestBase + kMaxPinnedSize ||
            !inspect_bytes(pid, guest_target, {
                reinterpret_cast<const unsigned char*>(
                    expected->bytes.data()), expected->bytes.size()}))
            return false;
        targets[i] = guest_target;
        verified[i] = true;
    }
    for (std::size_t i = 0; i < verified.size(); ++i) {
        if (!verified[i]) return false;
        for (std::size_t j = i + 1U; j < verified.size(); ++j)
            if (targets[i] < targets[j] + 8U &&
                targets[j] < targets[i] + 8U)
                return false;
    }
    return true;
}

// Independently read kernel-owned procfs state while the exact source-owned
// GuestImage tracee is stopped, before requesting ANY guest instruction.
// This proves the leaf is in seccomp filter mode, NOT that the tracer has
// authenticated the precise BPF program or provided general containment.
[[nodiscard]] bool inspected_tracee_postbootstrap_policy(pid_t child) {
    std::ifstream status{"/proc/" + std::to_string(child) + "/status"};
    if (!status) return false;
    bool restricted = false;
    bool no_new_privs = false;
    bool has_filter = false;
    std::string line;
    while (std::getline(status, line)) {
        unsigned value = 0U;
        if (std::sscanf(line.c_str(), "Seccomp: %u", &value) == 1)
            restricted = value == 2U;
        else if (std::sscanf(line.c_str(), "Seccomp_filters: %u",
                             &value) == 1)
            has_filter = value >= 1U;
        else if (std::sscanf(line.c_str(), "NoNewPrivs: %u",
                             &value) == 1)
            no_new_privs = value == 1U;
    }
    return restricted && has_filter && no_new_privs;
}

[[nodiscard]] bool pinned_source_leaf_launch(
    std::string_view executable, const SourceFd& source) {
    char pid_text[32]{};
    if (std::snprintf(pid_text, sizeof(pid_text), "%ld",
                      static_cast<long>(::getppid())) <= 0 ||
        ::setenv("ASTRAEA_CONTROLLER_PID", pid_text, 1) != 0)
        return false;
    // A sealed source is the only non-stdio FD inherited through exec.
    if (source.fd != 3 && ::dup2(source.fd, 3) < 0)
        return false;
    if (::fcntl(3, F_SETFD, 0) < 0) return false;
    const int nullfd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd < 0 ||
        ::dup2(nullfd, STDIN_FILENO) < 0 ||
        ::dup2(nullfd, STDOUT_FILENO) < 0 ||
        ::dup2(nullfd, STDERR_FILENO) < 0 ||
        ::syscall(SYS_close_range, 4U, ~0U, 0U) != 0)
        return false;
    const std::string binary{executable};
    ::execl(binary.c_str(), binary.c_str(),
            "--ptrace-owned-leaf", static_cast<char*>(nullptr));
    return false;
}

[[nodiscard]] bool run_pinned_once(
    std::string_view executable, std::span<const std::byte> original,
    bool corrupt_artifact) {
    ::alarm(12U);
    std::vector<std::byte> bytes(original.begin(), original.end());
    if (corrupt_artifact) bytes[0] ^= std::byte{1U};
    SourceFd artifact;
    if (!seal_owned_source(bytes, artifact)) return false;
    const pid_t child = ::fork();
    if (child < 0) return false;
    if (child == 0)
        ::_exit(pinned_source_leaf_launch(executable, artifact) ? 90 : 91);
    ChildGuard guard{child};
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) return false;
    if (corrupt_artifact) {
        // No SIGSTOP can occur: the child SHA check must reject prior to
        // relocation, native mapping, ptrace registration or guest entry.
        const bool denied = WIFEXITED(status) && WEXITSTATUS(status) == 72;
        ::alarm(0);
        return denied;
    }
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGSTOP ||
        ::ptrace(PTRACE_SETOPTIONS, child, nullptr,
                 PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL |
                 PTRACE_O_TRACEFORK | PTRACE_O_TRACEVFORK |
                 PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC) != 0 ||
        !inspected_tracee_postbootstrap_policy(child) ||
        !inspect_pinned_guestimage(child, original))
        return false;
    auto before = registers(child);
    if (!before.has_value()) return false;
    before->rip = kPinnedEntry;
    if (::ptrace(PTRACE_SETREGS, child, nullptr, &before.value()) != 0)
        return false;
    if (::ptrace(PTRACE_SYSEMU_SINGLESTEP, child, nullptr, nullptr) != 0)
        return false;
    int signal = 0;
    if (!wait_stop(child, signal) || signal != SIGILL)
        return false;
    const auto after = registers(child);
    if (!after.has_value() || after->rip != kPinnedEntry ||
        !inspect_pinned_guestimage(child, original))
        return false;
    ::alarm(0);
    return true;
}

[[nodiscard]] int pinned_elf_controller(
    std::string_view title_path, std::string_view leaf_executable) {
    const auto source = read_exact_owned_source(title_path);
    if (!source.has_value()) return 2;
    if (!run_pinned_once(leaf_executable, source.value(), false) ||
        !run_pinned_once(leaf_executable, source.value(), false) ||
        !run_pinned_once(leaf_executable, source.value(), true)) {
        std::fputs("pinned_ptrace_guestimage_refused\n", stderr);
        return 3;
    }
    std::puts("C1 PINNED OWNED GUESTIMAGE PTRACE PASS");
    std::puts("source_sha256=45920de847b2d33896c186654bbf22a728f1910fad84accb15204aa2da50c678");
    std::puts("owned_guestimage_real_relocations_verified_per_run=8");
    std::puts("ptrace_pinned_postbootstrap_seccomp_filter_verified_runs=2");
    std::puts("ptrace_pinned_postbootstrap_no_new_privs_verified_runs=2");
    std::puts("ptrace_pinned_postbootstrap_denials=write_mmap_mprotect_ptrace");
    std::puts("owned_guestimage_entry_ud2_fault_runs=2");
    std::puts("owned_guestimage_tampered_source=refused_before_trace");
    std::puts("observations=independent_parent_ptrace_only");
    std::puts("guest_execution=owned_UD2_instruction_attempt_only");
    std::puts("external_guest_instructions=0");
    std::puts("sony_entry_abi=not_established");
    std::puts("hostile_guest_sandbox=not_established");
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--pinned-owned")
        return pinned_elf_controller(argv[2], argv[3]);
    if (argc == 3 && std::string_view(argv[1]) == "--leaf") {
        const auto mode = parse_mode(argv[2]);
        return mode.has_value() ? leaf(mode.value()) : 100;
    }
    if (argc != 1) return 2;

    constexpr std::array<Mode, 8U> cases{
        Mode::fault, Mode::syscall, Mode::escape,
        Mode::write_rx, Mode::write_none, Mode::write_rw,
        Mode::write_rw_guarded, Mode::write_controller_only,
    };
    for (const Mode mode : cases) {
        const auto first = run_once(mode);
        const auto second = run_once(mode);
        if (!first.has_value() || !second.has_value() ||
            !equal_evidence(first.value(), second.value()) ||
            !first->stopped || !first->only_tracer_observations ||
            (mode != Mode::write_rw && !first->host_canary_unchanged)) {
            std::fprintf(stderr, "ptrace_step_refused=%s\n", mode_name(mode));
            return 3;
        }
        if ((mode == Mode::fault &&
             (first->executed != 1U ||
              first->terminal_rip != kGuestBase + 5U)) ||
            (mode == Mode::syscall &&
             (first->executed != 2U || !first->syscall_prevented)) ||
            (mode == Mode::escape &&
             (first->executed != 2U || !first->host_escape_denied)) ||
            ((mode == Mode::write_rx || mode == Mode::write_none) &&
             (first->executed != 1U || !first->memory_protection_fault ||
              first->terminal_rip != kGuestBase + 10U)) ||
            (mode == Mode::write_rw &&
             (first->executed != 2U ||
              !first->writable_non_guest_memory_modified ||
              first->host_canary_unchanged ||
              first->terminal_rip != kGuestBase + 10U)) ||
            (mode == Mode::write_rw_guarded &&
             (first->executed != 1U ||
              !first->non_guest_write_denied_before_step ||
              first->writable_non_guest_memory_modified ||
              !first->host_canary_unchanged ||
              first->terminal_rip != kGuestBase + 10U)) ||
            (mode == Mode::write_controller_only &&
             (first->executed != 1U ||
              !first->memory_protection_fault ||
              !first->controller_mapping_absent_in_tracee ||
              !first->controller_only_canary_unchanged ||
              first->writable_non_guest_memory_modified ||
              !first->host_canary_unchanged ||
              first->terminal_rip != kGuestBase + 10U))) {
            std::fprintf(stderr, "ptrace_evidence_mismatch=%s\n", mode_name(mode));
            return 4;
        }
    }
    std::puts("C1 OWNED PTRACE SYSEMU STEP BOUNDARY PASS");
    std::puts("owned_fault_repeated=2");
    std::puts("owned_syscall_exit_prevented_repeated=2");
    std::puts("owned_host_ip_escape_denied_repeated=2");
    std::puts("host_sentinel_canary=unchanged_in_denied_cases");
    std::puts("owned_write_rx_sigsegv_repeated=2");
    std::puts("owned_write_none_sigsegv_repeated=2");
    std::puts("owned_write_rw_non_guest_side_effect_repeated=2");
    std::puts("writable_host_canary_modified_in_positive_control=1");
    std::puts("owned_write_rw_guarded_refused_before_retirement_repeated=2");
    std::puts("writable_host_canary_unchanged_when_guarded=1");
    std::puts("owned_write_controller_only_sigsegv_maperr_repeated=2");
    std::puts("controller_canary_not_mapped_in_tracee_repeated=2");
    std::puts("controller_private_canary_unchanged_repeated=2");
    std::puts("observations=tracer_ptrace_registers_only");
    std::puts("tracee_external_files_and_controller_ipc=none");
    std::puts("external_elf_guest_instructions=0");
    std::puts("guest_memory_write_isolation=not_established");
    std::puts("hostile_guest_sandbox=not_established");
    return 0;
}
