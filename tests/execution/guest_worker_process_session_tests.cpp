#include "owned_two_elf_worker_fixture.hpp"
#include "owned_linked_elf_worker_fixture.hpp"
#include "owned_linked_pair_worker_fixture.hpp"
#include "owned_pair_sha256.hpp"

#include <astraea/execution/guest_worker_process_session.hpp>

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

#ifndef ASTRAEA_GUEST_WORKER_PROBE_PATH
#define ASTRAEA_GUEST_WORKER_PROBE_PATH ""
#endif

#ifndef ASTRAEA_APP_PATH
#define ASTRAEA_APP_PATH ""
#endif

namespace {

#if defined(_WIN32)
class TestHandle {
public:
    explicit TestHandle(HANDLE handle) noexcept
        : handle_(handle) {}

    TestHandle(const TestHandle&) = delete;
    TestHandle& operator=(const TestHandle&) = delete;

    ~TestHandle() {
        if (handle_ != nullptr &&
            handle_ != INVALID_HANDLE_VALUE) {
            (void)::CloseHandle(handle_);
        }
    }

    [[nodiscard]] HANDLE get() const noexcept {
        return handle_;
    }

private:
    HANDLE handle_ = nullptr;
};
#endif

astraea::execution::GuestWorkerProcessSessionConfig
config(
    std::vector<std::string> arguments = {},
    std::uint64_t timeout_milliseconds = 3000U) {
    return astraea::execution::
        GuestWorkerProcessSessionConfig{
            .worker_executable =
                ASTRAEA_GUEST_WORKER_PROBE_PATH,
            .worker_arguments =
                std::move(arguments),
            .run_budget_microseconds =
                100000U,
            .timeout_milliseconds =
                timeout_milliseconds,
            .syscall_service = {},
            .max_syscall_requests = 0U,
            .resource_policy = std::nullopt,
            .linux_artifact_bytes = std::nullopt,
        };
}

}  // namespace

TEST_CASE(
    "guest-worker process session availability is platform explicit",
    "[execution][c0][process]") {
#if defined(__linux__) || defined(_WIN32)
    REQUIRE(
        astraea::execution::
            guest_worker_process_session_available());
#else
    REQUIRE_FALSE(
        astraea::execution::
            guest_worker_process_session_available());
#endif
}

TEST_CASE(
    "guest-worker pidfd capability is explicit and session uses it when available",
    "[execution][c0][process][pidfd]") {
#if defined(__linux__)
    const auto host_has_pidfd =
        astraea::execution::
            guest_worker_process_pidfd_available();

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config());

    REQUIRE(result.has_value());
    REQUIRE(
        result->linux_pidfd_used ==
        host_has_pidfd);
#else
    REQUIRE_FALSE(
        astraea::execution::
            guest_worker_process_pidfd_available());
#endif
}

TEST_CASE(
    "production Linux retail worker reports malformed artifact as typed loader boundary",
    "[execution][c0][retail][process][production]") {
#if defined(__linux__) && defined(__x86_64__)
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                astraea::execution::
                    GuestWorkerProcessSessionConfig{
                        .worker_executable =
                            ASTRAEA_APP_PATH,
                        .worker_arguments = {
                            "--linux-retail-diagnostic-worker",
                        },
                        .run_budget_microseconds =
                            100000U,
                        .timeout_milliseconds =
                            3000U,
                        .syscall_service = {},
                        .max_syscall_requests = 0U,
                        .resource_policy = std::nullopt,
                        .linux_artifact_bytes =
                            std::vector<std::byte>{
                                std::byte{0x00},
                                std::byte{0x01},
                                std::byte{0x02},
                                std::byte{0x03},
                            },
                    });

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_diagnostic.has_value());
    REQUIRE_FALSE(result->terminal_fault.has_value());
    REQUIRE(
        result->terminal_diagnostic->kind ==
        astraea::execution::
            GuestWorkerDiagnosticKind::
                loader_rejected);
    REQUIRE(
        result->terminal_diagnostic->
            guest_rip.value() == 0U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                diagnostic_boundary);
    REQUIRE(
        result->stop.thread_id ==
        result->terminal_diagnostic->thread_id);
    REQUIRE(
        result->stop.guest_rip ==
        result->terminal_diagnostic->guest_rip);
    REQUIRE(result->child_exit_code == 0);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "owned SHA-256 evidence follows standard vectors and pinned independent ELF source",
    "[execution][c1][sha256][provenance]") {
    REQUIRE(astraea::test::owned_pair_sha256_hex(
        std::span<const std::byte>{}) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::array abc{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
    };
    REQUIRE(astraea::test::owned_pair_sha256_hex(abc) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    // Canonical *source-only* fixture uses a fixed reference base and a
    // 4096-byte page. It is never mapped or run at this address: runtime
    // binaries still use the host-selected address in their sealed bundle.
    // These NIST-independent values were cross-checked with Python hashlib.
    auto source = astraea::test::detail::make_owned_binary_pair(
        0x100000000ULL, 4096U);
    REQUIRE(source.has_value());
    REQUIRE(astraea::test::owned_pair_sha256_hex(source->client) ==
        "50a7fe41e4833f5ac5cfa87ced4a658efc584a5310805dff6fe08f4770335a31");
    REQUIRE(astraea::test::owned_pair_sha256_hex(source->provider) ==
        "da76c429e4d722f618a33396281add0b40bbb6e832b0fa78e21be4a77c4d9834");
#endif
}

TEST_CASE(
    "isolated Linux worker resolves two independently linked ELF modules",
    "[execution][c1][linked-pair][process][linux]") {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    // These source-authored inputs were linked independently: the client
    // carries a real JUMP_SLOT relocation and the provider defines its exact
    // long-form symbol identity. Both complete ELF files remain sealed.
    auto frozen = astraea::test::make_owned_linked_pair_sealed_bundle();
    REQUIRE(frozen.has_value());
    REQUIRE(astraea::test::owned_linked_pair_bundle_matches_source(
        frozen.value()));
    const auto original = frozen.value();
    const auto digest = astraea::test::owned_pair_sha256(original);
    auto settings = config({"--owned-linked-pair-execution"}, 15000U);
    settings.linux_artifact_bytes = original;
    settings.resource_policy =
        astraea::execution::GuestWorkerResourcePolicy{
            .process_memory_limit_bytes = std::nullopt,
            .process_cpu_time_seconds = 5U,
            .linux_max_open_files = 64U,
            .linux_disable_core_dumps = true,
            .linux_disable_file_growth = true,
        };
    const auto first =
        astraea::execution::run_guest_worker_process_session(settings);
    const auto second =
        astraea::execution::run_guest_worker_process_session(settings);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    const auto verify = [](const auto& result) {
        REQUIRE(result.child_exit_code == 0);
        REQUIRE(result.syscall_request_count == 0U);
        REQUIRE_FALSE(result.terminal_fault.has_value());
        REQUIRE_FALSE(result.terminal_diagnostic.has_value());
        REQUIRE(result.stop.reason ==
            astraea::execution::GuestWorkerStopReason::
                normal_guest_return);
        REQUIRE(result.stop.guest_rip.value() != 0U);
        REQUIRE(result.stop.worker_id == result.ready.worker_id);
        REQUIRE(result.stop.thread_id.value == 1U);
    };
    verify(first.value());
    verify(second.value());
    REQUIRE(settings.linux_artifact_bytes == original);
    REQUIRE(astraea::test::owned_pair_sha256(
        settings.linux_artifact_bytes.value()) == digest);

    // Tamper checks reuse the same supervisor/fd3 containment; they cannot
    // be mistaken for a guest return or a successful import resolution.
    const auto reject = [&](std::optional<std::vector<std::byte>> altered) {
        auto invalid = settings;
        invalid.linux_artifact_bytes = std::move(altered);
        const auto result =
            astraea::execution::run_guest_worker_process_session(invalid);
        REQUIRE(result.has_value());
        REQUIRE(result->child_exit_code == 0);
        REQUIRE(result->terminal_diagnostic.has_value());
        REQUIRE(result->terminal_diagnostic->kind ==
            astraea::execution::GuestWorkerDiagnosticKind::loader_rejected);
        REQUIRE(result->terminal_diagnostic->guest_rip.value() == 0U);
        REQUIRE(result->stop.reason ==
            astraea::execution::GuestWorkerStopReason::
                diagnostic_boundary);
        REQUIRE(result->stop.guest_rip.value() == 0U);
        REQUIRE_FALSE(result->terminal_fault.has_value());
    };
    auto altered_magic = original;
    altered_magic[0U] ^= std::byte{1};
    reject(std::move(altered_magic));
    auto altered_client_import = original;
    altered_client_import[16U + 24U] ^= std::byte{1};
    reject(std::move(altered_client_import));
    auto altered_provider = original;
    altered_provider.back() ^= std::byte{1};
    reject(std::move(altered_provider));
    auto altered_rebase = original;
    altered_rebase[8U] ^= std::byte{1};
    reject(std::move(altered_rebase));
    auto truncated = original;
    truncated.pop_back();
    reject(std::move(truncated));
    reject(std::nullopt);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "isolated Linux worker executes a complete host-linked ELF twice",
    "[execution][c1][linked-elf][process][linux]") {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    // This is a complete freestanding linker output whose immutable section
    // table, program header and symbol table were checked during the build.
    // Only its test-owned linked base and synthetic exit gate are rebased.
    auto bundle = astraea::test::make_owned_linked_elf_sealed_bundle();
    REQUIRE(bundle.has_value());
    REQUIRE(astraea::test::owned_linked_elf_bundle_matches_source(
        bundle.value()));
    const auto frozen = bundle.value();
    REQUIRE(frozen.size() > 4096U + 16U);
    const auto original_hash = astraea::test::owned_pair_sha256(frozen);

    auto settings = config({"--owned-linked-elf-execution"}, 15000U);
    settings.linux_artifact_bytes = frozen;
    settings.resource_policy =
        astraea::execution::GuestWorkerResourcePolicy{
            .process_memory_limit_bytes = std::nullopt,
            .process_cpu_time_seconds = 5U,
            .linux_max_open_files = 64U,
            .linux_disable_core_dumps = true,
            .linux_disable_file_growth = true,
        };
    const auto one =
        astraea::execution::run_guest_worker_process_session(settings);
    const auto two =
        astraea::execution::run_guest_worker_process_session(settings);
    REQUIRE(one.has_value());
    REQUIRE(two.has_value());
    const auto check = [](const auto& result) {
        REQUIRE(result.child_exit_code == 0);
        REQUIRE(result.syscall_request_count == 0U);
        REQUIRE_FALSE(result.terminal_fault.has_value());
        REQUIRE_FALSE(result.terminal_diagnostic.has_value());
        REQUIRE(result.stop.reason ==
            astraea::execution::GuestWorkerStopReason::
                normal_guest_return);
        REQUIRE(result.stop.guest_rip.value() != 0U);
        REQUIRE(result.stop.worker_id == result.ready.worker_id);
        REQUIRE(result.stop.thread_id.value == 1U);
    };
    check(one.value());
    check(two.value());
    REQUIRE(settings.linux_artifact_bytes == frozen);
    REQUIRE(astraea::test::owned_pair_sha256(
        settings.linux_artifact_bytes.value()) == original_hash);

    const auto reject = [&](std::optional<std::vector<std::byte>> data) {
        auto damaged = settings;
        damaged.linux_artifact_bytes = std::move(data);
        const auto result =
            astraea::execution::run_guest_worker_process_session(damaged);
        REQUIRE(result.has_value());
        REQUIRE(result->child_exit_code == 0);
        REQUIRE(result->terminal_diagnostic.has_value());
        REQUIRE(result->terminal_diagnostic->kind ==
            astraea::execution::GuestWorkerDiagnosticKind::loader_rejected);
        REQUIRE(result->terminal_diagnostic->guest_rip.value() == 0U);
        REQUIRE(result->stop.reason ==
            astraea::execution::GuestWorkerStopReason::diagnostic_boundary);
        REQUIRE(result->stop.guest_rip.value() == 0U);
        REQUIRE_FALSE(result->terminal_fault.has_value());
    };

    // Each refusal happens before mmap/guest entry and uses the existing
    // sealed fd, controller/worker framing and resource ceilings.
    auto changed_magic = frozen;
    changed_magic[0] ^= std::byte{1};
    reject(std::move(changed_magic));
    auto changed_entry = frozen;
    changed_entry[16U + 24U] ^= std::byte{1};
    reject(std::move(changed_entry));
    auto changed_load_address = frozen;
    changed_load_address[16U + 64U + 16U] ^= std::byte{1};
    reject(std::move(changed_load_address));
    auto changed_gate = frozen;
    changed_gate[16U + 0x1000U + 12U] ^= std::byte{1};
    reject(std::move(changed_gate));
    auto truncated = frozen;
    truncated.pop_back();
    reject(std::move(truncated));
    reject(std::nullopt);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "isolated Linux worker executes two source-owned ELF modules to a repeatable typed stop",
    "[execution][c1][two-elf][process][linux]") {
#if defined(__linux__) && defined(__x86_64__)
    // This is an independently authored research-only process. The
    // controller does not execute native guest instructions. The worker
    // internally requires exit_code=42, the exact graph-resolved provider
    // control transfer, a single synthetic exit gate and zero HLE output.
    auto frozen_bundle = astraea::test::make_owned_two_elf_sealed_bundle();
    REQUIRE(frozen_bundle.has_value());
    REQUIRE(frozen_bundle->size() > 16U);
    const auto exactly_frozen_bytes = frozen_bundle.value();

    // Record both exact-runtime bundle identity and its two individual
    // authored ELF digests. This SHA-256 is evidence, not an HLE or Sony
    // trust policy. The bundle's hash may differ across independent hosts
    // because it embeds the controller-selected guest virtual address.
    const auto whole_sha =
        astraea::test::owned_pair_sha256(exactly_frozen_bytes);
    REQUIRE((exactly_frozen_bytes.size() - 16U) % 5U == 0U);
    const auto page =
        (exactly_frozen_bytes.size() - 16U) / 5U;
    const std::span<const std::byte> client_bytes{
        exactly_frozen_bytes.data() + 16U, 3U * page,
    };
    const std::span<const std::byte> provider_bytes{
        exactly_frozen_bytes.data() + 16U + 3U * page,
        2U * page,
    };
    const auto client_sha =
        astraea::test::owned_pair_sha256(client_bytes);
    const auto provider_sha =
        astraea::test::owned_pair_sha256(provider_bytes);
    REQUIRE(client_sha != provider_sha);

    auto settings = config({"--owned-two-elf-execution"}, 15000U);
    // The same immutable byte vector is sealed by the controller in two
    // separately launched workers. The worker accepts only byte-exact
    // authored ELF images, never generated run-specific replacements.
    settings.linux_artifact_bytes = frozen_bundle.value();
    settings.resource_policy =
        astraea::execution::GuestWorkerResourcePolicy{
            .process_memory_limit_bytes = std::nullopt,
            .process_cpu_time_seconds = 5U,
            .linux_max_open_files = 64U,
            .linux_disable_core_dumps = true,
            .linux_disable_file_growth = true,
        };

    const auto first =
        astraea::execution::run_guest_worker_process_session(settings);
    REQUIRE(first.has_value());
    const auto second =
        astraea::execution::run_guest_worker_process_session(settings);
    REQUIRE(second.has_value());

    const auto check = [](const auto& result) {
        REQUIRE(result.child_exit_code == 0);
        REQUIRE(result.syscall_request_count == 0U);
        REQUIRE_FALSE(result.terminal_fault.has_value());
        REQUIRE_FALSE(result.terminal_diagnostic.has_value());
        REQUIRE(result.stop.reason ==
            astraea::execution::GuestWorkerStopReason::
                normal_guest_return);
        REQUIRE(result.stop.guest_rip.value() != 0U);
        REQUIRE(result.stop.worker_id == result.ready.worker_id);
        REQUIRE(result.stop.thread_id.value == 1U);
    };
    check(first.value());
    check(second.value());
    REQUIRE(settings.linux_artifact_bytes.has_value());
    REQUIRE(settings.linux_artifact_bytes.value() == exactly_frozen_bytes);
    REQUIRE(astraea::test::owned_pair_sha256(
        settings.linux_artifact_bytes.value()) == whole_sha);
    REQUIRE(astraea::test::owned_pair_sha256(
        std::span<const std::byte>{
            settings.linux_artifact_bytes->data() + 16U,
            3U * page}) == client_sha);
    REQUIRE(astraea::test::owned_pair_sha256(
        std::span<const std::byte>{
            settings.linux_artifact_bytes->data() +
                16U + 3U * page, 2U * page}) == provider_sha);

    // A mismatch, missing artifact or malformed bundle must produce a
    // typed diagnostic boundary and clean worker exit, never appear to
    // have executed guest code. All cases reuse the same containment policy.
    const auto require_rejection = [&](
        std::optional<std::vector<std::byte>> modified) {
        auto invalid = settings;
        invalid.linux_artifact_bytes = std::move(modified);
        const auto result =
            astraea::execution::run_guest_worker_process_session(invalid);
        REQUIRE(result.has_value());
        REQUIRE(result->child_exit_code == 0);
        REQUIRE_FALSE(result->terminal_fault.has_value());
        REQUIRE(result->terminal_diagnostic.has_value());
        REQUIRE(result->terminal_diagnostic->kind ==
            astraea::execution::GuestWorkerDiagnosticKind::loader_rejected);
        REQUIRE(result->terminal_diagnostic->guest_rip.value() == 0U);
        REQUIRE(result->stop.reason ==
            astraea::execution::GuestWorkerStopReason::diagnostic_boundary);
        REQUIRE(result->stop.guest_rip.value() == 0U);
    };

    // A one-byte change to either ELF, or to the container framing,
    // cannot pass exact source identity preflight.
    auto damaged_provider = exactly_frozen_bytes;
    damaged_provider.back() ^= std::byte{0x01};
    REQUIRE(astraea::test::owned_pair_sha256(damaged_provider) !=
        whole_sha);
    require_rejection(std::move(damaged_provider));
    auto damaged_client = exactly_frozen_bytes;
    damaged_client[16U] ^= std::byte{0x01};
    require_rejection(std::move(damaged_client));
    auto damaged_magic = exactly_frozen_bytes;
    damaged_magic[0U] ^= std::byte{0x01};
    require_rejection(std::move(damaged_magic));
    auto truncated = exactly_frozen_bytes;
    truncated.pop_back();
    require_rejection(std::move(truncated));
    // No fd 3 at all is refused inside the worker, with a typed stop.
    require_rejection(std::nullopt);

    // An explicitly empty optional byte vector is rejected by the
    // controller's pre-spawn configuration validation instead.
    auto empty_config = settings;
    empty_config.linux_artifact_bytes = std::vector<std::byte>{};
    auto empty_result =
        astraea::execution::run_guest_worker_process_session(
            empty_config);
    REQUIRE_FALSE(empty_result.has_value());
    REQUIRE(empty_result.error().code ==
        astraea::execution::GuestWorkerProcessSessionErrorCode::invalid_config);

    // Compare normalized protocol evidence. Absolute native guest RIPs
    // are intentionally ASLR-dependent and must not be matched bytewise.
    REQUIRE(first->stop.reason == second->stop.reason);
    REQUIRE(first->stop.worker_id == second->stop.worker_id);
    REQUIRE(first->stop.thread_id == second->stop.thread_id);
    REQUIRE(first->child_exit_code == second->child_exit_code);
    REQUIRE(first->syscall_request_count ==
        second->syscall_request_count);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "Linux sealed artifact handoff reaches worker as immutable fd 3 bytes",
    "[execution][c0][process][artifact][linux]") {
#if defined(__linux__)
    auto artifact_config =
        config({"--artifact-probe"});
    artifact_config.linux_artifact_bytes =
        std::vector<std::byte>{
            std::byte{0x00},
            std::byte{0x41},
            std::byte{0xff},
            std::byte{0x7f},
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
            std::byte{0x40},
            std::byte{0xaa},
            std::byte{0x55},
            std::byte{0x00},
            std::byte{0xee},
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                artifact_config);

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "Linux sealed artifact consumer rejects bytes above explicit worker limit",
    "[execution][c0][process][artifact][linux][bound]") {
#if defined(__linux__)
    auto artifact_config =
        config({"--artifact-limit-probe"});
    artifact_config.linux_artifact_bytes =
        std::vector<std::byte>{
            std::byte{0x00},
            std::byte{0x41},
            std::byte{0xff},
            std::byte{0x7f},
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
            std::byte{0x40},
            std::byte{0xaa},
            std::byte{0x55},
            std::byte{0x00},
            std::byte{0xee},
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                artifact_config);

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "session without artifact leaves child fd 3 closed",
    "[execution][c0][process][artifact][negative]") {
#if defined(__linux__)
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--no-artifact-fd"}));

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);
#else
    SUCCEED();
#endif
}

TEST_CASE(
    "Linux artifact option is rejected on non-Linux hosts and rejects empty bytes",
    "[execution][c0][process][artifact][config]") {
    auto artifact_config = config();
    artifact_config.linux_artifact_bytes =
        std::vector<std::byte>{};

    const auto empty =
        astraea::execution::
            run_guest_worker_process_session(
                artifact_config);
    REQUIRE_FALSE(empty.has_value());
    REQUIRE(
        empty.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                invalid_config);

#if !defined(__linux__)
    artifact_config.linux_artifact_bytes =
        std::vector<std::byte>{
            std::byte{0x01},
        };
    const auto unsupported =
        astraea::execution::
            run_guest_worker_process_session(
                artifact_config);
    REQUIRE_FALSE(unsupported.has_value());
    REQUIRE(
        unsupported.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                invalid_config);
#endif
}

TEST_CASE(
    "guest-worker process session rejects invalid configuration before launch",
    "[execution][c0][process][negative]") {
    auto invalid = config();
    invalid.worker_executable = "relative-worker";

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                invalid);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                invalid_config);
}

#if defined(__linux__) || defined(_WIN32)

TEST_CASE(
    "controller and worker complete bounded handshake run stop terminate",
    "[execution][c0][process][supervised]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config());

    REQUIRE(result.has_value());
    REQUIRE(
        result->ready.protocol_version ==
        astraea::execution::
            kGuestWorkerProtocolVersion);
    REQUIRE(result->ready.worker_id.value == 1U);
    REQUIRE(
        result->stop.worker_id ==
        result->ready.worker_id);
    REQUIRE(result->stop.thread_id.value == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE(result->stop.guest_rip.value() == 0U);
    REQUIRE(result->child_exit_code == 0);
#if defined(__linux__)
    REQUIRE(
        result->linux_pidfd_used ==
        astraea::execution::
            guest_worker_process_pidfd_available());
#else
    REQUIRE_FALSE(result->linux_pidfd_used);
#endif
}

TEST_CASE(
    "worker crash before READY becomes deterministic EOF",
    "[execution][c0][process][supervised][negative]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config(
                    {"--crash-before-ready"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                unexpected_eof);
}

TEST_CASE(
    "malformed worker frame fails at wire boundary",
    "[execution][c0][process][supervised][negative][wire]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config(
                    {"--bad-frame-after-hello"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                wire_failure);
    REQUIRE(result.error().wire_error.has_value());
    REQUIRE(
        result.error().wire_error->code ==
        astraea::execution::
            GuestWorkerWireErrorCode::
                invalid_magic);
}

TEST_CASE(
    "worker timeout returns only after cleanup and session is reusable",
    "[execution][c0][process][supervised][negative][timeout]") {
    const auto started =
        std::chrono::steady_clock::now();

    const auto timed_out =
        astraea::execution::
            run_guest_worker_process_session(
                config(
                    {"--hang-after-run"},
                    1000U));

    const auto elapsed =
        std::chrono::steady_clock::now() -
        started;

    REQUIRE_FALSE(timed_out.has_value());
    REQUIRE(
        timed_out.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                timeout);
    REQUIRE(
        elapsed <
        std::chrono::seconds{5});

    // A second child must launch and complete immediately after the timed-out
    // child was force-terminated and reaped.
    const auto subsequent =
        astraea::execution::
            run_guest_worker_process_session(
                config());
    REQUIRE(subsequent.has_value());
    REQUIRE(subsequent->child_exit_code == 0);
}

TEST_CASE(
    "supervised session brokers one typed syscall request",
    "[execution][c0][process][syscall]") {
    std::optional<
        astraea::execution::GuestWorkerSyscallRequest>
        observed;

    auto syscall_config =
        config({"--syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [&observed](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            observed = request;
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE(observed.has_value());
    REQUIRE(observed->request_id.value == 41U);
    REQUIRE(observed->worker_id.value == 1U);
    REQUIRE(observed->thread_id.value == 1U);
    REQUIRE(
        observed->guest_syscall_number ==
        0x5152535455565758ULL);
    REQUIRE(
        observed->arguments[0] ==
        0x1111111111111111ULL);
    REQUIRE(
        observed->arguments[1] ==
        0x2222222222222222ULL);
    REQUIRE(
        observed->arguments[2] ==
        0x3333333333333333ULL);
    REQUIRE(
        observed->arguments[3] ==
        0x4444444444444444ULL);
    REQUIRE(
        observed->arguments[4] ==
        0x5555555555555555ULL);
    REQUIRE(
        observed->arguments[5] ==
        0x6666666666666666ULL);
    REQUIRE(
        observed->guest_rip ==
        astraea::memory::GuestAddress{
            0x400100U});
}

TEST_CASE(
    "controller refuses incomplete worker syscall identity before service",
    "[execution][c0][process][syscall][negative][identity]") {
    const auto verify_refused =
        [](const std::string& mode) {
            bool service_called = false;
            auto session = config({mode});
            session.max_syscall_requests = 1U;
            session.syscall_service =
                [&service_called](
                    const astraea::execution::
                        GuestWorkerSyscallRequest& request)
                -> std::optional<astraea::execution::
                    GuestWorkerSyscallResult> {
                    service_called = true;
                    return astraea::execution::
                        GuestWorkerSyscallResult{
                            .request_id = request.request_id,
                            .worker_id = request.worker_id,
                            .thread_id = request.thread_id,
                            .return_value = 0,
                            .guest_errno = 0,
                        };
                };

            const auto result =
                astraea::execution::
                    run_guest_worker_process_session(session);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error().code ==
                    astraea::execution::
                        GuestWorkerProcessSessionErrorCode::
                            protocol_failure);
            REQUIRE_FALSE(service_called);
        };

    SECTION("zero request id") {
        verify_refused("--syscall-zero-request-id");
    }
    SECTION("zero thread id") {
        verify_refused("--syscall-zero-thread-id");
    }
    SECTION("zero guest rip") {
        verify_refused("--syscall-zero-rip");
    }
}

TEST_CASE(
    "supervised worker round-trips one registered native syscall and resumes",
    "[execution][c0][process][syscall][native]") {
    std::optional<
        astraea::execution::GuestWorkerSyscallRequest>
        observed;

    auto syscall_config =
        config({"--native-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [&observed](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            observed = request;
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE(observed.has_value());
    REQUIRE(observed->request_id.value == 41U);
    REQUIRE(observed->worker_id.value == 1U);
    REQUIRE(observed->thread_id.value == 1U);
    REQUIRE(
        observed->guest_syscall_number ==
        0x5152535455565758ULL);
    REQUIRE(
        observed->arguments[0] ==
        0x1111111111111111ULL);
    REQUIRE(
        observed->arguments[1] ==
        0x2222222222222222ULL);
    REQUIRE(
        observed->arguments[2] ==
        0x3333333333333333ULL);
    REQUIRE(
        observed->arguments[3] ==
        0x4444444444444444ULL);
    REQUIRE(
        observed->arguments[4] ==
        0x5555555555555555ULL);
    REQUIRE(
        observed->arguments[5] ==
        0x6666666666666666ULL);
    REQUIRE(observed->guest_rip.value() != 0U);
}

TEST_CASE(
    "native worker refuses mismatched controller result before guest re-entry",
    "[execution][c0][process][syscall][native][negative][identity]") {
    auto syscall_config =
        config({"--native-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        astraea::execution::
                            GuestThreadId{
                                .value =
                                    request.thread_id.value +
                                    1U,
                            },
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                protocol_failure);
}

TEST_CASE(
    "worker rejects mismatched syscall result identity before continuing",
    "[execution][c0][process][syscall][negative][identity]") {
    auto syscall_config =
        config({"--syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        astraea::execution::
                            GuestRequestId{
                                .value =
                                    request.request_id.value +
                                    1U,
                            },
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = 99,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                protocol_failure);
}

TEST_CASE(
    "syscall request without configured service tears worker down",
    "[execution][c0][process][syscall][negative][service]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--syscall-roundtrip"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                syscall_service_unavailable);
}

TEST_CASE(
    "controller syscall service may reject a request without resuming worker",
    "[execution][c0][process][syscall][negative][service]") {
    auto syscall_config =
        config({"--syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest&)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return std::nullopt;
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                syscall_service_rejected);
}

TEST_CASE(
    "supervisor propagates native access violation as terminal typed fault",
    "[execution][c0][process][fault][native][access]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--native-access-fault"}));

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_fault.has_value());
    REQUIRE(
        result->terminal_fault->kind ==
        astraea::execution::
            GuestWorkerFaultKind::
                access_violation);
    REQUIRE(result->terminal_fault->worker_id.value == 1U);
    REQUIRE(result->terminal_fault->thread_id.value == 1U);
    REQUIRE(result->terminal_fault->guest_rip.value() != 0U);
    REQUIRE(
        result->terminal_fault->fault_address.value() ==
        0U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::guest_fault);
    REQUIRE(
        result->stop.guest_rip ==
        result->terminal_fault->guest_rip);
    REQUIRE(
        result->stop.thread_id ==
        result->terminal_fault->thread_id);
    REQUIRE(result->syscall_request_count == 0U);
    REQUIRE(result->child_exit_code == 0);

    // Terminal guest faults must not poison process-session reuse.
    const auto subsequent =
        astraea::execution::
            run_guest_worker_process_session(
                config());
    REQUIRE(subsequent.has_value());
    REQUIRE_FALSE(
        subsequent->terminal_fault.has_value());
}

TEST_CASE(
    "supervisor propagates unregistered UD2 as illegal-instruction fault",
    "[execution][c0][process][fault][native][illegal]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({
                    "--native-illegal-instruction-fault"}));

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_fault.has_value());
    REQUIRE(
        result->terminal_fault->kind ==
        astraea::execution::
            GuestWorkerFaultKind::
                illegal_instruction);
    REQUIRE(result->terminal_fault->guest_rip.value() != 0U);
    REQUIRE(
        result->terminal_fault->fault_address.value() ==
        0U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::guest_fault);
    REQUIRE(
        result->stop.guest_rip ==
        result->terminal_fault->guest_rip);
    REQUIRE(result->syscall_request_count == 0U);
    REQUIRE(result->child_exit_code == 0);
}

TEST_CASE(
    "terminal guest fault cannot be followed by resumable syscall event",
    "[execution][c0][process][fault][negative][ordering]") {
    bool service_called = false;
    auto invalid =
        config({"--fault-then-syscall"});
    invalid.max_syscall_requests = 1U;
    invalid.syscall_service =
        [&service_called](
            const astraea::execution::
                GuestWorkerSyscallRequest&)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            service_called = true;
            return std::nullopt;
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                invalid);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                protocol_failure);
    REQUIRE_FALSE(service_called);
}

TEST_CASE(
    "supervisor propagates terminal diagnostic with matching stop",
    "[execution][c0][process][diagnostic]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--diagnostic-event"}));

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_diagnostic.has_value());
    REQUIRE_FALSE(result->terminal_fault.has_value());
    REQUIRE(
        result->terminal_diagnostic->kind ==
        astraea::execution::
            GuestWorkerDiagnosticKind::
                unsupported_tls);
    REQUIRE(
        result->terminal_diagnostic->detail0 ==
        8U);
    REQUIRE(
        result->terminal_diagnostic->detail1 ==
        1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                diagnostic_boundary);
    REQUIRE(
        result->stop.thread_id ==
        result->terminal_diagnostic->thread_id);
    REQUIRE(
        result->stop.guest_rip ==
        result->terminal_diagnostic->guest_rip);
    REQUIRE(result->syscall_request_count == 0U);
    REQUIRE(result->child_exit_code == 0);
}

TEST_CASE(
    "terminal diagnostic cannot be followed by resumable syscall event",
    "[execution][c0][process][diagnostic][negative][ordering]") {
    bool service_called = false;
    auto invalid =
        config({"--diagnostic-then-syscall"});
    invalid.max_syscall_requests = 1U;
    invalid.syscall_service =
        [&service_called](
            const astraea::execution::
                GuestWorkerSyscallRequest&)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            service_called = true;
            return std::nullopt;
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                invalid);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                protocol_failure);
    REQUIRE_FALSE(service_called);
}

TEST_CASE(
    "diagnostic boundary stop requires preceding typed diagnostic",
    "[execution][c0][process][diagnostic][negative][stop]") {
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({"--diagnostic-stop-only"}));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                protocol_failure);
}

TEST_CASE(
    "worker runs under explicit kernel resource ceilings",
    "[execution][c0][process][resource-policy]") {
    auto limited = config();

    astraea::execution::GuestWorkerResourcePolicy policy{};
#if !defined(__SANITIZE_ADDRESS__)
    policy.process_memory_limit_bytes =
        16ULL * 1024ULL * 1024ULL * 1024ULL;
#endif
    policy.process_cpu_time_seconds = 30U;
#if defined(__linux__)
    policy.linux_max_open_files = 32U;
    policy.linux_disable_core_dumps = true;
    policy.linux_disable_file_growth = true;
#endif
    limited.resource_policy = policy;

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                limited);

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
}

TEST_CASE(
    "kernel CPU ceiling terminates busy worker before controller deadline",
    "[execution][c0][process][resource-policy][cpu]") {
    // CPU time is consumed only while the worker is scheduled. A loaded
    // shared CI host can take several wall-clock seconds to accumulate one
    // process CPU second, so keep the controller deadline comfortably above
    // the kernel CPU ceiling while still requiring termination before it.
    auto limited =
        config({"--burn-cpu"}, 15000U);

    astraea::execution::GuestWorkerResourcePolicy policy{};
    policy.process_cpu_time_seconds = 1U;
    limited.resource_policy = policy;

    const auto started =
        std::chrono::steady_clock::now();
    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                limited);
    const auto elapsed =
        std::chrono::steady_clock::now() -
        started;

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                unexpected_eof);
    REQUIRE(elapsed < std::chrono::seconds{15});

    const auto subsequent =
        astraea::execution::
            run_guest_worker_process_session(
                config());
    REQUIRE(subsequent.has_value());
    REQUIRE(subsequent->child_exit_code == 0);
}

TEST_CASE(
    "resource policy rejects zero common ceilings",
    "[execution][c0][process][resource-policy][negative]") {
    auto invalid = config();
    astraea::execution::GuestWorkerResourcePolicy policy{};
    policy.process_cpu_time_seconds = 0U;
    invalid.resource_policy = policy;

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                invalid);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            GuestWorkerProcessSessionErrorCode::
                invalid_config);
}

#if defined(__linux__) && defined(__x86_64__)

TEST_CASE(
    "Linux supervised worker round-trips one unmodified seccomp-trapped syscall",
    "[execution][c0][process][seccomp][syscall][native]") {
    std::optional<
        astraea::execution::GuestWorkerSyscallRequest>
        observed;

    auto syscall_config =
        config({
            "--native-seccomp-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [&observed](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            observed = request;
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                normal_guest_return);
    REQUIRE_FALSE(result->terminal_fault.has_value());
    REQUIRE(observed.has_value());
    REQUIRE(observed->request_id.value == 61U);
    REQUIRE(observed->worker_id.value == 1U);
    REQUIRE(observed->thread_id.value == 1U);
    REQUIRE(observed->guest_syscall_number == 0x1234U);
    REQUIRE(
        observed->arguments[0] ==
        0x1111111111111111ULL);
    REQUIRE(
        observed->arguments[1] ==
        0x2222222222222222ULL);
    REQUIRE(
        observed->arguments[2] ==
        0x3333333333333333ULL);
    REQUIRE(
        observed->arguments[3] ==
        0x4444444444444444ULL);
    REQUIRE(
        observed->arguments[4] ==
        0x5555555555555555ULL);
    REQUIRE(
        observed->arguments[5] ==
        0x6666666666666666ULL);
    REQUIRE(observed->guest_rip.value() != 0U);
}

TEST_CASE(
    "Linux seccomp worker refuses mismatched controller identity before resume",
    "[execution][c0][process][seccomp][negative][identity]") {
    auto syscall_config =
        config({
            "--native-seccomp-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        astraea::execution::
                            GuestThreadId{
                                .value =
                                    request.thread_id.value +
                                    1U,
                            },
                    .return_value = -77,
                    .guest_errno = 0,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                protocol_failure);
}

TEST_CASE(
    "Linux seccomp worker refuses nonzero guest errno semantics before resume",
    "[execution][c0][process][seccomp][negative][errno]") {
    auto syscall_config =
        config({
            "--native-seccomp-syscall-roundtrip"});
    syscall_config.max_syscall_requests = 1U;
    syscall_config.syscall_service =
        [](
            const astraea::execution::
                GuestWorkerSyscallRequest& request)
            -> std::optional<
                astraea::execution::
                    GuestWorkerSyscallResult> {
            return astraea::execution::
                GuestWorkerSyscallResult{
                    .request_id =
                        request.request_id,
                    .worker_id =
                        request.worker_id,
                    .thread_id =
                        request.thread_id,
                    .return_value = -1,
                    .guest_errno = 5,
                };
        };

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                syscall_config);

    REQUIRE(result.has_value());
    REQUIRE(result->syscall_request_count == 1U);
    REQUIRE(
        result->stop.reason ==
        astraea::execution::
            GuestWorkerStopReason::
                protocol_failure);
}

#endif

#if defined(_WIN32)

TEST_CASE(
    "Windows worker does not inherit unrelated inheritable parent handle",
    "[execution][c0][process][windows][inheritance]") {
    SECURITY_ATTRIBUTES attributes{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };

    TestHandle unrelated_event{
        ::CreateEventW(
            &attributes,
            TRUE,
            FALSE,
            nullptr)};
    REQUIRE(unrelated_event.get() != nullptr);

    const auto handle_value =
        reinterpret_cast<std::uintptr_t>(
            unrelated_event.get());
    const auto argument =
        std::string{"--signal-handle="} +
        std::to_string(handle_value);

    const auto result =
        astraea::execution::
            run_guest_worker_process_session(
                config({argument}));

    REQUIRE(result.has_value());
    REQUIRE(result->child_exit_code == 0);

    // If broad inheritance leaked this exact event into the worker, the worker
    // would have signaled it before the HELLO/READY exchange.
    REQUIRE(
        ::WaitForSingleObject(
            unrelated_event.get(),
            0U) ==
        WAIT_TIMEOUT);
}

#endif

#endif
