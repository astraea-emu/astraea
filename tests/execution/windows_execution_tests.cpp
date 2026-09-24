#include <astraea/execution/windows_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32) && defined(_M_X64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#if defined(_WIN32) && defined(_M_X64)

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

GuestRange range(
    std::uint64_t base,
    std::uint64_t size) {
    auto result =
        GuestRange::create(
            GuestAddress{base},
            GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

GuestPermissions permissions(
    std::uint8_t bits) {
    auto result =
        GuestPermissions::checked_from_bits(
            bits);
    REQUIRE(result.has_value());
    return result.value();
}

struct WindowsGeometry {
    std::uint64_t page = 0;
    std::uint64_t granularity = 0;
};

WindowsGeometry geometry() {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    REQUIRE(info.dwPageSize != 0);
    REQUIRE(
        info.dwAllocationGranularity != 0);
    return WindowsGeometry{
        .page =
            static_cast<std::uint64_t>(
                info.dwPageSize),
        .granularity =
            static_cast<std::uint64_t>(
                info.dwAllocationGranularity),
    };
}

std::uint64_t find_free_block(
    std::size_t size) {
    void* const reserved =
        ::VirtualAlloc(
            nullptr,
            size,
            MEM_RESERVE,
            PAGE_NOACCESS);
    REQUIRE(reserved != nullptr);
    const auto base =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                reserved));
    REQUIRE(
        ::VirtualFree(
            reserved,
            0,
            MEM_RELEASE) != 0);
    return base;
}

GuestImage make_guest_image(
    std::uint64_t code_base,
    std::uint64_t stack_base,
    std::uint64_t stack_size,
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
            stack_base + stack_size - 8U};

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
                        permissions(
                            kRead | kExecute),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = 0,
                            .byte_count =
                                GuestSize{
                                    code_size},
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
                        stack_size),
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
                        .function_id =
                            HleFunctionId{1},
                    },
                },
                image);
    REQUIRE(gate.has_value());
    return std::move(gate).value();
}

void append_u32(
    std::vector<std::byte>& code,
    std::uint32_t value) {
    for (unsigned shift = 0;
         shift < 32;
         shift += 8) {
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) &
                    0xffU)));
    }
}

void append_u64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    for (unsigned shift = 0;
         shift < 64;
         shift += 8) {
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) &
                    0xffU)));
    }
}

void append_mov_imm64(
    std::vector<std::byte>& code,
    unsigned register_index,
    std::uint64_t value) {
    REQUIRE(register_index < 16);
    REQUIRE(register_index != 4);

    const bool extended =
        register_index >= 8;
    const unsigned low_index =
        extended
            ? register_index - 8U
            : register_index;

    code.push_back(
        static_cast<std::byte>(
            extended
                ? 0x49U
                : 0x48U));
    code.push_back(
        static_cast<std::byte>(
            static_cast<unsigned char>(
                0xb8U + low_index)));
    append_u64(
        code,
        value);
}

std::vector<std::byte> call_gate_code(
    std::uint64_t code_base,
    std::uint64_t gate_base) {
    const std::int64_t displacement =
        static_cast<std::int64_t>(
            gate_base) -
        static_cast<std::int64_t>(
            code_base + 5U);
    REQUIRE(
        displacement >=
        std::numeric_limits<
            std::int32_t>::min());
    REQUIRE(
        displacement <=
        std::numeric_limits<
            std::int32_t>::max());

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
    "Windows native execution backend availability matches host",
    "[execution][windows-transition]") {
#if defined(_WIN32) && defined(_M_X64)
    REQUIRE(
        astraea::execution::
            windows_native_execution_backend_available());
#else
    REQUIRE_FALSE(
        astraea::execution::
            windows_native_execution_backend_available());
#endif
}

#if defined(_WIN32) && defined(_M_X64)

TEST_CASE(
    "Windows guest call stops at registered synthetic gate",
    "[execution][windows-transition]") {
    const auto g = geometry();
    REQUIRE(
        g.granularity * 4U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t>::max()));
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity * 4U));
    const auto stack_base =
        base + g.granularity;
    const auto gate_base =
        base + g.granularity * 3U;

    auto image =
        make_guest_image(
            base,
            stack_base,
            g.granularity * 2U,
            call_gate_code(
                base,
                gate_base));
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(
                image);
    REQUIRE(prepared.has_value());

    auto gate =
        make_gate_region(
            image,
            gate_base);
    auto context =
        astraea::execution::
            make_synthetic_initial_context(
                image);
    const auto initial_rsp =
        context.rsp;

    auto stopped =
        astraea::execution::
            enter_windows_guest(
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
    REQUIRE(
        stopped->context.rip ==
        gate_base);
    REQUIRE(
        stopped->context.rsp ==
        initial_rsp - 8U);

    std::uint64_t return_address = 0;
    std::memcpy(
        &return_address,
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(
                stopped->context.rsp)),
        sizeof(return_address));
    REQUIRE(
        return_address ==
        base + 5U);
}

TEST_CASE(
    "Windows illegal instruction becomes normalized guest fault",
    "[execution][windows-transition]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity * 4U));
    const auto stack_base =
        base + g.granularity;
    const auto gate_base =
        base + g.granularity * 3U;

    auto image =
        make_guest_image(
            base,
            stack_base,
            g.granularity * 2U,
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            });
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(
                image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    auto stopped =
        astraea::execution::
            enter_windows_guest(
                image,
                prepared.value(),
                gate,
                astraea::execution::
                    make_synthetic_initial_context(
                        image));

    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::guest_fault);
    REQUIRE(stopped->has_fault);
    REQUIRE(
        stopped->fault.kind ==
        GuestFaultKind::
            illegal_instruction);
    REQUIRE(
        stopped->fault.
            instruction_pointer ==
        base);
}

TEST_CASE(
    "Windows unmapped read becomes normalized access violation",
    "[execution][windows-transition]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity * 4U));
    const auto stack_base =
        base + g.granularity;
    const auto gate_base =
        base + g.granularity * 3U;

    auto image =
        make_guest_image(
            base,
            stack_base,
            g.granularity * 2U,
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
            prepare_windows_guest_memory(
                image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    auto stopped =
        astraea::execution::
            enter_windows_guest(
                image,
                prepared.value(),
                gate,
                astraea::execution::
                    make_synthetic_initial_context(
                        image));

    REQUIRE(stopped.has_value());
    REQUIRE(
        stopped->reason ==
        ExecutionStopReason::guest_fault);
    REQUIRE(stopped->has_fault);
    REQUIRE(
        stopped->fault.kind ==
        GuestFaultKind::
            access_violation);
    REQUIRE(
        stopped->fault.has_fault_address);
    REQUIRE(
        stopped->fault.fault_address ==
        0);
    REQUIRE(
        stopped->context.rip ==
        base + 3U);
}

TEST_CASE(
    "Windows recovery captures guest GPRs and restores host execution",
    "[execution][windows-transition]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity * 4U));
    const auto stack_base =
        base + g.granularity;
    const auto gate_base =
        base + g.granularity * 3U;

    std::array<std::uint64_t, 16>
        values{};
    std::vector<std::byte> code;
    for (unsigned register_index = 0;
         register_index <
             values.size();
         ++register_index) {
        if (register_index == 4) {
            continue;
        }

        values[register_index] =
            0x2222000000000000ULL +
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
            g.granularity * 2U,
            std::move(code));
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(
                image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    volatile std::uint64_t host_sentinel =
        0xfeedfacecafebeefULL;
    const auto initial =
        astraea::execution::
            make_synthetic_initial_context(
                image);

    auto stopped =
        astraea::execution::
            enter_windows_guest(
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
        GuestFaultKind::
            illegal_instruction);

    for (unsigned register_index = 0;
         register_index <
             values.size();
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
    REQUIRE(
        stopped->context.rsp ==
        initial.rsp);
}

TEST_CASE(
    "Windows native entry rejects invalid synthetic context",
    "[execution][windows-transition]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity * 4U));
    const auto stack_base =
        base + g.granularity;
    const auto gate_base =
        base + g.granularity * 3U;

    auto image =
        make_guest_image(
            base,
            stack_base,
            g.granularity * 2U,
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            });
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(
                image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    SECTION("nonzero FS base") {
        auto context =
            astraea::execution::
                make_synthetic_initial_context(
                    image);
        context.fs_base = 1;

        auto result =
            astraea::execution::
                enter_windows_guest(
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
                make_synthetic_initial_context(
                    image);
        context.rip =
            base + g.page - 1U;

        auto result =
            astraea::execution::
                enter_windows_guest(
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
                make_synthetic_initial_context(
                    image);
        context.rsp = base;

        auto result =
            astraea::execution::
                enter_windows_guest(
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

    SECTION("stack pointer lacks Windows exception headroom") {
        auto context =
            astraea::execution::
                make_synthetic_initial_context(
                    image);
        context.rsp =
            stack_base + g.page;

        auto result =
            astraea::execution::
                enter_windows_guest(
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
    "Windows guest entry and gate teardown can repeat deterministically",
    "[execution][windows-transition]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity * 4U));
    const auto stack_base =
        base + g.granularity;
    const auto gate_base =
        base + g.granularity * 3U;

    auto image =
        make_guest_image(
            base,
            stack_base,
            g.granularity * 2U,
            call_gate_code(
                base,
                gate_base));
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(
                image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);
    const auto initial =
        astraea::execution::
            make_synthetic_initial_context(
                image);

    auto first =
        astraea::execution::
            enter_windows_guest(
                image,
                prepared.value(),
                gate,
                initial);
    auto second =
        astraea::execution::
            enter_windows_guest(
                image,
                prepared.value(),
                gate,
                initial);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(
        first.value() ==
        second.value());
}

TEST_CASE(
    "Windows registered syscall patch becomes typed native stop",
    "[execution][windows-transition][c0][syscall-trap]") {
    const auto granularity =
        geometry().granularity;
    REQUIRE(
        granularity * 3U <=
        std::numeric_limits<std::size_t>::max());

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                granularity * 3U));
    const auto stack_base =
        base + granularity;
    const auto gate_base =
        base + 2U * granularity;

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
                std::span<std::byte>{code},
                trap.value())
            .has_value());

    auto image =
        make_guest_image(
            base,
            stack_base,
            granularity,
            std::move(code));
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    const std::array traps{
        trap.value(),
    };
    auto stopped =
        astraea::execution::enter_windows_guest(
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
    "Windows refuses registered syscall site until mapped bytes are UD2",
    "[execution][windows-transition][c0][syscall-trap][negative]") {
    const auto granularity =
        geometry().granularity;
    REQUIRE(
        granularity * 3U <=
        std::numeric_limits<std::size_t>::max());

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                granularity * 3U));
    const auto stack_base =
        base + granularity;
    const auto gate_base =
        base + 2U * granularity;

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
            granularity,
            original_code);
    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(image);
    REQUIRE(prepared.has_value());
    auto gate =
        make_gate_region(
            image,
            gate_base);

    const std::array traps{
        trap.value(),
    };
    const auto entered =
        astraea::execution::enter_windows_guest(
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
