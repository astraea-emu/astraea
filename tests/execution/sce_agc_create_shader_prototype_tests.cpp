#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_execution.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/sce_agc_create_shader.hpp>
#include <astraea/execution/sce_import_binding.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/execution/sce_jump_slot_apply.hpp>
#include <astraea/execution/sce_jump_slot_patch.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/dynamic_string.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/guest_address.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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

constexpr std::size_t kGotCreateOffset = 0x40;
constexpr std::size_t kStringOffset = 0x100;
constexpr std::size_t kSymbolOffset = 0x200;
constexpr std::size_t kRelaOffset = 0x240;
constexpr std::size_t kDynamicOffset = 0x280;
constexpr std::size_t kOutputOffset = 0x400;
constexpr std::size_t kShaderHeaderOffset = 0x500;
constexpr std::size_t kShaderTextOffset = 0x800;

constexpr std::size_t kShaderHeaderSize = 0x160;
constexpr std::size_t kShaderTextSize = 0x80;
constexpr std::size_t kContextRegistersOffset = 0xc8;
constexpr std::size_t kShaderRegistersOffset = 0x98;
constexpr std::size_t kUserDataOffset = 0x110;
constexpr std::size_t kDirectResourceOffset = 0x148;

constexpr std::size_t kDynamicEntryCount = 13;
constexpr std::size_t kDynamicSize =
    kDynamicEntryCount * 16;

constexpr std::uint16_t kSceDynExecType = 0xfe10;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;
constexpr std::uint32_t kPfExecute = 0x1;
constexpr std::uint32_t kPfWrite = 0x2;
constexpr std::uint32_t kPfRead = 0x4;

constexpr std::int64_t kDtNull = 0;
constexpr std::int64_t kDtNeeded = 1;
constexpr std::int64_t kDtPltRelSz = 2;
constexpr std::int64_t kDtStrTab = 5;
constexpr std::int64_t kDtSymTab = 6;
constexpr std::int64_t kDtRela = 7;
constexpr std::int64_t kDtStrSz = 10;
constexpr std::int64_t kDtSymEnt = 11;
constexpr std::int64_t kDtPltRel = 20;
constexpr std::int64_t kDtJmpRel = 23;
constexpr std::int64_t kDtSymTabSz = 39;
constexpr std::int64_t kDtSceImportLibraryAttributes =
    0x61000019;
constexpr std::int64_t kDtSceNeededModule =
    0x61000045;
constexpr std::int64_t kDtSceImportLibrary =
    0x61000049;

constexpr std::uint64_t kElf64SymbolSize = 24;
constexpr std::uint64_t kElf64RelaSize = 24;
constexpr std::uint16_t kAgcModuleVersion = 0x0101;
constexpr std::uint16_t kAgcLibraryVersion = 0x0001;
constexpr std::uint64_t kAgcModuleId = 1;
constexpr std::uint64_t kAgcLibraryId = 0;

const std::string kAgcImport =
    "f3dg2CSgRKY#A#B";
const std::string kAgcSoname =
    "libSceAgc.prx";
const std::string kAgcModuleName =
    "libSceAgc";

struct AgcPrototypeFixture {
    std::vector<std::byte> bytes;
    std::uint64_t code_base = 0;
    std::uint64_t data_base = 0;
    std::uint64_t stack_base = 0;
    std::uint64_t gate_base = 0;
    std::uint64_t page = 0;
    std::uint64_t got_create = 0;
    std::uint64_t output_address = 0;
    std::uint64_t shader_header_address = 0;
    std::uint64_t shader_text_address = 0;
    std::uint64_t after_create_rip = 0;
    std::uint64_t sce_needed_module_value = 0;
    std::uint64_t sce_import_library_value = 0;
    std::uint64_t sce_import_library_attributes = 0;
};

template <typename T>
void write_unsigned(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    T value) {
    static_assert(std::is_unsigned_v<T>);

    for (std::size_t index = 0;
         index < sizeof(T);
         ++index) {
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
    write_unsigned(
        bytes,
        header_offset + 4,
        flags);
    write_unsigned(
        bytes,
        header_offset + 8,
        file_offset);
    write_unsigned(
        bytes,
        header_offset + 16,
        virtual_address);
    write_unsigned<std::uint64_t>(
        bytes,
        header_offset + 24,
        0);
    write_unsigned(
        bytes,
        header_offset + 32,
        file_size);
    write_unsigned(
        bytes,
        header_offset + 40,
        memory_size);
    write_unsigned(
        bytes,
        header_offset + 48,
        alignment);
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
    write_unsigned(
        bytes,
        offset + 8,
        value);
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

void append_mov_rdx_imm64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xba});
    append_u64(code, value);
}

void append_indirect_call(
    std::vector<std::byte>& code,
    std::uint64_t code_base,
    std::uint64_t target) {
    const auto next_rip =
        code_base +
        static_cast<std::uint64_t>(
            code.size()) +
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

void append_call_rel32(
    std::vector<std::byte>& code,
    std::uint64_t code_base,
    std::uint64_t target) {
    const auto next_rip =
        code_base +
        static_cast<std::uint64_t>(
            code.size()) +
        5U;
    const auto displacement =
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
        .arguments = {"sce-agc-v1"},
        .environment = {"ASTRAEA=1"},
        .auxiliary_vector = {},
    };
}

std::uint32_t write_string(
    std::vector<std::byte>& bytes,
    std::size_t table_file_offset,
    std::uint32_t cursor,
    const std::string& value) {
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        bytes[
            table_file_offset +
            static_cast<std::size_t>(cursor) +
            index] =
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        value[index]));
    }
    bytes[
        table_file_offset +
        static_cast<std::size_t>(cursor) +
        value.size()] =
            std::byte{0};

    return cursor +
           static_cast<std::uint32_t>(
               value.size()) +
           1U;
}

void make_public_shape_shader(
    std::vector<std::byte>& header,
    std::vector<std::byte>& text) {
    header.assign(
        kShaderHeaderSize,
        std::byte{0});
    text.assign(
        kShaderTextSize,
        std::byte{0});

    write_unsigned<std::uint32_t>(
        header,
        0,
        0x34333231U);
    write_unsigned<std::uint32_t>(
        header,
        4,
        0x18U);

    write_unsigned<std::uint64_t>(
        header,
        0x08,
        kUserDataOffset - 0x08U);
    write_unsigned<std::uint64_t>(
        header,
        0x18,
        kContextRegistersOffset - 0x18U);
    write_unsigned<std::uint64_t>(
        header,
        0x20,
        kShaderRegistersOffset - 0x20U);
    write_unsigned<std::uint64_t>(
        header,
        0x28,
        0x60U - 0x28U);
    write_unsigned<std::uint64_t>(
        header,
        0x30,
        0x90U - 0x30U);

    write_unsigned<std::uint32_t>(
        header,
        0x40,
        static_cast<std::uint32_t>(
            header.size()));
    write_unsigned<std::uint32_t>(
        header,
        0x44,
        static_cast<std::uint32_t>(
            text.size()));
    header[0x5a] = std::byte{1};
    header[0x5b] = std::byte{1};
    header[0x5c] = std::byte{2};

    write_unsigned<std::uint16_t>(
        header,
        kShaderRegistersOffset,
        0x0008U);
    write_unsigned<std::uint32_t>(
        header,
        kShaderRegistersOffset + 4U,
        0xaaaaaaaaU);
    write_unsigned<std::uint16_t>(
        header,
        kShaderRegistersOffset + 8U,
        0x0009U);
    write_unsigned<std::uint32_t>(
        header,
        kShaderRegistersOffset + 12U,
        0xbbbbbbbbU);

    write_unsigned<std::uint16_t>(
        header,
        kContextRegistersOffset,
        0x01c4U);
    write_unsigned<std::uint32_t>(
        header,
        kContextRegistersOffset + 4U,
        0x00000004U);

    write_unsigned<std::uint64_t>(
        header,
        kUserDataOffset + 0x00U,
        kDirectResourceOffset -
            (kUserDataOffset + 0x00U));
    write_unsigned<std::uint64_t>(
        header,
        kUserDataOffset + 0x08U,
        kShaderHeaderSize -
            (kUserDataOffset + 0x08U));
    write_unsigned<std::uint64_t>(
        header,
        kUserDataOffset + 0x10U,
        kShaderHeaderSize -
            (kUserDataOffset + 0x10U));
    write_unsigned<std::uint64_t>(
        header,
        kUserDataOffset + 0x18U,
        kShaderHeaderSize -
            (kUserDataOffset + 0x18U));
    write_unsigned<std::uint64_t>(
        header,
        kUserDataOffset + 0x20U,
        kShaderHeaderSize -
            (kUserDataOffset + 0x20U));

    header[0x70] = std::byte{0x5a};

    write_unsigned<std::uint32_t>(
        text,
        0,
        0xbf800000U);
    write_unsigned<std::uint32_t>(
        text,
        4,
        0xbf810000U);
    const auto trailer =
        text.size() - 0x30U;
    write_unsigned<std::uint32_t>(
        text,
        trailer + 0x14U,
        8U);
    write_unsigned<std::uint32_t>(
        text,
        trailer + 0x1cU,
        0U);
}

AgcPrototypeFixture make_fixture() {
    const auto page = page_size();
    REQUIRE(
        page >=
        static_cast<std::uint64_t>(
            kShaderTextOffset +
            kShaderTextSize));
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
        bytes,
        16,
        kSceDynExecType);
    write_unsigned<std::uint16_t>(
        bytes,
        18,
        62);
    write_unsigned<std::uint32_t>(
        bytes,
        20,
        1);
    write_unsigned(
        bytes,
        24,
        code_base);
    write_unsigned<std::uint64_t>(
        bytes,
        32,
        kProgramHeaderOffset);
    write_unsigned<std::uint16_t>(
        bytes,
        52,
        kElfHeaderSize);
    write_unsigned<std::uint16_t>(
        bytes,
        54,
        kProgramHeaderSize);
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

    const auto got_create =
        data_base + kGotCreateOffset;
    const auto output_address =
        data_base + kOutputOffset;
    const auto shader_header_address =
        data_base + kShaderHeaderOffset;
    const auto shader_text_address =
        data_base + kShaderTextOffset;

    REQUIRE(
        (shader_text_address & 0xffU) == 0U);
    REQUIRE((shader_text_address >> 48U) == 0U);

    std::vector<std::byte> code;
    append_mov_rdi_imm64(
        code,
        output_address);
    append_mov_rsi_imm64(
        code,
        shader_header_address);
    append_mov_rdx_imm64(
        code,
        shader_text_address);
    append_indirect_call(
        code,
        code_base,
        got_create);
    const auto after_create_rip =
        code_base +
        static_cast<std::uint64_t>(
            code.size());

    // Propagate the real service return value into the controlled exit
    // service. V1 success therefore exits with zero only after guest resume.
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0x89});
    code.push_back(std::byte{0xc7});
    append_call_rel32(
        code,
        code_base,
        gate_base +
            astraea::execution::
                kSyntheticGateStride);
    code.push_back(std::byte{0x0f});
    code.push_back(std::byte{0x0b});

    REQUIRE(code.size() < page);
    for (std::size_t index = 0;
         index < code.size();
         ++index) {
        bytes[
            code_file_offset +
            index] =
                code[index];
    }

    const auto string_file_offset =
        data_file_offset + kStringOffset;
    bytes[string_file_offset] = std::byte{0};
    std::uint32_t cursor = 1;

    const auto import_name_offset = cursor;
    cursor =
        write_string(
            bytes,
            string_file_offset,
            cursor,
            kAgcImport);

    const auto soname_offset = cursor;
    cursor =
        write_string(
            bytes,
            string_file_offset,
            cursor,
            kAgcSoname);

    const auto module_name_offset = cursor;
    cursor =
        write_string(
            bytes,
            string_file_offset,
            cursor,
            kAgcModuleName);

    // libSceAgc publishes a library under the same name as its module.
    // The public linker string table deduplicates identical strings.
    const auto library_name_offset =
        module_name_offset;

    const auto symbol_file_offset =
        data_file_offset + kSymbolOffset;
    write_unsigned<std::uint32_t>(
        bytes,
        symbol_file_offset +
            kElf64SymbolSize,
        import_name_offset);
    bytes[
        symbol_file_offset +
        kElf64SymbolSize +
        4] =
            std::byte{0x12};

    const auto rela_file_offset =
        data_file_offset + kRelaOffset;
    write_unsigned<std::uint64_t>(
        bytes,
        rela_file_offset,
        got_create);
    write_unsigned<std::uint64_t>(
        bytes,
        rela_file_offset + 8,
        (std::uint64_t{1} << 32U) |
            static_cast<std::uint64_t>(
                astraea::execution::
                    kX86_64JumpSlotRelocationType));
    write_i64(
        bytes,
        rela_file_offset + 16,
        0);

    const auto sce_needed_module_value =
        static_cast<std::uint64_t>(
            module_name_offset) |
        (static_cast<std::uint64_t>(
             kAgcModuleVersion)
         << 32U) |
        (kAgcModuleId << 48U);
    const auto sce_import_library_value =
        static_cast<std::uint64_t>(
            library_name_offset) |
        (static_cast<std::uint64_t>(
             kAgcLibraryVersion)
         << 32U) |
        (kAgcLibraryId << 48U);
    const auto sce_import_library_attributes =
        (kAgcLibraryId << 48U) |
        0x09U;

    std::size_t d = 0;
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtNeeded,
        soname_offset);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtSceNeededModule,
        sce_needed_module_value);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtSceImportLibrary,
        sce_import_library_value);
    write_dynamic(
        bytes,
        dynamic_file_offset,
        d++,
        kDtSceImportLibraryAttributes,
        sce_import_library_attributes);
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
        cursor);
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

    std::vector<std::byte> header;
    std::vector<std::byte> text;
    make_public_shape_shader(
        header,
        text);
    for (std::size_t index = 0;
         index < header.size();
         ++index) {
        bytes[
            data_file_offset +
            kShaderHeaderOffset +
            index] =
                header[index];
    }
    for (std::size_t index = 0;
         index < text.size();
         ++index) {
        bytes[
            data_file_offset +
            kShaderTextOffset +
            index] =
                text[index];
    }

    return AgcPrototypeFixture{
        .bytes = std::move(bytes),
        .code_base = code_base,
        .data_base = data_base,
        .stack_base = stack_base,
        .gate_base = gate_base,
        .page = page,
        .got_create = got_create,
        .output_address = output_address,
        .shader_header_address =
            shader_header_address,
        .shader_text_address =
            shader_text_address,
        .after_create_rip =
            after_create_rip,
        .sce_needed_module_value =
            sce_needed_module_value,
        .sce_import_library_value =
            sce_import_library_value,
        .sce_import_library_attributes =
            sce_import_library_attributes,
    };
}

astraea::execution::HleRegistry make_registry() {
    auto result =
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
                {
                    .id =
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                    .canonical_name =
                        "sceAgcCreateShader",
                    .argument_count = 3,
                },
            });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

template <typename T>
T read_guest_value(
    const astraea::execution::GuestMemoryAccess& memory,
    std::uint64_t address) {
    std::array<std::byte, sizeof(T)> bytes{};
    auto result =
        memory.read(
            astraea::memory::GuestAddress{
                address},
            bytes);
    REQUIRE(result.has_value());

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
    return static_cast<T>(value);
}

std::vector<std::byte> read_guest_bytes(
    const astraea::execution::GuestMemoryAccess& memory,
    std::uint64_t address,
    std::size_t size) {
    std::vector<std::byte> bytes(
        size,
        std::byte{0});
    auto result =
        memory.read(
            astraea::memory::GuestAddress{
                address},
            bytes);
    REQUIRE(result.has_value());
    return bytes;
}

const astraea::loader::SceDynamicRecord*
find_sce_record(
    const astraea::loader::SceDynamicMetadata& metadata,
    astraea::loader::SceDynamicTagKind kind) {
    for (const auto& record : metadata.records) {
        if (record.kind == kind) {
            return &record;
        }
    }
    return nullptr;
}

astraea::execution::ExecutionStop make_agc_stop(
    std::uint64_t output,
    std::uint64_t header,
    std::uint64_t text) {
    astraea::execution::ExecutionStop stop{};
    stop.reason =
        astraea::execution::
            ExecutionStopReason::host_gate;
    stop.has_gate_slot = true;
    stop.gate_slot = 0;
    stop.context.rdi = output;
    stop.context.rsi = header;
    stop.context.rdx = text;
    return stop;
}

#endif

}  // namespace

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "owned SCE guest executes real sceAgcCreateShader HLE and resumes",
    "[execution][sce-agc][v1][prototype]") {
    const auto fixture =
        make_fixture();

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
    REQUIRE(built->dynamic_table.has_value());
    REQUIRE(built->dynamic_strings.has_value());
    REQUIRE(
        built->dynamic_strings->
            string_table.has_value());
    REQUIRE(
        built->dynamic_strings->
            needed.size() == 1);
    REQUIRE(built->dynamic_symbols.has_value());
    REQUIRE(built->plt_relocations.has_value());
    REQUIRE(built->plt_relocations->count == 1);

    auto view =
        built->initialized_image_view();
    REQUIRE(view.has_value());

    auto needed =
        astraea::loader::
            resolve_dynamic_string(
                *built->dynamic_strings->
                    string_table,
                built->dynamic_strings->
                    needed[0],
                view.value());
    REQUIRE(needed.has_value());
    REQUIRE(needed.value() == kAgcSoname);

    auto sce_metadata =
        astraea::loader::
            build_sce_dynamic_metadata(
                *built->dynamic_table);
    REQUIRE(sce_metadata.has_value());

    const auto* needed_module =
        find_sce_record(
            sce_metadata.value(),
            astraea::loader::
                SceDynamicTagKind::
                    needed_module);
    const auto* import_library =
        find_sce_record(
            sce_metadata.value(),
            astraea::loader::
                SceDynamicTagKind::
                    import_library);
    const auto* import_attributes =
        find_sce_record(
            sce_metadata.value(),
            astraea::loader::
                SceDynamicTagKind::
                    import_library_attributes);
    REQUIRE(needed_module != nullptr);
    REQUIRE(import_library != nullptr);
    REQUIRE(import_attributes != nullptr);
    REQUIRE(
        needed_module->raw_value ==
        fixture.sce_needed_module_value);
    REQUIRE(
        import_library->raw_value ==
        fixture.sce_import_library_value);
    REQUIRE(
        import_attributes->raw_value ==
        fixture.sce_import_library_attributes);

    auto symbol =
        astraea::loader::
            materialize_sce_dynamic_symbol(
                *built->dynamic_symbols,
                *built->dynamic_strings->
                    string_table,
                1,
                view.value());
    REQUIRE(symbol.has_value());
    REQUIRE(symbol->name.raw == kAgcImport);
    REQUIRE(symbol->name.identity.has_value());
    REQUIRE(
        symbol->name.identity->nid ==
        "f3dg2CSgRKY");
    REQUIRE(
        symbol->name.identity->library_id ==
        "A");
    REQUIRE(
        symbol->name.identity->module_id ==
        "B");

    auto relocation =
        astraea::loader::
            parse_dynamic_relocation(
                *built->plt_relocations,
                0,
                *built->dynamic_symbols,
                view.value());
    REQUIRE(relocation.has_value());
    REQUIRE(relocation->symbol_index == 1);
    REQUIRE(
        relocation->target ==
        astraea::memory::GuestAddress{
            fixture.got_create});
    REQUIRE(
        relocation->relocation_type ==
        astraea::execution::
            kX86_64JumpSlotRelocationType);

    auto registry = make_registry();
    const std::array binding{
        astraea::execution::
            SceImportBinding{
                .identity =
                    symbol->name.identity.value(),
                .function_id =
                    astraea::execution::
                        kSceAgcCreateShaderHleId,
            },
    };
    auto import_bindings =
        astraea::execution::
            SceImportBindingRegistry::create(
                registry,
                binding);
    REQUIRE(import_bindings.has_value());

    auto wrong_identity =
        symbol->name.identity.value();
    wrong_identity.library_id = "B";
    const auto wrong =
        import_bindings->resolve(
            wrong_identity);
    REQUIRE(
        wrong.kind ==
        astraea::execution::
            SceImportResolutionKind::
                unresolved);

    auto import_plan =
        astraea::execution::
            plan_sce_import_resolution(
                relocation.value(),
                symbol.value(),
                import_bindings.value());
    REQUIRE(import_plan.has_value());
    REQUIRE(
        import_plan->function_id ==
        astraea::execution::
            kSceAgcCreateShaderHleId);

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
                                kSceAgcCreateShaderHleId,
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

    auto jump_patch =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                import_plan.value(),
                gates.value(),
                0);
    REQUIRE(jump_patch.has_value());
    REQUIRE(
        jump_patch->gate_destination ==
        astraea::memory::GuestAddress{
            fixture.gate_base});

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
                jump_patch.value(),
                memory)
            .has_value());
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            fixture.got_create) ==
        fixture.gate_base);

    astraea::execution::HleDispatchState
        dispatch_state;

    SECTION(
        "malformed raw AGC failure preserves nested parser provenance") {
        const auto raw =
            read_guest_value<std::uint32_t>(
                memory,
                fixture.shader_header_address);
        const std::array<std::byte, 4> bad_magic{
            std::byte{0x78},
            std::byte{0x56},
            std::byte{0x34},
            std::byte{0x12},
        };
        REQUIRE(
            memory.write(
                astraea::memory::GuestAddress{
                    fixture.shader_header_address},
                bad_magic)
            .has_value());

        const auto before =
            read_guest_bytes(
                memory,
                fixture.shader_header_address,
                kShaderHeaderSize);
        auto result =
            astraea::execution::dispatch_hle(
                registry,
                gates.value(),
                make_agc_stop(
                    fixture.output_address,
                    fixture.shader_header_address,
                    fixture.shader_text_address),
                memory,
                dispatch_state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                HleRuntimeErrorCode::
                    sce_agc_create_shader_plan_failure);
        REQUIRE(
            result.error().
                sce_agc_create_shader_plan_error
                .has_value());
        REQUIRE(
            result.error().
                sce_agc_create_shader_plan_error->
                code ==
            astraea::execution::
                SceAgcCreateShaderPlanErrorCode::
                    invalid_shader_binary);
        REQUIRE(
            result.error().
                sce_agc_create_shader_plan_error->
                shader_binary_error.has_value());
        REQUIRE(
            result.error().
                sce_agc_create_shader_plan_error->
                shader_binary_error->code ==
            astraea::graphics::
                AgcShaderBinaryErrorCode::
                    bad_shader_header_magic);
        REQUIRE(
            read_guest_bytes(
                memory,
                fixture.shader_header_address,
                kShaderHeaderSize) ==
            before);

        REQUIRE(
            dispatch_state.created_agc_shaders.size() == 0U);

        std::array<std::byte, 4> restored{};
        for (std::size_t index = 0;
             index < restored.size();
             ++index) {
            restored[index] =
                static_cast<std::byte>(
                    (raw >> (index * 8U)) &
                    0xffU);
        }
        REQUIRE(
            memory.write(
                astraea::memory::GuestAddress{
                    fixture.shader_header_address},
                restored)
            .has_value());
    }

    SECTION(
        "RDNA2 lowering failure precedes guest mutation and registry insertion") {
        const auto header_before =
            read_guest_bytes(
                memory,
                fixture.shader_header_address,
                kShaderHeaderSize);
        const auto output_before =
            read_guest_bytes(
                memory,
                fixture.output_address,
                8U);

        // V_MOV_B32 with literal source selector 255 requires one extension
        // dword. Making it the final program word keeps the AGC envelope
        // valid but forces Shader IR materialization to fail after the
        // leading NOP has already lowered successfully.
        const std::array<std::byte, 4> missing_extension{
            std::byte{0xff},
            std::byte{0x02},
            std::byte{0x0a},
            std::byte{0x7e},
        };
        REQUIRE(
            memory.write(
                astraea::memory::GuestAddress{
                    fixture.shader_text_address + 4U},
                missing_extension)
            .has_value());

        auto result =
            astraea::execution::dispatch_hle(
                registry,
                gates.value(),
                make_agc_stop(
                    fixture.output_address,
                    fixture.shader_header_address,
                    fixture.shader_text_address),
                memory,
                dispatch_state);

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                HleRuntimeErrorCode::
                    sce_agc_shader_materialization_failure);
        REQUIRE(
            result.error().
                sce_agc_shader_materialization_error
                .has_value());
        REQUIRE(
            result.error().
                sce_agc_shader_materialization_error->
                code ==
            astraea::execution::
                CreatedAgcShaderMaterializationErrorCode::
                    shader_ir_lowering_failure);
        REQUIRE(
            result.error().
                sce_agc_shader_materialization_error->
                shader_ir_error.has_value());
        REQUIRE(
            result.error().
                sce_agc_shader_materialization_error->
                shader_ir_error->
                code ==
            astraea::graphics::
                ShaderIrProgramErrorCode::
                    decode_failure);
        REQUIRE(
            result.error().
                sce_agc_shader_materialization_error->
                shader_ir_error->
                word_index == 1U);
        REQUIRE(
            result.error().
                sce_agc_shader_materialization_error->
                shader_ir_error->
                lowered_instruction_count == 1U);
        REQUIRE(
            dispatch_state.created_agc_shaders.size() == 0U);
        REQUIRE(
            read_guest_bytes(
                memory,
                fixture.shader_header_address,
                kShaderHeaderSize) ==
            header_before);
        REQUIRE(
            read_guest_bytes(
                memory,
                fixture.output_address,
                8U) ==
            output_before);

        const std::array<std::byte, 4> restored_end{
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x81},
            std::byte{0xbf},
        };
        REQUIRE(
            memory.write(
                astraea::memory::GuestAddress{
                    fixture.shader_text_address + 4U},
                restored_end)
            .has_value());
    }

    SECTION(
        "unsupported stage preserves preparation provenance") {
        const std::array original{
            std::byte{1},
        };
        const std::array compute{
            std::byte{0},
        };
        REQUIRE(
            memory.write(
                astraea::memory::GuestAddress{
                    fixture.shader_header_address +
                    0x5aU},
                compute)
            .has_value());

        auto result =
            astraea::execution::dispatch_hle(
                registry,
                gates.value(),
                make_agc_stop(
                    fixture.output_address,
                    fixture.shader_header_address,
                    fixture.shader_text_address),
                memory,
                dispatch_state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                HleRuntimeErrorCode::
                    sce_agc_shader_preparation_failure);
        REQUIRE(
            result.error().
                sce_agc_shader_preparation_error
                .has_value());
        REQUIRE(
            result.error().
                sce_agc_shader_preparation_error->
                code ==
            astraea::execution::
                SceAgcShaderPreparationErrorCode::
                    unsupported_shader_stage);

        REQUIRE(
            dispatch_state.created_agc_shaders.size() == 0U);
        REQUIRE(
            memory.write(
                astraea::memory::GuestAddress{
                    fixture.shader_header_address +
                    0x5aU},
                original)
            .has_value());
    }

    SECTION(
        "unwritable output fails apply preflight without partial header mutation") {
        const auto before =
            read_guest_bytes(
                memory,
                fixture.shader_header_address,
                kShaderHeaderSize);

        auto result =
            astraea::execution::dispatch_hle(
                registry,
                gates.value(),
                make_agc_stop(
                    fixture.code_base,
                    fixture.shader_header_address,
                    fixture.shader_text_address),
                memory,
                dispatch_state);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::
                HleRuntimeErrorCode::
                    sce_agc_shader_apply_failure);
        REQUIRE(
            result.error().
                sce_agc_shader_apply_error
                .has_value());
        REQUIRE(
            result.error().
                sce_agc_shader_apply_error->
                code ==
            astraea::execution::
                SceAgcShaderApplyErrorCode::
                    guest_memory_preflight_failure);
        REQUIRE(
            result.error().
                sce_agc_shader_apply_error->
                applied_count == 0);
        REQUIRE(
            result.error().
                sce_agc_shader_apply_error->
                guest_memory_error.has_value());
        REQUIRE(
            result.error().
                sce_agc_shader_apply_error->
                guest_memory_error->code ==
            astraea::execution::
                GuestMemoryErrorCode::
                    guest_memory_permission_denied);
        REQUIRE(
            result.error().has_guest_memory_error);
        REQUIRE(
            result.error().guest_address ==
            fixture.code_base);
        REQUIRE(
            read_guest_bytes(
                memory,
                fixture.shader_header_address,
                kShaderHeaderSize) ==
            before);
        REQUIRE(
            dispatch_state.created_agc_shaders.size() == 0U);
    }

    SECTION(
        "guest call returns through generic HLE and publishes shader handle") {
        auto first_stop =
            astraea::execution::
                enter_linux_guest(
                    built.value(),
                    prepared.value(),
                    gates.value(),
                    astraea::execution::
                        make_synthetic_initial_context(
                            built.value()));
        REQUIRE(first_stop.has_value());
        REQUIRE(
            first_stop->reason ==
            astraea::execution::
                ExecutionStopReason::host_gate);
        REQUIRE(first_stop->has_gate_slot);
        REQUIRE(first_stop->gate_slot == 0);
        REQUIRE(
            first_stop->context.rdi ==
            fixture.output_address);
        REQUIRE(
            first_stop->context.rsi ==
            fixture.shader_header_address);
        REQUIRE(
            first_stop->context.rdx ==
            fixture.shader_text_address);

        auto create_result =
            astraea::execution::dispatch_hle(
                registry,
                gates.value(),
                first_stop.value(),
                memory,
                dispatch_state);
        REQUIRE(create_result.has_value());
        REQUIRE(
            create_result->action ==
            astraea::execution::
                HleHandlerAction::resume);
        REQUIRE(create_result->value == 0);
        REQUIRE(
            dispatch_state.created_agc_shaders.size() == 1U);

        const auto created_lookup =
            dispatch_state.created_agc_shaders.lookup_unique(
                astraea::graphics::
                    PixelProgramGpuAddress{
                        .value =
                            fixture.shader_text_address,
                    });
        REQUIRE(created_lookup.has_value());
        const auto& created =
            created_lookup.value().get();
        REQUIRE(
            created.shader_handle ==
            astraea::memory::GuestAddress{
                fixture.shader_header_address});
        REQUIRE(
            created.shader_header_address ==
            astraea::memory::GuestAddress{
                fixture.shader_header_address});
        REQUIRE(
            created.shader_text_address ==
            astraea::memory::GuestAddress{
                fixture.shader_text_address});
        REQUIRE(
            created.program_address ==
            astraea::graphics::
                PixelProgramGpuAddress{
                    .value =
                        fixture.shader_text_address,
                });
        REQUIRE(created.shader.rdna2_words.size() == 2U);
        REQUIRE(created.shader_ir.source_word_count == 2U);
        REQUIRE(created.shader_ir.emissions.size() == 2U);

        const auto header =
            fixture.shader_header_address;
        const auto text =
            fixture.shader_text_address;

        REQUIRE(
            read_guest_value<std::uint64_t>(
                memory,
                fixture.output_address) ==
            header);
        REQUIRE(
            read_guest_value<std::uint64_t>(
                memory,
                header + 0x08U) ==
            header + kUserDataOffset);
        REQUIRE(
            read_guest_value<std::uint64_t>(
                memory,
                header + 0x10U) ==
            text);
        REQUIRE(
            read_guest_value<std::uint64_t>(
                memory,
                header + 0x18U) ==
            header + kContextRegistersOffset);
        REQUIRE(
            read_guest_value<std::uint64_t>(
                memory,
                header + 0x20U) ==
            header + kShaderRegistersOffset);
        REQUIRE(
            read_guest_value<std::uint32_t>(
                memory,
                header +
                    kShaderRegistersOffset +
                    4U) ==
            static_cast<std::uint32_t>(
                (text >> 8U) &
                0xffffffffU));
        REQUIRE(
            read_guest_value<std::uint32_t>(
                memory,
                header +
                    kShaderRegistersOffset +
                    12U) ==
            static_cast<std::uint32_t>(
                (text >> 40U) &
                0xffU));

        std::array<std::byte, 1> unknown{};
        REQUIRE(
            memory.read(
                astraea::memory::GuestAddress{
                    header + 0x70U},
                unknown)
            .has_value());
        REQUIRE(
            unknown[0] ==
            std::byte{0x5a});

        auto resumed =
            astraea::execution::
                apply_hle_resume(
                    first_stop->context,
                    create_result.value(),
                    memory);
        REQUIRE(resumed.has_value());
        REQUIRE(resumed->rax == 0);
        REQUIRE(
            resumed->rip ==
            fixture.after_create_rip);
        REQUIRE(
            resumed->rsp ==
            built->initial_stack.rsp.value());

        auto second_stop =
            astraea::execution::
                enter_linux_guest(
                    built.value(),
                    prepared.value(),
                    gates.value(),
                    resumed.value());
        REQUIRE(second_stop.has_value());
        REQUIRE(
            second_stop->reason ==
            astraea::execution::
                ExecutionStopReason::host_gate);
        REQUIRE(second_stop->has_gate_slot);
        REQUIRE(second_stop->gate_slot == 1);
        REQUIRE(second_stop->context.rdi == 0);

        auto exit_result =
            astraea::execution::dispatch_hle(
                registry,
                gates.value(),
                second_stop.value(),
                memory,
                dispatch_state);
        REQUIRE(exit_result.has_value());
        REQUIRE(
            exit_result->action ==
            astraea::execution::
                HleHandlerAction::exit);
        REQUIRE(exit_result->value == 0);
    }
}

#endif
