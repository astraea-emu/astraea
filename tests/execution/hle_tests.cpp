#include <astraea/execution/hle.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

astraea::memory::GuestRange range(
    std::uint64_t base,
    std::uint64_t size) {
    auto result = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{base},
        astraea::memory::GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

astraea::memory::GuestPermissions read_only() {
    auto result =
        astraea::memory::GuestPermissions::checked_from_bits(
            static_cast<std::uint8_t>(
                astraea::memory::GuestPermission::read));
    REQUIRE(result.has_value());
    return result.value();
}

astraea::loader::GuestImage make_image(
    std::vector<astraea::memory::GuestRange> mapped_ranges,
    astraea::memory::GuestRange stack_storage) {
    std::vector<astraea::memory::MappingIntent> mappings;
    mappings.reserve(mapped_ranges.size());

    for (std::size_t i = 0; i < mapped_ranges.size(); ++i) {
        mappings.push_back(
            astraea::memory::MappingIntent{
                .range = mapped_ranges[i],
                .permissions = read_only(),
                .backing =
                    astraea::memory::MappingBacking{
                        .kind =
                            astraea::memory::MappingBackingKind::anonymous,
                        .file_offset = 0,
                        .byte_count = mapped_ranges[i].size(),
                    },
                .source_index = i,
            });
    }

    auto empty_used = range(
        stack_storage.base().value(),
        0);

    return astraea::loader::GuestImage{
        .image_bytes = {},
        .elf =
            astraea::loader::ElfImage{
                .header = {},
                .program_headers = {},
            },
        .mappings = std::move(mappings),
        .dynamic_table = std::nullopt,
        .dynamic_strings = std::nullopt,
        .dynamic_symbols = std::nullopt,
        .general_relocations =
            astraea::loader::GeneralDynamicRelocationMetadata{
                .rel = std::nullopt,
                .rela = std::nullopt,
            },
        .plt_relocations = std::nullopt,
        .tls = std::nullopt,
        .initial_stack =
            astraea::loader::InitialStackImage{
                .storage = stack_storage,
                .used_range = empty_used,
                .rsp = stack_storage.base(),
                .bytes = {},
            },
    };
}

astraea::execution::HleRegistry make_registry() {
    auto result = astraea::execution::HleRegistry::create(
        std::vector<astraea::execution::HleFunctionDescriptor>{
            {
                .id = astraea::execution::HleFunctionId{2},
                .canonical_name = "astraea.test.exit",
                .argument_count = 1,
            },
            {
                .id = astraea::execution::HleFunctionId{1},
                .canonical_name = "astraea.test.write",
                .argument_count = 2,
            },
        });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

}  // namespace

TEST_CASE(
    "HLE registry validates and orders descriptors deterministically",
    "[execution][hle]") {
    auto registry = make_registry();

    REQUIRE(registry.descriptors().size() == 2);
    REQUIRE(registry.descriptors()[0].id.value == 1);
    REQUIRE(
        registry.descriptors()[0].canonical_name ==
        "astraea.test.write");
    REQUIRE(registry.descriptors()[1].id.value == 2);
    REQUIRE(registry.find(astraea::execution::HleFunctionId{1}) != nullptr);
    REQUIRE(registry.find(astraea::execution::HleFunctionId{99}) == nullptr);
}

TEST_CASE(
    "HLE registry rejects invalid and duplicate descriptors",
    "[execution][hle]") {
    using astraea::execution::HleFunctionDescriptor;
    using astraea::execution::HleFunctionId;
    using astraea::execution::HleRegistry;
    using astraea::execution::HleSetupErrorCode;

    SECTION("zero function id") {
        auto result = HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                {
                    .id = HleFunctionId{0},
                    .canonical_name = "invalid",
                    .argument_count = 0,
                },
            });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::invalid_function_id);
    }

    SECTION("empty canonical name") {
        auto result = HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                {
                    .id = HleFunctionId{1},
                    .canonical_name = "",
                    .argument_count = 0,
                },
            });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::invalid_function_name);
    }

    SECTION("too many arguments") {
        auto result = HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                {
                    .id = HleFunctionId{1},
                    .canonical_name = "too-many",
                    .argument_count = 7,
                },
            });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::invalid_argument_count);
    }

    SECTION("duplicate id") {
        auto result = HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                {
                    .id = HleFunctionId{1},
                    .canonical_name = "first",
                    .argument_count = 0,
                },
                {
                    .id = HleFunctionId{1},
                    .canonical_name = "second",
                    .argument_count = 0,
                },
            });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::duplicate_function_id);
    }

    SECTION("duplicate name") {
        auto result = HleRegistry::create(
            std::vector<HleFunctionDescriptor>{
                {
                    .id = HleFunctionId{1},
                    .canonical_name = "duplicate",
                    .argument_count = 0,
                },
                {
                    .id = HleFunctionId{2},
                    .canonical_name = "duplicate",
                    .argument_count = 0,
                },
            });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::duplicate_function_name);
    }
}

TEST_CASE(
    "synthetic gate region is deterministic and recognized arithmetically",
    "[execution][hle]") {
    auto registry = make_registry();
    auto image = make_image(
        {range(0x400000, 0x1000)},
        range(0x700000, 0x1000));

    auto result = astraea::execution::build_synthetic_gate_region(
        registry,
        astraea::memory::GuestAddress{0x600000},
        3,
        std::vector<astraea::execution::GateBinding>{
            {
                .slot = 2,
                .function_id =
                    astraea::execution::HleFunctionId{2},
            },
            {
                .slot = 0,
                .function_id =
                    astraea::execution::HleFunctionId{1},
            },
        },
        image);

    REQUIRE(result.has_value());
    REQUIRE(result->slot_count() == 3);
    REQUIRE(result->bindings().size() == 2);
    REQUIRE(result->bindings()[0].slot == 0);
    REQUIRE(result->bindings()[1].slot == 2);
    REQUIRE(
        result->bytes().size() ==
        3U * astraea::execution::kSyntheticGateStride);

    for (std::uint32_t slot = 0; slot < 3; ++slot) {
        const std::size_t offset =
            static_cast<std::size_t>(
                static_cast<std::uint64_t>(slot) *
                astraea::execution::kSyntheticGateStride);
        REQUIRE(
            result->bytes()[offset] ==
            astraea::execution::kSyntheticGateUd2Byte0);
        REQUIRE(
            result->bytes()[offset + 1U] ==
            astraea::execution::kSyntheticGateUd2Byte1);
        for (std::size_t i = 2; i < astraea::execution::kSyntheticGateStride; ++i) {
            REQUIRE(
                result->bytes()[offset + i] ==
                astraea::execution::kSyntheticGatePaddingByte);
        }
    }

    REQUIRE(
        result->recognize_slot(
            astraea::memory::GuestAddress{0x600000}) ==
        std::optional<std::uint32_t>{0});
    REQUIRE(
        result->recognize_slot(
            astraea::memory::GuestAddress{0x600010}) ==
        std::optional<std::uint32_t>{1});
    REQUIRE(
        result->recognize_slot(
            astraea::memory::GuestAddress{0x600020}) ==
        std::optional<std::uint32_t>{2});
    REQUIRE_FALSE(
        result->recognize_slot(
            astraea::memory::GuestAddress{0x600001})
            .has_value());
    REQUIRE_FALSE(
        result->recognize_slot(
            astraea::memory::GuestAddress{0x600030})
            .has_value());

    REQUIRE(
        result->slot_address(2) ==
        std::optional<astraea::memory::GuestAddress>{
            astraea::memory::GuestAddress{0x600020}});
    REQUIRE_FALSE(result->slot_address(3).has_value());

    REQUIRE(result->binding_for_slot(0) != nullptr);
    REQUIRE(
        result->binding_for_slot(0)->function_id.value == 1);
    REQUIRE(result->binding_for_slot(1) == nullptr);
    REQUIRE(result->binding_for_slot(2) != nullptr);
    REQUIRE(
        result->binding_for_slot(2)->function_id.value == 2);
}

TEST_CASE(
    "synthetic gate region rejects invalid bindings",
    "[execution][hle]") {
    using astraea::execution::GateBinding;
    using astraea::execution::HleFunctionId;
    using astraea::execution::HleSetupErrorCode;

    auto registry = make_registry();
    auto image = make_image(
        {},
        range(0x700000, 0x1000));

    SECTION("zero slots") {
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                0,
                {},
                image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::invalid_gate_slot_count);
    }

    SECTION("slot out of bounds") {
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                1,
                std::vector<GateBinding>{
                    {
                        .slot = 1,
                        .function_id = HleFunctionId{1},
                    },
                },
                image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::gate_slot_out_of_bounds);
    }

    SECTION("unknown function") {
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                1,
                std::vector<GateBinding>{
                    {
                        .slot = 0,
                        .function_id = HleFunctionId{99},
                    },
                },
                image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::unknown_function);
    }

    SECTION("duplicate slot binding") {
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                2,
                std::vector<GateBinding>{
                    {
                        .slot = 0,
                        .function_id = HleFunctionId{1},
                    },
                    {
                        .slot = 0,
                        .function_id = HleFunctionId{2},
                    },
                },
                image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            HleSetupErrorCode::duplicate_gate_binding);
    }
}

TEST_CASE(
    "synthetic gate region rejects guest and runtime overlap",
    "[execution][hle]") {
    auto registry = make_registry();

    SECTION("PT_LOAD mapping") {
        auto image = make_image(
            {range(0x600000, 0x1000)},
            range(0x700000, 0x1000));
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                1,
                {},
                image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::HleSetupErrorCode::
                gate_region_overlap);
    }

    SECTION("stack storage") {
        auto image = make_image(
            {},
            range(0x600000, 0x1000));
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                1,
                {},
                image);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::HleSetupErrorCode::
                gate_region_overlap);
    }

    SECTION("additional runtime range") {
        auto image = make_image(
            {},
            range(0x700000, 0x1000));
        const std::array additional{
            range(0x600000, 0x1000),
        };
        auto result =
            astraea::execution::build_synthetic_gate_region(
                registry,
                astraea::memory::GuestAddress{0x600000},
                1,
                {},
                image,
                additional);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::execution::HleSetupErrorCode::
                gate_region_overlap);
    }
}

TEST_CASE(
    "synthetic gate region detects guest-range overflow",
    "[execution][hle]") {
    auto registry = make_registry();
    auto image = make_image(
        {},
        range(0x700000, 0x1000));

    auto result =
        astraea::execution::build_synthetic_gate_region(
            registry,
            astraea::memory::GuestAddress{
                std::numeric_limits<std::uint64_t>::max() - 7U},
            1,
            {},
            image);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        astraea::execution::HleSetupErrorCode::
            gate_range_overflow);
}
