#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_session.hpp>
#include <astraea/execution/sce_jump_slot_apply.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

using astraea::execution::ExecutionStop;
using astraea::execution::ExecutionStopReason;
using astraea::execution::GuestCpuContext;
using astraea::execution::GuestMemoryAccess;
using astraea::execution::GuestMemoryErrorCode;
using astraea::execution::HleFunctionDescriptor;
using astraea::execution::HleFunctionId;
using astraea::execution::HleHandlerAction;
using astraea::execution::HleRegistry;
using astraea::execution::HleRuntimeErrorCode;
using astraea::execution::SyntheticGateRegion;
using astraea::execution::SyntheticHleTranscript;
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

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

GuestPermissions permissions(std::uint8_t bits) {
    auto result =
        GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
}

HleRegistry make_test_registry() {
    auto registry =
        HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                HleFunctionDescriptor{
                    .id =
                        astraea::execution::
                            kSyntheticTestWriteId,
                    .canonical_name =
                        "astraea.test.write",
                    .argument_count = 2,
                },
                HleFunctionDescriptor{
                    .id =
                        astraea::execution::
                            kSyntheticTestExitId,
                    .canonical_name =
                        "astraea.test.exit",
                    .argument_count = 1,
                },
            });
    REQUIRE(registry.has_value());
    return std::move(registry).value();
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

struct TestImageLayout {
    GuestImage image;
    std::uint64_t code_base = 0;
    std::uint64_t data_base = 0;
    std::uint64_t stack_base = 0;
    std::uint64_t gate_base = 0;
    std::uint64_t page = 0;
};

TestImageLayout make_layout(
    std::vector<std::byte> code,
    std::vector<std::byte> data,
    GuestPermissions data_permissions) {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kExecute =
        static_cast<std::uint8_t>(
            GuestPermission::execute);

    const auto page = page_size();
    REQUIRE(
        page * 4U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 4U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base = base + 2U * page;
    const auto gate_base = base + 3U * page;

    const auto code_size =
        static_cast<std::uint64_t>(code.size());
    const auto data_size =
        static_cast<std::uint64_t>(data.size());

    std::vector<std::byte> image_bytes;
    image_bytes.reserve(
        code.size() + data.size());
    image_bytes.insert(
        image_bytes.end(),
        code.begin(),
        code.end());
    image_bytes.insert(
        image_bytes.end(),
        data.begin(),
        data.end());

    ElfHeader header{};
    header.entry = code_base;

    const auto stack_pointer =
        GuestAddress{
            stack_base + page - 8U};

    GuestImage image{
        .image_bytes = std::move(image_bytes),
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
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = 0,
                            .byte_count =
                                GuestSize{code_size},
                        },
                    .source_index = 0,
                },
                MappingIntent{
                    .range =
                        range(
                            data_base,
                            data_size),
                    .permissions =
                        data_permissions,
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = code_size,
                            .byte_count =
                                GuestSize{data_size},
                        },
                    .source_index = 1,
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
                        page),
                .used_range =
                    range(
                        stack_pointer.value(),
                        0),
                .rsp = stack_pointer,
                .bytes = {},
            },
    };

    return TestImageLayout{
        .image = std::move(image),
        .code_base = code_base,
        .data_base = data_base,
        .stack_base = stack_base,
        .gate_base = gate_base,
        .page = page,
    };
}

SyntheticGateRegion make_gate_region(
    const GuestImage& image,
    const HleRegistry& registry,
    std::uint64_t gate_base) {
    auto gates =
        astraea::execution::
            build_synthetic_gate_region(
                registry,
                GuestAddress{gate_base},
                2,
                std::vector<
                    astraea::execution::GateBinding>{
                    astraea::execution::GateBinding{
                        .slot = 0,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestWriteId,
                    },
                    astraea::execution::GateBinding{
                        .slot = 1,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestExitId,
                    },
                },
                image);
    REQUIRE(gates.has_value());
    return std::move(gates).value();
}

void append_u32(
    std::vector<std::byte>& bytes,
    std::uint32_t value) {
    for (unsigned shift = 0;
         shift < 32;
         shift += 8) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_u64(
    std::vector<std::byte>& bytes,
    std::uint64_t value) {
    for (unsigned shift = 0;
         shift < 64;
         shift += 8) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_mov_rdi_imm64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbf});
    append_u64(code, value);
}

void append_mov_rsi_imm64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbe});
    append_u64(code, value);
}

void append_call_rel32(
    std::vector<std::byte>& code,
    std::uint64_t code_base,
    std::uint64_t target) {
    const auto next_rip =
        code_base +
        static_cast<std::uint64_t>(
            code.size()) +
        5U;
    const std::int64_t displacement =
        static_cast<std::int64_t>(target) -
        static_cast<std::int64_t>(next_rip);
    REQUIRE(
        displacement >=
        std::numeric_limits<std::int32_t>::min());
    REQUIRE(
        displacement <=
        std::numeric_limits<std::int32_t>::max());

    code.push_back(std::byte{0xe8});
    append_u32(
        code,
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(
                displacement)));
}

#endif

}  // namespace

TEST_CASE(
    "guest memory access rejects unavailable prepared memory",
    "[execution][guest-memory]") {
    GuestImage image{
        .image_bytes = {},
        .elf =
            ElfImage{
                .header = {},
                .program_headers = {},
            },
        .mappings = {},
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
                .storage = range(0, 0),
                .used_range = range(0, 0),
                .rsp = GuestAddress{0},
                .bytes = {},
            },
    };
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    std::array<std::byte, 1> byte{};
    auto result =
        memory.read(
            GuestAddress{0x1000},
            byte);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        GuestMemoryErrorCode::
            prepared_memory_unavailable);
}

TEST_CASE(
    "validated JUMP_SLOT application preserves unavailable-memory failure",
    "[execution][sce-jump-slot-apply]") {
    GuestImage image{
        .image_bytes = {},
        .elf =
            ElfImage{
                .header = {},
                .program_headers = {},
            },
        .mappings = {},
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
                .storage = range(0, 0),
                .used_range = range(0, 0),
                .rsp = GuestAddress{0},
                .bytes = {},
            },
    };
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    const astraea::execution::SceJumpSlotPatch patch{
        .relocation_target = GuestAddress{0x1000},
        .gate_destination = GuestAddress{0x600000},
        .gate_slot = 0,
        .function_id = HleFunctionId{1},
        .bytes = {
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x60},
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x00},
        },
        .raw_addend = std::nullopt,
    };

    const auto result =
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                patch,
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        GuestMemoryErrorCode::
            prepared_memory_unavailable);
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "guest memory access enforces exact coverage and permissions",
    "[execution][guest-memory]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            },
            std::vector<std::byte>{
                std::byte{'a'},
                std::byte{'b'},
                std::byte{'c'},
                std::byte{0},
            },
            permissions(kRead | kWrite));

    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    std::array<std::byte, 4> read_back{};
    auto read =
        memory.read(
            GuestAddress{layout.data_base},
            read_back);
    REQUIRE(read.has_value());
    REQUIRE(read.value() == read_back.size());
    REQUIRE(read_back[0] == std::byte{'a'});
    REQUIRE(read_back[1] == std::byte{'b'});
    REQUIRE(read_back[2] == std::byte{'c'});
    REQUIRE(read_back[3] == std::byte{0});

    const std::array replacement{
        std::byte{'x'},
        std::byte{'y'},
        std::byte{'z'},
    };
    auto write =
        memory.write(
            GuestAddress{layout.data_base},
            replacement);
    REQUIRE(write.has_value());
    REQUIRE(write.value() == replacement.size());

    std::array<std::byte, 3> after_write{};
    REQUIRE(
        memory.read(
            GuestAddress{layout.data_base},
            after_write)
            .has_value());
    REQUIRE(after_write == replacement);

    std::array<std::byte, 1> one{};
    auto unmapped =
        memory.read(
            GuestAddress{
                layout.data_base +
                layout.page -
                1U},
            one);
    REQUIRE_FALSE(unmapped.has_value());
    REQUIRE(
        unmapped.error().code ==
        GuestMemoryErrorCode::
            guest_memory_unmapped);

    auto code_write =
        memory.write(
            GuestAddress{layout.code_base},
            std::span<const std::byte>{
                replacement.data(),
                1});
    REQUIRE_FALSE(code_write.has_value());
    REQUIRE(
        code_write.error().code ==
        GuestMemoryErrorCode::
            guest_memory_permission_denied);

    auto overflow =
        memory.read(
            GuestAddress{
                std::numeric_limits<
                    std::uint64_t>::max()},
            std::span<std::byte>{
                read_back.data(),
                2});
    REQUIRE_FALSE(overflow.has_value());
    REQUIRE(
        overflow.error().code ==
        GuestMemoryErrorCode::
            guest_memory_range_overflow);

    auto string =
        memory.read_c_string(
            GuestAddress{layout.data_base},
            4);
    REQUIRE(string.has_value());
    REQUIRE(string.value() == "xyz");

    REQUIRE(
        memory.is_exact_executable_address(
            GuestAddress{layout.code_base}));
    REQUIRE_FALSE(
        memory.is_exact_executable_address(
            GuestAddress{layout.data_base}));

    auto mismatched_image = layout.image;
    mismatched_image.mappings[1].range =
        range(
            layout.gate_base + layout.page,
            4);
    GuestMemoryAccess mismatched{
        mismatched_image,
        prepared.value()};
    auto prepared_mismatch =
        mismatched.read(
            GuestAddress{
                layout.gate_base +
                layout.page},
            one);
    REQUIRE_FALSE(prepared_mismatch.has_value());
    REQUIRE(
        prepared_mismatch.error().code ==
        GuestMemoryErrorCode::
            prepared_memory_unavailable);
}

TEST_CASE(
    "synthetic HLE dispatch writes bytes and resumes through validated RET",
    "[execution][hle-runtime]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);

    const std::array message{
        std::byte{'h'},
        std::byte{'i'},
    };
    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x90},
                std::byte{0x90},
            },
            std::vector<std::byte>{
                message[0],
                message[1],
            },
            permissions(kRead));

    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    auto registry = make_test_registry();
    auto gates =
        make_gate_region(
            layout.image,
            registry,
            layout.gate_base);
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto return_rip =
        layout.code_base + 1U;
    const auto guest_rsp =
        layout.image.initial_stack.rsp.value() -
        8U;
    std::array<std::byte, 8> return_bytes{};
    for (std::size_t i = 0;
         i < return_bytes.size();
         ++i) {
        return_bytes[i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (return_rip >> (i * 8U)) &
                    0xffU));
    }
    REQUIRE(
        memory.write(
            GuestAddress{guest_rsp},
            return_bytes)
            .has_value());

    ExecutionStop stop{};
    stop.reason = ExecutionStopReason::host_gate;
    stop.has_gate_slot = true;
    stop.gate_slot = 0;
    stop.context.rip = layout.gate_base;
    stop.context.rsp = guest_rsp;
    stop.context.rdi = layout.data_base;
    stop.context.rsi = message.size();

    SyntheticHleTranscript transcript;
    auto dispatched =
        astraea::execution::
            dispatch_synthetic_hle(
                registry,
                gates,
                stop,
                memory,
                transcript);

    REQUIRE(dispatched.has_value());
    REQUIRE(
        dispatched->action ==
        HleHandlerAction::resume);
    REQUIRE(
        dispatched->value ==
        message.size());
    REQUIRE(transcript.output.size() == 2);
    REQUIRE(transcript.output[0] == message[0]);
    REQUIRE(transcript.output[1] == message[1]);

    auto resumed =
        astraea::execution::
            apply_synthetic_hle_resume(
                stop.context,
                dispatched.value(),
                memory);
    REQUIRE(resumed.has_value());
    REQUIRE(resumed->rax == message.size());
    REQUIRE(resumed->rip == return_rip);
    REQUIRE(resumed->rsp == guest_rsp + 8U);
}

TEST_CASE(
    "synthetic HLE exit preserves guest exit code",
    "[execution][hle-runtime]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);

    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            },
            std::vector<std::byte>{
                std::byte{0},
            },
            permissions(kRead));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    auto registry = make_test_registry();
    auto gates =
        make_gate_region(
            layout.image,
            registry,
            layout.gate_base);
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    ExecutionStop stop{};
    stop.reason = ExecutionStopReason::host_gate;
    stop.has_gate_slot = true;
    stop.gate_slot = 1;
    stop.context.rip =
        layout.gate_base +
        astraea::execution::
            kSyntheticGateStride;
    stop.context.rdi = 42;

    SyntheticHleTranscript transcript;
    auto dispatched =
        astraea::execution::
            dispatch_synthetic_hle(
                registry,
                gates,
                stop,
                memory,
                transcript);

    REQUIRE(dispatched.has_value());
    REQUIRE(
        dispatched->action ==
        HleHandlerAction::exit);
    REQUIRE(dispatched->value == 42);
    REQUIRE(transcript.output.empty());
}

TEST_CASE(
    "synthetic HLE rejects unbound gates and invalid return targets",
    "[execution][hle-runtime]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);

    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x90},
            },
            std::vector<std::byte>{
                std::byte{0},
            },
            permissions(kRead));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    auto registry = make_test_registry();
    auto unbound =
        astraea::execution::
            build_synthetic_gate_region(
                registry,
                GuestAddress{layout.gate_base},
                2,
                std::vector<
                    astraea::execution::GateBinding>{
                    astraea::execution::GateBinding{
                        .slot = 0,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestWriteId,
                    },
                },
                layout.image);
    REQUIRE(unbound.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};
    ExecutionStop stop{};
    stop.reason = ExecutionStopReason::host_gate;
    stop.has_gate_slot = true;
    stop.gate_slot = 1;

    SyntheticHleTranscript transcript;
    auto dispatch =
        astraea::execution::
            dispatch_synthetic_hle(
                registry,
                unbound.value(),
                stop,
                memory,
                transcript);
    REQUIRE_FALSE(dispatch.has_value());
    REQUIRE(
        dispatch.error().code ==
        HleRuntimeErrorCode::unbound_gate);

    const auto guest_rsp =
        layout.image.initial_stack.rsp.value() -
        8U;
    std::array<std::byte, 8> return_bytes{};
    const auto invalid_return =
        layout.data_base;
    for (std::size_t i = 0;
         i < return_bytes.size();
         ++i) {
        return_bytes[i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (invalid_return >> (i * 8U)) &
                    0xffU));
    }
    REQUIRE(
        memory.write(
            GuestAddress{guest_rsp},
            return_bytes)
            .has_value());

    GuestCpuContext context{};
    context.rsp = guest_rsp;
    auto resumed =
        astraea::execution::
            apply_synthetic_hle_resume(
                context,
                astraea::execution::
                    HleHandlerResult{
                        .action =
                            HleHandlerAction::resume,
                        .value = 7,
                    },
                memory);
    REQUIRE_FALSE(resumed.has_value());
    REQUIRE(
        resumed.error().code ==
        HleRuntimeErrorCode::
            guest_return_address_not_executable);
}

TEST_CASE(
    "Linux synthetic session performs write resume and exit",
    "[execution][linux-session]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);

    const std::vector<std::byte> message{
        std::byte{'H'},
        std::byte{'e'},
        std::byte{'l'},
        std::byte{'l'},
        std::byte{'o'},
        std::byte{' '},
        std::byte{'f'},
        std::byte{'r'},
        std::byte{'o'},
        std::byte{'m'},
        std::byte{' '},
        std::byte{'g'},
        std::byte{'u'},
        std::byte{'e'},
        std::byte{'s'},
        std::byte{'t'},
    };

    const auto page = page_size();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 4U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base = base + 2U * page;
    const auto gate_base = base + 3U * page;

    std::vector<std::byte> code;
    append_mov_rdi_imm64(
        code,
        data_base);
    append_mov_rsi_imm64(
        code,
        message.size());
    append_call_rel32(
        code,
        code_base,
        gate_base);
    append_mov_rdi_imm64(
        code,
        42);
    append_call_rel32(
        code,
        code_base,
        gate_base +
            astraea::execution::
                kSyntheticGateStride);
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    const auto code_size =
        static_cast<std::uint64_t>(
            code.size());
    const auto data_size =
        static_cast<std::uint64_t>(
            message.size());

    std::vector<std::byte> image_bytes;
    image_bytes.reserve(
        code.size() + message.size());
    image_bytes.insert(
        image_bytes.end(),
        code.begin(),
        code.end());
    image_bytes.insert(
        image_bytes.end(),
        message.begin(),
        message.end());

    constexpr auto kExecute =
        static_cast<std::uint8_t>(
            GuestPermission::execute);
    ElfHeader header{};
    header.entry = code_base;
    const auto stack_pointer =
        GuestAddress{
            stack_base + page - 8U};

    GuestImage image{
        .image_bytes = std::move(image_bytes),
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
                                GuestSize{code_size},
                        },
                    .source_index = 0,
                },
                MappingIntent{
                    .range =
                        range(
                            data_base,
                            data_size),
                    .permissions =
                        permissions(kRead),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = code_size,
                            .byte_count =
                                GuestSize{data_size},
                        },
                    .source_index = 1,
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
                        page),
                .used_range =
                    range(
                        stack_pointer.value(),
                        0),
                .rsp = stack_pointer,
                .bytes = {},
            },
    };

    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());

    auto registry = make_test_registry();
    auto gates =
        make_gate_region(
            image,
            registry,
            gate_base);

    auto result =
        astraea::execution::
            run_linux_synthetic_session(
                image,
                prepared.value(),
                registry,
                gates,
                astraea::execution::
                    make_synthetic_initial_context(
                        image));

    REQUIRE(result.has_value());
    REQUIRE(result->exit_code == 42);
    REQUIRE(result->gate_stop_count == 2);
    REQUIRE(result->output == message);
    REQUIRE(
        result->final_context.rax ==
        message.size());
    REQUIRE(
        result->final_context.rdi == 42);
    REQUIRE(
        result->final_context.rip ==
        gate_base +
            astraea::execution::
                kSyntheticGateStride);
}

TEST_CASE(
    "validated JUMP_SLOT patch writes exact gate bytes through GuestMemoryAccess",
    "[execution][sce-jump-slot-apply]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            },
            std::vector<std::byte>(8, std::byte{0}),
            permissions(kRead | kWrite));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const astraea::execution::SceJumpSlotPatch patch{
        .relocation_target =
            GuestAddress{layout.data_base},
        .gate_destination =
            GuestAddress{layout.gate_base},
        .gate_slot = 0,
        .function_id =
            astraea::execution::
                kSyntheticTestWriteId,
        .bytes = {
            static_cast<std::byte>(
                layout.gate_base & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 8U) & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 16U) & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 24U) & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 32U) & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 40U) & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 48U) & 0xffU),
            static_cast<std::byte>(
                (layout.gate_base >> 56U) & 0xffU),
        },
        .raw_addend = std::int64_t{-9},
    };

    const auto applied =
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                patch,
                memory);
    REQUIRE(applied.has_value());
    REQUIRE(
        applied->relocation_target ==
        patch.relocation_target);
    REQUIRE(
        applied->gate_destination ==
        patch.gate_destination);
    REQUIRE(applied->gate_slot == 0);
    REQUIRE(
        applied->function_id ==
        patch.function_id);

    std::array<std::byte, 8> read_back{};
    const auto read =
        memory.read(
            GuestAddress{layout.data_base},
            read_back);
    REQUIRE(read.has_value());
    REQUIRE(read_back == patch.bytes);
}

TEST_CASE(
    "validated JUMP_SLOT patch preserves read-only target failure",
    "[execution][sce-jump-slot-apply]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);

    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            },
            std::vector<std::byte>(8, std::byte{0}),
            permissions(kRead));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const astraea::execution::SceJumpSlotPatch patch{
        .relocation_target =
            GuestAddress{layout.data_base},
        .gate_destination =
            GuestAddress{layout.gate_base},
        .gate_slot = 0,
        .function_id = HleFunctionId{1},
        .bytes = {},
        .raw_addend = std::nullopt,
    };

    const auto applied =
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                patch,
                memory);
    REQUIRE_FALSE(applied.has_value());
    REQUIRE(
        applied.error().code ==
        GuestMemoryErrorCode::
            guest_memory_permission_denied);
}

TEST_CASE(
    "validated JUMP_SLOT patch preserves unmapped target failure",
    "[execution][sce-jump-slot-apply]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            std::vector<std::byte>{
                std::byte{0x0f},
                std::byte{0x0b},
            },
            std::vector<std::byte>(8, std::byte{0}),
            permissions(kRead | kWrite));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const astraea::execution::SceJumpSlotPatch patch{
        .relocation_target =
            GuestAddress{layout.gate_base},
        .gate_destination =
            GuestAddress{layout.gate_base},
        .gate_slot = 0,
        .function_id = HleFunctionId{1},
        .bytes = {},
        .raw_addend = std::nullopt,
    };

    const auto applied =
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                patch,
                memory);
    REQUIRE_FALSE(applied.has_value());
    REQUIRE(
        applied.error().code ==
        GuestMemoryErrorCode::
            guest_memory_unmapped);
}

#endif
