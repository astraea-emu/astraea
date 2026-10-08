#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/sce_glob_dat_apply.hpp>
#include <astraea/execution/module_graph_import_apply.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/mapping.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

using astraea::execution::GuestMemoryAccess;
using astraea::execution::GuestMemoryErrorCode;
using astraea::execution::HleFunctionId;
using astraea::loader::ElfHeader;
using astraea::loader::ElfImage;
using astraea::loader::
    GeneralDynamicRelocationMetadata;
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

GuestImage make_empty_image() {
    return GuestImage{
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
}

astraea::execution::SceGlobDatPatch
make_patch(
    std::uint64_t target,
    std::uint64_t destination =
        0x600000) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<std::byte>(
                (destination >>
                 (index * 8U)) &
                0xffU);
    }

    return astraea::execution::
        SceGlobDatPatch{
            .relocation_target =
                GuestAddress{target},
            .gate_destination =
                GuestAddress{destination},
            .gate_slot = 0,
            .function_id =
                HleFunctionId{1},
            .bytes = bytes,
            .raw_addend =
                std::int64_t{-9},
        };
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

GuestPermissions permissions(
    std::uint8_t bits) {
    auto result =
        GuestPermissions::checked_from_bits(
            bits);
    REQUIRE(result.has_value());
    return result.value();
}

std::uint64_t page_size() {
    const long value = ::sysconf(_SC_PAGESIZE);
    REQUIRE(value > 0);
    return static_cast<std::uint64_t>(
        value);
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

struct TestLayout {
    GuestImage image;
    std::uint64_t code_base = 0;
    std::uint64_t data_base = 0;
    std::uint64_t stack_base = 0;
    std::uint64_t unmapped_base = 0;
    std::uint64_t page = 0;
};

TestLayout make_layout(
    GuestPermissions data_permissions,
    std::size_t data_size = 8) {
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
            std::numeric_limits<
                std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 4U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base =
        base + 2U * page;
    const auto unmapped_base =
        base + 3U * page;

    const std::vector<std::byte> code{
        std::byte{0x0f},
        std::byte{0x0b},
    };
    const std::vector<std::byte> data(
        data_size,
        std::byte{0});

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
        .image_bytes =
            std::move(image_bytes),
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
                            code.size()),
                    .permissions =
                        permissions(
                            kRead |
                            kExecute),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::
                                    file,
                            .file_offset = 0,
                            .byte_count =
                                GuestSize{
                                    code.size()},
                        },
                    .source_index = 0,
                },
                MappingIntent{
                    .range =
                        range(
                            data_base,
                            data.size()),
                    .permissions =
                        data_permissions,
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::
                                    file,
                            .file_offset =
                                code.size(),
                            .byte_count =
                                GuestSize{
                                    data.size()},
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

    return TestLayout{
        .image = std::move(image),
        .code_base = code_base,
        .data_base = data_base,
        .stack_base = stack_base,
        .unmapped_base =
            unmapped_base,
        .page = page,
    };
}

#endif

}  // namespace

TEST_CASE(
    "validated GLOB_DAT application preserves unavailable-memory failure",
    "[execution][sce-glob-dat-apply]") {
    auto image = make_empty_image();
    astraea::execution::
        LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{
        image,
        prepared};

    const auto result =
        astraea::execution::
            apply_synthetic_glob_dat_patch(
                make_patch(0x1000),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        GuestMemoryErrorCode::
            prepared_memory_unavailable);
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "validated GLOB_DAT patch writes exact gate bytes through GuestMemoryAccess",
    "[execution][sce-glob-dat-apply]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            permissions(
                kRead | kWrite));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto patch =
        make_patch(
            layout.data_base,
            layout.unmapped_base);

    const auto applied =
        astraea::execution::
            apply_synthetic_glob_dat_patch(
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
    REQUIRE(
        memory.read(
            GuestAddress{
                layout.data_base},
            read_back)
            .has_value());
    REQUIRE(read_back == patch.bytes);
}

TEST_CASE(
    "validated GLOB_DAT patch preserves read-only target failure",
    "[execution][sce-glob-dat-apply]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);

    auto layout =
        make_layout(
            permissions(kRead));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto applied =
        astraea::execution::
            apply_synthetic_glob_dat_patch(
                make_patch(
                    layout.data_base),
                memory);

    REQUIRE_FALSE(applied.has_value());
    REQUIRE(
        applied.error().code ==
        GuestMemoryErrorCode::
            guest_memory_permission_denied);
}

TEST_CASE(
    "validated GLOB_DAT patch preserves unmapped target failure",
    "[execution][sce-glob-dat-apply]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            permissions(
                kRead | kWrite));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto applied =
        astraea::execution::
            apply_synthetic_glob_dat_patch(
                make_patch(
                    layout.unmapped_base),
                memory);

    REQUIRE_FALSE(applied.has_value());
    REQUIRE(
        applied.error().code ==
        GuestMemoryErrorCode::
            guest_memory_unmapped);
}

#endif


TEST_CASE(
    "empty GLOB_DAT batch succeeds without guest-memory writes",
    "[execution][sce-glob-dat-apply][batch]") {
    auto image = make_empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{
        image,
        prepared};

    const std::span<
        const astraea::execution::SceGlobDatPatch>
        patches{};

    const auto applied =
        astraea::execution::
            apply_synthetic_glob_dat_patches(
                patches,
                memory);

    REQUIRE(applied.has_value());
    REQUIRE(applied->empty());
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "batch GLOB_DAT application writes two ordered patches exactly",
    "[execution][sce-glob-dat-apply][batch]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            permissions(kRead | kWrite),
            16);
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto first =
        make_patch(
            layout.data_base,
            layout.unmapped_base);
    auto second =
        make_patch(
            layout.data_base + 8U,
            layout.unmapped_base + 16U);
    second.gate_slot = 1;
    second.function_id = HleFunctionId{2};

    const std::array patches{
        first,
        second,
    };

    const auto applied =
        astraea::execution::
            apply_synthetic_glob_dat_patches(
                patches,
                memory);

    REQUIRE(applied.has_value());
    REQUIRE(applied->size() == 2);
    REQUIRE(
        applied->at(0).relocation_target ==
        patches[0].relocation_target);
    REQUIRE(
        applied->at(1).relocation_target ==
        patches[1].relocation_target);
    REQUIRE(applied->at(0).gate_slot == 0);
    REQUIRE(applied->at(1).gate_slot == 1);
    REQUIRE(
        applied->at(1).function_id ==
        HleFunctionId{2});

    std::array<std::byte, 8> first_bytes{};
    std::array<std::byte, 8> second_bytes{};
    REQUIRE(
        memory.read(
            GuestAddress{layout.data_base},
            first_bytes)
            .has_value());
    REQUIRE(
        memory.read(
            GuestAddress{layout.data_base + 8U},
            second_bytes)
            .has_value());
    REQUIRE(first_bytes == patches[0].bytes);
    REQUIRE(second_bytes == patches[1].bytes);
}

TEST_CASE(
    "batch GLOB_DAT failure reports partial application without rollback",
    "[execution][sce-glob-dat-apply][batch]") {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);

    auto layout =
        make_layout(
            permissions(kRead | kWrite));
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto first =
        make_patch(
            layout.data_base,
            layout.unmapped_base);
    auto second =
        make_patch(
            layout.unmapped_base,
            layout.unmapped_base + 16U);
    second.gate_slot = 1;
    second.function_id = HleFunctionId{2};

    const std::array patches{
        first,
        second,
    };

    const auto applied =
        astraea::execution::
            apply_synthetic_glob_dat_patches(
                patches,
                memory);

    REQUIRE_FALSE(applied.has_value());
    REQUIRE(
        applied.error().code ==
        astraea::execution::
            SceGlobDatApplyBatchErrorCode::
                apply_failure);
    REQUIRE(applied.error().patch_index == 1);
    REQUIRE(applied.error().patch_count == 2);
    REQUIRE(applied.error().applied_count == 1);
    REQUIRE(applied.error().memory_error.has_value());
    REQUIRE(
        applied.error().memory_error->code ==
        GuestMemoryErrorCode::
            guest_memory_unmapped);

    std::array<std::byte, 8> read_back{};
    REQUIRE(
        memory.read(
            GuestAddress{layout.data_base},
            read_back)
            .has_value());
    REQUIRE(read_back == patches[0].bytes);
}

#endif


TEST_CASE(
    "owned module import application refuses unavailable prepared guest memory",
    "[execution][module-graph][owned-apply]") {
    auto image = make_empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};
    const auto synthetic = make_patch(0x1000);
    const astraea::execution::OwnedModuleAbsolutePatch patch{
        .target = synthetic.relocation_target,
        .source_symbol_address = synthetic.gate_destination,
        .raw_relocation_type = 6U,
        .raw_addend = std::int64_t{-9},
        .bytes = synthetic.bytes,
    };
    const auto applied =
        astraea::execution::apply_owned_module_import_patch(
            patch, memory);
    REQUIRE_FALSE(applied.has_value());
    REQUIRE(applied.error().code ==
        GuestMemoryErrorCode::prepared_memory_unavailable);
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "owned module import application writes exact bytes to checked guest mapping",
    "[execution][module-graph][owned-apply]") {
    constexpr auto read = static_cast<std::uint8_t>(GuestPermission::read);
    constexpr auto write = static_cast<std::uint8_t>(GuestPermission::write);
    auto layout = make_layout(permissions(read | write));
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{layout.image, prepared.value()};
    const auto synthetic = make_patch(layout.data_base, 0x12345678U);
    const astraea::execution::OwnedModuleAbsolutePatch patch{
        .target = synthetic.relocation_target,
        .source_symbol_address = synthetic.gate_destination,
        .raw_relocation_type = 6U,
        .raw_addend = std::int64_t{17},
        .bytes = synthetic.bytes,
    };
    const auto applied =
        astraea::execution::apply_owned_module_import_patch(
            patch, memory);
    REQUIRE(applied.has_value());
    REQUIRE(applied->target == patch.target);
    REQUIRE(applied->provider_guest_address ==
        patch.source_symbol_address);
    std::array<std::byte, 8> actual{};
    REQUIRE(memory.read(
        GuestAddress{layout.data_base}, actual).has_value());
    REQUIRE(actual == patch.bytes);
}

TEST_CASE(
    "owned module import application preserves data on readonly refusal",
    "[execution][module-graph][owned-apply]") {
    constexpr auto read = static_cast<std::uint8_t>(GuestPermission::read);
    auto layout = make_layout(permissions(read));
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{layout.image, prepared.value()};
    const auto synthetic = make_patch(layout.data_base, 0x12345678U);
    const astraea::execution::OwnedModuleAbsolutePatch patch{
        .target = synthetic.relocation_target,
        .source_symbol_address = synthetic.gate_destination,
        .raw_relocation_type = 6U,
        .raw_addend = std::int64_t{0},
        .bytes = synthetic.bytes,
    };
    std::array<std::byte, 8> before{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, before).has_value());
    const auto applied =
        astraea::execution::apply_owned_module_import_patch(
            patch, memory);
    REQUIRE_FALSE(applied.has_value());
    REQUIRE(applied.error().code ==
        GuestMemoryErrorCode::guest_memory_permission_denied);
    std::array<std::byte, 8> after{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, after).has_value());
    REQUIRE(after == before);
}

TEST_CASE(
    "owned module import application rejects unmapped and partial target spans",
    "[execution][module-graph][owned-apply]") {
    constexpr auto read = static_cast<std::uint8_t>(GuestPermission::read);
    constexpr auto write = static_cast<std::uint8_t>(GuestPermission::write);
    auto layout = make_layout(permissions(read | write), 4);
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{layout.image, prepared.value()};
    const auto synthetic = make_patch(layout.data_base, 0x12345678U);
    auto partial = astraea::execution::OwnedModuleAbsolutePatch{
        .target = synthetic.relocation_target,
        .source_symbol_address = synthetic.gate_destination,
        .raw_relocation_type = 6U,
        .raw_addend = std::int64_t{0},
        .bytes = synthetic.bytes,
    };
    std::array<std::byte, 4> before{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, before).has_value());
    const auto rejected =
        astraea::execution::apply_owned_module_import_patch(partial, memory);
    REQUIRE_FALSE(rejected.has_value());
    REQUIRE(rejected.error().code ==
        GuestMemoryErrorCode::guest_memory_unmapped);
    std::array<std::byte, 4> after{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, after).has_value());
    REQUIRE(after == before);

    partial.target = GuestAddress{layout.unmapped_base};
    const auto unmapped =
        astraea::execution::apply_owned_module_import_patch(partial, memory);
    REQUIRE_FALSE(unmapped.has_value());
    REQUIRE(unmapped.error().code ==
        GuestMemoryErrorCode::guest_memory_unmapped);
}

#endif


TEST_CASE(
    "empty owned import batch succeeds without prepared guest memory",
    "[execution][module-graph][owned-batch]") {
    auto image = make_empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};
    const std::span<const astraea::execution::OwnedModuleAbsolutePatch>
        empty{};
    const auto result =
        astraea::execution::apply_owned_module_import_batch(empty, memory);
    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

TEST_CASE(
    "owned import batch is explicitly bounded before memory preflight",
    "[execution][module-graph][owned-batch]") {
    auto image = make_empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};
    std::vector<astraea::execution::OwnedModuleAbsolutePatch> patches(257);
    const auto result =
        astraea::execution::apply_owned_module_import_batch(patches, memory);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        astraea::execution::OwnedModuleImportBatchErrorCode::
            too_many_patches);
    REQUIRE(result.error().applied_count == 0U);
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "owned batch writes two disjoint prepared guest patch targets",
    "[execution][module-graph][owned-batch]") {
    constexpr auto read = static_cast<std::uint8_t>(GuestPermission::read);
    constexpr auto write = static_cast<std::uint8_t>(GuestPermission::write);
    auto layout = make_layout(permissions(read | write), 16);
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{layout.image, prepared.value()};

    const auto a = make_patch(layout.data_base, 0x12345678U);
    const auto b = make_patch(layout.data_base + 8U, 0x23456789U);
    const std::array patches{
        astraea::execution::OwnedModuleAbsolutePatch{
            .target = a.relocation_target,
            .source_symbol_address = a.gate_destination,
            .raw_relocation_type = 6U,
            .raw_addend = std::int64_t{0},
            .bytes = a.bytes,
        },
        astraea::execution::OwnedModuleAbsolutePatch{
            .target = b.relocation_target,
            .source_symbol_address = b.gate_destination,
            .raw_relocation_type = 7U,
            .raw_addend = std::int64_t{0},
            .bytes = b.bytes,
        },
    };
    const auto result =
        astraea::execution::apply_owned_module_import_batch(patches, memory);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2U);
    std::array<std::byte, 8> first{};
    std::array<std::byte, 8> second{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, first).has_value());
    REQUIRE(memory.read(GuestAddress{layout.data_base + 8U}, second).has_value());
    REQUIRE(first == patches[0].bytes);
    REQUIRE(second == patches[1].bytes);
}

TEST_CASE(
    "owned import batch rejects overlap before changing any target",
    "[execution][module-graph][owned-batch]") {
    constexpr auto read = static_cast<std::uint8_t>(GuestPermission::read);
    constexpr auto write = static_cast<std::uint8_t>(GuestPermission::write);
    auto layout = make_layout(permissions(read | write), 16);
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{layout.image, prepared.value()};
    const auto a = make_patch(layout.data_base, 0x12345678U);
    const auto b = make_patch(layout.data_base + 4U, 0x23456789U);
    const std::array patches{
        astraea::execution::OwnedModuleAbsolutePatch{
            .target = a.relocation_target,
            .source_symbol_address = a.gate_destination,
            .raw_relocation_type = 6U,
            .raw_addend = std::int64_t{0},
            .bytes = a.bytes,
        },
        astraea::execution::OwnedModuleAbsolutePatch{
            .target = b.relocation_target,
            .source_symbol_address = b.gate_destination,
            .raw_relocation_type = 6U,
            .raw_addend = std::int64_t{0},
            .bytes = b.bytes,
        },
    };
    std::array<std::byte, 16> before{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, before).has_value());
    const auto result =
        astraea::execution::apply_owned_module_import_batch(patches, memory);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        astraea::execution::OwnedModuleImportBatchErrorCode::
            conflicting_target);
    REQUIRE(result.error().patch_index == 1U);
    REQUIRE(result.error().conflicting_patch_index == 0U);
    REQUIRE(result.error().applied_count == 0U);
    std::array<std::byte, 16> after{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, after).has_value());
    REQUIRE(after == before);
}

TEST_CASE(
    "owned import batch preflights every target before any guest write",
    "[execution][module-graph][owned-batch]") {
    constexpr auto read = static_cast<std::uint8_t>(GuestPermission::read);
    constexpr auto write = static_cast<std::uint8_t>(GuestPermission::write);
    auto layout = make_layout(permissions(read | write), 16);
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{layout.image, prepared.value()};
    const auto a = make_patch(layout.data_base, 0x12345678U);
    const auto b = make_patch(layout.unmapped_base, 0x23456789U);
    const std::array patches{
        astraea::execution::OwnedModuleAbsolutePatch{
            .target = a.relocation_target,
            .source_symbol_address = a.gate_destination,
            .raw_relocation_type = 6U,
            .raw_addend = std::int64_t{0},
            .bytes = a.bytes,
        },
        astraea::execution::OwnedModuleAbsolutePatch{
            .target = b.relocation_target,
            .source_symbol_address = b.gate_destination,
            .raw_relocation_type = 6U,
            .raw_addend = std::int64_t{0},
            .bytes = b.bytes,
        },
    };
    std::array<std::byte, 8> before{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, before).has_value());
    const auto result =
        astraea::execution::apply_owned_module_import_batch(patches, memory);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code ==
        astraea::execution::OwnedModuleImportBatchErrorCode::
            preflight_failure);
    REQUIRE(result.error().patch_index == 1U);
    REQUIRE(result.error().applied_count == 0U);
    REQUIRE(result.error().memory_error.has_value());
    REQUIRE(result.error().memory_error->code ==
        GuestMemoryErrorCode::guest_memory_unmapped);
    std::array<std::byte, 8> after{};
    REQUIRE(memory.read(GuestAddress{layout.data_base}, after).has_value());
    REQUIRE(after == before);
}

#endif
