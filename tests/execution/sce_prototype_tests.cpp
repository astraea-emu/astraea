#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/linux_session.hpp>
#include <astraea/execution/sce_import_binding.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/execution/sce_jump_slot_apply.hpp>
#include <astraea/execution/sce_jump_slot_patch.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/guest_address.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::size_t kProgramHeaderCount = 3;

constexpr std::size_t kGotOffset = 0x80;
constexpr std::size_t kStringOffset = 0x100;
constexpr std::size_t kSymbolOffset = 0x180;
constexpr std::size_t kRelaOffset = 0x200;
constexpr std::size_t kDynamicOffset = 0x300;
constexpr std::size_t kDynamicEntryCount = 9;
constexpr std::size_t kDynamicSize =
    kDynamicEntryCount * 16;

constexpr std::uint16_t kSceDynExecType = 0xfe10;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;
constexpr std::uint32_t kPfExecute = 0x1;
constexpr std::uint32_t kPfWrite = 0x2;
constexpr std::uint32_t kPfRead = 0x4;

constexpr std::int64_t kDtNull = 0;
constexpr std::int64_t kDtPltRelSz = 2;
constexpr std::int64_t kDtStrTab = 5;
constexpr std::int64_t kDtSymTab = 6;
constexpr std::int64_t kDtRela = 7;
constexpr std::int64_t kDtStrSz = 10;
constexpr std::int64_t kDtSymEnt = 11;
constexpr std::int64_t kDtPltRel = 20;
constexpr std::int64_t kDtJmpRel = 23;
constexpr std::int64_t kDtSymTabSz = 39;

constexpr std::uint64_t kElf64SymbolSize = 24;
constexpr std::uint64_t kElf64RelaSize = 24;

const std::string kRawImportName =
    "ABCDEFGHIJK#library-a#module-a";

struct PrototypeFixture {
    std::vector<std::byte> bytes;
    std::uint64_t code_base = 0;
    std::uint64_t data_base = 0;
    std::uint64_t stack_base = 0;
    std::uint64_t gate_base = 0;
    std::uint64_t page = 0;
    std::uint64_t got_address = 0;
};

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    for (std::size_t index = 0;
         index < 2;
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (index * 8U)) &
                0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t index = 0;
         index < 4;
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (index * 8U)) &
                0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t index = 0;
         index < 8;
         ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (value >> (index * 8U)) &
                0xffU);
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
    std::size_t header_offset,
    std::uint32_t type,
    std::uint32_t flags,
    std::uint64_t file_offset,
    std::uint64_t virtual_address,
    std::uint64_t file_size,
    std::uint64_t memory_size,
    std::uint64_t alignment) {
    write_u32(bytes, header_offset, type);
    write_u32(bytes, header_offset + 4, flags);
    write_u64(bytes, header_offset + 8, file_offset);
    write_u64(
        bytes,
        header_offset + 16,
        virtual_address);
    write_u64(bytes, header_offset + 24, 0);
    write_u64(bytes, header_offset + 32, file_size);
    write_u64(bytes, header_offset + 40, memory_size);
    write_u64(bytes, header_offset + 48, alignment);
}

void write_dynamic(
    std::vector<std::byte>& bytes,
    std::size_t dynamic_file_offset,
    std::size_t index,
    std::int64_t tag,
    std::uint64_t value) {
    const auto offset =
        dynamic_file_offset + index * 16;
    write_i64(bytes, offset, tag);
    write_u64(bytes, offset + 8, value);
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
                    (value >> shift) &
                    0xffU)));
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
                    (value >> shift) &
                    0xffU)));
    }
}

std::uint64_t page_size() {
    const long value = ::sysconf(_SC_PAGESIZE);
    REQUIRE(value > 0);
    return static_cast<std::uint64_t>(value);
}

std::uint64_t find_free_block(
    std::size_t size) {
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
            reinterpret_cast<std::uintptr_t>(
                mapped));
    REQUIRE(::munmap(mapped, size) == 0);
    return address;
}

astraea::loader::InitialStackRequest
make_stack_request(
    std::uint64_t stack_base,
    std::uint64_t page) {
    auto storage =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{
                stack_base},
            astraea::memory::GuestSize{page});
    REQUIRE(storage.has_value());

    return astraea::loader::InitialStackRequest{
        .storage = storage.value(),
        .arguments = {"sce-prototype"},
        .environment = {"ASTRAEA=1"},
        .auxiliary_vector = {},
    };
}

PrototypeFixture make_fixture() {
    const auto page = page_size();
    REQUIRE(
        page >=
        static_cast<std::uint64_t>(
            kDynamicOffset +
            kDynamicSize));
    REQUIRE(
        page * 4U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::
                max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 4U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base = base + 2U * page;
    const auto gate_base = base + 3U * page;

    const auto code_file_offset =
        static_cast<std::size_t>(page);
    const auto data_file_offset =
        static_cast<std::size_t>(2U * page);
    const auto image_size =
        static_cast<std::size_t>(3U * page);

    std::vector<std::byte> bytes(
        image_size,
        std::byte{0});

    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};

    write_u16(bytes, 16, kSceDynExecType);
    write_u16(bytes, 18, 62);
    write_u32(bytes, 20, 1);
    write_u64(bytes, 24, code_base);
    write_u64(
        bytes,
        32,
        kProgramHeaderOffset);
    write_u16(bytes, 52, kElfHeaderSize);
    write_u16(bytes, 54, kProgramHeaderSize);
    write_u16(
        bytes,
        56,
        static_cast<std::uint16_t>(
            kProgramHeaderCount));

    write_program_header(
        bytes,
        kProgramHeaderOffset,
        kPtLoad,
        kPfRead | kPfExecute,
        page,
        code_base,
        page,
        page,
        page);

    write_program_header(
        bytes,
        kProgramHeaderOffset +
            kProgramHeaderSize,
        kPtLoad,
        kPfRead | kPfWrite,
        2U * page,
        data_base,
        page,
        page,
        page);

    const auto dynamic_file_offset =
        data_file_offset +
        kDynamicOffset;
    write_program_header(
        bytes,
        kProgramHeaderOffset +
            2U * kProgramHeaderSize,
        kPtDynamic,
        kPfRead | kPfWrite,
        static_cast<std::uint64_t>(
            dynamic_file_offset),
        data_base + kDynamicOffset,
        kDynamicSize,
        kDynamicSize,
        8);

    std::vector<std::byte> code;
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbf});
    append_u64(code, 42);

    const auto got_address =
        data_base + kGotOffset;
    const auto call_next_rip =
        code_base +
        static_cast<std::uint64_t>(
            code.size()) +
        6U;
    const auto displacement =
        static_cast<std::int64_t>(
            got_address) -
        static_cast<std::int64_t>(
            call_next_rip);
    REQUIRE(
        displacement >=
        std::numeric_limits<std::int32_t>::
            min());
    REQUIRE(
        displacement <=
        std::numeric_limits<std::int32_t>::
            max());

    code.push_back(std::byte{0xff});
    code.push_back(std::byte{0x15});
    append_u32(
        code,
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(
                displacement)));

    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    REQUIRE(code.size() < page);
    for (std::size_t index = 0;
         index < code.size();
         ++index) {
        bytes[code_file_offset + index] =
            code[index];
    }

    const auto string_file_offset =
        data_file_offset +
        kStringOffset;
    bytes[string_file_offset] =
        std::byte{0};
    for (std::size_t index = 0;
         index < kRawImportName.size();
         ++index) {
        bytes[
            string_file_offset +
            1U +
            index] =
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        kRawImportName[index]));
    }
    bytes[
        string_file_offset +
        1U +
        kRawImportName.size()] =
            std::byte{0};

    const auto symbol_file_offset =
        data_file_offset +
        kSymbolOffset;
    write_u32(
        bytes,
        symbol_file_offset +
            kElf64SymbolSize,
        1);
    bytes[
        symbol_file_offset +
        kElf64SymbolSize +
        4] =
            std::byte{0x12};
    bytes[
        symbol_file_offset +
        kElf64SymbolSize +
        5] =
            std::byte{0};
    write_u16(
        bytes,
        symbol_file_offset +
            kElf64SymbolSize +
            6,
        0);
    write_u64(
        bytes,
        symbol_file_offset +
            kElf64SymbolSize +
            8,
        0);
    write_u64(
        bytes,
        symbol_file_offset +
            kElf64SymbolSize +
            16,
        0);

    const auto rela_file_offset =
        data_file_offset +
        kRelaOffset;
    write_u64(
        bytes,
        rela_file_offset,
        got_address);
    write_u64(
        bytes,
        rela_file_offset + 8,
        (std::uint64_t{1} << 32U) |
            std::uint64_t{
                astraea::execution::
                    kX86_64JumpSlotRelocationType});
    write_i64(
        bytes,
        rela_file_offset + 16,
        0);

    std::size_t d = 0;
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtStrTab,
        data_base + kStringOffset);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtStrSz,
        2U +
            static_cast<std::uint64_t>(
                kRawImportName.size()));
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtSymTab,
        data_base + kSymbolOffset);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtSymEnt,
        kElf64SymbolSize);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtSymTabSz,
        2U * kElf64SymbolSize);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtJmpRel,
        data_base + kRelaOffset);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtPltRelSz,
        kElf64RelaSize);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtPltRel,
        static_cast<std::uint64_t>(
            kDtRela));
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtNull,
        0);
    REQUIRE(d == kDynamicEntryCount);

    return PrototypeFixture{
        .bytes = std::move(bytes),
        .code_base = code_base,
        .data_base = data_base,
        .stack_base = stack_base,
        .gate_base = gate_base,
        .page = page,
        .got_address = got_address,
    };
}

astraea::execution::HleRegistry
make_registry() {
    auto registry =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::
                    HleFunctionDescriptor>{
                {
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

#endif

}  // namespace

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "owned PS5 SCE-profile ELF resolves patched import and exits through HLE",
    "[execution][sce-prototype][m4]") {
    const auto fixture = make_fixture();

    auto generic =
        astraea::loader::build_guest_image(
            astraea::loader::GuestImageRequest{
                .image_bytes = fixture.bytes,
                .initial_stack =
                    make_stack_request(
                        fixture.stack_base,
                        fixture.page),
            });
    REQUIRE_FALSE(generic.has_value());
    REQUIRE(
        generic.error().code ==
        astraea::loader::
            GuestImageErrorCode::
                elf_parse_failure);

    auto built =
        astraea::loader::build_guest_image(
            astraea::loader::GuestImageRequest{
                .image_bytes = fixture.bytes,
                .initial_stack =
                    make_stack_request(
                        fixture.stack_base,
                        fixture.page),
                .elf_profile =
                    astraea::loader::
                        ElfParseProfile::ps5_sce,
            });
    REQUIRE(built.has_value());
    REQUIRE(
        built->elf.header.type ==
        kSceDynExecType);
    REQUIRE(built->dynamic_table.has_value());
    REQUIRE(built->dynamic_strings.has_value());
    REQUIRE(
        built->dynamic_strings->
            string_table.has_value());
    REQUIRE(built->dynamic_symbols.has_value());
    REQUIRE(built->plt_relocations.has_value());
    REQUIRE(
        built->plt_relocations->kind ==
        astraea::loader::
            RelocationTableKind::plt_rela);

    auto view =
        built->initialized_image_view();
    REQUIRE(view.has_value());

    auto symbol =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                *built->dynamic_symbols,
                *built->dynamic_strings->
                    string_table,
                1,
                view.value());
    REQUIRE(symbol.has_value());
    REQUIRE(
        symbol->name.raw ==
        kRawImportName);
    REQUIRE(symbol->name.identity.has_value());
    REQUIRE(
        symbol->name.identity->nid ==
        "ABCDEFGHIJK");
    REQUIRE(
        symbol->name.identity->library_id ==
        "library-a");
    REQUIRE(
        symbol->name.identity->module_id ==
        "module-a");

    auto relocation =
        astraea::loader::
            parse_dynamic_relocation(
                *built->plt_relocations,
                0,
                *built->dynamic_symbols,
                view.value());
    REQUIRE(relocation.has_value());
    REQUIRE(
        relocation->target ==
        astraea::memory::GuestAddress{
            fixture.got_address});
    REQUIRE(relocation->symbol_index == 1);
    REQUIRE(
        relocation->relocation_type ==
        astraea::execution::
            kX86_64JumpSlotRelocationType);

    auto registry = make_registry();

    const std::array<
        astraea::execution::
            SceImportBinding,
        0>
        no_bindings{};
    auto unresolved_bindings =
        astraea::execution::
            SceImportBindingRegistry::create(
                registry,
                no_bindings);
    REQUIRE(unresolved_bindings.has_value());

    auto unresolved_plan =
        astraea::execution::
            plan_sce_import_resolution(
                relocation.value(),
                symbol.value(),
                unresolved_bindings.value());
    REQUIRE_FALSE(unresolved_plan.has_value());
    REQUIRE(
        unresolved_plan.error().code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                unresolved_import);

    const std::array bindings{
        astraea::execution::
            SceImportBinding{
                .identity =
                    symbol->name.identity.value(),
                .function_id =
                    astraea::execution::
                        kSyntheticTestExitId,
            },
    };
    auto import_bindings =
        astraea::execution::
            SceImportBindingRegistry::create(
                registry,
                bindings);
    REQUIRE(import_bindings.has_value());

    auto plan =
        astraea::execution::
            plan_sce_import_resolution(
                relocation.value(),
                symbol.value(),
                import_bindings.value());
    REQUIRE(plan.has_value());
    REQUIRE(
        plan->function_id ==
        astraea::execution::
            kSyntheticTestExitId);

    auto gates =
        astraea::execution::
            build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{
                    fixture.gate_base},
                1,
                std::vector<
                    astraea::execution::
                        GateBinding>{
                    {
                        .slot = 0,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestExitId,
                    },
                },
                built.value());
    REQUIRE(gates.has_value());

    auto patch =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                plan.value(),
                gates.value(),
                0);
    REQUIRE(patch.has_value());
    REQUIRE(
        patch->relocation_target ==
        astraea::memory::GuestAddress{
            fixture.got_address});
    REQUIRE(
        patch->gate_destination ==
        astraea::memory::GuestAddress{
            fixture.gate_base});

    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                built.value());
    REQUIRE(prepared.has_value());

    astraea::execution::GuestMemoryAccess
        memory{
            built.value(),
            prepared.value()};

    auto applied =
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                patch.value(),
                memory);
    REQUIRE(applied.has_value());

    std::array<std::byte, 8> got_bytes{};
    auto read_back =
        memory.read(
            astraea::memory::GuestAddress{
                fixture.got_address},
            got_bytes);
    REQUIRE(read_back.has_value());
    REQUIRE(got_bytes == patch->bytes);

    std::uint64_t got_destination = 0;
    for (std::size_t index = 0;
         index < got_bytes.size();
         ++index) {
        got_destination |=
            static_cast<std::uint64_t>(
                std::to_integer<
                    std::uint8_t>(
                    got_bytes[index]))
            << (index * 8U);
    }
    REQUIRE(
        got_destination ==
        fixture.gate_base);

    auto session =
        astraea::execution::
            run_linux_synthetic_session(
                built.value(),
                prepared.value(),
                registry,
                gates.value(),
                astraea::execution::
                    make_synthetic_initial_context(
                        built.value()));
    REQUIRE(session.has_value());
    REQUIRE(session->exit_code == 42);
    REQUIRE(session->gate_stop_count == 1);
    REQUIRE(session->output.empty());
    REQUIRE(session->events.size() == 3);

    REQUIRE(
        session->events[0].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                guest_entry);
    REQUIRE(
        session->events[0].rip ==
        fixture.code_base);

    REQUIRE(
        session->events[1].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                gate_stop);
    REQUIRE(session->events[1].has_gate_slot);
    REQUIRE(session->events[1].gate_slot == 0);
    REQUIRE(
        session->events[1].rip ==
        fixture.gate_base);

    REQUIRE(
        session->events[2].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                hle_exit);
    REQUIRE(
        session->events[2].has_function_id);
    REQUIRE(
        session->events[2].function_id ==
        astraea::execution::
            kSyntheticTestExitId);
    REQUIRE(session->events[2].value == 42);
}

#endif
