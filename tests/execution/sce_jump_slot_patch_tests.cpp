#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/execution/sce_jump_slot_patch.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/mapping.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

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

astraea::loader::GuestImage make_image() {
    const auto stack = range(0x700000, 0x1000);
    const auto empty_used =
        range(stack.base().value(), 0);

    return astraea::loader::GuestImage{
        .image_bytes = {},
        .elf =
            astraea::loader::ElfImage{
                .header = {},
                .program_headers = {},
            },
        .mappings = {},
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
                .storage = stack,
                .used_range = empty_used,
                .rsp = stack.base(),
                .bytes = {},
            },
    };
}

astraea::execution::HleRegistry make_hle_registry() {
    auto result =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::HleFunctionDescriptor>{
                {
                    .id =
                        astraea::execution::HleFunctionId{1},
                    .canonical_name =
                        "synthetic.service.one",
                    .argument_count = 0,
                },
                {
                    .id =
                        astraea::execution::HleFunctionId{2},
                    .canonical_name =
                        "synthetic.service.two",
                    .argument_count = 0,
                },
            });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::execution::SyntheticGateRegion
make_gate_region() {
    const auto hle = make_hle_registry();
    const auto image = make_image();

    auto result =
        astraea::execution::
            build_synthetic_gate_region(
                hle,
                astraea::memory::GuestAddress{
                    0x600000},
                3,
                std::vector<
                    astraea::execution::GateBinding>{
                    {
                        .slot = 1,
                        .function_id =
                            astraea::execution::
                                HleFunctionId{1},
                    },
                    {
                        .slot = 2,
                        .function_id =
                            astraea::execution::
                                HleFunctionId{2},
                    },
                },
                image);

    REQUIRE(result.has_value());
    return std::move(result).value();
}

astraea::execution::SceImportResolutionPlan
make_plan(
    astraea::loader::RelocationTableKind table_kind =
        astraea::loader::RelocationTableKind::plt_rela,
    std::uint32_t relocation_type =
        astraea::execution::
            kX86_64JumpSlotRelocationType,
    std::optional<std::int64_t> addend =
        std::int64_t{-9}) {
    return astraea::execution::
        SceImportResolutionPlan{
            .table_kind = table_kind,
            .table_index = 4,
            .relocation_target =
                astraea::memory::GuestAddress{
                    0x500000},
            .raw_relocation_type =
                relocation_type,
            .raw_addend = addend,
            .symbol_index = 3,
            .raw_symbol_name =
                "ABCDEFGHIJK#library-a#module-a",
            .identity =
                astraea::loader::
                    SceSymbolIdentity{
                        .nid = "ABCDEFGHIJK",
                        .library_id = "library-a",
                        .module_id = "module-a",
                    },
            .function_id =
                astraea::execution::HleFunctionId{1},
        };
}

}  // namespace

TEST_CASE(
    "x86-64 JUMP_SLOT patch encodes exact synthetic gate address",
    "[execution][sce-jump-slot]") {
    const auto gates = make_gate_region();
    const auto plan = make_plan();

    const auto result =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                plan,
                gates,
                1);

    REQUIRE(result.has_value());
    REQUIRE(
        result->relocation_target ==
        astraea::memory::GuestAddress{
            0x500000});
    REQUIRE(
        result->gate_destination ==
        astraea::memory::GuestAddress{
            0x600010});
    REQUIRE(result->gate_slot == 1);
    REQUIRE(
        result->function_id ==
        astraea::execution::HleFunctionId{1});
    REQUIRE(
        result->raw_addend ==
        std::optional<std::int64_t>{-9});

    const std::array<std::byte, 8> expected{
        std::byte{0x10},
        std::byte{0x00},
        std::byte{0x60},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
    };
    REQUIRE(result->bytes == expected);
}

TEST_CASE(
    "x86-64 JUMP_SLOT patch requires PLT RELA source",
    "[execution][sce-jump-slot]") {
    const auto gates = make_gate_region();

    const auto result =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                make_plan(
                    astraea::loader::
                        RelocationTableKind::rela),
                gates,
                1);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceJumpSlotPatchErrorCode::
                unsupported_table_kind);
}

TEST_CASE(
    "x86-64 JUMP_SLOT patch rejects other relocation types",
    "[execution][sce-jump-slot]") {
    const auto gates = make_gate_region();

    const auto result =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                make_plan(
                    astraea::loader::
                        RelocationTableKind::plt_rela,
                    6),
                gates,
                1);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceJumpSlotPatchErrorCode::
                unsupported_relocation_type);
    REQUIRE(result.error().relocation_type == 6);
}

TEST_CASE(
    "x86-64 JUMP_SLOT patch requires an explicitly bound gate slot",
    "[execution][sce-jump-slot]") {
    const auto gates = make_gate_region();

    const auto result =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                make_plan(),
                gates,
                0);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceJumpSlotPatchErrorCode::
                unbound_gate_slot);
    REQUIRE(result.error().gate_slot == 0);
}

TEST_CASE(
    "x86-64 JUMP_SLOT gate function must match resolved import",
    "[execution][sce-jump-slot]") {
    const auto gates = make_gate_region();

    const auto result =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                make_plan(),
                gates,
                2);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::
            SceJumpSlotPatchErrorCode::
                gate_function_mismatch);
    REQUIRE(
        result.error().expected_function_id ==
        astraea::execution::HleFunctionId{1});
    REQUIRE(
        result.error().actual_function_id ==
        astraea::execution::HleFunctionId{2});
}

TEST_CASE(
    "JUMP_SLOT addend remains evidence and does not alter destination bytes",
    "[execution][sce-jump-slot]") {
    const auto gates = make_gate_region();

    const auto first =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                make_plan(
                    astraea::loader::
                        RelocationTableKind::plt_rela,
                    astraea::execution::
                        kX86_64JumpSlotRelocationType,
                    std::int64_t{-9}),
                gates,
                1);
    const auto second =
        astraea::execution::
            build_synthetic_x86_64_jump_slot_patch(
                make_plan(
                    astraea::loader::
                        RelocationTableKind::plt_rela,
                    astraea::execution::
                        kX86_64JumpSlotRelocationType,
                    std::int64_t{123}),
                gates,
                1);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->bytes == second->bytes);
    REQUIRE(first->gate_destination ==
            second->gate_destination);
    REQUIRE(first->raw_addend != second->raw_addend);
}
