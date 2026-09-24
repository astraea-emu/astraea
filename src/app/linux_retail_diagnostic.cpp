#include "linux_retail_diagnostic.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_worker_artifact.hpp>
#include <astraea/execution/guest_worker_process_session.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/guest_worker_wire.hpp>
#include <astraea/execution/linux_retail_diagnostic.hpp>
#include <astraea/loader/elf64.hpp>
#include <astraea/memory/guest_address.hpp>

#if defined(__linux__)
#include <csignal>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace astraea::app {
namespace {

constexpr std::size_t kMaxRetailArtifactBytes =
    1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kPlanningStackSize =
    2ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kPlanningStackFirstBase =
    0x00007fff00000000ULL;
constexpr std::uint64_t kPlanningStackStride =
    0x01000000ULL;
constexpr std::size_t kPlanningStackCandidateCount = 256U;

constexpr astraea::execution::GuestWorkerId kWorkerId{
    .value = 1U,
};
constexpr astraea::execution::GuestThreadId kThreadId{
    .value = 1U,
};

using WorkerReadResult =
    astraea::core::Result<
        astraea::execution::GuestWorkerWireMessage,
        int>;

[[nodiscard]] bool read_exact(
    std::span<std::byte> bytes) {
    if (bytes.empty()) {
        return true;
    }

    std::cin.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(
            bytes.size()));
    return
        std::cin.good() ||
        (std::cin.eof() &&
         static_cast<std::size_t>(
             std::cin.gcount()) ==
             bytes.size());
}

[[nodiscard]] WorkerReadResult
read_worker_message() {
    using namespace astraea::execution;

    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};
    if (!read_exact(header_bytes)) {
        return WorkerReadResult::failure(1);
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return WorkerReadResult::failure(2);
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
                    kGuestWorkerWireHeaderSize,
                header->payload_size};
        if (!read_exact(payload)) {
            return WorkerReadResult::failure(3);
        }
    }

    const auto decoded =
        decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) {
        return WorkerReadResult::failure(4);
    }

    return WorkerReadResult::success(
        decoded.value());
}

[[nodiscard]] bool write_worker_message(
    const astraea::execution::
        GuestWorkerWireMessage& message) {
    const auto encoded =
        astraea::execution::
            encode_guest_worker_wire_message(
                message);
    if (!encoded.has_value()) {
        return false;
    }

    std::cout.write(
        reinterpret_cast<const char*>(
            encoded->data()),
        static_cast<std::streamsize>(
            encoded->size()));
    std::cout.flush();
    return std::cout.good();
}

[[nodiscard]] bool
bind_worker_lifetime_to_controller() noexcept {
#if defined(__linux__)
    const auto* expected_text =
        std::getenv("ASTRAEA_CONTROLLER_PID");
    if (expected_text == nullptr) {
        return false;
    }

    pid_t expected = 0;
    const auto text =
        std::string_view{expected_text};
    const auto parsed =
        std::from_chars(
            text.data(),
            text.data() + text.size(),
            expected);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size() ||
        expected <= 1) {
        return false;
    }

    if (::prctl(
            PR_SET_PDEATHSIG,
            SIGKILL) != 0) {
        return false;
    }

    return ::getppid() == expected;
#else
    return false;
#endif
}

[[nodiscard]] bool overlaps_load_segment(
    astraea::memory::GuestRange candidate,
    const astraea::loader::ElfImage& elf) noexcept {
    constexpr std::uint32_t kPtLoad = 1U;

    for (const auto& header :
         elf.program_headers) {
        if (header.type != kPtLoad ||
            header.memory_size == 0U) {
            continue;
        }

        const auto range =
            astraea::memory::GuestRange::create(
                astraea::memory::GuestAddress{
                    header.virtual_address},
                astraea::memory::GuestSize{
                    header.memory_size});
        if (!range.has_value() ||
            candidate.overlaps(range.value())) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::optional<
    astraea::memory::GuestRange>
choose_planning_stack(
    std::span<const std::byte> artifact) {
    using namespace astraea;

    const auto parsed =
        loader::parse_elf64(
            artifact,
            loader::ElfParseProfile::ps5_sce);

    for (std::size_t index = 0U;
         index < kPlanningStackCandidateCount;
         ++index) {
        const auto delta =
            static_cast<std::uint64_t>(index) *
            kPlanningStackStride;
        if (delta > kPlanningStackFirstBase) {
            break;
        }

        const auto candidate =
            memory::GuestRange::create(
                memory::GuestAddress{
                    kPlanningStackFirstBase -
                    delta},
                memory::GuestSize{
                    kPlanningStackSize});
        if (!candidate.has_value()) {
            continue;
        }

        // If parsing already failed, the preflight will preserve that loader
        // error before stack-overlap validation. Any well-formed candidate is
        // therefore sufficient.
        if (!parsed.has_value() ||
            !overlaps_load_segment(
                candidate.value(),
                parsed.value())) {
            return candidate.value();
        }
    }

    return std::nullopt;
}

[[nodiscard]] astraea::execution::
    GuestWorkerDiagnostic
diagnostic_from_preflight(
    const astraea::execution::
        LinuxRetailDiagnosticPreflight& preflight) {
    using namespace astraea::execution;

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
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        loader_rejected:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                loader_rejected;
        if (preflight.loader_error.has_value()) {
            diagnostic.detail0 =
                static_cast<std::uint64_t>(
                    preflight.loader_error->code);
            if (preflight.loader_error->
                    source_program_header_index
                    .has_value()) {
                diagnostic.detail1 =
                    static_cast<std::uint64_t>(
                        preflight.loader_error->
                            source_program_header_index
                            .value()) +
                    1U;
            }
        }
        break;
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        entry_not_executable:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                entry_not_executable;
        break;
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        sce_dynamic_metadata_rejected:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                sce_dynamic_metadata_rejected;
        if (preflight.sce_dynamic_metadata_error
                .has_value()) {
            diagnostic.detail0 =
                static_cast<std::uint64_t>(
                    preflight.
                        sce_dynamic_metadata_error->
                        code);
            diagnostic.detail1 =
                std::bit_cast<std::uint64_t>(
                    preflight.
                        sce_dynamic_metadata_error->
                        tag);
        }
        break;
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        unsupported_dynamic_dependencies:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                unsupported_dynamic_dependencies;
        diagnostic.detail0 =
            static_cast<std::uint64_t>(
                preflight.dynamic_dependency_count);
        break;
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        unsupported_relocations:
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                unsupported_relocations;
        diagnostic.detail0 =
            preflight.relocation_count;
        break;
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        unsupported_tls:
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
                    alignment;
        }
        break;
    case LinuxRetailDiagnosticPreflightBoundaryKind::
        ready_for_native_entry:
        // Public evidence is sufficient to reject Astraea's current synthetic
        // startup stack as a PS5 retail contract, but not sufficient to claim
        // an exact PS5 process-entry ABI. Stop here rather than manufacturing
        // a meaningless native fault.
        diagnostic.kind =
            GuestWorkerDiagnosticKind::
                unsupported_initial_process_abi;
        break;
    }

    return diagnostic;
}

[[nodiscard]] std::string_view diagnostic_name(
    astraea::execution::GuestWorkerDiagnosticKind
        kind) noexcept {
    using astraea::execution::GuestWorkerDiagnosticKind;

    switch (kind) {
    case GuestWorkerDiagnosticKind::loader_rejected:
        return "loader_rejected";
    case GuestWorkerDiagnosticKind::entry_not_executable:
        return "entry_not_executable";
    case GuestWorkerDiagnosticKind::
        sce_dynamic_metadata_rejected:
        return "sce_dynamic_metadata_rejected";
    case GuestWorkerDiagnosticKind::
        unsupported_dynamic_dependencies:
        return "unsupported_dynamic_dependencies";
    case GuestWorkerDiagnosticKind::
        unsupported_relocations:
        return "unsupported_relocations";
    case GuestWorkerDiagnosticKind::unsupported_tls:
        return "unsupported_tls";
    case GuestWorkerDiagnosticKind::native_backend_error:
        return "native_backend_error";
    case GuestWorkerDiagnosticKind::
        unsupported_initial_process_abi:
        return "unsupported_initial_process_abi";
    case GuestWorkerDiagnosticKind::unsupported_syscall:
        return "unsupported_syscall";
    }
    return "unknown";
}

[[nodiscard]] std::optional<
    std::vector<std::byte>>
read_artifact_file(
    std::string_view artifact_path) {
    const std::filesystem::path path{
        std::string{artifact_path}};

    // Size and read through the same opened file object. Do not stat a path
    // and then reopen it: that creates a path-replacement race and can turn a
    // growing file into a silently truncated diagnostic artifact.
    std::ifstream input(
        path,
        std::ios::binary |
            std::ios::ate);
    if (!input) {
        return std::nullopt;
    }

    const auto end = input.tellg();
    if (end <= std::streampos{0}) {
        return std::nullopt;
    }

    const auto byte_count =
        static_cast<std::uintmax_t>(
            static_cast<std::streamoff>(end));
    if (byte_count >
            static_cast<std::uintmax_t>(
                kMaxRetailArtifactBytes) ||
        byte_count >
            static_cast<std::uintmax_t>(
                std::numeric_limits<
                    std::size_t>::max()) ||
        byte_count >
            static_cast<std::uintmax_t>(
                std::numeric_limits<
                    std::streamsize>::max())) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::beg);
    if (!input) {
        return std::nullopt;
    }

    const auto host_size =
        static_cast<std::size_t>(
            byte_count);
    std::vector<std::byte> bytes(
        host_size);
    input.read(
        reinterpret_cast<char*>(
            bytes.data()),
        static_cast<std::streamsize>(
            host_size));
    if (!input ||
        static_cast<std::size_t>(
            input.gcount()) !=
            host_size) {
        return std::nullopt;
    }

    // Refuse a file that grew after sizing instead of silently diagnosing a
    // prefix of a moving target.
    char trailing = '\0';
    input.read(&trailing, 1);
    if (input.gcount() != 0) {
        return std::nullopt;
    }

    return bytes;
}

[[nodiscard]] std::optional<std::string>
current_executable_path() {
#if defined(__linux__)
    std::error_code error;
    const auto path =
        std::filesystem::read_symlink(
            "/proc/self/exe",
            error);
    if (error || path.empty()) {
        return std::nullopt;
    }
    return path.string();
#else
    return std::nullopt;
#endif
}

}  // namespace

int run_linux_retail_diagnostic_worker() {
#if !(defined(__linux__) && defined(__x86_64__))
    return 70;
#else
    using namespace astraea::execution;

    if (!bind_worker_lifetime_to_controller()) {
        return 71;
    }

    const auto artifact =
        read_linux_sealed_worker_artifact(
            kMaxRetailArtifactBytes);
    if (!artifact.has_value()) {
        return 72;
    }

    const auto hello_message =
        read_worker_message();
    if (!hello_message.has_value()) {
        return 73;
    }
    const auto* hello =
        std::get_if<GuestWorkerHello>(
            &hello_message.value());
    if (hello == nullptr ||
        hello->protocol_version !=
            kGuestWorkerProtocolVersion) {
        return 74;
    }

    if (!write_worker_message(
            GuestWorkerWireMessage{
                GuestWorkerReady{
                    .protocol_version =
                        kGuestWorkerProtocolVersion,
                    .worker_id = kWorkerId,
                }})) {
        return 75;
    }

    const auto run_message =
        read_worker_message();
    if (!run_message.has_value()) {
        return 76;
    }
    const auto* run =
        std::get_if<GuestWorkerRunRequest>(
            &run_message.value());
    if (run == nullptr ||
        !validate_guest_worker_run_request(
             *run)
             .has_value()) {
        return 77;
    }

    const auto stack =
        choose_planning_stack(
            artifact.value());
    if (!stack.has_value()) {
        return 78;
    }

    auto preflight =
        preflight_linux_retail_diagnostic(
            LinuxRetailDiagnosticPreflightRequest{
                .artifact_bytes =
                    artifact.value(),
                .stack_storage =
                    stack.value(),
                .arguments = {
                    "astraea-retail-diagnostic",
                },
                .environment = {},
                .auxiliary_vector = {},
            });

    const auto diagnostic =
        diagnostic_from_preflight(
            preflight);

    if (!write_worker_message(
            GuestWorkerWireMessage{
                diagnostic}) ||
        !write_worker_message(
            GuestWorkerWireMessage{
                GuestWorkerStop{
                    .worker_id = kWorkerId,
                    .thread_id = kThreadId,
                    .reason =
                        GuestWorkerStopReason::
                            diagnostic_boundary,
                    .guest_rip =
                        diagnostic.guest_rip,
                }})) {
        return 79;
    }

    const auto terminate_message =
        read_worker_message();
    if (!terminate_message.has_value()) {
        return 80;
    }
    const auto* terminate =
        std::get_if<GuestWorkerTerminate>(
            &terminate_message.value());
    if (terminate == nullptr ||
        terminate->reason !=
            GuestWorkerTerminationReason::
                unsupported_guest_behavior) {
        return 81;
    }

    return 0;
#endif
}

int run_linux_retail_diagnostic(
    std::string_view artifact_path) {
#if !(defined(__linux__) && defined(__x86_64__))
    (void)artifact_path;
    std::cerr
        << "Retail diagnostics are currently admitted only on Linux x86-64.\n";
    return 2;
#else
    using namespace astraea::execution;

    auto artifact =
        read_artifact_file(
            artifact_path);
    if (!artifact.has_value()) {
        std::cerr
            << "Could not read a non-empty artifact within the "
            << kMaxRetailArtifactBytes
            << "-byte diagnostic limit.\n";
        return 3;
    }

    const auto executable =
        current_executable_path();
    if (!executable.has_value()) {
        std::cerr
            << "Could not resolve the Astraea executable path.\n";
        return 4;
    }

    GuestWorkerResourcePolicy policy{};
    policy.process_cpu_time_seconds = 10U;
    policy.linux_max_open_files = 32U;
    policy.linux_disable_core_dumps = true;
    policy.linux_disable_file_growth = true;

    const auto session =
        run_guest_worker_process_session(
            GuestWorkerProcessSessionConfig{
                .worker_executable =
                    executable.value(),
                .worker_arguments = {
                    "--linux-retail-diagnostic-worker",
                },
                .run_budget_microseconds =
                    1'000'000U,
                .timeout_milliseconds =
                    15'000U,
                .syscall_service = {},
                .max_syscall_requests = 0U,
                .resource_policy =
                    policy,
                .linux_artifact_bytes =
                    std::move(artifact.value()),
            });

    if (!session.has_value()) {
        std::cerr
            << "Retail diagnostic infrastructure failure: "
            << static_cast<std::uint32_t>(
                   session.error().code)
            << "\n";
        return 5;
    }

    if (session->terminal_diagnostic.has_value()) {
        const auto& diagnostic =
            session->terminal_diagnostic.value();

        std::cout
            << "Astraea retail diagnostic\n"
            << "boundary="
            << diagnostic_name(
                   diagnostic.kind)
            << "\n"
            << "guest_rip=0x"
            << std::hex
            << diagnostic.guest_rip.value()
            << std::dec
            << "\n"
            << "detail0="
            << diagnostic.detail0
            << "\n"
            << "detail1="
            << diagnostic.detail1
            << "\n";
        return 0;
    }

    if (session->terminal_fault.has_value()) {
        const auto& fault =
            session->terminal_fault.value();
        std::cout
            << "Astraea retail diagnostic\n"
            << "boundary=guest_fault\n"
            << "guest_rip=0x"
            << std::hex
            << fault.guest_rip.value()
            << std::dec
            << "\n"
            << "fault_kind="
            << static_cast<std::uint32_t>(
                   fault.kind)
            << "\n";
        return 0;
    }

    std::cerr
        << "Retail diagnostic ended without a typed boundary.\n";
    return 6;
#endif
}

}  // namespace astraea::app
