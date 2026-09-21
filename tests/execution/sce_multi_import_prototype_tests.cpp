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
#include <string>
#include <type_traits>
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

constexpr std::size_t kGotWriteOffset = 0x40;
constexpr std::size_t kGotExitOffset = 0x48;
constexpr std::size_t kMessageOffset = 0x80;
constexpr std::size_t kStringOffset = 0x100;
constexpr std::size_t kSymbolOffset = 0x200;
constexpr std::size_t kRelaOffset = 0x300;
constexpr std::size_t kDynamicOffset = 0x400;
constexpr std::size_t kDynamicEntryCount = 9;
constexpr std::size_t kDynamicSize = kDynamicEntryCount * 16;

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

const std::string kWriteImport =
    "ABCDEFGHIJK#library-a#module-a";
const std::string kExitImport =
    "LMNOPQRSTUV#library-b#module-b";

struct MultiImportFixture {
    std::vector<std::byte> bytes;
    std::uint64_t code_base = 0;
    std::uint64_t data_base = 0;
    std::uint64_t stack_base = 0;
    std::uint64_t gate_base = 0;
    std::uint64_t page = 0;
    std::uint64_t got_write = 0;
    std::uint64_t got_exit = 0;
    std::uint64_t message_address = 0;
};

template <typename T>
void write_unsigned(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    T value) {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t index = 0; index < sizeof(T); ++index) {
        bytes[offset + index] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (index * 8U)) &
                0xffU);
    }
}

void write_i64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::int64_t value) {
    write_unsigned(
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
    write_unsigned(bytes, header_offset, type);
    write_unsigned(bytes, header_offset + 4, flags);
    write_unsigned(bytes, header_offset + 8, file_offset);
    write_unsigned(bytes, header_offset + 16, virtual_address);
    write_unsigned<std::uint64_t>(bytes, header_offset + 24, 0);
    write_unsigned(bytes, header_offset + 32, file_size);
    write_unsigned(bytes, header_offset + 40, memory_size);
    write_unsigned(bytes, header_offset + 48, alignment);
}

void write_dynamic(
    std::vector<std::byte>& bytes,
    std::size_t dynamic_file_offset,
    std::size_t index,
    std::int64_t tag,
    std::uint64_t value) {
    const auto offset = dynamic_file_offset + index * 16;
    write_i64(bytes, offset, tag);
    write_unsigned(bytes, offset + 8, value);
}

void append_u32(
    std::vector<std::byte>& bytes,
    std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_u64(
    std::vector<std::byte>& bytes,
    std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) & 0xffU)));
    }
}

void append_indirect_call(
    std::vector<std::byte>& code,
    std::uint64_t code_base,
    std::uint64_t target) {
    const auto next_rip =
        code_base +
        static_cast<std::uint64_t>(code.size()) +
        6U;
    const auto displacement =
        static_cast<std::int64_t>(target) -
        static_cast<std::int64_t>(next_rip);
    REQUIRE(
        displacement >=
        std::numeric_limits<std::int32_t>::min());
    REQUIRE(
        displacement <=
        std::numeric_limits<std::int32_t>::max());

    code.push_back(std::byte{0xff});
    code.push_back(std::byte{0x15});
    append_u32(
        code,
        static_cast<std::uint32_t>(
            static_cast<std::int32_t>(
                displacement)));
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
        .arguments = {"sce-multi-import"},
        .environment = {"ASTRAEA=1"},
        .auxiliary_vector = {},
    };
}

void write_symbol(
    std::vector<std::byte>& bytes,
    std::size_t symbol_file_offset,
    std::size_t index,
    std::uint32_t name_offset) {
    const auto offset =
        symbol_file_offset +
        index * kElf64SymbolSize;
    write_unsigned(bytes, offset, name_offset);
    bytes[offset + 4] = std::byte{0x12};
    bytes[offset + 5] = std::byte{0};
    write_unsigned<std::uint16_t>(bytes, offset + 6, 0);
    write_unsigned<std::uint64_t>(bytes, offset + 8, 0);
    write_unsigned<std::uint64_t>(bytes, offset + 16, 0);
}

void write_relocation(
    std::vector<std::byte>& bytes,
    std::size_t rela_file_offset,
    std::size_t index,
    std::uint64_t target,
    std::uint32_t symbol_index) {
    const auto offset =
        rela_file_offset +
        index * kElf64RelaSize;
    write_unsigned(bytes, offset, target);
    write_unsigned(
        bytes,
        offset + 8,
        (static_cast<std::uint64_t>(symbol_index) << 32U) |
            static_cast<std::uint64_t>(
                astraea::execution::
                    kX86_64JumpSlotRelocationType));
    write_i64(bytes, offset + 16, 0);
}

MultiImportFixture make_fixture() {
    const auto page = page_size();
    REQUIRE(
        page >=
        static_cast<std::uint64_t>(
            kDynamicOffset + kDynamicSize));
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

    write_unsigned<std::uint16_t>(
        bytes, 16, kSceDynExecType);
    write_unsigned<std::uint16_t>(bytes, 18, 62);
    write_unsigned<std::uint32_t>(bytes, 20, 1);
    write_unsigned(bytes, 24, code_base);
    write_unsigned<std::uint64_t>(
        bytes, 32, kProgramHeaderOffset);
    write_unsigned<std::uint16_t>(
        bytes, 52, kElfHeaderSize);
    write_unsigned<std::uint16_t>(
        bytes, 54, kProgramHeaderSize);
    write_unsigned<std::uint16_t>(
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
        kProgramHeaderOffset + kProgramHeaderSize,
        kPtLoad,
        kPfRead | kPfWrite,
        2U * page,
        data_base,
        page,
        page,
        page);

    const auto dynamic_file_offset =
        data_file_offset + kDynamicOffset;
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

    const auto got_write =
        data_base + kGotWriteOffset;
    const auto got_exit =
        data_base + kGotExitOffset;
    const auto message_address =
        data_base + kMessageOffset;

    std::vector<std::byte> code;

    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbf});
    append_u64(code, message_address);
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbe});
    append_u64(code, 2);
    append_indirect_call(
        code,
        code_base,
        got_write);

    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbf});
    append_u64(code, 42);
    append_indirect_call(
        code,
        code_base,
        got_exit);

    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    REQUIRE(code.size() < page);
    for (std::size_t index = 0;
         index < code.size();
         ++index) {
        bytes[code_file_offset + index] =
            code[index];
    }

    bytes[data_file_offset + kMessageOffset] =
        std::byte{'O'};
    bytes[data_file_offset + kMessageOffset + 1U] =
        std::byte{'K'};

    const auto string_file_offset =
        data_file_offset + kStringOffset;
    bytes[string_file_offset] = std::byte{0};

    const std::uint32_t write_name_offset = 1;
    const std::uint32_t exit_name_offset =
        write_name_offset +
        static_cast<std::uint32_t>(
            kWriteImport.size()) +
        1U;

    for (std::size_t index = 0;
         index < kWriteImport.size();
         ++index) {
        bytes[
            string_file_offset +
            write_name_offset +
            index] =
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        kWriteImport[index]));
    }
    bytes[
        string_file_offset +
        write_name_offset +
        kWriteImport.size()] =
            std::byte{0};

    for (std::size_t index = 0;
         index < kExitImport.size();
         ++index) {
        bytes[
            string_file_offset +
            exit_name_offset +
            index] =
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        kExitImport[index]));
    }
    bytes[
        string_file_offset +
        exit_name_offset +
        kExitImport.size()] =
            std::byte{0};

    const auto symbol_file_offset =
        data_file_offset + kSymbolOffset;
    write_symbol(
        bytes,
        symbol_file_offset,
        1,
        write_name_offset);
    write_symbol(
        bytes,
        symbol_file_offset,
        2,
        exit_name_offset);

    const auto rela_file_offset =
        data_file_offset + kRelaOffset;
    write_relocation(
        bytes,
        rela_file_offset,
        0,
        got_write,
        1);
    write_relocation(
        bytes,
        rela_file_offset,
        1,
        got_exit,
        2);

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
        static_cast<std::uint64_t>(
            exit_name_offset) +
            static_cast<std::uint64_t>(
                kExitImport.size()) +
            1U);
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
        3U * kElf64SymbolSize);
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
        2U * kElf64RelaSize);
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

    return MultiImportFixture{
        .bytes = std::move(bytes),
        .code_base = code_base,
        .data_base = data_base,
        .stack_base = stack_base,
        .gate_base = gate_base,
        .page = page,
        .got_write = got_write,
        .got_exit = got_exit,
        .message_address = message_address,
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
                            kSyntheticTestWriteId,
                    .canonical_name =
                        "astraea.test.write",
                    .argument_count = 2,
                },
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

std::uint64_t read_u64(
    const std::array<std::byte, 8>& bytes) {
    std::uint64_t value = 0;
    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[index]))
            << (index * 8U);
    }
    return value;
}

#endif

}  // namespace

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "owned SCE-profile ELF executes two exact imports with HLE resume",
    "[execution][sce-prototype][multi-import][m4]") {
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
    REQUIRE(built->dynamic_symbols.has_value());
    REQUIRE(built->dynamic_strings.has_value());
    REQUIRE(
        built->dynamic_strings->
            string_table.has_value());
    REQUIRE(built->plt_relocations.has_value());
    REQUIRE(built->plt_relocations->count == 2);

    auto view =
        built->initialized_image_view();
    REQUIRE(view.has_value());

    auto write_symbol =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                *built->dynamic_symbols,
                *built->dynamic_strings->
                    string_table,
                1,
                view.value());
    auto exit_symbol =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                *built->dynamic_symbols,
                *built->dynamic_strings->
                    string_table,
                2,
                view.value());

    REQUIRE(write_symbol.has_value());
    REQUIRE(exit_symbol.has_value());
    REQUIRE(write_symbol->name.raw == kWriteImport);
    REQUIRE(exit_symbol->name.raw == kExitImport);
    REQUIRE(write_symbol->name.identity.has_value());
    REQUIRE(exit_symbol->name.identity.has_value());
    REQUIRE(
        write_symbol->name.identity->nid ==
        "ABCDEFGHIJK");
    REQUIRE(
        write_symbol->name.identity->library_id ==
        "library-a");
    REQUIRE(
        write_symbol->name.identity->module_id ==
        "module-a");
    REQUIRE(
        exit_symbol->name.identity->nid ==
        "LMNOPQRSTUV");
    REQUIRE(
        exit_symbol->name.identity->library_id ==
        "library-b");
    REQUIRE(
        exit_symbol->name.identity->module_id ==
        "module-b");

    auto write_relocation =
        astraea::loader::parse_dynamic_relocation(
            *built->plt_relocations,
            0,
            *built->dynamic_symbols,
            view.value());
    auto exit_relocation =
        astraea::loader::parse_dynamic_relocation(
            *built->plt_relocations,
            1,
            *built->dynamic_symbols,
            view.value());

    REQUIRE(write_relocation.has_value());
    REQUIRE(exit_relocation.has_value());
    REQUIRE(write_relocation->symbol_index == 1);
    REQUIRE(exit_relocation->symbol_index == 2);
    REQUIRE(
        write_relocation->target ==
        astraea::memory::GuestAddress{
            fixture.got_write});
    REQUIRE(
        exit_relocation->target ==
        astraea::memory::GuestAddress{
            fixture.got_exit});

    auto registry = make_registry();
    const std::array bindings{
        astraea::execution::SceImportBinding{
            .identity =
                write_symbol->name.identity.value(),
            .function_id =
                astraea::execution::
                    kSyntheticTestWriteId,
        },
        astraea::execution::SceImportBinding{
            .identity =
                exit_symbol->name.identity.value(),
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

    auto write_plan =
        astraea::execution::
            plan_sce_import_resolution(
                write_relocation.value(),
                write_symbol.value(),
                import_bindings.value());
    auto exit_plan =
        astraea::execution::
            plan_sce_import_resolution(
                exit_relocation.value(),
                exit_symbol.value(),
                import_bindings.value());

    REQUIRE(write_plan.has_value());
    REQUIRE(exit_plan.has_value());
    REQUIRE(
        write_plan->function_id ==
        astraea::execution::
            kSyntheticTestWriteId);
    REQUIRE(
        exit_plan->function_id ==
        astraea::execution::
            kSyntheticTestExitId);

    auto gates =
        astraea::execution::
            build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{
                    fixture.gate_base},
                2,
                std::vector<
                    astraea::execution::
                        GateBinding>{
                    {
                        .slot = 0,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestWriteId,
                    },
                    {
                        .slot = 1,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestExitId,
                    },
                },
                built.value());
    REQUIRE(gates.has_value());

    auto write_patch =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                write_plan.value(),
                gates.value(),
                0);
    auto exit_patch =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                exit_plan.value(),
                gates.value(),
                1);

    REQUIRE(write_patch.has_value());
    REQUIRE(exit_patch.has_value());
    REQUIRE(
        write_patch->gate_destination ==
        astraea::memory::GuestAddress{
            fixture.gate_base});
    REQUIRE(
        exit_patch->gate_destination ==
        astraea::memory::GuestAddress{
            fixture.gate_base +
            astraea::execution::
                kSyntheticGateStride});

    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                built.value());
    REQUIRE(prepared.has_value());

    astraea::execution::GuestMemoryAccess memory{
        built.value(),
        prepared.value()};

    REQUIRE(
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                write_patch.value(),
                memory)
            .has_value());
    REQUIRE(
        astraea::execution::
            apply_synthetic_jump_slot_patch(
                exit_patch.value(),
                memory)
            .has_value());

    std::array<std::byte, 8> write_got{};
    std::array<std::byte, 8> exit_got{};
    REQUIRE(
        memory.read(
            astraea::memory::GuestAddress{
                fixture.got_write},
            write_got)
            .has_value());
    REQUIRE(
        memory.read(
            astraea::memory::GuestAddress{
                fixture.got_exit},
            exit_got)
            .has_value());
    REQUIRE(
        read_u64(write_got) ==
        fixture.gate_base);
    REQUIRE(
        read_u64(exit_got) ==
        fixture.gate_base +
            astraea::execution::
                kSyntheticGateStride);

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
    REQUIRE(session->gate_stop_count == 2);
    REQUIRE(
        session->output ==
        std::vector<std::byte>{
            std::byte{'O'},
            std::byte{'K'},
        });

    REQUIRE(session->events.size() == 6);
    REQUIRE(
        session->events[0].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                guest_entry);
    REQUIRE(
        session->events[1].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                gate_stop);
    REQUIRE(
        session->events[1].function_id ==
        astraea::execution::
            kSyntheticTestWriteId);
    REQUIRE(session->events[1].gate_slot == 0);

    REQUIRE(
        session->events[2].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                hle_resume);
    REQUIRE(
        session->events[2].function_id ==
        astraea::execution::
            kSyntheticTestWriteId);
    REQUIRE(session->events[2].value == 2);

    REQUIRE(
        session->events[3].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                guest_entry);
    REQUIRE(
        session->events[3].rip ==
        session->events[2].rip);

    REQUIRE(
        session->events[4].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                gate_stop);
    REQUIRE(
        session->events[4].function_id ==
        astraea::execution::
            kSyntheticTestExitId);
    REQUIRE(session->events[4].gate_slot == 1);

    REQUIRE(
        session->events[5].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                hle_exit);
    REQUIRE(
        session->events[5].function_id ==
        astraea::execution::
            kSyntheticTestExitId);
    REQUIRE(session->events[5].value == 42);
}

#endif
