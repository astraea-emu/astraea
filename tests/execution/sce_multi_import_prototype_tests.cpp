#include <astraea/execution/module_graph.hpp>
#include <astraea/execution/module_graph_import_apply.hpp>
#include <astraea/execution/module_graph_import_patch.hpp>
#include <astraea/execution/module_graph_import_plan.hpp>
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
        page * 5U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 5U));
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


TEST_CASE(
    "independently validated client and provider ELFs execute across owned mapping boundary",
    "[execution][c1][owned-provider-bridge]") {
    // Two independent, source-authored SCE-profile ELF byte streams
    // are parsed separately before a test-only view joins their disjoint
    // file-backed mappings. This does not implement a retail module loader,
    // infer Sony provider identities, or authorize commercial entry.
    const auto fixture = make_fixture();
    const auto provider_address = fixture.gate_base + fixture.page;
    const auto exit_gate_address =
        fixture.gate_base +
        astraea::execution::kSyntheticGateStride;
    REQUIRE(fixture.page <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max() / 2U));
    REQUIRE(fixture.page >= kElfHeaderSize + kProgramHeaderSize);

    // Authored provider entry: movabs rax, synthetic_exit_gate; jmp rax.
    // The client will reach this executable page only through a checked
    // graph-resolved GOT JUMP_SLOT. It is not a Sony runtime module.
    std::vector<std::byte> provider_code{
        std::byte{0x48}, std::byte{0xb8},
    };
    append_u64(provider_code, exit_gate_address);
    provider_code.push_back(std::byte{0xff});
    provider_code.push_back(std::byte{0xe0});
    REQUIRE(provider_code.size() == 12U);

    std::vector<std::byte> provider_artifact(
        static_cast<std::size_t>(fixture.page * 2U), std::byte{0});
    provider_artifact[0] = std::byte{0x7f};
    provider_artifact[1] = std::byte{'E'};
    provider_artifact[2] = std::byte{'L'};
    provider_artifact[3] = std::byte{'F'};
    provider_artifact[4] = std::byte{2};
    provider_artifact[5] = std::byte{1};
    provider_artifact[6] = std::byte{1};
    write_unsigned<std::uint16_t>(
        provider_artifact, 16, kSceDynExecType);
    write_unsigned<std::uint16_t>(provider_artifact, 18, 62);
    write_unsigned<std::uint32_t>(provider_artifact, 20, 1);
    write_unsigned(provider_artifact, 24, provider_address);
    write_unsigned<std::uint64_t>(
        provider_artifact, 32, kProgramHeaderOffset);
    write_unsigned<std::uint16_t>(
        provider_artifact, 52, kElfHeaderSize);
    write_unsigned<std::uint16_t>(
        provider_artifact, 54, kProgramHeaderSize);
    write_unsigned<std::uint16_t>(
        provider_artifact, 56, 1);
    write_program_header(
        provider_artifact, kProgramHeaderOffset, kPtLoad,
        kPfRead | kPfExecute, fixture.page,
        provider_address, fixture.page, fixture.page, fixture.page);
    for (std::size_t i = 0; i < provider_code.size(); ++i) {
        provider_artifact[
            static_cast<std::size_t>(fixture.page) + i] =
            provider_code[i];
    }

    std::vector<astraea::execution::SyntheticSessionEvent>
        first_events;

    for (unsigned iteration = 0; iteration < 2U; ++iteration) {
        auto built = astraea::loader::build_guest_image({
            .image_bytes = fixture.bytes,
            .initial_stack =
                make_stack_request(fixture.stack_base, fixture.page),
            .elf_profile = astraea::loader::ElfParseProfile::ps5_sce,
        });
        REQUIRE(built.has_value());
        REQUIRE(built->dynamic_symbols.has_value());
        REQUIRE(built->dynamic_strings.has_value());
        REQUIRE(built->dynamic_strings->string_table.has_value());
        REQUIRE(built->plt_relocations.has_value());

        // The provider is a *second* structurally validated ELF artifact,
        // never a function secretly placed in the client's ELF bytes.
        auto provider = astraea::loader::build_guest_image({
            .image_bytes = provider_artifact,
            .initial_stack =
                make_stack_request(fixture.stack_base, fixture.page),
            .elf_profile = astraea::loader::ElfParseProfile::ps5_sce,
        });
        REQUIRE(provider.has_value());
        REQUIRE(provider->elf.header.entry == provider_address);
        REQUIRE(provider->mappings.size() == 1U);
        REQUIRE_FALSE(provider->dynamic_table.has_value());
        REQUIRE_FALSE(provider->tls.has_value());

        auto view = built->initialized_image_view();
        REQUIRE(view.has_value());
        auto write_symbol =
            astraea::loader::materialize_sce_dynamic_symbol(
                *built->dynamic_symbols,
                *built->dynamic_strings->string_table,
                1U, view.value());
        auto exit_symbol =
            astraea::loader::materialize_sce_dynamic_symbol(
                *built->dynamic_symbols,
                *built->dynamic_strings->string_table,
                2U, view.value());
        REQUIRE(write_symbol.has_value());
        REQUIRE(exit_symbol.has_value());
        REQUIRE(exit_symbol->name.identity.has_value());

        auto write_relocation =
            astraea::loader::parse_dynamic_relocation(
                *built->plt_relocations, 0U,
                *built->dynamic_symbols, view.value());
        auto exit_relocation =
            astraea::loader::parse_dynamic_relocation(
                *built->plt_relocations, 1U,
                *built->dynamic_symbols, view.value());
        REQUIRE(write_relocation.has_value());
        REQUIRE(exit_relocation.has_value());

        // Fail closed when an export is declared but its independent
        // executable mapping has not been staged by the owner.
        {
            auto client_only =
                astraea::execution::prepare_linux_guest_memory(
                    built.value());
            REQUIRE(client_only.has_value());
            astraea::execution::GuestMemoryAccess client_memory{
                built.value(), client_only.value()};
            REQUIRE_FALSE(client_memory.is_exact_executable_address(
                astraea::memory::GuestAddress{provider_address}));
        }

        // Research-only staging adapter: copy the provider's already
        // validated, disjoint PT_LOAD file backing into the test image.
        // No Sony dependencies, separate runtime, or general relocation
        // ordering semantics are invented here.
        auto provider_mapping = provider->mappings.front();
        REQUIRE(provider_mapping.range.base() ==
            astraea::memory::GuestAddress{provider_address});
        REQUIRE(provider_mapping.backing.kind ==
            astraea::memory::MappingBackingKind::file);
        REQUIRE_FALSE(provider_mapping.range.overlaps(
            built->initial_stack.storage));
        for (const auto& mapping : built->mappings) {
            REQUIRE_FALSE(provider_mapping.range.overlaps(
                mapping.range));
        }
        const auto appended_at = built->image_bytes.size();
        REQUIRE(provider->image_bytes.size() <=
            std::numeric_limits<std::size_t>::max() - appended_at);
        REQUIRE(provider_mapping.backing.file_offset <=
            std::numeric_limits<std::uint64_t>::max() -
                static_cast<std::uint64_t>(appended_at));
        provider_mapping.backing.file_offset +=
            static_cast<std::uint64_t>(appended_at);
        provider_mapping.source_index +=
            built->elf.program_headers.size();
        built->image_bytes.insert(
            built->image_bytes.end(),
            provider->image_bytes.begin(),
            provider->image_bytes.end());
        built->mappings.push_back(provider_mapping);
        auto staged_view = built->initialized_image_view();
        REQUIRE(staged_view.has_value());
        auto provider_first_byte =
            staged_view->read_byte(
                astraea::memory::GuestAddress{provider_address});
        REQUIRE(provider_first_byte.has_value());
        REQUIRE(provider_first_byte.value() == std::byte{0x48});

        const std::array modules{
            astraea::execution::ModuleGraphDeclaration{
                .module_key = "client",
                .dependency_keys = {"owned-provider"},
                .exports = {},
            },
            astraea::execution::ModuleGraphDeclaration{
                .module_key = "owned-provider",
                .dependency_keys = {},
                .exports = {
                    astraea::execution::ModuleGraphExport{
                        .identity = exit_symbol->name.identity.value(),
                        .guest_address =
                            astraea::memory::GuestAddress{provider_address},
                    },
                },
            },
        };
        auto graph = astraea::execution::ModuleGraph::create(modules);
        REQUIRE(graph.has_value());

        // The client and provider have each passed their independent ELF
        // parser. The exact same live export identities must also survive
        // module registration. Conflicts are rejected before patching.
        {
            auto duplicate = modules;
            duplicate[1].module_key = "client";
            auto refused =
                astraea::execution::ModuleGraph::create(duplicate);
            REQUIRE_FALSE(refused.has_value());
            REQUIRE(refused.error().code ==
                astraea::execution::ModuleGraphErrorCode::
                    duplicate_module_key);
        }
        {
            auto duplicate_export = modules;
            duplicate_export[1].exports.push_back(
                duplicate_export[1].exports.front());
            auto refused =
                astraea::execution::ModuleGraph::create(duplicate_export);
            REQUIRE_FALSE(refused.has_value());
            REQUIRE(refused.error().code ==
                astraea::execution::ModuleGraphErrorCode::
                    duplicate_export_identity);
        }
        {
            auto mismatched_symbol = exit_symbol.value();
            REQUIRE(mismatched_symbol.name.identity.has_value());
            mismatched_symbol.name.identity->nid = "ZZZZZZZZZZZ";
            auto refused =
                astraea::execution::plan_module_graph_import(
                    exit_relocation.value(), mismatched_symbol,
                    graph.value(), "client", "owned-provider");
            REQUIRE_FALSE(refused.has_value());
            REQUIRE(refused.error().code ==
                astraea::execution::ModuleGraphImportPlanErrorCode::
                    module_resolution_failure);
            REQUIRE(refused.error().resolution_failure ==
                astraea::execution::ModuleGraphResolutionKind::
                    unresolved_export);
        }
        {
            auto mismatched_relocation = exit_relocation.value();
            ++mismatched_relocation.symbol_index;
            auto refused =
                astraea::execution::plan_module_graph_import(
                    mismatched_relocation, exit_symbol.value(),
                    graph.value(), "client", "owned-provider");
            REQUIRE_FALSE(refused.has_value());
            REQUIRE(refused.error().code ==
                astraea::execution::ModuleGraphImportPlanErrorCode::
                    symbol_index_mismatch);
        }

        auto import_plan =
            astraea::execution::plan_module_graph_import(
                exit_relocation.value(), exit_symbol.value(),
                graph.value(), "client", "owned-provider");
        REQUIRE(import_plan.has_value());
        auto owned_patch =
            astraea::execution::build_owned_x86_64_module_import_patch(
                import_plan.value());
        REQUIRE(owned_patch.has_value());
        REQUIRE(owned_patch->source_symbol_address ==
            astraea::memory::GuestAddress{provider_address});

        // A structurally valid import plan does NOT grant blanket
        // relocation or symbol-binding permission. Reject both before
        // preparing or mutating any native guest memory.
        {
            auto unsupported = import_plan.value();
            unsupported.raw_relocation_type = 0xffffU;
            auto refused =
                astraea::execution::build_owned_x86_64_module_import_patch(
                    unsupported);
            REQUIRE_FALSE(refused.has_value());
            REQUIRE(refused.error().code ==
                astraea::execution::OwnedModuleAbsolutePatchErrorCode::
                    unsupported_relocation_type);
        }
        {
            auto unsupported = import_plan.value();
            unsupported.symbol_binding = 0U;
            auto refused =
                astraea::execution::build_owned_x86_64_module_import_patch(
                    unsupported);
            REQUIRE_FALSE(refused.has_value());
            REQUIRE(refused.error().code ==
                astraea::execution::OwnedModuleAbsolutePatchErrorCode::
                    unsupported_symbol_binding);
        }

        // Exact mismatches refuse to resolve before any memory mutation.
        const auto unresolved =
            astraea::execution::plan_module_graph_import(
                exit_relocation.value(), exit_symbol.value(),
                graph.value(), "client", "undeclared-provider");
        REQUIRE_FALSE(unresolved.has_value());
        REQUIRE(unresolved.error().resolution_failure ==
            astraea::execution::ModuleGraphResolutionKind::
                undeclared_dependency);

        auto registry = make_registry();
        const std::array bindings{
            astraea::execution::SceImportBinding{
                .identity = write_symbol->name.identity.value(),
                .function_id =
                    astraea::execution::kSyntheticTestWriteId,
            },
        };
        auto import_bindings =
            astraea::execution::SceImportBindingRegistry::create(
                registry, bindings);
        REQUIRE(import_bindings.has_value());
        auto write_plan =
            astraea::execution::plan_sce_import_resolution(
                write_relocation.value(), write_symbol.value(),
                import_bindings.value());
        REQUIRE(write_plan.has_value());

        auto gates = astraea::execution::build_synthetic_gate_region(
            registry,
            astraea::memory::GuestAddress{fixture.gate_base},
            2U,
            std::vector<astraea::execution::GateBinding>{
                { .slot = 0U,
                  .function_id =
                      astraea::execution::kSyntheticTestWriteId },
                { .slot = 1U,
                  .function_id =
                      astraea::execution::kSyntheticTestExitId },
            },
            built.value());
        REQUIRE(gates.has_value());
        auto write_patch =
            astraea::execution::build_synthetic_x86_64_jump_slot_patch(
                write_plan.value(), gates.value(), 0U);
        REQUIRE(write_patch.has_value());

        auto prepared =
            astraea::execution::prepare_linux_guest_memory(built.value());
        REQUIRE(prepared.has_value());
        astraea::execution::GuestMemoryAccess memory{
            built.value(), prepared.value()};

        // Require a mapped, executable provider before admitting its
        // jump target. Graph resolution alone does not prove lifetime.
        REQUIRE(memory.is_exact_executable_address(
            astraea::memory::GuestAddress{provider_address}));

        // The graph holds valid *declarations*, not executable lifetime.
        // A data address can be an ELF address, but cannot be used as a
        // callable JUMP_SLOT provider in the live owned integration.
        {
            auto non_executable = owned_patch.value();
            non_executable.source_symbol_address =
                astraea::memory::GuestAddress{fixture.data_base};
            const auto address = fixture.data_base;
            for (std::size_t byte_index = 0U;
                 byte_index < non_executable.bytes.size();
                 ++byte_index) {
                non_executable.bytes[byte_index] =
                    static_cast<std::byte>(
                        (address >> (8U * byte_index)) & 0xffU);
            }
            const std::array invalid_sources{
                owned_patch.value(), non_executable,
            };
            const auto denied =
                astraea::execution::apply_live_owned_jump_slot_batch(
                    invalid_sources, memory);
            REQUIRE_FALSE(denied.has_value());
            REQUIRE(denied.error().code ==
                astraea::execution::OwnedLiveJumpSlotErrorCode::
                    provider_not_live_executable);
            REQUIRE(denied.error().patch_index == 1U);
            REQUIRE(denied.error().applied_count == 0U);
        }

        {
            auto corrupt_encoding = owned_patch.value();
            corrupt_encoding.bytes[0] ^= std::byte{0x01};
            const std::array invalid_patches{corrupt_encoding};
            const auto denied =
                astraea::execution::apply_live_owned_jump_slot_batch(
                    invalid_patches, memory);
            REQUIRE_FALSE(denied.has_value());
            REQUIRE(denied.error().code ==
                astraea::execution::OwnedLiveJumpSlotErrorCode::
                    invalid_patch_encoding);
            REQUIRE(denied.error().applied_count == 0U);
        }
        {
            auto wrong_type = owned_patch.value();
            wrong_type.raw_relocation_type = 6U;
            const std::array invalid_patches{wrong_type};
            const auto denied =
                astraea::execution::apply_live_owned_jump_slot_batch(
                    invalid_patches, memory);
            REQUIRE_FALSE(denied.has_value());
            REQUIRE(denied.error().code ==
                astraea::execution::OwnedLiveJumpSlotErrorCode::
                    unsupported_relocation_type);
            REQUIRE(denied.error().applied_count == 0U);
        }

        std::array<std::byte, 8> before{};
        REQUIRE(memory.read(
            astraea::memory::GuestAddress{fixture.got_exit},
            before).has_value());
        REQUIRE(read_u64(before) == 0U);

        // A read-only guest instruction address is never a valid
        // relocation target; preflight must leave the real GOT unchanged.
        auto invalid_patch = owned_patch.value();
        invalid_patch.target =
            astraea::memory::GuestAddress{fixture.code_base};
        const std::array invalid_batch{invalid_patch};
        auto rejected =
            astraea::execution::apply_owned_module_import_batch(
                invalid_batch, memory);
        REQUIRE_FALSE(rejected.has_value());
        REQUIRE(rejected.error().code ==
            astraea::execution::OwnedModuleImportBatchErrorCode::
                preflight_failure);
        REQUIRE(rejected.error().applied_count == 0U);

        // Importantly, the first batch member is valid, but the second
        // deliberately targets a read-only code page. Full preflight must
        // refuse the *entire* batch, not partially apply the first one.
        const std::array mixed_batch{
            owned_patch.value(), invalid_patch,
        };
        const auto mixed =
            astraea::execution::apply_owned_module_import_batch(
                mixed_batch, memory);
        REQUIRE_FALSE(mixed.has_value());
        REQUIRE(mixed.error().code ==
            astraea::execution::OwnedModuleImportBatchErrorCode::
                preflight_failure);
        REQUIRE(mixed.error().patch_index == 1U);
        REQUIRE(mixed.error().applied_count == 0U);

        // Two patches to the same GOT target cannot silently overwrite
        // each other even when their byte values happen to match.
        const std::array overlapping{
            owned_patch.value(), owned_patch.value(),
        };
        const auto conflict =
            astraea::execution::apply_owned_module_import_batch(
                overlapping, memory);
        REQUIRE_FALSE(conflict.has_value());
        REQUIRE(conflict.error().code ==
            astraea::execution::OwnedModuleImportBatchErrorCode::
                conflicting_target);
        REQUIRE(conflict.error().applied_count == 0U);

        std::array<std::byte, 8> still_zero{};
        REQUIRE(memory.read(
            astraea::memory::GuestAddress{fixture.got_exit},
            still_zero).has_value());
        REQUIRE(still_zero == before);

        // Apply the exact caller-authorized provider and the separately
        // authorized synthetic write gate. No generic Sony HLE fallback.
        const std::array provider_batch{owned_patch.value()};
        auto applied =
            astraea::execution::apply_live_owned_jump_slot_batch(
                provider_batch, memory);
        REQUIRE(applied.has_value());
        REQUIRE(applied->size() == 1U);
        REQUIRE(astraea::execution::apply_synthetic_jump_slot_patch(
            write_patch.value(), memory).has_value());

        std::array<std::byte, 8> patched{};
        REQUIRE(memory.read(
            astraea::memory::GuestAddress{fixture.got_exit},
            patched).has_value());
        REQUIRE(read_u64(patched) == provider_address);

        auto session = astraea::execution::run_linux_synthetic_session(
            built.value(), prepared.value(), registry, gates.value(),
            astraea::execution::make_synthetic_initial_context(
                built.value()));
        REQUIRE(session.has_value());
        REQUIRE(session->exit_code == 42U);
        REQUIRE(session->gate_stop_count == 2U);
        REQUIRE(session->output == std::vector<std::byte>{
            std::byte{'O'}, std::byte{'K'},
        });
        REQUIRE(session->events.size() == 6U);
        REQUIRE(session->events[4].kind ==
            astraea::execution::SyntheticSessionEventKind::gate_stop);
        REQUIRE(session->events[4].function_id ==
            astraea::execution::kSyntheticTestExitId);

        if (iteration == 0U) {
            first_events = session->events;
        } else {
            REQUIRE(session->events == first_events);
        }

        // A transferred/moved-from prepared mapping owner must not
        // leave a still-admissible executable export in the old facade.
        auto transferred = std::move(prepared.value());
        REQUIRE_FALSE(memory.is_exact_executable_address(
            astraea::memory::GuestAddress{provider_address}));
        astraea::execution::GuestMemoryAccess active{
            built.value(), transferred};
        REQUIRE(active.is_exact_executable_address(
            astraea::memory::GuestAddress{provider_address}));

        // Explicitly release/unmap the current prepared memory owner.
        // The data-only module graph can still report the old export,
        // but the owned callable import gate must now refuse to patch.
        transferred = astraea::execution::LinuxPreparedMemory{};
        REQUIRE_FALSE(active.is_exact_executable_address(
            astraea::memory::GuestAddress{provider_address}));
        const auto stale = graph->resolve(
            "client", "owned-provider",
            exit_symbol->name.identity.value());
        REQUIRE(stale.kind ==
            astraea::execution::ModuleGraphResolutionKind::resolved);
        const auto after_release =
            astraea::execution::apply_live_owned_jump_slot_batch(
                provider_batch, active);
        REQUIRE_FALSE(after_release.has_value());
        REQUIRE(after_release.error().code ==
            astraea::execution::OwnedLiveJumpSlotErrorCode::
                provider_not_live_executable);
        REQUIRE(after_release.error().applied_count == 0U);
        REQUIRE_FALSE(active.preflight_write(
            astraea::memory::GuestAddress{fixture.got_exit},
            8U).has_value());
    }
}

#endif
