#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/guest_worker_artifact.hpp>
#include <astraea/execution/guest_worker_fault_projection.hpp>
#include <astraea/execution/guest_worker_syscall_context.hpp>
#include <astraea/execution/guest_worker_wire.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/linux_execution.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/windows_execution.hpp>
#include <astraea/execution/windows_memory.hpp>
#include <astraea/loader/guest_image.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#if defined(__linux__)
#include <csignal>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

namespace {

enum class ProbeMode {
    normal,
    crash_before_ready,
    hang_after_run,
    bad_frame_after_hello,
    syscall_roundtrip,
    native_syscall_roundtrip,
    native_access_fault,
    native_illegal_instruction_fault,
    fault_then_syscall,
    burn_cpu,
    artifact_probe,
    artifact_limit_probe,
    no_artifact_fd_probe,
};

[[nodiscard]] bool configure_binary_stdio() noexcept {
#if defined(_WIN32)
    return
        _setmode(_fileno(stdin), _O_BINARY) != -1 &&
        _setmode(_fileno(stdout), _O_BINARY) != -1;
#else
    return true;
#endif
}

[[nodiscard]] bool bind_lifetime_to_controller() noexcept {
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
    return true;
#endif
}

[[nodiscard]] bool read_exact(
    std::span<std::byte> bytes) {
    if (bytes.empty()) {
        return true;
    }

    std::cin.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    return
        std::cin.good() ||
        (std::cin.eof() &&
         static_cast<std::size_t>(
             std::cin.gcount()) ==
             bytes.size());
}

using ReadResult =
    astraea::core::Result<
        astraea::execution::GuestWorkerWireMessage,
        int>;

[[nodiscard]] ReadResult read_message() {
    using namespace astraea::execution;

    std::array<std::byte, kGuestWorkerWireHeaderSize>
        header_bytes{};
    if (!read_exact(header_bytes)) {
        return ReadResult::failure(1);
    }

    const auto header =
        decode_guest_worker_wire_header(
            header_bytes);
    if (!header.has_value()) {
        return ReadResult::failure(2);
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
            return ReadResult::failure(3);
        }
    }

    const auto decoded =
        decode_guest_worker_wire_message(frame);
    if (!decoded.has_value()) {
        return ReadResult::failure(4);
    }

    return ReadResult::success(
        decoded.value());
}

[[nodiscard]] bool write_message(
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

[[nodiscard]] ProbeMode mode_from_args(
    int argc,
    char** argv) noexcept {
    for (int index = 1; index < argc; ++index) {
        const auto argument =
            std::string_view{argv[index]};
        if (argument ==
            "--crash-before-ready") {
            return ProbeMode::crash_before_ready;
        }
        if (argument ==
            "--hang-after-run") {
            return ProbeMode::hang_after_run;
        }
        if (argument ==
            "--bad-frame-after-hello") {
            return ProbeMode::bad_frame_after_hello;
        }
        if (argument ==
            "--syscall-roundtrip") {
            return ProbeMode::syscall_roundtrip;
        }
        if (argument ==
            "--native-syscall-roundtrip") {
            return ProbeMode::native_syscall_roundtrip;
        }
        if (argument ==
            "--native-access-fault") {
            return ProbeMode::native_access_fault;
        }
        if (argument ==
            "--native-illegal-instruction-fault") {
            return ProbeMode::native_illegal_instruction_fault;
        }
        if (argument ==
            "--fault-then-syscall") {
            return ProbeMode::fault_then_syscall;
        }
        if (argument ==
            "--burn-cpu") {
            return ProbeMode::burn_cpu;
        }
        if (argument ==
            "--artifact-probe") {
            return ProbeMode::artifact_probe;
        }
        if (argument ==
            "--artifact-limit-probe") {
            return ProbeMode::artifact_limit_probe;
        }
        if (argument ==
            "--no-artifact-fd") {
            return ProbeMode::no_artifact_fd_probe;
        }
    }
    return ProbeMode::normal;
}

#if defined(__linux__)

constexpr std::array<std::byte, 12> kExpectedArtifactBytes{
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

[[nodiscard]] bool artifact_fd_is_closed() noexcept {
    errno = 0;
    const auto flags =
        ::fcntl(3, F_GETFD);
    return flags == -1 && errno == EBADF;
}

[[nodiscard]] bool validate_and_close_artifact_fd() {
    constexpr int kRequiredSeals =
        F_SEAL_WRITE |
        F_SEAL_GROW |
        F_SEAL_SHRINK |
        F_SEAL_SEAL;

    const auto seals =
        ::fcntl(
            astraea::execution::
                kLinuxWorkerArtifactFd,
            F_GET_SEALS);
    if (seals < 0 ||
        (seals & kRequiredSeals) !=
            kRequiredSeals) {
        return false;
    }

    const std::byte replacement{0x99};
    errno = 0;
    if (::pwrite(
            astraea::execution::
                kLinuxWorkerArtifactFd,
            &replacement,
            1U,
            0) != -1 ||
        errno != EPERM) {
        return false;
    }

    errno = 0;
    if (::ftruncate(
            astraea::execution::
                kLinuxWorkerArtifactFd,
            static_cast<off_t>(
                kExpectedArtifactBytes.size() + 1U)) != -1 ||
        errno != EPERM) {
        return false;
    }

    errno = 0;
    if (::ftruncate(
            astraea::execution::
                kLinuxWorkerArtifactFd,
            static_cast<off_t>(
                kExpectedArtifactBytes.size() - 1U)) != -1 ||
        errno != EPERM) {
        return false;
    }

    const auto artifact =
        astraea::execution::
            read_linux_sealed_worker_artifact(
                1024U);
    if (!artifact.has_value() ||
        artifact->size() !=
            kExpectedArtifactBytes.size() ||
        !std::equal(
            artifact->begin(),
            artifact->end(),
            kExpectedArtifactBytes.begin(),
            kExpectedArtifactBytes.end())) {
        return false;
    }

    return artifact_fd_is_closed();
}

#endif

#if defined(_WIN32)
[[nodiscard]] std::optional<std::uintptr_t>
signal_handle_from_args(
    int argc,
    char** argv) noexcept {
    constexpr std::string_view kPrefix =
        "--signal-handle=";

    for (int index = 1; index < argc; ++index) {
        const auto argument =
            std::string_view{argv[index]};
        if (!argument.starts_with(kPrefix)) {
            continue;
        }

        const auto value_text =
            argument.substr(kPrefix.size());
        std::uintptr_t value = 0U;
        const auto parsed =
            std::from_chars(
                value_text.data(),
                value_text.data() +
                    value_text.size(),
                value);
        if (parsed.ec != std::errc{} ||
            parsed.ptr !=
                value_text.data() +
                    value_text.size()) {
            return std::nullopt;
        }
        return value;
    }

    return std::nullopt;
}
#endif


#if (defined(__linux__) && defined(__x86_64__)) || \
    (defined(_WIN32) && defined(_M_X64))

std::optional<astraea::memory::GuestRange>
make_range(
    std::uint64_t base,
    std::uint64_t size) {
    auto range =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{base},
            astraea::memory::GuestSize{size});
    if (!range.has_value()) {
        return std::nullopt;
    }
    return range.value();
}

std::optional<astraea::memory::GuestPermissions>
make_permissions(std::uint8_t bits) {
    auto permissions =
        astraea::memory::GuestPermissions::
            checked_from_bits(bits);
    if (!permissions.has_value()) {
        return std::nullopt;
    }
    return permissions.value();
}

std::optional<std::uint64_t>
native_mapping_unit() noexcept {
#if defined(__linux__) && defined(__x86_64__)
    const auto page = ::sysconf(_SC_PAGESIZE);
    if (page <= 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(page);
#elif defined(_WIN32) && defined(_M_X64)
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    if (info.dwAllocationGranularity == 0U) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(
        info.dwAllocationGranularity);
#else
    return std::nullopt;
#endif
}

std::optional<std::uint64_t>
find_native_free_block(std::size_t size) noexcept {
#if defined(__linux__) && defined(__x86_64__)
    void* const mapped =
        ::mmap(
            nullptr,
            size,
            PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
    if (mapped == MAP_FAILED) {
        return std::nullopt;
    }

    const auto address =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(mapped));
    if (::munmap(mapped, size) != 0) {
        return std::nullopt;
    }
    return address;
#elif defined(_WIN32) && defined(_M_X64)
    void* const reserved =
        ::VirtualAlloc(
            nullptr,
            size,
            MEM_RESERVE,
            PAGE_NOACCESS);
    if (reserved == nullptr) {
        return std::nullopt;
    }

    const auto address =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                reserved));
    if (::VirtualFree(
            reserved,
            0,
            MEM_RELEASE) == 0) {
        return std::nullopt;
    }
    return address;
#else
    (void)size;
    return std::nullopt;
#endif
}

void append_u32(
    std::vector<std::byte>& code,
    std::uint32_t value) {
    for (unsigned shift = 0U;
         shift < 32U;
         shift += 8U) {
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_u64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    for (unsigned shift = 0U;
         shift < 64U;
         shift += 8U) {
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

bool append_mov_imm64(
    std::vector<std::byte>& code,
    unsigned register_index,
    std::uint64_t value) {
    if (register_index >= 16U ||
        register_index == 4U) {
        return false;
    }

    const bool extended =
        register_index >= 8U;
    const auto low_index =
        extended
            ? register_index - 8U
            : register_index;

    code.push_back(
        static_cast<std::byte>(
            extended ? 0x49U : 0x48U));
    code.push_back(
        static_cast<std::byte>(
            static_cast<unsigned char>(
                0xb8U + low_index)));
    append_u64(code, value);
    return true;
}

bool append_call_gate(
    std::vector<std::byte>& code,
    std::uint64_t code_base,
    std::uint64_t gate_base) {
    if (code_base >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) ||
        gate_base >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    const auto code_size =
        static_cast<std::uint64_t>(
            code.size());
    if (code_size >
            std::numeric_limits<std::uint64_t>::max() -
                5U ||
        code_base >
            std::numeric_limits<std::uint64_t>::max() -
                code_size -
                5U) {
        return false;
    }

    const auto next_rip =
        code_base + code_size + 5U;
    if (next_rip >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return false;
    }

    const auto displacement =
        static_cast<std::int64_t>(gate_base) -
        static_cast<std::int64_t>(next_rip);
    if (displacement <
            std::numeric_limits<std::int32_t>::min() ||
        displacement >
            std::numeric_limits<std::int32_t>::max()) {
        return false;
    }

    code.push_back(std::byte{0xe8});
    append_u32(
        code,
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(
                displacement)));
    return true;
}

std::optional<astraea::loader::GuestImage>
make_native_guest_image(
    std::uint64_t code_base,
    std::uint64_t stack_base,
    std::uint64_t stack_size,
    std::vector<std::byte> code) {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            astraea::memory::GuestPermission::read);
    constexpr auto kExecute =
        static_cast<std::uint8_t>(
            astraea::memory::GuestPermission::execute);

    const auto code_range =
        make_range(
            code_base,
            static_cast<std::uint64_t>(
                code.size()));
    const auto stack_range =
        make_range(
            stack_base,
            stack_size);
    if (!code_range.has_value() ||
        !stack_range.has_value()) {
        return std::nullopt;
    }

    const auto permissions =
        make_permissions(kRead | kExecute);
    if (!permissions.has_value()) {
        return std::nullopt;
    }

    const auto stack_pointer =
        astraea::memory::GuestAddress{
            stack_base + stack_size - 8U};
    const auto used_range =
        make_range(
            stack_pointer.value(),
            0U);
    if (!used_range.has_value()) {
        return std::nullopt;
    }

    astraea::loader::ElfHeader header{};
    header.entry = code_base;

    const auto code_size =
        static_cast<std::uint64_t>(
            code.size());

    return astraea::loader::GuestImage{
        .image_bytes = std::move(code),
        .elf =
            astraea::loader::ElfImage{
                .header = header,
                .program_headers = {},
            },
        .mappings =
            std::vector<
                astraea::memory::MappingIntent>{
                astraea::memory::MappingIntent{
                    .range = code_range.value(),
                    .permissions =
                        permissions.value(),
                    .backing =
                        astraea::memory::MappingBacking{
                            .kind =
                                astraea::memory::
                                    MappingBackingKind::
                                        file,
                            .file_offset = 0U,
                            .byte_count =
                                astraea::memory::GuestSize{
                                    code_size},
                        },
                    .source_index = 0U,
                },
            },
        .dynamic_table = std::nullopt,
        .dynamic_strings = std::nullopt,
        .dynamic_symbols = std::nullopt,
        .general_relocations =
            astraea::loader::
                GeneralDynamicRelocationMetadata{
                    .rel = std::nullopt,
                    .rela = std::nullopt,
                },
        .plt_relocations = std::nullopt,
        .tls = std::nullopt,
        .initial_stack =
            astraea::loader::InitialStackImage{
                .storage = stack_range.value(),
                .used_range = used_range.value(),
                .rsp = stack_pointer,
                .bytes = {},
            },
    };
}

std::optional<astraea::execution::SyntheticGateRegion>
make_native_gate_region(
    const astraea::loader::GuestImage& image,
    std::uint64_t gate_base) {
    auto registry =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::
                    HleFunctionDescriptor>{
                astraea::execution::
                    HleFunctionDescriptor{
                        .id =
                            astraea::execution::
                                HleFunctionId{1U},
                        .canonical_name =
                            "astraea.test.syscall-return",
                        .argument_count = 0U,
                    },
            });
    if (!registry.has_value()) {
        return std::nullopt;
    }

    auto gate =
        astraea::execution::
            build_synthetic_gate_region(
                registry.value(),
                astraea::memory::GuestAddress{
                    gate_base},
                1U,
                std::vector<
                    astraea::execution::GateBinding>{
                    astraea::execution::GateBinding{
                        .slot = 0U,
                        .function_id =
                            astraea::execution::
                                HleFunctionId{1U},
                    },
                },
                image);
    if (!gate.has_value()) {
        return std::nullopt;
    }
    return std::move(gate).value();
}

#endif

struct NativeFaultProbeResult {
    astraea::execution::GuestWorkerFault fault;
    astraea::execution::GuestWorkerStop stop;
};

enum class NativeFaultProbeKind {
    access_violation,
    illegal_instruction,
};

std::optional<NativeFaultProbeResult>
run_owned_native_fault(
    NativeFaultProbeKind kind,
    astraea::execution::GuestWorkerId worker_id,
    astraea::execution::GuestThreadId thread_id) {
#if !((defined(__linux__) && defined(__x86_64__)) || \
      (defined(_WIN32) && defined(_M_X64)))
    (void)kind;
    (void)worker_id;
    (void)thread_id;
    return std::nullopt;
#else
    using namespace astraea::execution;

    const auto unit =
        native_mapping_unit();
    if (!unit.has_value() ||
        unit.value() >
            std::numeric_limits<std::size_t>::max() /
                4U) {
        return std::nullopt;
    }

    const auto block =
        find_native_free_block(
            static_cast<std::size_t>(
                unit.value() * 4U));
    if (!block.has_value()) {
        return std::nullopt;
    }

    const auto code_base = block.value();
    if (unit.value() >
            std::numeric_limits<std::uint64_t>::max() /
                3U ||
        code_base >
            std::numeric_limits<std::uint64_t>::max() -
                unit.value() * 3U) {
        return std::nullopt;
    }

    const auto stack_base =
        code_base + unit.value();
    const auto gate_base =
        code_base + unit.value() * 3U;

    std::vector<std::byte> code;
    if (kind ==
        NativeFaultProbeKind::access_violation) {
        // xor rax, rax; mov rax, [rax]
        // Address zero is intentionally outside the owned guest mappings.
        code = {
            std::byte{0x48},
            std::byte{0x31},
            std::byte{0xc0},
            std::byte{0x48},
            std::byte{0x8b},
            std::byte{0x00},
        };
    } else {
        // An unregistered UD2 must remain a generic illegal-instruction fault.
        code = {
            std::byte{0x0f},
            std::byte{0x0b},
        };
    }

    if (!append_call_gate(
            code,
            code_base,
            gate_base)) {
        return std::nullopt;
    }

    auto image =
        make_native_guest_image(
            code_base,
            stack_base,
            unit.value() * 2U,
            std::move(code));
    if (!image.has_value()) {
        return std::nullopt;
    }

    auto gate =
        make_native_gate_region(
            image.value(),
            gate_base);
    if (!gate.has_value()) {
        return std::nullopt;
    }

#if defined(__linux__) && defined(__x86_64__)
    if (!linux_native_execution_backend_available()) {
        return std::nullopt;
    }
    auto prepared =
        prepare_linux_guest_memory(
            image.value());
    if (!prepared.has_value()) {
        return std::nullopt;
    }
    const auto stopped =
        enter_linux_guest(
            image.value(),
            prepared.value(),
            gate.value(),
            make_synthetic_initial_context(
                image.value()));
#elif defined(_WIN32) && defined(_M_X64)
    if (!windows_native_execution_backend_available()) {
        return std::nullopt;
    }
    auto prepared =
        prepare_windows_guest_memory(
            image.value());
    if (!prepared.has_value()) {
        return std::nullopt;
    }
    const auto stopped =
        enter_windows_guest(
            image.value(),
            prepared.value(),
            gate.value(),
            make_synthetic_initial_context(
                image.value()));
#endif

    if (!stopped.has_value() ||
        stopped->reason !=
            ExecutionStopReason::guest_fault ||
        !stopped->has_fault) {
        return std::nullopt;
    }

    const auto expected_kind =
        kind ==
                NativeFaultProbeKind::access_violation
            ? GuestFaultKind::access_violation
            : GuestFaultKind::illegal_instruction;
    if (stopped->fault.kind != expected_kind) {
        return std::nullopt;
    }

    if (kind ==
            NativeFaultProbeKind::access_violation &&
        (!stopped->fault.has_fault_address ||
         stopped->fault.fault_address != 0U)) {
        return std::nullopt;
    }

    const auto projected =
        project_guest_worker_fault(
            stopped.value(),
            worker_id,
            thread_id);
    if (!projected.has_value()) {
        return std::nullopt;
    }

    return NativeFaultProbeResult{
        .fault = projected.value(),
        .stop =
            GuestWorkerStop{
                .worker_id = worker_id,
                .thread_id = thread_id,
                .reason =
                    GuestWorkerStopReason::
                        guest_fault,
                .guest_rip =
                    projected->guest_rip,
            },
    };
#endif
}

std::optional<astraea::execution::GuestWorkerStop>
run_owned_native_syscall_roundtrip(
    astraea::execution::GuestWorkerId worker_id,
    astraea::execution::GuestThreadId thread_id) {
#if !((defined(__linux__) && defined(__x86_64__)) || \
      (defined(_WIN32) && defined(_M_X64)))
    (void)worker_id;
    (void)thread_id;
    return std::nullopt;
#else
    using namespace astraea::execution;

    const auto unit =
        native_mapping_unit();
    if (!unit.has_value() ||
        unit.value() >
            std::numeric_limits<std::size_t>::max() /
                4U) {
        return std::nullopt;
    }

    const auto block =
        find_native_free_block(
            static_cast<std::size_t>(
                unit.value() * 4U));
    if (!block.has_value()) {
        return std::nullopt;
    }

    const auto code_base = block.value();
    if (unit.value() >
            std::numeric_limits<std::uint64_t>::max() /
                3U ||
        code_base >
            std::numeric_limits<std::uint64_t>::max() -
                unit.value() * 3U) {
        return std::nullopt;
    }

    const auto stack_base =
        code_base + unit.value();
    const auto gate_base =
        code_base + unit.value() * 3U;

    constexpr std::uint64_t kSyscallNumber =
        0x5152535455565758ULL;
    constexpr std::array<std::uint64_t, 6> kArguments{
        0x1111111111111111ULL,
        0x2222222222222222ULL,
        0x3333333333333333ULL,
        0x4444444444444444ULL,
        0x5555555555555555ULL,
        0x6666666666666666ULL,
    };

    std::vector<std::byte> code;
    code.reserve(96U);

    if (!append_mov_imm64(code, 0U, kSyscallNumber) ||
        !append_mov_imm64(code, 7U, kArguments[0]) ||
        !append_mov_imm64(code, 6U, kArguments[1]) ||
        !append_mov_imm64(code, 2U, kArguments[2]) ||
        !append_mov_imm64(code, 10U, kArguments[3]) ||
        !append_mov_imm64(code, 8U, kArguments[4]) ||
        !append_mov_imm64(code, 9U, kArguments[5])) {
        return std::nullopt;
    }

    const auto syscall_offset = code.size();
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x05});

    // The fixture must consume the synthetic return value after resume.
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0x83});
    code.push_back(std::byte{0xc0});
    code.push_back(std::byte{0x01});

    if (!append_call_gate(
            code,
            code_base,
            gate_base)) {
        return std::nullopt;
    }

    // A returned gate call is always an error in this owned fixture.
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    if (syscall_offset >
        std::numeric_limits<std::uint64_t>::max() -
            code_base) {
        return std::nullopt;
    }
    const auto syscall_rip =
        astraea::memory::GuestAddress{
            code_base +
            static_cast<std::uint64_t>(
                syscall_offset)};

    const auto planned_trap =
        plan_registered_syscall_trap(
            astraea::memory::GuestAddress{
                code_base},
            code,
            syscall_rip);
    if (!planned_trap.has_value()) {
        return std::nullopt;
    }

    if (!apply_registered_syscall_trap(
             std::span<std::byte>{code},
             planned_trap.value())
             .has_value()) {
        return std::nullopt;
    }

    auto image =
        make_native_guest_image(
            code_base,
            stack_base,
            unit.value() * 2U,
            std::move(code));
    if (!image.has_value()) {
        return std::nullopt;
    }

    auto gate =
        make_native_gate_region(
            image.value(),
            gate_base);
    if (!gate.has_value()) {
        return std::nullopt;
    }

    const std::array<
        RegisteredSyscallTrapSite,
        1> traps{
            planned_trap.value(),
        };

#if defined(__linux__) && defined(__x86_64__)
    if (!linux_native_execution_backend_available()) {
        return std::nullopt;
    }
    auto prepared =
        prepare_linux_guest_memory(
            image.value());
    if (!prepared.has_value()) {
        return std::nullopt;
    }
    const auto enter =
        [&](GuestCpuContext context) {
            return enter_linux_guest(
                image.value(),
                prepared.value(),
                gate.value(),
                context,
                traps);
        };
#elif defined(_WIN32) && defined(_M_X64)
    if (!windows_native_execution_backend_available()) {
        return std::nullopt;
    }
    auto prepared =
        prepare_windows_guest_memory(
            image.value());
    if (!prepared.has_value()) {
        return std::nullopt;
    }
    const auto enter =
        [&](GuestCpuContext context) {
            return enter_windows_guest(
                image.value(),
                prepared.value(),
                gate.value(),
                context,
                traps);
        };
#endif

    const auto first_stop =
        enter(
            make_synthetic_initial_context(
                image.value()));
    if (!first_stop.has_value() ||
        first_stop->reason !=
            ExecutionStopReason::
                registered_syscall_trap) {
        return std::nullopt;
    }

    const auto request =
        project_registered_syscall_request(
            first_stop.value(),
            GuestRequestId{.value = 41U},
            worker_id,
            thread_id);
    if (!request.has_value()) {
        return std::nullopt;
    }

    if (!write_message(
            GuestWorkerWireMessage{
                request.value()})) {
        return std::nullopt;
    }

    const auto result_message =
        read_message();
    if (!result_message.has_value()) {
        return std::nullopt;
    }

    const auto* syscall_result =
        std::get_if<GuestWorkerSyscallResult>(
            &result_message.value());
    if (syscall_result == nullptr) {
        return GuestWorkerStop{
            .worker_id = worker_id,
            .thread_id = thread_id,
            .reason =
                GuestWorkerStopReason::
                    protocol_failure,
            .guest_rip =
                request->guest_rip,
        };
    }

    const auto resumed_context =
        apply_registered_syscall_result_to_context(
            first_stop.value(),
            planned_trap.value(),
            request.value(),
            *syscall_result);
    if (!resumed_context.has_value()) {
        return GuestWorkerStop{
            .worker_id = worker_id,
            .thread_id = thread_id,
            .reason =
                GuestWorkerStopReason::
                    protocol_failure,
            .guest_rip =
                request->guest_rip,
        };
    }

    const auto second_stop =
        enter(resumed_context.value());
    if (!second_stop.has_value() ||
        second_stop->reason !=
            ExecutionStopReason::host_gate ||
        !second_stop->has_gate_slot ||
        second_stop->gate_slot != 0U) {
        return std::nullopt;
    }

    const auto expected_rax =
        std::bit_cast<std::uint64_t>(
            syscall_result->return_value) +
        1U;
    if (second_stop->context.rax !=
        expected_rax) {
        return std::nullopt;
    }

    return GuestWorkerStop{
        .worker_id = worker_id,
        .thread_id = thread_id,
        .reason =
            GuestWorkerStopReason::
                normal_guest_return,
        .guest_rip =
            astraea::memory::GuestAddress{
                second_stop->context.rip},
    };
#endif
}

}  // namespace

int main(int argc, char** argv) {
    using namespace astraea::execution;

    if (!configure_binary_stdio()) {
        return 10;
    }
    if (!bind_lifetime_to_controller()) {
        return 11;
    }

#if defined(_WIN32)
    const auto inherited_handle_probe =
        signal_handle_from_args(argc, argv);
    if (inherited_handle_probe.has_value()) {
        (void)::SetEvent(
            reinterpret_cast<HANDLE>(
                inherited_handle_probe.value()));
    }
#endif

    const auto mode =
        mode_from_args(argc, argv);

#if defined(__linux__)
    if (mode == ProbeMode::artifact_probe) {
        if (!validate_and_close_artifact_fd()) {
            return 38;
        }
    } else if (
        mode ==
            ProbeMode::artifact_limit_probe) {
        const auto artifact =
            astraea::execution::
                read_linux_sealed_worker_artifact(
                    4U);
        if (artifact.has_value() ||
            artifact.error().code !=
                astraea::execution::
                    LinuxWorkerArtifactErrorCode::
                        artifact_too_large ||
            !artifact_fd_is_closed()) {
            return 42;
        }
    } else if (
        mode ==
        ProbeMode::no_artifact_fd_probe) {
        if (!artifact_fd_is_closed()) {
            return 39;
        }
    }
#else
    if (mode == ProbeMode::artifact_probe ||
        mode ==
            ProbeMode::no_artifact_fd_probe) {
        return 40;
    }
#endif

    const auto hello_message =
        read_message();
    if (!hello_message.has_value()) {
        return 12;
    }
    const auto* hello =
        std::get_if<GuestWorkerHello>(
            &hello_message.value());
    if (hello == nullptr ||
        hello->protocol_version !=
            kGuestWorkerProtocolVersion) {
        return 13;
    }

    if (mode ==
        ProbeMode::crash_before_ready) {
        return 23;
    }

    if (mode ==
        ProbeMode::bad_frame_after_hello) {
        const std::array<std::byte, kGuestWorkerWireHeaderSize>
            invalid_header{};
        std::cout.write(
            reinterpret_cast<const char*>(
                invalid_header.data()),
            static_cast<std::streamsize>(
                invalid_header.size()));
        std::cout.flush();
        return 24;
    }

    constexpr GuestWorkerId kWorkerId{
        .value = 1U,
    };
    constexpr GuestThreadId kThreadId{
        .value = 1U,
    };

    if (!write_message(
            GuestWorkerWireMessage{
                GuestWorkerReady{
                    .protocol_version =
                        kGuestWorkerProtocolVersion,
                    .worker_id = kWorkerId,
                }})) {
        return 14;
    }

    const auto run_message =
        read_message();
    if (!run_message.has_value()) {
        return 15;
    }
    const auto* run =
        std::get_if<GuestWorkerRunRequest>(
            &run_message.value());
    if (run == nullptr ||
        !validate_guest_worker_run_request(
             *run)
             .has_value()) {
        return 16;
    }

#if defined(__linux__)
    if ((mode == ProbeMode::artifact_probe ||
         mode ==
             ProbeMode::artifact_limit_probe ||
         mode ==
             ProbeMode::no_artifact_fd_probe) &&
        !artifact_fd_is_closed()) {
        return 41;
    }
#endif

    if (mode == ProbeMode::hang_after_run) {
        std::this_thread::sleep_for(
            std::chrono::hours{1});
        return 25;
    }

    if (mode == ProbeMode::burn_cpu) {
        std::atomic<std::uint64_t> counter{0U};
        for (;;) {
            (void)counter.fetch_add(
                1U,
                std::memory_order_relaxed);
        }
    }

    if (mode == ProbeMode::native_access_fault ||
        mode ==
            ProbeMode::native_illegal_instruction_fault) {
        const auto native_fault =
            run_owned_native_fault(
                mode ==
                        ProbeMode::native_access_fault
                    ? NativeFaultProbeKind::
                          access_violation
                    : NativeFaultProbeKind::
                          illegal_instruction,
                kWorkerId,
                kThreadId);
        if (!native_fault.has_value()) {
            return 31;
        }

        if (!write_message(
                GuestWorkerWireMessage{
                    native_fault->fault}) ||
            !write_message(
                GuestWorkerWireMessage{
                    native_fault->stop})) {
            return 32;
        }

        const auto terminate_message =
            read_message();
        if (!terminate_message.has_value()) {
            return 33;
        }
        const auto* terminate =
            std::get_if<GuestWorkerTerminate>(
                &terminate_message.value());
        if (terminate == nullptr ||
            terminate->reason !=
                GuestWorkerTerminationReason::
                    fatal_guest_fault) {
            return 34;
        }
        return 0;
    }

    if (mode == ProbeMode::fault_then_syscall) {
        constexpr astraea::memory::GuestAddress
            kFaultRip{0x400100U};

        if (!write_message(
                GuestWorkerWireMessage{
                    GuestWorkerFault{
                        .worker_id = kWorkerId,
                        .thread_id = kThreadId,
                        .kind =
                            GuestWorkerFaultKind::
                                illegal_instruction,
                        .guest_rip = kFaultRip,
                        .fault_address =
                            astraea::memory::
                                GuestAddress{0U},
                    }})) {
            return 35;
        }

        if (!write_message(
                GuestWorkerWireMessage{
                    GuestWorkerSyscallRequest{
                        .request_id =
                            GuestRequestId{
                                .value = 77U},
                        .worker_id = kWorkerId,
                        .thread_id = kThreadId,
                        .guest_syscall_number =
                            0x7172737475767778ULL,
                        .arguments = {},
                        .guest_rip = kFaultRip,
                    }})) {
            return 36;
        }

        // The controller must reject the resumable event after a terminal
        // fault and tear this worker down. Remaining alive here makes any
        // accidental acceptance observable as the session deadline.
        std::this_thread::sleep_for(
            std::chrono::hours{1});
        return 37;
    }

    if (mode ==
        ProbeMode::native_syscall_roundtrip) {
        const auto native_stop =
            run_owned_native_syscall_roundtrip(
                kWorkerId,
                kThreadId);
        if (!native_stop.has_value()) {
            return 28;
        }

        if (!write_message(
                GuestWorkerWireMessage{
                    native_stop.value()})) {
            return 29;
        }

        const auto terminate_message =
            read_message();
        if (!terminate_message.has_value() ||
            std::get_if<GuestWorkerTerminate>(
                &terminate_message.value()) ==
                nullptr) {
            return 30;
        }
        return 0;
    }

    GuestWorkerStopReason stop_reason =
        GuestWorkerStopReason::normal_guest_return;
    astraea::memory::GuestAddress stop_rip{0U};

    if (mode == ProbeMode::syscall_roundtrip) {
        const GuestWorkerSyscallRequest syscall_request{
            .request_id =
                GuestRequestId{.value = 41U},
            .worker_id = kWorkerId,
            .thread_id = kThreadId,
            .guest_syscall_number =
                0x5152535455565758ULL,
            .arguments = {
                0x1111111111111111ULL,
                0x2222222222222222ULL,
                0x3333333333333333ULL,
                0x4444444444444444ULL,
                0x5555555555555555ULL,
                0x6666666666666666ULL,
            },
            .guest_rip =
                astraea::memory::GuestAddress{
                    0x400100U},
        };

        if (!write_message(
                GuestWorkerWireMessage{
                    syscall_request})) {
            return 26;
        }

        const auto result_message =
            read_message();
        if (!result_message.has_value()) {
            return 27;
        }

        const auto* syscall_result =
            std::get_if<GuestWorkerSyscallResult>(
                &result_message.value());
        if (syscall_result == nullptr ||
            !validate_guest_worker_syscall_result(
                 syscall_request,
                 *syscall_result)
                 .has_value()) {
            stop_reason =
                GuestWorkerStopReason::
                    protocol_failure;
        }

        stop_rip = syscall_request.guest_rip;
    }

    if (!write_message(
            GuestWorkerWireMessage{
                GuestWorkerStop{
                    .worker_id = kWorkerId,
                    .thread_id = kThreadId,
                    .reason = stop_reason,
                    .guest_rip = stop_rip,
                }})) {
        return 17;
    }

    const auto terminate_message =
        read_message();
    if (!terminate_message.has_value() ||
        std::get_if<GuestWorkerTerminate>(
            &terminate_message.value()) ==
            nullptr) {
        return 18;
    }

    return 0;
}
