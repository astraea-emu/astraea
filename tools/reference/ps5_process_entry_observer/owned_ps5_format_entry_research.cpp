// C1 research-only, exact-source PS5-format ELF first-instruction proof.
// This binary is built only with ASTRAEA_BUILD_C1_RESEARCH=ON (Linux x86-64).
// It accepts ONE independently authored, pinned SHA-256 image. It does not
// implement or enable production retail entry or Sony process ABI semantics.
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
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/types.h>
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

struct Terminal {
    e::GuestWorkerFault fault;
    e::GuestWorkerStop stop;
};

[[nodiscard]] std::optional<Terminal> execute_pinned_owned_title(
    std::span<const std::byte> source,
    std::uint64_t& stage) {
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
    stage = 6U;
    auto prepared = e::prepare_linux_guest_memory(image);
    if (!prepared.has_value()) return std::nullopt;
    e::GuestMemoryAccess memory{image, prepared.value()};

    stage = 7U;
    const auto applied = e::apply_owned_relative_batch(patches, memory);
    if (!applied.has_value() || applied->size() != 8U) return std::nullopt;
    std::array<std::byte, 2U> first_opcode{};
    if (!memory.read(m::GuestAddress{image.elf.header.entry},
                     first_opcode).has_value() ||
        first_opcode != std::array<std::byte, 2U>{
            std::byte{0x0f}, std::byte{0x0b}})
        return std::nullopt;

    // Explicitly synthetic entry context: UD2 consumes no PS5 process
    // parameters. This NEVER promotes an initial-process ABI contract.
    stage = 8U;
    auto run = e::enter_linux_guest_with_seccomp_syscall_trap(
        image, prepared.value(), e::make_synthetic_initial_context(image));
    if (!run.has_value()) return std::nullopt;
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
        if (argc != 2) return 1;
        if (std::string_view{argv[1]} == "--sealed-worker")
            return worker_main();
        return controller_main(argv[1]);
    } catch (const std::exception& error) {
        std::cerr << "Research probe infrastructure error: "
                  << error.what() << "\n";
        return 99;
    }
}
