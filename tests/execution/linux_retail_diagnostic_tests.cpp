#include <astraea/execution/linux_retail_diagnostic.hpp>
#include <astraea/execution/guest_worker_process_session.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#ifndef ASTRAEA_APP_PATH
#define ASTRAEA_APP_PATH ""
#endif

namespace {

constexpr std::size_t kElfHeaderSize = 64U;
constexpr std::size_t kProgramHeaderSize = 56U;
constexpr std::size_t kProgramHeaderOffset = 64U;
constexpr std::size_t kDynamicOffset = 0x280U;
constexpr std::size_t kStringOffset = 0x340U;
constexpr std::size_t kSymbolOffset = 0x360U;
constexpr std::size_t kRelaOffset = 0x390U;
constexpr std::size_t kTlsOffset = 0x3c0U;
constexpr std::size_t kImageSize = 0x500U;

constexpr std::uint64_t kGuestBase = 0x400000U;
constexpr std::uint64_t kEntry = kGuestBase + 0x100U;

struct FixtureOptions {
    bool executable = true;
    bool generic_needed = false;
    bool sce_needed = false;
    bool sce_metadata_conflict = false;
    bool relocation = false;
    bool tls = false;
};

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    const auto widened =
        static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 2U; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (widened >> (i * 8U)) & 0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    const auto widened =
        static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 4U; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (widened >> (i * 8U)) & 0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t i = 0; i < 8U; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (value >> (i * 8U)) & 0xffU);
    }
}

void write_i64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::int64_t value) {
    write_u64(
        bytes,
        offset,
        std::bit_cast<std::uint64_t>(value));
}

void write_program_header(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t type,
    std::uint32_t flags,
    std::uint64_t file_offset,
    std::uint64_t virtual_address,
    std::uint64_t file_size,
    std::uint64_t memory_size,
    std::uint64_t alignment) {
    write_u32(bytes, offset, type);
    write_u32(bytes, offset + 4U, flags);
    write_u64(bytes, offset + 8U, file_offset);
    write_u64(bytes, offset + 16U, virtual_address);
    write_u64(bytes, offset + 24U, 0U);
    write_u64(bytes, offset + 32U, file_size);
    write_u64(bytes, offset + 40U, memory_size);
    write_u64(bytes, offset + 48U, alignment);
}

void write_dynamic_entry(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::int64_t tag,
    std::uint64_t value) {
    const auto offset =
        kDynamicOffset + index * 16U;
    write_i64(bytes, offset, tag);
    write_u64(bytes, offset + 8U, value);
}

std::vector<std::byte>
make_sce_fixture(FixtureOptions options = {}) {
    std::vector<std::byte> bytes(
        kImageSize,
        std::byte{0});

    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};

    const bool has_dynamic =
        options.generic_needed ||
        options.sce_needed ||
        options.sce_metadata_conflict ||
        options.relocation;
    const std::uint16_t ph_count =
        static_cast<std::uint16_t>(
            1U +
            (has_dynamic ? 1U : 0U) +
            (options.tls ? 1U : 0U));

    write_u16(bytes, 16U, 0xfe10U);
    write_u16(bytes, 18U, 62U);
    write_u32(bytes, 20U, 1U);
    write_u64(bytes, 24U, kEntry);
    write_u64(bytes, 32U, kProgramHeaderOffset);
    write_u16(bytes, 52U, kElfHeaderSize);
    write_u16(bytes, 54U, kProgramHeaderSize);
    write_u16(bytes, 56U, ph_count);

    write_program_header(
        bytes,
        kProgramHeaderOffset,
        1U,
        options.executable ? 0x5U : 0x4U,
        0U,
        kGuestBase,
        kImageSize,
        kImageSize,
        0x1000U);

    std::size_t next_ph =
        kProgramHeaderOffset +
        kProgramHeaderSize;

    if (has_dynamic) {
        std::size_t dynamic_count = 0U;

        if (options.generic_needed) {
            constexpr std::string_view kNeeded =
                "libprobe.sprx";
            bytes[kStringOffset] = std::byte{0};
            for (std::size_t i = 0U;
                 i < kNeeded.size();
                 ++i) {
                bytes[kStringOffset + 1U + i] =
                    static_cast<std::byte>(
                        static_cast<unsigned char>(
                            kNeeded[i]));
            }
            bytes[
                kStringOffset +
                1U +
                kNeeded.size()] =
                std::byte{0};

            write_dynamic_entry(
                bytes,
                dynamic_count++,
                1,
                1U);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                5,
                kGuestBase + kStringOffset);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                10,
                static_cast<std::uint64_t>(
                    kNeeded.size() + 2U));
        }

        if (options.sce_needed) {
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                0x61000045,
                0x1234U);
        }

        if (options.sce_metadata_conflict) {
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                0x61000011,
                1U);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                0x61000011,
                2U);
        }

        if (options.relocation) {
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                6,
                kGuestBase + kSymbolOffset);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                11,
                24U);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                39,
                24U);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                7,
                kGuestBase + kRelaOffset);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                8,
                24U);
            write_dynamic_entry(
                bytes,
                dynamic_count++,
                9,
                24U);

            // One zero-symbol RELA record with relocation type 8.
            write_u64(
                bytes,
                kRelaOffset,
                kGuestBase + 0x440U);
            write_u64(
                bytes,
                kRelaOffset + 8U,
                8U);
            write_i64(
                bytes,
                kRelaOffset + 16U,
                0);
        }

        write_dynamic_entry(
            bytes,
            dynamic_count++,
            0,
            0U);

        const auto dynamic_size =
            static_cast<std::uint64_t>(
                dynamic_count * 16U);
        write_program_header(
            bytes,
            next_ph,
            2U,
            0x4U,
            kDynamicOffset,
            kGuestBase + kDynamicOffset,
            dynamic_size,
            dynamic_size,
            8U);
        next_ph += kProgramHeaderSize;
    }

    if (options.tls) {
        bytes[kTlsOffset] = std::byte{0xaa};
        bytes[kTlsOffset + 1U] = std::byte{0xbb};

        write_program_header(
            bytes,
            next_ph,
            7U,
            0x4U,
            kTlsOffset,
            kGuestBase + kTlsOffset,
            2U,
            8U,
            1U);
    }

    return bytes;
}

astraea::memory::GuestRange diagnostic_stack() {
    const auto stack =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{
                0x70000000U},
            astraea::memory::GuestSize{
                0x20000U});
    REQUIRE(stack.has_value());
    return stack.value();
}

astraea::execution::
    LinuxRetailDiagnosticPreflight
preflight(std::vector<std::byte> bytes) {
    return astraea::execution::
        preflight_linux_retail_diagnostic(
            astraea::execution::
                LinuxRetailDiagnosticPreflightRequest{
                    .artifact_bytes =
                        std::move(bytes),
                    .stack_storage =
                        diagnostic_stack(),
                    .arguments = {"retail-diagnostic"},
                    .environment = {},
                    .auxiliary_vector = {},
                });
}

#if defined(__linux__) && defined(__x86_64__)

astraea::execution::
    GuestWorkerProcessSessionRunResult
run_production_worker(
    std::vector<std::byte> artifact) {
    astraea::execution::GuestWorkerResourcePolicy
        policy{};
    policy.process_cpu_time_seconds = 10U;
    policy.linux_max_open_files = 32U;
    policy.linux_disable_core_dumps = true;
    policy.linux_disable_file_growth = true;

    return astraea::execution::
        run_guest_worker_process_session(
            astraea::execution::
                GuestWorkerProcessSessionConfig{
                    .worker_executable =
                        ASTRAEA_APP_PATH,
                    .worker_arguments = {
                        "--linux-retail-diagnostic-worker",
                    },
                    .run_budget_microseconds =
                        1'000'000U,
                    .timeout_milliseconds =
                        15'000U,
                    .syscall_service = {},
                    .max_syscall_requests = 0U,
                    .resource_policy = policy,
                    .linux_artifact_bytes =
                        std::move(artifact),
                });
}

void require_production_boundary(
    std::vector<std::byte> artifact,
    astraea::execution::GuestWorkerDiagnosticKind
        expected_kind,
    std::optional<std::uint64_t> detail0 =
        std::nullopt,
    std::optional<std::uint64_t> detail1 =
        std::nullopt) {
    const auto result =
        run_production_worker(
            std::move(artifact));

    REQUIRE(result.has_value());
    REQUIRE(result->terminal_diagnostic.has_value());
    REQUIRE_FALSE(result->terminal_fault.has_value());
    REQUIRE(result->syscall_request_count == 0U);
    REQUIRE(result->child_exit_code == 0);
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
    REQUIRE(
        result->terminal_diagnostic->kind ==
        expected_kind);

    if (detail0.has_value()) {
        REQUIRE(
            result->terminal_diagnostic->detail0 ==
            detail0.value());
    }
    if (detail1.has_value()) {
        REQUIRE(
            result->terminal_diagnostic->detail1 ==
            detail1.value());
    }
}

#endif

}  // namespace

TEST_CASE(
    "retail preflight preserves structural loader rejection",
    "[execution][c0][retail][preflight][loader]") {
    auto bytes = make_sce_fixture();
    bytes[0] = std::byte{0};

    const auto result =
        preflight(std::move(bytes));

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                loader_rejected);
    REQUIRE(result.loader_error.has_value());
    REQUIRE(
        result.loader_error->code ==
        astraea::loader::
            GuestImageErrorCode::
                elf_parse_failure);
    REQUIRE_FALSE(result.image.has_value());
}

TEST_CASE(
    "retail preflight rejects non-executable entry before native admission",
    "[execution][c0][retail][preflight][entry]") {
    const auto result =
        preflight(
            make_sce_fixture(
                FixtureOptions{
                    .executable = false,
                }));

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                entry_not_executable);
    REQUIRE(result.image.has_value());
    REQUIRE_FALSE(result.loader_error.has_value());
}

TEST_CASE(
    "retail preflight preserves SCE metadata conflict separately",
    "[execution][c0][retail][preflight][sce-metadata]") {
    const auto result =
        preflight(
            make_sce_fixture(
                FixtureOptions{
                    .sce_metadata_conflict = true,
                }));

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                sce_dynamic_metadata_rejected);
    REQUIRE(
        result.sce_dynamic_metadata_error.has_value());
    REQUIRE(
        result.sce_dynamic_metadata_error->code ==
        astraea::loader::
            SceDynamicMetadataErrorCode::
                conflicting_singleton_tag);
    REQUIRE(result.image.has_value());
}

TEST_CASE(
    "retail preflight stops on unresolved generic and SCE dependencies",
    "[execution][c0][retail][preflight][dependency]") {
    SECTION("generic needed module") {
        const auto result =
            preflight(
                make_sce_fixture(
                    FixtureOptions{
                        .generic_needed = true,
                    }));

        REQUIRE(
            result.boundary ==
            astraea::execution::
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    unsupported_dynamic_dependencies);
        REQUIRE(
            result.dynamic_dependency_count == 1U);
    }

    SECTION("SCE needed module") {
        const auto result =
            preflight(
                make_sce_fixture(
                    FixtureOptions{
                        .sce_needed = true,
                    }));

        REQUIRE(
            result.boundary ==
            astraea::execution::
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    unsupported_dynamic_dependencies);
        REQUIRE(
            result.dynamic_dependency_count == 1U);
    }
}

TEST_CASE(
    "retail preflight stops on unapplied relocations",
    "[execution][c0][retail][preflight][relocation]") {
    const auto result =
        preflight(
            make_sce_fixture(
                FixtureOptions{
                    .relocation = true,
                }));

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                unsupported_relocations);
    REQUIRE(result.relocation_count == 1U);
    REQUIRE(result.image.has_value());
}

TEST_CASE(
    "retail preflight stops on unsupported TLS setup",
    "[execution][c0][retail][preflight][tls]") {
    const auto result =
        preflight(
            make_sce_fixture(
                FixtureOptions{
                    .tls = true,
                }));

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                unsupported_tls);
    REQUIRE(result.image.has_value());
    REQUIRE(result.image->tls.has_value());
}

TEST_CASE(
    "retail preflight boundary priority is deterministic",
    "[execution][c0][retail][preflight][priority]") {
    const auto result =
        preflight(
            make_sce_fixture(
                FixtureOptions{
                    .generic_needed = true,
                    .relocation = true,
                    .tls = true,
                }));

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                unsupported_dynamic_dependencies);
    REQUIRE(result.dynamic_dependency_count == 1U);
}

TEST_CASE(
    "retail preflight admits only a fully known pre-entry image",
    "[execution][c0][retail][preflight][ready]") {
    const auto result =
        preflight(make_sce_fixture());

    REQUIRE(
        result.boundary ==
        astraea::execution::
            LinuxRetailDiagnosticPreflightBoundaryKind::
                ready_for_native_entry);
    REQUIRE(result.image.has_value());
    REQUIRE_FALSE(result.loader_error.has_value());
    REQUIRE_FALSE(
        result.sce_dynamic_metadata_error.has_value());
    REQUIRE(result.dynamic_dependency_count == 0U);
    REQUIRE(result.relocation_count == 0U);
    REQUIRE(
        result.image->elf.header.entry ==
        kEntry);
}


TEST_CASE(
    "production retail worker preserves dependency relocation TLS and initial-ABI boundaries",
    "[execution][c0][retail][production][boundaries]") {
#if defined(__linux__) && defined(__x86_64__)
    SECTION("dynamic dependency") {
        require_production_boundary(
            make_sce_fixture(
                FixtureOptions{
                    .generic_needed = true,
                }),
            astraea::execution::
                GuestWorkerDiagnosticKind::
                    unsupported_dynamic_dependencies,
            1U);
    }

    SECTION("relocation") {
        require_production_boundary(
            make_sce_fixture(
                FixtureOptions{
                    .relocation = true,
                }),
            astraea::execution::
                GuestWorkerDiagnosticKind::
                    unsupported_relocations,
            1U);
    }

    SECTION("TLS") {
        require_production_boundary(
            make_sce_fixture(
                FixtureOptions{
                    .tls = true,
                }),
            astraea::execution::
                GuestWorkerDiagnosticKind::
                    unsupported_tls,
            8U,
            1U);
    }

    SECTION("otherwise ready image still stops before native entry") {
        require_production_boundary(
            make_sce_fixture(),
            astraea::execution::
                GuestWorkerDiagnosticKind::
                    unsupported_initial_process_abi);
    }
#else
    SUCCEED();
#endif
}
