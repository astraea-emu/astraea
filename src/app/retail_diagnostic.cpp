#include "retail_diagnostic.hpp"

#include <astraea/core/version.hpp>
#include <astraea/execution/guest_worker_artifact.hpp>
#include <astraea/execution/guest_worker_fault_projection.hpp>
#include <astraea/execution/guest_worker_process_session.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/guest_worker_wire.hpp>
#include <astraea/execution/linux_retail_diagnostic.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

#if defined(__linux__)
#include <cerrno>
#include <csignal>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace astraea::app {
namespace {

using astraea::execution::GuestThreadId;
using astraea::execution::GuestWorkerDiagnostic;
using astraea::execution::GuestWorkerDiagnosticKind;
using astraea::execution::GuestWorkerFault;
using astraea::execution::GuestWorkerFaultKind;
using astraea::execution::GuestWorkerId;
using astraea::execution::GuestWorkerStop;
using astraea::execution::GuestWorkerStopReason;
using astraea::execution::GuestWorkerTerminationReason;
using astraea::execution::GuestWorkerWireMessage;

constexpr std::size_t kMaxDiagnosticArtifactBytes =
    512U * 1024U * 1024U;
constexpr std::uint64_t kControllerRunBudgetUs =
    1'000'000U;
constexpr std::uint64_t kControllerTimeoutMs =
    5'000U;
constexpr std::uint64_t kDiagnosticMemoryLimitBytes =
    32ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kDiagnosticCpuLimitSeconds = 2U;
constexpr std::uint64_t kDiagnosticOpenFileLimit = 16U;

constexpr GuestWorkerId kWorkerId{.value = 1U};
constexpr GuestThreadId kThreadId{.value = 1U};

[[nodiscard]] const char* diagnostic_name(
    GuestWorkerDiagnosticKind kind) noexcept {
    switch (kind) {
    case GuestWorkerDiagnosticKind::loader_rejected:
        return "loader_rejected";
    case GuestWorkerDiagnosticKind::entry_not_executable:
        return "entry_not_executable";
    case GuestWorkerDiagnosticKind::sce_dynamic_metadata_rejected:
        return "sce_dynamic_metadata_rejected";
    case GuestWorkerDiagnosticKind::unsupported_dynamic_dependencies:
        return "unsupported_dynamic_dependencies";
    case GuestWorkerDiagnosticKind::unsupported_relocations:
        return "unsupported_relocations";
    case GuestWorkerDiagnosticKind::unsupported_tls:
        return "unsupported_tls";
    case GuestWorkerDiagnosticKind::native_backend_error:
        return "native_backend_error";
    case GuestWorkerDiagnosticKind::unsupported_syscall:
        return "unsupported_syscall";
    case GuestWorkerDiagnosticKind::stack_placement_failure:
        return "stack_placement_failure";
    case GuestWorkerDiagnosticKind::unsupported_fault_class:
        return "unsupported_fault_class";
    }
    return "unknown";
}

[[nodiscard]] const char* fault_name(
    GuestWorkerFaultKind kind) noexcept {
    switch (kind) {
    case GuestWorkerFaultKind::access_violation:
        return "access_violation";
    case GuestWorkerFaultKind::illegal_instruction:
        return "illegal_instruction";
    case GuestWorkerFaultKind::unregistered_trap_site:
        return "unregistered_trap_site";
    case GuestWorkerFaultKind::protocol_failure:
        return "protocol_failure";
    }
    return "unknown";
}

#if defined(__linux__)

[[nodiscard]] bool read_exact(
    int fd,
    std::span<std::byte> destination) noexcept {
    std::size_t offset = 0U;
    while (offset < destination.size()) {
        const auto count =
            ::recv(
                fd,
                destination.data() + offset,
                destination.size() - offset,
                0);
        if (count == 0) {
            return false;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        offset +=
            static_cast<std::size_t>(count);
    }
    return true;
}

[[nodiscard]] bool write_all(
    int fd,
    std::span<const std::byte> source) noexcept {
    std::size_t offset = 0U;
    while (offset < source.size()) {
        const auto count =
            ::send(
                fd,
                source.data() + offset,
                source.size() - offset,
                MSG_NOSIGNAL);
        if (count <= 0) {
            if (count < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        offset +=
            static_cast<std::size_t>(count);
    }
    return true;
}

[[nodiscard]] std::optional<GuestWorkerWireMessage>
read_worker_message() {
    std::array<
        std::byte,
        astraea::execution::kGuestWorkerWireHeaderSize>
        header_bytes{};
    if (!read_exact(
            STDIN_FILENO,
            header_bytes)) {
        return std::nullopt;
    }

    const auto header =
        astraea::execution::
            decode_guest_worker_wire_header(
                header_bytes);
    if (!header.has_value()) {
        return std::nullopt;
    }

    std::vector<std::byte> frame(
        header->frame_size);
    std::copy(
        header_bytes.begin(),
        header_bytes.end(),
        frame.begin());

    if (header->payload_size != 0U) {
        auto payload =
            std::span<std::byte>{
                frame.data() +
                    astraea::execution::
                        kGuestWorkerWireHeaderSize,
                header->payload_size};
        if (!read_exact(
                STDIN_FILENO,
                payload)) {
            return std::nullopt;
        }
    }

    const auto decoded =
        astraea::execution::
            decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    return decoded.value();
}

[[nodiscard]] bool write_worker_message(
    const GuestWorkerWireMessage& message) {
    const auto encoded =
        astraea::execution::
            encode_guest_worker_wire_message(message);
    return
        encoded.has_value() &&
        write_all(
            STDOUT_FILENO,
            encoded.value());
}

[[nodiscard]] bool bind_worker_to_controller() noexcept {
    const auto* text =
        std::getenv("ASTRAEA_CONTROLLER_PID");
    if (text == nullptr) {
        return false;
    }

    pid_t expected = 0;
    const auto view = std::string_view{text};
    const auto parsed =
        std::from_chars(
            view.data(),
            view.data() + view.size(),
            expected);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != view.data() + view.size() ||
        expected <= 1) {
        return false;
    }

    if (::prctl(
            PR_SET_PDEATHSIG,
            SIGKILL) != 0) {
        return false;
    }
    return ::getppid() == expected;
}

[[nodiscard]] bool wait_for_termination(
    GuestWorkerTerminationReason expected) {
    const auto message =
        read_worker_message();
    if (!message.has_value()) {
        return false;
    }
    const auto* terminate =
        std::get_if<
            astraea::execution::GuestWorkerTerminate>(
                &message.value());
    return
        terminate != nullptr &&
        terminate->reason == expected;
}

[[nodiscard]] bool emit_diagnostic(
    GuestWorkerDiagnostic diagnostic) {
    if (!write_worker_message(
            GuestWorkerWireMessage{diagnostic})) {
        return false;
    }
    if (!write_worker_message(
            GuestWorkerWireMessage{
                GuestWorkerStop{
                    .worker_id = diagnostic.worker_id,
                    .thread_id = diagnostic.thread_id,
                    .reason =
                        GuestWorkerStopReason::
                            diagnostic_boundary,
                    .guest_rip = diagnostic.guest_rip,
                }})) {
        return false;
    }
    return wait_for_termination(
        GuestWorkerTerminationReason::
            unsupported_guest_behavior);
}

[[nodiscard]] bool emit_fault(
    const GuestWorkerFault& fault) {
    if (!write_worker_message(
            GuestWorkerWireMessage{fault})) {
        return false;
    }
    if (!write_worker_message(
            GuestWorkerWireMessage{
                GuestWorkerStop{
                    .worker_id = fault.worker_id,
                    .thread_id = fault.thread_id,
                    .reason =
                        GuestWorkerStopReason::guest_fault,
                    .guest_rip = fault.guest_rip,
                }})) {
        return false;
    }
    return wait_for_termination(
        GuestWorkerTerminationReason::
            fatal_guest_fault);
}

[[nodiscard]] astraea::memory::GuestRange
fallback_stack() {
    const auto range =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{0x10000U},
            astraea::memory::GuestSize{
                astraea::execution::
                    kLinuxRetailDiagnosticStackSize});
    if (!range.has_value()) {
        std::abort();
    }
    return range.value();
}

[[nodiscard]] GuestWorkerDiagnostic
diagnostic_from_preflight(
    const astraea::execution::
        LinuxRetailDiagnosticPreflight& preflight) {
    using Boundary =
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind;

    GuestWorkerDiagnostic diagnostic{
        .worker_id = kWorkerId,
        .thread_id = kThreadId,
        .kind =
            GuestWorkerDiagnosticKind::
                native_backend_error,
        .guest_rip =
            astraea::memory::GuestAddress{0U},
        .detail0 = 0U,
        .detail1 = 0U,
    };

    if (preflight.image.has_value()) {
        diagnostic.guest_rip =
            astraea::memory::GuestAddress{
                preflight.image->elf.header.entry};
    }

    switch (preflight.boundary) {
    case Boundary::loader_rejected:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                loader_rejected;
        if (preflight.loader_error.has_value()) {
            diagnostic.detail0 =
                static_cast<std::uint64_t>(
                    preflight.loader_error->code);
            diagnostic.detail1 =
                preflight.loader_error->
                        source_program_header_index
                    .has_value()
                    ? static_cast<std::uint64_t>(
                          preflight.loader_error->
                              source_program_header_index
                                  .value())
                    : std::numeric_limits<
                          std::uint64_t>::max();
        }
        break;
    case Boundary::entry_not_executable:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                entry_not_executable;
        break;
    case Boundary::sce_dynamic_metadata_rejected:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                sce_dynamic_metadata_rejected;
        if (preflight.sce_dynamic_metadata_error.has_value()) {
            diagnostic.detail0 =
                static_cast<std::uint64_t>(
                    preflight.sce_dynamic_metadata_error->
                        code);
            diagnostic.detail1 =
                std::bit_cast<std::uint64_t>(
                    preflight.sce_dynamic_metadata_error->
                        tag);
        }
        break;
    case Boundary::unsupported_dynamic_dependencies:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                unsupported_dynamic_dependencies;
        diagnostic.detail0 =
            static_cast<std::uint64_t>(
                preflight.dynamic_dependency_count);
        break;
    case Boundary::unsupported_relocations:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                unsupported_relocations;
        diagnostic.detail0 =
            preflight.relocation_count;
        break;
    case Boundary::unsupported_tls:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                unsupported_tls;
        if (preflight.image.has_value() &&
            preflight.image->tls.has_value()) {
            diagnostic.detail0 =
                preflight.image->tls->
                    total_size.value();
            diagnostic.detail1 =
                preflight.image->tls->
                    initialized_size.value();
        }
        break;
    case Boundary::ready_for_native_entry:
        break;
    }
    return diagnostic;
}

[[nodiscard]] int run_internal_worker() {
    if (!bind_worker_to_controller()) {
        return 20;
    }

    const auto hello_message =
        read_worker_message();
    if (!hello_message.has_value()) {
        return 21;
    }
    const auto* hello =
        std::get_if<
            astraea::execution::GuestWorkerHello>(
                &hello_message.value());
    if (hello == nullptr ||
        hello->protocol_version !=
            astraea::execution::
                kGuestWorkerProtocolVersion) {
        return 22;
    }

    if (!write_worker_message(
            GuestWorkerWireMessage{
                astraea::execution::GuestWorkerReady{
                    .protocol_version =
                        astraea::execution::
                            kGuestWorkerProtocolVersion,
                    .worker_id = kWorkerId,
                }})) {
        return 23;
    }

    const auto run_message =
        read_worker_message();
    if (!run_message.has_value()) {
        return 24;
    }
    const auto* run =
        std::get_if<
            astraea::execution::GuestWorkerRunRequest>(
                &run_message.value());
    if (run == nullptr ||
        !astraea::execution::
             validate_guest_worker_run_request(*run)
             .has_value()) {
        return 25;
    }

    auto artifact =
        astraea::execution::
            read_linux_sealed_worker_artifact(
                kMaxDiagnosticArtifactBytes);
    if (!artifact.has_value()) {
        return 26;
    }

    const auto stack =
        astraea::execution::
            choose_linux_retail_diagnostic_stack(
                artifact.value());

    astraea::memory::GuestRange stack_range =
        fallback_stack();
    if (stack.has_value()) {
        stack_range = stack.value();
    } else if (
        stack.error().code !=
        astraea::execution::
            LinuxRetailDiagnosticStackErrorCode::
                elf_parse_failure) {
        return emit_diagnostic(
                   GuestWorkerDiagnostic{
                       .worker_id = kWorkerId,
                       .thread_id = kThreadId,
                       .kind =
                           GuestWorkerDiagnosticKind::
                               stack_placement_failure,
                       .guest_rip =
                           astraea::memory::
                               GuestAddress{0U},
                       .detail0 =
                           static_cast<std::uint64_t>(
                               stack.error().code),
                       .detail1 =
                           stack.error().detail,
                   })
            ? 0
            : 27;
    }

    auto preflight =
        astraea::execution::
            preflight_linux_retail_diagnostic(
                astraea::execution::
                    LinuxRetailDiagnosticPreflightRequest{
                        .artifact_bytes =
                            std::move(artifact.value()),
                        .stack_storage = stack_range,
                        .arguments = {
                            "retail-diagnostic"},
                        .environment = {},
                        .auxiliary_vector = {},
                    });

    if (preflight.boundary !=
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                ready_for_native_entry) {
        return emit_diagnostic(
                   diagnostic_from_preflight(
                       preflight))
            ? 0
            : 28;
    }

    if (!preflight.image.has_value()) {
        return 29;
    }

    const auto runtime =
        astraea::execution::
            run_linux_retail_diagnostic_native_once(
                *preflight.image);

    using RuntimeKind =
        astraea::execution::
            LinuxRetailDiagnosticRuntimeBoundaryKind;
    switch (runtime.kind) {
    case RuntimeKind::unsupported_syscall:
        return emit_diagnostic(
                   GuestWorkerDiagnostic{
                       .worker_id = kWorkerId,
                       .thread_id = kThreadId,
                       .kind =
                           GuestWorkerDiagnosticKind::
                               unsupported_syscall,
                       .guest_rip =
                           runtime.guest_rip,
                       .detail0 =
                           static_cast<std::uint64_t>(
                               static_cast<std::uint32_t>(
                                   runtime.syscall_number)),
                       .detail1 =
                           runtime.audit_arch,
                   })
            ? 0
            : 30;

    case RuntimeKind::guest_fault:
        if (!runtime.guest_fault.has_value()) {
            return 31;
        }
        switch (runtime.guest_fault->kind) {
        case astraea::execution::GuestFaultKind::
            access_violation:
            return emit_fault(
                       GuestWorkerFault{
                           .worker_id = kWorkerId,
                           .thread_id = kThreadId,
                           .kind =
                               GuestWorkerFaultKind::
                                   access_violation,
                           .guest_rip =
                               runtime.guest_rip,
                           .fault_address =
                               astraea::memory::GuestAddress{
                                   runtime.guest_fault->
                                           has_fault_address
                                       ? runtime.guest_fault->
                                             fault_address
                                       : 0U},
                       })
                ? 0
                : 32;
        case astraea::execution::GuestFaultKind::
            illegal_instruction:
            return emit_fault(
                       GuestWorkerFault{
                           .worker_id = kWorkerId,
                           .thread_id = kThreadId,
                           .kind =
                               GuestWorkerFaultKind::
                                   illegal_instruction,
                           .guest_rip =
                               runtime.guest_rip,
                           .fault_address =
                               astraea::memory::
                                   GuestAddress{0U},
                       })
                ? 0
                : 33;
        case astraea::execution::GuestFaultKind::arithmetic:
        case astraea::execution::GuestFaultKind::
            breakpoint_or_trap:
        case astraea::execution::GuestFaultKind::unknown:
            return emit_diagnostic(
                       GuestWorkerDiagnostic{
                           .worker_id = kWorkerId,
                           .thread_id = kThreadId,
                           .kind =
                               GuestWorkerDiagnosticKind::
                                   unsupported_fault_class,
                           .guest_rip =
                               runtime.guest_rip,
                           .detail0 =
                               static_cast<std::uint64_t>(
                                   runtime.guest_fault->
                                       kind),
                           .detail1 =
                               runtime.guest_fault->
                                   host_code,
                       })
                ? 0
                : 34;
        }
        return 35;

    case RuntimeKind::native_backend_error:
        if (!runtime.backend_error.has_value()) {
            return 36;
        }
        return emit_diagnostic(
                   GuestWorkerDiagnostic{
                       .worker_id = kWorkerId,
                       .thread_id = kThreadId,
                       .kind =
                           GuestWorkerDiagnosticKind::
                               native_backend_error,
                       .guest_rip =
                           runtime.guest_rip,
                       .detail0 =
                           static_cast<std::uint64_t>(
                               runtime.backend_error->
                                   code),
                       .detail1 =
                           runtime.backend_error->
                                   has_guest_address
                               ? runtime.backend_error->
                                     guest_address
                               : runtime.backend_error->
                                         has_host_code
                                   ? runtime.backend_error->
                                         host_code
                                   : 0U,
                   })
            ? 0
            : 37;
    }

    return 38;
}

[[nodiscard]] std::optional<std::vector<std::byte>>
read_artifact_file(const std::filesystem::path& path) {
    std::ifstream stream(
        path,
        std::ios::binary |
            std::ios::ate);
    if (!stream) {
        return std::nullopt;
    }

    const auto end = stream.tellg();
    if (end <= std::streampos{0}) {
        return std::nullopt;
    }
    const auto end_offset =
        static_cast<std::streamoff>(end);
    if (end_offset <= 0) {
        return std::nullopt;
    }
    const auto size =
        static_cast<std::uint64_t>(
            end_offset);
    if (size >
        static_cast<std::uint64_t>(
            kMaxDiagnosticArtifactBytes)) {
        return std::nullopt;
    }

    std::vector<std::byte> bytes(
        static_cast<std::size_t>(size));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(
                bytes.size()))) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::optional<std::string>
current_executable_path() {
    std::error_code error;
    const auto path =
        std::filesystem::read_symlink(
            "/proc/self/exe",
            error);
    if (error || path.empty()) {
        return std::nullopt;
    }
    return path.string();
}

[[nodiscard]] int run_controller(
    const std::filesystem::path& artifact_path) {
    auto bytes =
        read_artifact_file(artifact_path);
    if (!bytes.has_value()) {
        std::cerr
            << "Astraea: unable to read a non-empty artifact within "
            << kMaxDiagnosticArtifactBytes
            << " bytes.\n";
        return 2;
    }

    const auto executable =
        current_executable_path();
    if (!executable.has_value()) {
        std::cerr
            << "Astraea: unable to resolve the current executable.\n";
        return 2;
    }

    auto result =
        astraea::execution::
            run_guest_worker_process_session(
                astraea::execution::
                    GuestWorkerProcessSessionConfig{
                        .worker_executable =
                            executable.value(),
                        .worker_arguments = {
                            "--internal-retail-diagnostic-worker"},
                        .run_budget_microseconds =
                            kControllerRunBudgetUs,
                        .timeout_milliseconds =
                            kControllerTimeoutMs,
                        .syscall_service = {},
                        .max_syscall_requests = 0U,
                        .resource_policy =
                            astraea::execution::
                                GuestWorkerResourcePolicy{
                                    .process_memory_limit_bytes =
                                        kDiagnosticMemoryLimitBytes,
                                    .process_cpu_time_seconds =
                                        kDiagnosticCpuLimitSeconds,
                                    .linux_max_open_files =
                                        kDiagnosticOpenFileLimit,
                                    .linux_disable_core_dumps =
                                        true,
                                    .linux_disable_file_growth =
                                        true,
                                },
                        .linux_artifact_bytes =
                            std::move(bytes.value()),
                    });

    if (!result.has_value()) {
        std::cerr
            << "Astraea: supervised diagnostic failed; session_error="
            << static_cast<unsigned>(
                   result.error().code)
            << " platform_error="
            << result.error().platform_error
            << "\n";
        return 2;
    }

    if (result->terminal_diagnostic.has_value()) {
        const auto& diagnostic =
            result->terminal_diagnostic.value();
        std::cout
            << "boundary="
            << diagnostic_name(diagnostic.kind)
            << " rip=0x"
            << std::hex
            << diagnostic.guest_rip.value()
            << " detail0=0x"
            << diagnostic.detail0
            << " detail1=0x"
            << diagnostic.detail1
            << std::dec
            << "\n";
        return 0;
    }

    if (result->terminal_fault.has_value()) {
        const auto& fault =
            result->terminal_fault.value();
        std::cout
            << "boundary=guest_fault"
            << " kind="
            << fault_name(fault.kind)
            << " rip=0x"
            << std::hex
            << fault.guest_rip.value()
            << " fault_address=0x"
            << fault.fault_address.value()
            << std::dec
            << "\n";
        return 0;
    }

    std::cout
        << "boundary=stop"
        << " reason="
        << static_cast<unsigned>(
               result->stop.reason)
        << " rip=0x"
        << std::hex
        << result->stop.guest_rip.value()
        << std::dec
        << "\n";
    return 0;
}

#endif

void print_usage() {
    std::cout
        << "Astraea "
        << astraea::core::version()
        << "\n"
        << "Usage:\n"
        << "  astraea diagnose <authorized-executable-path>\n";
}

}  // namespace

int run(int argc, char** argv) {
#if defined(__linux__) && defined(__x86_64__)
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--internal-retail-diagnostic-worker") {
        return run_internal_worker();
    }

    if (argc == 3 &&
        std::string_view{argv[1]} == "diagnose") {
        return run_controller(
            std::filesystem::path{argv[2]});
    }
#else
    if (argc >= 2 &&
        std::string_view{argv[1]} == "diagnose") {
        std::cerr
            << "Astraea: the first retail diagnostic is Linux x86-64 only.\n";
        return 2;
    }
#endif

    print_usage();
    return argc == 1 ? 0 : 2;
}

}  // namespace astraea::app
