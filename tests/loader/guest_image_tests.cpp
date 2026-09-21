#include <astraea/loader/guest_image.hpp>

#include <astraea/loader/dynamic_string.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kProgramHeaderOffset = 64;

constexpr std::size_t kLoadHeaderOffset = kProgramHeaderOffset;
constexpr std::size_t kDynamicHeaderOffset =
    kProgramHeaderOffset + kProgramHeaderSize;
constexpr std::size_t kTlsHeaderOffset =
    kProgramHeaderOffset + 2 * kProgramHeaderSize;

constexpr std::size_t kDynamicOffset = 0x200;
constexpr std::size_t kDynamicEntryCount = 18;
constexpr std::size_t kDynamicSize = kDynamicEntryCount * 16;
constexpr std::size_t kStringOffset = 0x400;
constexpr std::size_t kSymbolOffset = 0x420;
constexpr std::size_t kHashOffset = 0x460;
constexpr std::size_t kRelaOffset = 0x480;
constexpr std::size_t kRelOffset = 0x4a0;
constexpr std::size_t kPltOffset = 0x4b0;
constexpr std::size_t kTlsOffset = 0x500;
constexpr std::size_t kImageSize = 0x600;

constexpr std::uint64_t kGuestBase = 0x400000;
constexpr std::uint64_t kStringAddress = kGuestBase + kStringOffset;
constexpr std::uint64_t kSymbolAddress = kGuestBase + kSymbolOffset;
constexpr std::uint64_t kHashAddress = kGuestBase + kHashOffset;
constexpr std::uint64_t kRelaAddress = kGuestBase + kRelaOffset;
constexpr std::uint64_t kRelAddress = kGuestBase + kRelOffset;
constexpr std::uint64_t kPltAddress = kGuestBase + kPltOffset;

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 2; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    const auto widened = static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((widened >> (i * 8U)) & 0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

void write_i64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::int64_t value) {
    write_u64(bytes, offset, std::bit_cast<std::uint64_t>(value));
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
    write_u64(bytes, header_offset + 16, virtual_address);
    write_u64(bytes, header_offset + 24, 0);
    write_u64(bytes, header_offset + 32, file_size);
    write_u64(bytes, header_offset + 40, memory_size);
    write_u64(bytes, header_offset + 48, alignment);
}

void write_dynamic(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::int64_t tag,
    std::uint64_t value) {
    const auto offset = kDynamicOffset + index * 16;
    write_i64(bytes, offset, tag);
    write_u64(bytes, offset + 8, value);
}

std::vector<std::byte> make_integrated_elf() {
    std::vector<std::byte> bytes(kImageSize, std::byte{0});

    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};

    write_u16(bytes, 16, 2);
    write_u16(bytes, 18, 62);
    write_u32(bytes, 20, 1);
    write_u64(bytes, 24, kGuestBase + 0x100);
    write_u64(bytes, 32, kProgramHeaderOffset);
    write_u16(bytes, 52, kElfHeaderSize);
    write_u16(bytes, 54, kProgramHeaderSize);
    write_u16(bytes, 56, 3);

    write_program_header(
        bytes,
        kLoadHeaderOffset,
        1,
        0x5,
        0,
        kGuestBase,
        kImageSize,
        0x700,
        0x1000);

    write_program_header(
        bytes,
        kDynamicHeaderOffset,
        2,
        0x4,
        kDynamicOffset,
        kGuestBase + kDynamicOffset,
        kDynamicSize,
        kDynamicSize,
        8);

    write_program_header(
        bytes,
        kTlsHeaderOffset,
        7,
        0x4,
        kTlsOffset,
        kGuestBase + kTlsOffset,
        4,
        8,
        1);

    const std::string needed = "libsynthetic.so";
    const std::string soname = "guest.so";
    bytes[kStringOffset] = std::byte{0};
    for (std::size_t i = 0; i < needed.size(); ++i) {
        bytes[kStringOffset + 1 + i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(needed[i]));
    }
    const auto needed_end = kStringOffset + 1 + needed.size();
    bytes[needed_end] = std::byte{0};
    const auto soname_offset =
        static_cast<std::uint64_t>(needed.size() + 2);
    const auto soname_start =
        kStringOffset + static_cast<std::size_t>(soname_offset);
    for (std::size_t i = 0; i < soname.size(); ++i) {
        bytes[soname_start + i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(soname[i]));
    }
    bytes[soname_start + soname.size()] = std::byte{0};
    const auto string_size =
        static_cast<std::uint64_t>(
            soname_offset + soname.size() + 1);

    write_u32(bytes, kSymbolOffset + 24, 1);
    bytes[kSymbolOffset + 28] = std::byte{0x12};
    bytes[kSymbolOffset + 29] = std::byte{0};
    write_u16(bytes, kSymbolOffset + 30, 1);
    write_u64(bytes, kSymbolOffset + 32, kGuestBase + 0x100);
    write_u64(bytes, kSymbolOffset + 40, 4);

    write_u32(bytes, kHashOffset, 1);
    write_u32(bytes, kHashOffset + 4, 2);

    write_u64(bytes, kRelaOffset, kGuestBase + 0x620);
    write_u64(
        bytes,
        kRelaOffset + 8,
        (std::uint64_t{1} << 32U) | std::uint64_t{8});
    write_i64(bytes, kRelaOffset + 16, -4);

    write_u64(bytes, kRelOffset, kGuestBase + 0x628);
    write_u64(bytes, kRelOffset + 8, 8);

    write_u64(bytes, kPltOffset, kGuestBase + 0x630);
    write_u64(
        bytes,
        kPltOffset + 8,
        (std::uint64_t{1} << 32U) | std::uint64_t{7});
    write_i64(bytes, kPltOffset + 16, 0);

    bytes[kTlsOffset] = std::byte{0xde};
    bytes[kTlsOffset + 1] = std::byte{0xad};
    bytes[kTlsOffset + 2] = std::byte{0xbe};
    bytes[kTlsOffset + 3] = std::byte{0xef};

    std::size_t d = 0;
    write_dynamic(bytes, d++, 1, 1);
    write_dynamic(bytes, d++, 14, soname_offset);
    write_dynamic(bytes, d++, 5, kStringAddress);
    write_dynamic(bytes, d++, 10, string_size);
    write_dynamic(bytes, d++, 6, kSymbolAddress);
    write_dynamic(bytes, d++, 11, 24);
    write_dynamic(bytes, d++, 4, kHashAddress);
    write_dynamic(bytes, d++, 7, kRelaAddress);
    write_dynamic(bytes, d++, 8, 24);
    write_dynamic(bytes, d++, 9, 24);
    write_dynamic(bytes, d++, 17, kRelAddress);
    write_dynamic(bytes, d++, 18, 16);
    write_dynamic(bytes, d++, 19, 16);
    write_dynamic(bytes, d++, 23, kPltAddress);
    write_dynamic(bytes, d++, 2, 24);
    write_dynamic(bytes, d++, 20, 7);
    write_dynamic(bytes, d++, 0x60000001, 0x1122334455667788ULL);
    write_dynamic(bytes, d++, 0, 0);
    REQUIRE(d == kDynamicEntryCount);

    return bytes;
}

astraea::loader::InitialStackRequest make_stack_request(
    std::uint64_t size = 0x1000) {
    auto storage = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{0x7fff0000},
        astraea::memory::GuestSize{size});
    REQUIRE(storage.has_value());

    return astraea::loader::InitialStackRequest{
        .storage = storage.value(),
        .arguments = {"probe", "--verify"},
        .environment = {"ASTRAEA=1"},
        .auxiliary_vector = {
            astraea::loader::AuxiliaryVectorEntry{
                .type = 6,
                .value = 4096,
            },
        },
    };
}

astraea::loader::GuestImageRequest make_request() {
    return astraea::loader::GuestImageRequest{
        .image_bytes = make_integrated_elf(),
        .initial_stack = make_stack_request(),
    };
}

}  // namespace

TEST_CASE(
    "integrated synthetic ELF builds deterministic M1 GuestImage",
    "[loader][guest-image][m1-exit]") {
    auto first = astraea::loader::build_guest_image(make_request());
    auto second = astraea::loader::build_guest_image(make_request());

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(first->elf.header.entry == kGuestBase + 0x100);
    REQUIRE(first->elf.program_headers.size() == 3);
    REQUIRE(first->mappings.size() == 2);
    REQUIRE(first->mappings == second->mappings);
    REQUIRE(first->image_bytes == second->image_bytes);
    REQUIRE(first->initial_stack.bytes == second->initial_stack.bytes);
    REQUIRE(first->initial_stack.rsp == second->initial_stack.rsp);
    REQUIRE((first->initial_stack.rsp.value() % 16U) == 0);

    REQUIRE(first->dynamic_table.has_value());
    REQUIRE(second->dynamic_table.has_value());
    REQUIRE(
        first->dynamic_table->entries ==
        second->dynamic_table->entries);
    REQUIRE(first->dynamic_table->entries.size() == kDynamicEntryCount);

    bool preserved_unknown = false;
    for (const auto& entry : first->dynamic_table->entries) {
        if (entry.tag == 0x60000001 &&
            entry.value == 0x1122334455667788ULL) {
            preserved_unknown = true;
        }
    }
    REQUIRE(preserved_unknown);

    auto view = first->initialized_image_view();
    REQUIRE(view.has_value());

    REQUIRE(first->dynamic_strings.has_value());
    REQUIRE(first->dynamic_strings->string_table.has_value());
    REQUIRE(first->dynamic_strings->needed.size() == 1);
    REQUIRE(first->dynamic_strings->soname.has_value());

    auto needed = astraea::loader::resolve_dynamic_string(
        *first->dynamic_strings->string_table,
        first->dynamic_strings->needed.front(),
        view.value());
    REQUIRE(needed.has_value());
    REQUIRE(needed.value() == "libsynthetic.so");

    auto soname = astraea::loader::resolve_dynamic_string(
        *first->dynamic_strings->string_table,
        *first->dynamic_strings->soname,
        view.value());
    REQUIRE(soname.has_value());
    REQUIRE(soname.value() == "guest.so");

    REQUIRE(first->dynamic_symbols.has_value());
    REQUIRE(first->dynamic_symbols->symbol_count == 2);

    auto symbol = astraea::loader::parse_dynamic_symbol(
        *first->dynamic_symbols,
        1,
        view.value());
    REQUIRE(symbol.has_value());
    REQUIRE(symbol->name_offset == 1);
    REQUIRE(symbol->binding() == 1);
    REQUIRE(symbol->type() == 2);
    REQUIRE(symbol->value == kGuestBase + 0x100);

    REQUIRE(first->general_relocations.rel.has_value());
    REQUIRE(first->general_relocations.rela.has_value());
    REQUIRE(first->plt_relocations.has_value());
    REQUIRE(
        first->plt_relocations->kind ==
        astraea::loader::RelocationTableKind::plt_rela);

    auto rel = astraea::loader::parse_dynamic_relocation(
        *first->general_relocations.rel,
        0,
        *first->dynamic_symbols,
        view.value());
    REQUIRE(rel.has_value());
    REQUIRE(rel->target == astraea::memory::GuestAddress{kGuestBase + 0x628});
    REQUIRE(rel->symbol_index == 0);
    REQUIRE(rel->relocation_type == 8);
    REQUIRE_FALSE(rel->addend.has_value());

    auto rela = astraea::loader::parse_dynamic_relocation(
        *first->general_relocations.rela,
        0,
        *first->dynamic_symbols,
        view.value());
    REQUIRE(rela.has_value());
    REQUIRE(rela->symbol_index == 1);
    REQUIRE(rela->relocation_type == 8);
    REQUIRE(rela->addend == std::int64_t{-4});

    auto plt = astraea::loader::parse_dynamic_relocation(
        *first->plt_relocations,
        0,
        *first->dynamic_symbols,
        view.value());
    REQUIRE(plt.has_value());
    REQUIRE(plt->symbol_index == 1);
    REQUIRE(plt->relocation_type == 7);
    REQUIRE(plt->addend == std::int64_t{0});

    REQUIRE(first->tls.has_value());
    REQUIRE(second->tls.has_value());
    REQUIRE(
        first->tls->initialized_size ==
        astraea::memory::GuestSize{4});
    REQUIRE(
        first->tls->total_size ==
        astraea::memory::GuestSize{8});
    REQUIRE(*first->tls == *second->tls);

    auto tls_bytes = first->materialize_tls();
    REQUIRE(tls_bytes.has_value());
    REQUIRE(tls_bytes->has_value());
    REQUIRE(tls_bytes->value().size() == 8);
    REQUIRE(tls_bytes->value()[0] == std::byte{0xde});
    REQUIRE(tls_bytes->value()[1] == std::byte{0xad});
    REQUIRE(tls_bytes->value()[2] == std::byte{0xbe});
    REQUIRE(tls_bytes->value()[3] == std::byte{0xef});
    for (std::size_t i = 4; i < 8; ++i) {
        REQUIRE(tls_bytes->value()[i] == std::byte{0});
    }
}

TEST_CASE(
    "GuestImage preserves ELF parse failure at top level",
    "[loader][guest-image]") {
    auto request = make_request();
    request.image_bytes[0] = std::byte{0};

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::elf_parse_failure);
    REQUIRE(std::holds_alternative<astraea::loader::ElfError>(
        result.error().cause));
}

TEST_CASE(
    "GuestImage rejects duplicate PT_DYNAMIC segments deterministically",
    "[loader][guest-image]") {
    auto request = make_request();
    write_u32(request.image_bytes, kTlsHeaderOffset, 2);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::duplicate_dynamic_segment);
    REQUIRE(result.error().source_program_header_index == std::size_t{2});
    REQUIRE(result.error().conflicting_program_header_index == std::size_t{1});
}

TEST_CASE(
    "GuestImage preserves dynamic table structural failure",
    "[loader][guest-image]") {
    auto request = make_request();
    write_dynamic(
        request.image_bytes,
        kDynamicEntryCount - 1,
        0x60000002,
        1);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::dynamic_parse_failure);
    REQUIRE(std::holds_alternative<astraea::loader::DynamicError>(
        result.error().cause));
}

TEST_CASE(
    "GuestImage preserves TLS validation failure",
    "[loader][guest-image]") {
    auto request = make_request();
    write_u32(request.image_bytes, kTlsHeaderOffset + 4, 0x6);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::tls_failure);
    REQUIRE(std::holds_alternative<astraea::loader::TlsTemplateError>(
        result.error().cause));
}

TEST_CASE(
    "GuestImage preserves initial stack failure",
    "[loader][guest-image]") {
    auto request = make_request();
    request.initial_stack = make_stack_request(16);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::initial_stack_failure);
    REQUIRE(std::holds_alternative<astraea::loader::InitialStackError>(
        result.error().cause));
}


TEST_CASE(
    "GuestImage rejects synthetic stack storage overlapping PT_LOAD",
    "[loader][guest-image]") {
    auto request = make_request();
    auto overlapping = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{kGuestBase + 0x100},
        astraea::memory::GuestSize{0x100});
    REQUIRE(overlapping.has_value());
    request.initial_stack.storage = overlapping.value();

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::initial_stack_overlaps_load_mapping);
    REQUIRE(result.error().source_program_header_index == std::size_t{0});
}


TEST_CASE(
    "GuestImage construction keeps oversized TLS descriptor lazy",
    "[loader][guest-image]") {
    auto request = make_request();
    const auto huge = std::numeric_limits<std::uint64_t>::max();
    write_u64(request.image_bytes, kTlsHeaderOffset + 32, 0);
    write_u64(request.image_bytes, kTlsHeaderOffset + 40, huge);
    write_u64(request.image_bytes, kTlsHeaderOffset + 48, 0);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE(result.has_value());
    REQUIRE(result->tls.has_value());
    REQUIRE(result->tls->total_size == astraea::memory::GuestSize{huge});

    auto materialized = result->materialize_tls();
    REQUIRE_FALSE(materialized.has_value());
    REQUIRE(
        materialized.error().code ==
        astraea::loader::TlsTemplateErrorCode::host_size_unrepresentable);
}


TEST_CASE(
    "GuestImage rejects non-empty relocations without bounded symbols",
    "[loader][guest-image]") {
    auto request = make_request();

    write_dynamic(request.image_bytes, 4, 0x60000010, 0);
    write_dynamic(request.image_bytes, 5, 0x60000011, 0);
    write_dynamic(request.image_bytes, 6, 0x60000012, 0);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::dynamic_relocation_failure);
    REQUIRE(std::holds_alternative<astraea::loader::DynamicRelocationError>(
        result.error().cause));
    const auto& cause =
        std::get<astraea::loader::DynamicRelocationError>(
            result.error().cause);
    REQUIRE(
        cause.code ==
        astraea::loader::DynamicRelocationErrorCode::missing_symbol_table);
}

TEST_CASE(
    "GuestImage rejects conflicting overlapping PT_LOAD initialization",
    "[loader][guest-image]") {
    auto request = make_request();

    write_program_header(
        request.image_bytes,
        kTlsHeaderOffset,
        1,
        0x4,
        0x510,
        kGuestBase + 0x500,
        0x10,
        0x10,
        1);

    auto result = astraea::loader::build_guest_image(std::move(request));

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::loader::GuestImageErrorCode::mapping_overlap_conflict);
    REQUIRE(result.error().source_program_header_index.has_value());
    REQUIRE(result.error().conflicting_program_header_index.has_value());
}


TEST_CASE(
    "GuestImage requires explicit PS5/SCE profile for SCE file type",
    "[loader][guest-image][sce]") {
    auto generic_request = make_request();
    write_u16(
        generic_request.image_bytes,
        16,
        0xfe10);

    const auto generic =
        astraea::loader::build_guest_image(
            std::move(generic_request));
    REQUIRE_FALSE(generic.has_value());
    REQUIRE(
        generic.error().code ==
        astraea::loader::GuestImageErrorCode::
            elf_parse_failure);
    REQUIRE(
        std::get<astraea::loader::ElfError>(
            generic.error().cause)
            .code ==
        astraea::loader::ElfErrorCode::
            unsupported_file_type);

    auto sce_request = make_request();
    write_u16(
        sce_request.image_bytes,
        16,
        0xfe10);
    sce_request.elf_profile =
        astraea::loader::ElfParseProfile::ps5_sce;

    const auto sce =
        astraea::loader::build_guest_image(
            std::move(sce_request));
    REQUIRE(sce.has_value());
    REQUIRE(sce->elf.header.type == 0xfe10);
}
