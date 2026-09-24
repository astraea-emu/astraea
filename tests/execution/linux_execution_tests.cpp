#include <astraea/execution/linux_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <linux/audit.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

using astraea::execution::ExecutionStopReason;
using astraea::execution::GuestCpuContext;
using astraea::execution::GuestFaultKind;
using astraea::execution::HleFunctionDescriptor;
using astraea::execution::HleFunctionId;
using astraea::execution::HleRegistry;
using astraea::execution::SyntheticGateRegion;
using astraea::loader::ElfHeader;
using astraea::loader::ElfImage;
using astraea::loader::GeneralDynamicRelocationMetadata;
using astraea::loader::GuestImage;
using astraea::loader::InitialStackImage;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermission;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

GuestRange range(std::uint64_t base, std::uint64_t size) {
    auto result =
        GuestRange::create(
            GuestAddress{base},
            GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

GuestPermissions permissions(std::uint8_t bits) {
    auto result =
        GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
}

std::uint64_t page_size() {
    const long value = ::sysconf(_SC_PAGESIZE);
    REQUIRE(value > 0);
    return static_cast<std::uint64_t>(value);
}

std::uint64_t find_free_block(std::size_t size) {
    void* const mapped =
        ::mmap(
            nullptr,
            size,
            PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
    REQUIRE(mapped != MAP_FAILED);

    const auto address =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(mapped));
    REQUIRE(::munmap(mapped, size) == 0);
    return address;
}

GuestImage make_guest_image(
    std::uint64_t code_base,
    std::uint64_t stack_base,
    std::uint64_t host_page_size,
    std::vector<std::byte> code) {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kExecute =
        static_cast<std::uint8_t>(
            GuestPermission::execute);

    const auto code_size =
        static_cast<std::uint64_t>(
            code.size());

    ElfHeader header{};
    header.entry = code_base;

    const auto stack_pointer =
        GuestAddress{
            stack_base + host_page_size - 8U};

    return GuestImage{
        .image_bytes = std::move(code),
        .elf =
            ElfImage{
                .header = header,
                .program_headers = {},
            },
        .mappings =
            std::vector<MappingIntent>{
                MappingIntent{
                    .range =
                        range(
                            code_base,
                            code_size),
                    .permissions =
                        permissions(kRead | kExecute),
                    .backing =
                        MappingBacking{
                            .kind = MappingBackingKind::file,
                            .file_offset = 0,
                            .byte_count = GuestSize{code_size},
                        },
                    .source_index = 0,
                },
            },
        .dynamic_table = std::nullopt,
        .dynamic_strings = std::nullopt,
        .dynamic_symbols = std::nullopt,
        .general_relocations =
            GeneralDynamicRelocationMetadata{
                .rel = std::nullopt,
                .rela = std::nullopt,
            },
        .plt_relocations = std::nullopt,
        .tls = std::nullopt,
        .initial_stack =
            InitialStackImage{
                .storage =
                    range(
                        stack_base,
                        host_page_size),
                .used_range =
                    range(
                        stack_pointer.value(),
                        0),
                .rsp = stack_pointer,
                .bytes = {},
            },
    };
}

SyntheticGateRegion make_gate_region(
    const GuestImage& image,
    std::uint64_t gate_base) {
    auto registry =
        HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                HleFunctionDescriptor{
                    .id = HleFunctionId{1},
                    .canonical_name =
                        "astraea.test.write",
                    .argument_count = 2,
                },
            });
    REQUIRE(registry.has_value());

    auto gate =
        astraea::execution::
            build_synthetic_gate_region(
                registry.value(),
                GuestAddress{gate_base},
                1,
                std::vector<
                    astraea::execution::GateBinding>{
                    astraea::execution::GateBinding{
                        .slot = 0,
                        .function_id = HleFunctionId{1},
                    },
                },
                image);
    REQUIRE(gate.has_value());
    return std::move(gate).value();
}

void append_u32(
    std::vector<std::byte>& code,
    std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_u64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_mov_imm64(
    std::vector<std::byte>& code,
    unsigned register_index,
    std::uint64_t value) {
    REQUIRE(register_index < 16);
    REQUIRE(register_index != 4);

    const bool extended = register_index >= 8;
    const unsigned low_index =
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
}

std::vector<std::byte> call_gate_code(
    std::uint64_t code_base,
    std::uint64_t gate_base) {
    const std::int64_t displacement =
        static_cast<std::int64_t>(gate_base) -
        static_cast<std::int64_t>(code_base + 5U);
    REQUIRE(
        displacement >=
        std::numeric_limits<std::int32_t>::min());
    REQUIRE(
        displacement <=
        std::numeric_limits<std::int32_t>::max());

    std::vector<std::byte> code{
        std::byte{0xe8},
    };
    append_u32(
        code,
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(
                displacement)));
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});
    return code;
}

std::uint64_t captured_register(
    const GuestCpuContext& context,
    unsigned register_index) {
    switch (register_index) {
    case 0:
        return context.rax;
    case 1:
        return context.rcx;
    case 2:
        return context.rdx;
    case 3:
        return context.rbx;
    case 5:
        return context.rbp;
    case 6:
        return context.rsi;
    case 7:
        return context.rdi;
    case 8:
        return context.r8;
    case 9:
        return context.r9;
    case 10:
        return context.r10;
    case 11:
        return context.r11;
    case 12:
        return context.r12;
    case 13:
        return context.r13;
    case 14:
        return context.r14;
    case 15:
        return context.r15;
    default:
        return 0;
    }
}

#endif

}  // namespace

TEST_CASE(
    "Linux native execution backend availability matches host",
    "[execution][linux-transition]") {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    REQUIRE(
        astraea::execution::
            linux_native_execution_backend_available());
#else
    REQUIRE_FALSE(
        astraea::execution::
            linux_native_execution_backend_available());
#endif
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "Linux seccomp guest syscall interception availability matches native host",
    "[execution][linux-transition][c0][seccomp]") {
    REQUIRE(
        astraea::execution::
            linux_guest_syscall_seccomp_available());
}

TEST_CASE(
    "Linux seccomp traps unmodified guest SYSCALL before host execution",
    "[execution][linux-transition][c0][seccomp][syscall]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    std::vector<std::byte> code;
    append_mov_imm64(
        code,
        0U,
        static_cast<std::uint64_t>(
            SYS_getpid));
    const auto syscall_offset = code.size();
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x05});

    // If the syscall executes instead of trapping, the next instruction
    // deliberately produces the existing illegal-instruction guest fault.
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::move(code));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    const auto result =
        astraea::execution::
            enter_linux_guest_with_seccomp_syscall_trap(
                image,
                prepared.value(),
                gate,
                astraea::execution::
                    make_synthetic_initial_context(
                        image));

    REQUIRE(result.has_value());
    const auto* trapped =
        std::get_if<
            astraea::execution::
                LinuxSeccompSyscallTrap>(
                    &result.value());
    REQUIRE(trapped != nullptr);

    const auto expected_rip =
        base +
        static_cast<std::uint64_t>(
            syscall_offset);
    CAPTURE(trapped->guest_rip.value());
    CAPTURE(expected_rip);
    CAPTURE(trapped->context.rip);
    REQUIRE(trapped->guest_rip.value() == expected_rip);
    REQUIRE(
        trapped->syscall_number ==
        static_cast<std::int32_t>(
            SYS_getpid));
    REQUIRE(
        trapped->audit_arch ==
        static_cast<std::uint32_t>(
            AUDIT_ARCH_X86_64));

    // SECCOMP_RET_TRAP reports the original call site separately while the
    // interrupted processor context already reflects post-SYSCALL PC.
    REQUIRE(trapped->context.rip == expected_rip + 2U);
}

TEST_CASE(
    "Linux seccomp range filter traps guest int 0x80 alternate ABI entry",
    "[execution][linux-transition][c0][seccomp][int80]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    // i386 getpid is syscall 20. Using INT 0x80 from long mode deliberately
    // exercises an alternate ABI entry path from the same guest mapping.
    std::vector<std::byte> code{
        std::byte{0xb8},
        std::byte{0x14},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0xcd},
        std::byte{0x80},
        std::byte{0x0f},
        std::byte{0x0b},
    };

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::move(code));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    const auto result =
        astraea::execution::
            enter_linux_guest_with_seccomp_syscall_trap(
                image,
                prepared.value(),
                gate,
                astraea::execution::
                    make_synthetic_initial_context(
                        image));

    REQUIRE(result.has_value());
    const auto* trapped =
        std::get_if<
            astraea::execution::
                LinuxSeccompSyscallTrap>(
                    &result.value());
    REQUIRE(trapped != nullptr);
    CAPTURE(trapped->guest_rip.value());
    CAPTURE(base);
    CAPTURE(trapped->context.rip);
    REQUIRE(trapped->guest_rip.value() == base + 5U);
    REQUIRE(trapped->syscall_number == 20);
    REQUIRE(
        trapped->audit_arch ==
        static_cast<std::uint32_t>(
            AUDIT_ARCH_I386));
    REQUIRE(trapped->context.rip == base + 7U);
}

TEST_CASE(
    "Linux guest call stops at registered synthetic gate",
    "[execution][linux-transition]") {
    const auto page = page_size();
    REQUIRE(
        page * 3U <=
        std::numeric_limits<std::size_t>::max());

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            call_gate_code(
                base,
                gate_base));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());

    auto gate =
        make_gate_region(
            image,
            gate_base);
    auto context =
        astraea::execution::
            make_synthetic_initial_context(image);

    const auto initial_rsp = context.rsp;
    auto stopped =
        astraea::execution::enter_linux_guest(
            image,
            prepared.value(),
            gate,
            context);

    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::host_gate);
    REQUIRE(stopped->has_gate_slot);
    REQUIRE(stopped->gate_slot == 0);
    REQUIRE_FALSE(stopped->has_fault);
    REQUIRE(stopped->context.rip == gate_base);
    REQUIRE(stopped->context.rsp == initial_rsp - 8U);

    std::uint64_t return_address = 0;
    std::memcpy(
        &return_address,
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(
                stopped->context.rsp)),
        sizeof(return_address));
    REQUIRE(return_address == base + 5U);
}

TEST_CASE(
    "Linux guest illegal instruction becomes normalized guest fault",
    "[execution][linux-transition]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            });
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    auto stopped =
        astraea::execution::enter_linux_guest(
            image,
            prepared.value(),
            gate,
            astraea::execution::
                make_synthetic_initial_context(image));

    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::guest_fault);
    REQUIRE_FALSE(stopped->has_gate_slot);
    REQUIRE(stopped->has_fault);
    REQUIRE(
        stopped->fault.kind ==
        GuestFaultKind::illegal_instruction);
    REQUIRE(stopped->fault.instruction_pointer == base);
    REQUIRE(stopped->context.rip == base);
}

TEST_CASE(
    "Linux guest unmapped read becomes normalized access violation",
    "[execution][linux-transition]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::vector<std::byte>{
                std::byte{0x48},
                std::byte{0x31},
                std::byte{0xc0},
                std::byte{0x48},
                std::byte{0x8b},
                std::byte{0x00},
            });
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    auto stopped =
        astraea::execution::enter_linux_guest(
            image,
            prepared.value(),
            gate,
            astraea::execution::
                make_synthetic_initial_context(image));

    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::guest_fault);
    REQUIRE(stopped->has_fault);
    REQUIRE(
        stopped->fault.kind ==
        GuestFaultKind::access_violation);
    REQUIRE(stopped->fault.has_fault_address);
    REQUIRE(stopped->fault.fault_address == 0);
    REQUIRE(stopped->context.rip == base + 3U);
}

TEST_CASE(
    "Linux signal recovery captures guest GPRs and restores host execution",
    "[execution][linux-transition]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    std::array<std::uint64_t, 16> values{};
    std::vector<std::byte> code;
    for (unsigned register_index = 0;
         register_index < values.size();
         ++register_index) {
        if (register_index == 4) {
            continue;
        }

        values[register_index] =
            0x1111000000000000ULL +
            static_cast<std::uint64_t>(
                register_index);
        append_mov_imm64(
            code,
            register_index,
            values[register_index]);
    }
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::move(code));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    volatile std::uint64_t host_sentinel =
        0xfeedfacecafebeefULL;
    const auto initial =
        astraea::execution::
            make_synthetic_initial_context(image);

    auto stopped =
        astraea::execution::enter_linux_guest(
            image,
            prepared.value(),
            gate,
            initial);

    REQUIRE(
        host_sentinel ==
        0xfeedfacecafebeefULL);
    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::guest_fault);
    REQUIRE(
        stopped->fault.kind ==
        GuestFaultKind::illegal_instruction);

    for (unsigned register_index = 0;
         register_index < values.size();
         ++register_index) {
        if (register_index == 4) {
            continue;
        }
        REQUIRE(
            captured_register(
                stopped->context,
                register_index) ==
            values[register_index]);
    }
    REQUIRE(stopped->context.rsp == initial.rsp);
}

TEST_CASE(
    "Linux native entry rejects unsupported TLS and invalid synthetic context",
    "[execution][linux-transition]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            });
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    SECTION("nonzero FS base") {
        auto context =
            astraea::execution::
                make_synthetic_initial_context(image);
        context.fs_base = 1;

        auto result =
            astraea::execution::enter_linux_guest(
                image,
                prepared.value(),
                gate,
                context);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                NativeBackendErrorCode::
                    invalid_guest_context);
    }

    SECTION("entry outside exact executable mapping") {
        auto context =
            astraea::execution::
                make_synthetic_initial_context(image);
        context.rip = base + page - 1U;

        auto result =
            astraea::execution::enter_linux_guest(
                image,
                prepared.value(),
                gate,
                context);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                NativeBackendErrorCode::
                    invalid_guest_context);
    }

    SECTION("stack pointer outside synthetic stack") {
        auto context =
            astraea::execution::
                make_synthetic_initial_context(image);
        context.rsp = base;

        auto result =
            astraea::execution::enter_linux_guest(
                image,
                prepared.value(),
                gate,
                context);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                NativeBackendErrorCode::
                    invalid_guest_context);
    }
}

TEST_CASE(
    "Linux registered syscall patch becomes typed native stop",
    "[execution][linux-transition][c0][syscall-trap]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    std::vector<std::byte> code{
        std::byte{0x0f},
        std::byte{0x05},
    };

    const auto trap =
        astraea::execution::
            plan_registered_syscall_trap(
                GuestAddress{base},
                code,
                GuestAddress{base});
    REQUIRE(trap.has_value());
    REQUIRE(
        astraea::execution::
            apply_registered_syscall_trap(
                code,
                trap.value())
            .has_value());
    REQUIRE(
        code[0] ==
        astraea::execution::
            kX86Ud2Bytes[0]);
    REQUIRE(
        code[1] ==
        astraea::execution::
            kX86Ud2Bytes[1]);

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            std::move(code));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    const std::array traps{
        trap.value(),
    };
    auto stopped =
        astraea::execution::enter_linux_guest(
            image,
            prepared.value(),
            gate,
            astraea::execution::
                make_synthetic_initial_context(image),
            traps);

    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::
            registered_syscall_trap);
    REQUIRE_FALSE(stopped->has_gate_slot);
    REQUIRE_FALSE(stopped->has_fault);
    REQUIRE(stopped->context.rip == base);
}

TEST_CASE(
    "Linux refuses registered syscall site until mapped bytes are UD2",
    "[execution][linux-transition][c0][syscall-trap][negative]") {
    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto stack_base = base + page;
    const auto gate_base = base + 2U * page;

    const std::vector<std::byte> original_code{
        std::byte{0x0f},
        std::byte{0x05},
    };
    const auto trap =
        astraea::execution::
            plan_registered_syscall_trap(
                GuestAddress{base},
                original_code,
                GuestAddress{base});
    REQUIRE(trap.has_value());

    auto image =
        make_guest_image(
            base,
            stack_base,
            page,
            original_code);
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    const std::array traps{
        trap.value(),
    };
    const auto entered =
        astraea::execution::enter_linux_guest(
            image,
            prepared.value(),
            gate,
            astraea::execution::
                make_synthetic_initial_context(image),
            traps);

    REQUIRE_FALSE(entered.has_value());
    REQUIRE(
        entered.error().code ==
        astraea::execution::
            NativeBackendErrorCode::
                invalid_registered_syscall_trap);
}


#endif
