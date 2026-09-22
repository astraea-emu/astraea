#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_binding.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/dynamic_symbols.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/initialized_image_view.hpp>
#include <astraea/memory/mapping.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::uint64_t kGuestBase = 0x1000;
constexpr std::uint64_t kRelaBase = 0x1000;
constexpr std::uint64_t kSymbolBase = 0x1100;
constexpr std::uint64_t kStringBase = 0x1200;
constexpr std::size_t kSymbolFileOffset = 0x100;
constexpr std::size_t kStringFileOffset = 0x200;
constexpr std::size_t kImageSize = 0x400;
constexpr std::uint64_t kRelaSize = 24;
constexpr std::uint64_t kSymbolSize = 24;

const std::string kFirstName =
    "ABCDEFGHIJK#library-a#module-a";
const std::string kSecondName =
    "LMNOPQRSTUV#library-b#module-b";

struct Fixture {
    std::vector<std::byte> bytes;
    astraea::loader::DynamicRelocationTableDescriptor relocations;
    astraea::loader::DynamicSymbolTableDescriptor symbols;
    astraea::loader::DynamicStringTableDescriptor strings;
};

astraea::memory::GuestRange range(
    std::uint64_t base,
    std::uint64_t size) {
    auto result =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{base},
            astraea::memory::GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    for (std::size_t i = 0; i < 2; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (i * 8U)) &
                0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (static_cast<std::uint64_t>(value) >>
                 (i * 8U)) &
                0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (value >> (i * 8U)) & 0xffU);
    }
}

void write_rela(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t target,
    std::uint32_t symbol_index,
    std::uint32_t type,
    std::int64_t addend) {
    write_u64(bytes, offset, target);
    write_u64(
        bytes,
        offset + 8,
        (static_cast<std::uint64_t>(symbol_index) << 32U) |
            static_cast<std::uint64_t>(type));
    write_u64(
        bytes,
        offset + 16,
        std::bit_cast<std::uint64_t>(addend));
}

void write_symbol(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::uint32_t name_offset) {
    const auto offset =
        kSymbolFileOffset +
        index * static_cast<std::size_t>(kSymbolSize);
    write_u32(bytes, offset, name_offset);
    bytes[offset + 4] = std::byte{0x12};
    bytes[offset + 5] = std::byte{0};
    write_u16(bytes, offset + 6, 0);
    write_u64(bytes, offset + 8, 0);
    write_u64(bytes, offset + 16, 0);
}

void write_string(
    std::vector<std::byte>& bytes,
    std::uint32_t string_offset,
    const std::string& value) {
    const auto offset =
        kStringFileOffset +
        static_cast<std::size_t>(string_offset);
    for (std::size_t i = 0; i < value.size(); ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    value[i]));
    }
    bytes[offset + value.size()] = std::byte{0};
}

Fixture make_fixture(
    std::uint32_t second_relocation_symbol = 2,
    std::string second_name = kSecondName) {
    std::vector<std::byte> bytes(
        kImageSize,
        std::byte{0});

    const std::uint32_t first_name_offset = 1;
    const std::uint32_t second_name_offset =
        first_name_offset +
        static_cast<std::uint32_t>(
            kFirstName.size()) +
        1U;

    write_rela(
        bytes,
        0,
        0x3000,
        1,
        0xfeedbeefU,
        -7);
    write_rela(
        bytes,
        static_cast<std::size_t>(kRelaSize),
        0x3010,
        second_relocation_symbol,
        7,
        11);

    write_symbol(bytes, 1, first_name_offset);
    write_symbol(bytes, 2, second_name_offset);
    write_string(bytes, first_name_offset, kFirstName);
    write_string(bytes, second_name_offset, second_name);

    return Fixture{
        .bytes = std::move(bytes),
        .relocations =
            astraea::loader::
                DynamicRelocationTableDescriptor{
                    .kind =
                        astraea::loader::
                            RelocationTableKind::plt_rela,
                    .range = range(
                        kRelaBase,
                        2U * kRelaSize),
                    .entry_size =
                        astraea::memory::GuestSize{
                            kRelaSize},
                    .count = 2,
                    .address_source_entry_index = 10,
                    .size_source_entry_index = 11,
                    .encoding_source_entry_index = 12,
                },
        .symbols =
            astraea::loader::
                DynamicSymbolTableDescriptor{
                    .range = range(
                        kSymbolBase,
                        3U * kSymbolSize),
                    .entry_size =
                        astraea::memory::GuestSize{
                            kSymbolSize},
                    .symbol_count = 3,
                    .count_from_symtabsz = true,
                    .count_from_sysv_hash = false,
                    .symtab_source_entry_index = 20,
                    .syment_source_entry_index = 21,
                    .symtabsz_source_entry_index =
                        std::size_t{22},
                    .hash_source_entry_index =
                        std::nullopt,
                },
        .strings =
            astraea::loader::
                DynamicStringTableDescriptor{
                    .range = range(
                        kStringBase,
                        0x100),
                },
    };
}

astraea::memory::InitializedImageView make_view(
    const std::vector<std::byte>& bytes) {
    auto permissions =
        astraea::memory::GuestPermissions::
            checked_from_bits(0x4U);
    REQUIRE(permissions.has_value());

    const std::array mappings{
        astraea::memory::MappingIntent{
            .range = range(
                kGuestBase,
                kImageSize),
            .permissions = permissions.value(),
            .backing =
                astraea::memory::MappingBacking{
                    .kind =
                        astraea::memory::
                            MappingBackingKind::file,
                    .file_offset = 0,
                    .byte_count =
                        astraea::memory::GuestSize{
                            kImageSize},
                },
            .source_index = 0,
        },
    };

    auto view =
        astraea::memory::InitializedImageView::create(
            bytes,
            mappings);
    REQUIRE(view.has_value());
    return view.value();
}

astraea::execution::HleRegistry make_hle_registry() {
    auto registry =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::
                    HleFunctionDescriptor>{
                {
                    .id =
                        astraea::execution::
                            HleFunctionId{1},
                    .canonical_name =
                        "synthetic.service.one",
                    .argument_count = 0,
                },
                {
                    .id =
                        astraea::execution::
                            HleFunctionId{2},
                    .canonical_name =
                        "synthetic.service.two",
                    .argument_count = 0,
                },
            });
    REQUIRE(registry.has_value());
    return std::move(registry).value();
}

astraea::loader::SceSymbolIdentity identity(
    const char* nid,
    const char* library,
    const char* module) {
    return astraea::loader::SceSymbolIdentity{
        .nid = nid,
        .library_id = library,
        .module_id = module,
    };
}

astraea::execution::SceImportBindingRegistry
make_bindings(
    const astraea::execution::HleRegistry& hle,
    bool include_second = true) {
    std::vector<astraea::execution::SceImportBinding>
        bindings{
            {
                .identity =
                    identity(
                        "ABCDEFGHIJK",
                        "library-a",
                        "module-a"),
                .function_id =
                    astraea::execution::
                        HleFunctionId{1},
            },
        };

    if (include_second) {
        bindings.push_back(
            astraea::execution::SceImportBinding{
                .identity =
                    identity(
                        "LMNOPQRSTUV",
                        "library-b",
                        "module-b"),
                .function_id =
                    astraea::execution::
                        HleFunctionId{2},
            });
    }

    auto registry =
        astraea::execution::
            SceImportBindingRegistry::create(
                hle,
                bindings);
    REQUIRE(registry.has_value());
    return std::move(registry).value();
}

}  // namespace

TEST_CASE(
    "batch SCE PLT planner returns ordered exact plans without applying semantics",
    "[execution][sce-import-resolution][batch]") {
    const auto fixture = make_fixture();
    const auto view = make_view(fixture.bytes);
    const auto hle = make_hle_registry();
    const auto bindings = make_bindings(hle);

    const auto result =
        astraea::execution::plan_sce_plt_imports(
            fixture.relocations,
            fixture.symbols,
            fixture.strings,
            view,
            bindings);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);

    const auto& first = result->at(0);
    REQUIRE(first.table_index == 0);
    REQUIRE(
        first.table_kind ==
        astraea::loader::
            RelocationTableKind::plt_rela);
    REQUIRE(
        first.relocation_target ==
        astraea::memory::GuestAddress{0x3000});
    REQUIRE(first.raw_relocation_type == 0xfeedbeefU);
    REQUIRE(
        first.raw_addend ==
        std::optional<std::int64_t>{-7});
    REQUIRE(first.symbol_index == 1);
    REQUIRE(first.raw_symbol_name == kFirstName);
    REQUIRE(
        first.identity ==
        identity(
            "ABCDEFGHIJK",
            "library-a",
            "module-a"));
    REQUIRE(
        first.function_id ==
        astraea::execution::HleFunctionId{1});

    const auto& second = result->at(1);
    REQUIRE(second.table_index == 1);
    REQUIRE(
        second.relocation_target ==
        astraea::memory::GuestAddress{0x3010});
    REQUIRE(second.raw_relocation_type == 7);
    REQUIRE(
        second.raw_addend ==
        std::optional<std::int64_t>{11});
    REQUIRE(second.symbol_index == 2);
    REQUIRE(second.raw_symbol_name == kSecondName);
    REQUIRE(
        second.identity ==
        identity(
            "LMNOPQRSTUV",
            "library-b",
            "module-b"));
    REQUIRE(
        second.function_id ==
        astraea::execution::HleFunctionId{2});
}

TEST_CASE(
    "empty PLT table produces an empty batch plan",
    "[execution][sce-import-resolution][batch]") {
    auto fixture = make_fixture();
    fixture.relocations.count = 0;
    fixture.relocations.range = range(kRelaBase, 0);
    const auto view = make_view(fixture.bytes);
    const auto hle = make_hle_registry();
    const auto bindings = make_bindings(hle);

    const auto result =
        astraea::execution::plan_sce_plt_imports(
            fixture.relocations,
            fixture.symbols,
            fixture.strings,
            view,
            bindings);

    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

TEST_CASE(
    "batch planner reports the first relocation parse failure with its table index",
    "[execution][sce-import-resolution][batch]") {
    const auto fixture = make_fixture(3);
    const auto view = make_view(fixture.bytes);
    const auto hle = make_hle_registry();
    const auto bindings = make_bindings(hle);

    const auto result =
        astraea::execution::plan_sce_plt_imports(
            fixture.relocations,
            fixture.symbols,
            fixture.strings,
            view,
            bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            ScePltImportPlanErrorCode::
                relocation_failure);
    REQUIRE(result.error().relocation_index == 1);
    REQUIRE(result.error().relocation_error.has_value());
    REQUIRE(
        result.error().relocation_error->code ==
        astraea::loader::
            DynamicRelocationErrorCode::
                relocation_symbol_index_out_of_bounds);
    REQUIRE_FALSE(result.error().symbol_error.has_value());
    REQUIRE_FALSE(
        result.error().resolution_error.has_value());
}

TEST_CASE(
    "batch planner preserves an indexed SCE symbol materialization failure",
    "[execution][sce-import-resolution][batch]") {
    const auto fixture =
        make_fixture(
            2,
            "ABCDEFGHIJ#library-b#module-b");
    const auto view = make_view(fixture.bytes);
    const auto hle = make_hle_registry();
    const auto bindings = make_bindings(hle);

    const auto result =
        astraea::execution::plan_sce_plt_imports(
            fixture.relocations,
            fixture.symbols,
            fixture.strings,
            view,
            bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            ScePltImportPlanErrorCode::
                symbol_failure);
    REQUIRE(result.error().relocation_index == 1);
    REQUIRE(result.error().symbol_error.has_value());
    REQUIRE(
        result.error().symbol_error->code ==
        astraea::loader::
            SceDynamicSymbolErrorCode::
                sce_identity_failure);
    REQUIRE_FALSE(
        result.error().relocation_error.has_value());
    REQUIRE_FALSE(
        result.error().resolution_error.has_value());
}

TEST_CASE(
    "batch planner preserves an indexed exact-binding resolution failure",
    "[execution][sce-import-resolution][batch]") {
    const auto fixture = make_fixture();
    const auto view = make_view(fixture.bytes);
    const auto hle = make_hle_registry();
    const auto bindings =
        make_bindings(hle, false);

    const auto result =
        astraea::execution::plan_sce_plt_imports(
            fixture.relocations,
            fixture.symbols,
            fixture.strings,
            view,
            bindings);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            ScePltImportPlanErrorCode::
                resolution_failure);
    REQUIRE(result.error().relocation_index == 1);
    REQUIRE(
        result.error().resolution_error.has_value());
    REQUIRE(
        result.error().resolution_error->code ==
        astraea::execution::
            SceImportResolutionPlanErrorCode::
                unresolved_import);
    REQUIRE_FALSE(
        result.error().relocation_error.has_value());
    REQUIRE_FALSE(result.error().symbol_error.has_value());
}


TEST_CASE(
    "generic SCE relocation batch planner preserves general RELA evidence",
    "[execution][sce-import-resolution][batch][rela]") {
    auto fixture = make_fixture();
    fixture.relocations.kind =
        astraea::loader::RelocationTableKind::rela;

    const auto view = make_view(fixture.bytes);
    const auto hle = make_hle_registry();
    const auto bindings = make_bindings(hle);

    const auto result =
        astraea::execution::plan_sce_imports(
            fixture.relocations,
            fixture.symbols,
            fixture.strings,
            view,
            bindings);

    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);

    REQUIRE(
        result->at(0).table_kind ==
        astraea::loader::RelocationTableKind::rela);
    REQUIRE(result->at(0).table_index == 0);
    REQUIRE(result->at(0).raw_relocation_type == 0xfeedbeefU);
    REQUIRE(
        result->at(0).raw_addend ==
        std::optional<std::int64_t>{-7});
    REQUIRE(result->at(0).raw_symbol_name == kFirstName);
    REQUIRE(
        result->at(0).function_id ==
        astraea::execution::HleFunctionId{1});

    REQUIRE(
        result->at(1).table_kind ==
        astraea::loader::RelocationTableKind::rela);
    REQUIRE(result->at(1).table_index == 1);
    REQUIRE(result->at(1).raw_relocation_type == 7);
    REQUIRE(
        result->at(1).raw_addend ==
        std::optional<std::int64_t>{11});
    REQUIRE(result->at(1).raw_symbol_name == kSecondName);
    REQUIRE(
        result->at(1).function_id ==
        astraea::execution::HleFunctionId{2});
}
