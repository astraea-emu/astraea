#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/sce_agc_link_shaders.hpp>
#include <astraea/loader/guest_image.hpp>

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
using astraea::execution::SceAgcLinkShadersMeasuredApplyErrorCode;
using astraea::execution::SceAgcLinkShadersMeasuredPatchKind;
using astraea::execution::SceAgcLinkShadersOutputCompleteness;
using astraea::execution::SceAgcLinkShadersPlan;
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

[[maybe_unused]] SceAgcLinkShadersPlan make_link_plan(
    std::uint64_t context_output,
    std::uint64_t user_config_output) {
    return SceAgcLinkShadersPlan{
        .context_output_address =
            GuestAddress{context_output},
        .context_output_range =
            range(
                context_output,
                astraea::execution::
                    kSceAgcLinkShadersContextOutputSize),
        .user_config_output_address =
            GuestAddress{user_config_output},
        .user_config_output_range =
            range(
                user_config_output,
                astraea::execution::
                    kSceAgcLinkShadersUserConfigOutputSize),
        .hull_shader_handle = GuestAddress{0},
        .pre_raster_shader =
            astraea::execution::
                SceAgcLinkedShaderIdentity{
                    .shader_handle = GuestAddress{0},
                    .code_address =
                        astraea::graphics::
                            GpuVirtualAddress{.value = 0},
                    .stage =
                        astraea::graphics::
                            AgcShaderStage::geometry,
                    .preparation_profile =
                        astraea::execution::
                            SceAgcShaderPreparationProfile::
                                v18_geometry_es_public_shape,
                },
        .pixel_shader =
            astraea::execution::
                SceAgcLinkedShaderIdentity{
                    .shader_handle = GuestAddress{0},
                    .code_address =
                        astraea::graphics::
                            GpuVirtualAddress{.value = 0},
                    .stage =
                        astraea::graphics::
                            AgcShaderStage::pixel,
                    .preparation_profile =
                        astraea::execution::
                            SceAgcShaderPreparationProfile::
                                v18_pixel_public_shape,
                },
        .primitive_type =
            astraea::execution::
                kSceAgcLinkShadersTriangleListPrimitiveType,
    };
}

[[maybe_unused]] std::array<std::byte, 8> record_bytes(
    std::uint32_t offset,
    std::uint32_t value) {
    std::array<std::byte, 8> bytes{};
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[index] =
            std::byte{
                static_cast<unsigned char>(
                    (offset >> (index * 8U)) &
                    0xffU)};
        bytes[4U + index] =
            std::byte{
                static_cast<unsigned char>(
                    (value >> (index * 8U)) &
                    0xffU)};
    }
    return bytes;
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

GuestPermissions permissions(std::uint8_t bits) {
    auto result =
        GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
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

struct MappedFixture {
    GuestImage image;
    std::uint64_t data_base = 0;
    std::uint64_t page = 0;
};

MappedFixture make_mapped_fixture(
    bool data_writable,
    std::byte initial_byte = std::byte{0}) {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);
    constexpr auto kExecute =
        static_cast<std::uint8_t>(
            GuestPermission::execute);

    const auto page = page_size();
    REQUIRE(
        page * 3U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 3U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base = base + 2U * page;

    std::vector<std::byte> image_bytes{
        std::byte{0x0f},
        std::byte{0x0b},
    };
    const auto data_file_offset =
        static_cast<std::uint64_t>(
            image_bytes.size());
    image_bytes.resize(
        image_bytes.size() +
            static_cast<std::size_t>(page),
        initial_byte);

    ElfHeader header{};
    header.entry = code_base;
    const auto stack_pointer =
        GuestAddress{
            stack_base + page - 8U};

    std::uint8_t data_permission_bits = kRead;
    if (data_writable) {
        data_permission_bits =
            static_cast<std::uint8_t>(
                data_permission_bits | kWrite);
    }

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
                    .range = range(code_base, 2),
                    .permissions =
                        permissions(kRead | kExecute),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = 0,
                            .byte_count = GuestSize{2},
                        },
                    .source_index = 0,
                },
                MappingIntent{
                    .range = range(data_base, page),
                    .permissions =
                        permissions(data_permission_bits),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset =
                                data_file_offset,
                            .byte_count =
                                GuestSize{page},
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
                    range(stack_base, page),
                .used_range =
                    range(
                        stack_pointer.value(),
                        0),
                .rsp = stack_pointer,
                .bytes = {},
            },
    };

    return MappedFixture{
        .image = std::move(image),
        .data_base = data_base,
        .page = page,
    };
}

template <std::size_t N>
std::array<std::byte, N> read_bytes(
    const GuestMemoryAccess& memory,
    std::uint64_t address) {
    std::array<std::byte, N> bytes{};
    REQUIRE(
        memory.read(
            GuestAddress{address},
            bytes)
            .has_value());
    return bytes;
}

#endif

}  // namespace

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "measured LinkShaders apply writes only proven context records and preserves every unknown byte",
    "[execution][agc][link-shaders][measured-output][apply][linux]") {
    auto fixture =
        make_mapped_fixture(
            true,
            std::byte{0x6d});
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                fixture.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        fixture.image,
        prepared.value()};

    const auto context =
        fixture.data_base + 0x100U;
    const auto user_config =
        fixture.data_base + 0x400U;

    constexpr std::array<std::byte, 8> context_before_guard{
        std::byte{0xa0}, std::byte{0xa1},
        std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5},
        std::byte{0xa6}, std::byte{0xa7},
    };
    constexpr std::array<std::byte, 8> context_after_guard{
        std::byte{0xb0}, std::byte{0xb1},
        std::byte{0xb2}, std::byte{0xb3},
        std::byte{0xb4}, std::byte{0xb5},
        std::byte{0xb6}, std::byte{0xb7},
    };
    constexpr std::array<std::byte, 8> uc_before_guard{
        std::byte{0xc0}, std::byte{0xc1},
        std::byte{0xc2}, std::byte{0xc3},
        std::byte{0xc4}, std::byte{0xc5},
        std::byte{0xc6}, std::byte{0xc7},
    };
    constexpr std::array<std::byte, 8> uc_after_guard{
        std::byte{0xd0}, std::byte{0xd1},
        std::byte{0xd2}, std::byte{0xd3},
        std::byte{0xd4}, std::byte{0xd5},
        std::byte{0xd6}, std::byte{0xd7},
    };
    std::array<
        std::byte,
        astraea::execution::
            kSceAgcLinkShadersContextOutputSize>
        context_sentinel{};
    context_sentinel.fill(std::byte{0x5a});
    std::array<
        std::byte,
        astraea::execution::
            kSceAgcLinkShadersUserConfigOutputSize>
        uc_sentinel{};
    uc_sentinel.fill(std::byte{0xa5});

    REQUIRE(
        memory.write(
            GuestAddress{context - 8U},
            context_before_guard)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{context},
            context_sentinel)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{
                context +
                astraea::execution::
                    kSceAgcLinkShadersContextOutputSize},
            context_after_guard)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{user_config - 8U},
            uc_before_guard)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{user_config},
            uc_sentinel)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{
                user_config +
                astraea::execution::
                    kSceAgcLinkShadersUserConfigOutputSize},
            uc_after_guard)
            .has_value());

    const auto request =
        make_link_plan(
            context,
            user_config);
    const auto measured =
        astraea::execution::
            materialize_sce_agc_link_shaders_measured_output(
                request);
    REQUIRE(measured.has_value());

    const auto applied =
        astraea::execution::
            apply_sce_agc_link_shaders_measured_output(
                measured.value(),
                memory);
    REQUIRE(applied.has_value());
    REQUIRE(
        applied->completeness ==
        SceAgcLinkShadersOutputCompleteness::
            measured_partial);
    REQUIRE(applied->applied_patch_count == 2U);
    REQUIRE(
        applied->applied_context_record_count ==
        33U);
    REQUIRE(
        applied->applied_user_config_record_count ==
        0U);

    for (std::uint32_t index = 0;
         index < 32U;
         ++index) {
        REQUIRE(
            read_bytes<8>(
                memory,
                context +
                    static_cast<std::uint64_t>(index) *
                        8U) ==
            record_bytes(
                0x191U + index,
                index));
    }

    REQUIRE(
        read_bytes<8>(
            memory,
            context + 0x100U) ==
        std::array<std::byte, 8>{
            std::byte{0x5a}, std::byte{0x5a},
            std::byte{0x5a}, std::byte{0x5a},
            std::byte{0x5a}, std::byte{0x5a},
            std::byte{0x5a}, std::byte{0x5a},
        });
    REQUIRE(
        read_bytes<8>(
            memory,
            context + 0x108U) ==
        record_bytes(
            0x29bU,
            2U));

    REQUIRE(
        read_bytes<
            astraea::execution::
                kSceAgcLinkShadersUserConfigOutputSize>(
            memory,
            user_config) ==
        uc_sentinel);

    REQUIRE(
        read_bytes<8>(
            memory,
            context - 8U) ==
        context_before_guard);
    REQUIRE(
        read_bytes<8>(
            memory,
            context +
                astraea::execution::
                    kSceAgcLinkShadersContextOutputSize) ==
        context_after_guard);
    REQUIRE(
        read_bytes<8>(
            memory,
            user_config - 8U) ==
        uc_before_guard);
    REQUIRE(
        read_bytes<8>(
            memory,
            user_config +
                astraea::execution::
                    kSceAgcLinkShadersUserConfigOutputSize) ==
        uc_after_guard);
}

TEST_CASE(
    "measured LinkShaders apply preflights both patches before any mutation",
    "[execution][agc][link-shaders][measured-output][apply][atomic][linux]") {
    SECTION("interpolant patch is read-only") {
        auto fixture =
            make_mapped_fixture(
                false,
                std::byte{0x7c});
        auto prepared =
            astraea::execution::
                prepare_linux_guest_memory(
                    fixture.image);
        REQUIRE(prepared.has_value());
        GuestMemoryAccess memory{
            fixture.image,
            prepared.value()};

        const auto context =
            fixture.data_base + 0x100U;
        const auto user_config =
            fixture.data_base + 0x400U;
        const auto before =
            read_bytes<
                astraea::execution::
                    kSceAgcLinkShadersContextOutputSize>(
                memory,
                context);

        const auto measured =
            astraea::execution::
                materialize_sce_agc_link_shaders_measured_output(
                    make_link_plan(
                        context,
                        user_config));
        REQUIRE(measured.has_value());

        const auto applied =
            astraea::execution::
                apply_sce_agc_link_shaders_measured_output(
                    measured.value(),
                    memory);
        REQUIRE_FALSE(applied.has_value());
        REQUIRE(
            applied.error().code ==
            SceAgcLinkShadersMeasuredApplyErrorCode::
                guest_memory_preflight_failure);
        REQUIRE(
            applied.error().patch_kind ==
            SceAgcLinkShadersMeasuredPatchKind::
                interpolant_table);
        REQUIRE(applied.error().patch_index == 0U);
        REQUIRE(
            applied.error().applied_patch_count ==
            0U);
        REQUIRE(
            applied.error().applied_record_count ==
            0U);
        REQUIRE(
            applied.error().guest_memory_error.has_value());
        REQUIRE(
            applied.error().guest_memory_error->code ==
            GuestMemoryErrorCode::
                guest_memory_permission_denied);

        REQUIRE(
            read_bytes<
                astraea::execution::
                    kSceAgcLinkShadersContextOutputSize>(
                memory,
                context) ==
            before);
    }

    SECTION("routing patch falls just beyond the writable mapping") {
        auto fixture =
            make_mapped_fixture(
                true,
                std::byte{0x3e});
        auto prepared =
            astraea::execution::
                prepare_linux_guest_memory(
                    fixture.image);
        REQUIRE(prepared.has_value());
        GuestMemoryAccess memory{
            fixture.image,
            prepared.value()};

        // The first 0x100 measured bytes and the preserved +0x100 record fit
        // in the mapped page. The measured +0x108 routing record begins
        // exactly at the first unmapped byte.
        const auto context =
            fixture.data_base +
            fixture.page -
            0x108U;
        const auto user_config =
            fixture.data_base + 0x100U;

        std::array<std::byte, 0x108> sentinel{};
        sentinel.fill(std::byte{0x4f});
        REQUIRE(
            memory.write(
                GuestAddress{context},
                sentinel)
                .has_value());
        const auto before =
            read_bytes<0x108>(
                memory,
                context);

        const auto measured =
            astraea::execution::
                materialize_sce_agc_link_shaders_measured_output(
                    make_link_plan(
                        context,
                        user_config));
        REQUIRE(measured.has_value());

        const auto applied =
            astraea::execution::
                apply_sce_agc_link_shaders_measured_output(
                    measured.value(),
                    memory);
        REQUIRE_FALSE(applied.has_value());
        REQUIRE(
            applied.error().code ==
            SceAgcLinkShadersMeasuredApplyErrorCode::
                guest_memory_preflight_failure);
        REQUIRE(
            applied.error().patch_kind ==
            SceAgcLinkShadersMeasuredPatchKind::
                gs_out_primitive_type);
        REQUIRE(applied.error().patch_index == 1U);
        REQUIRE(
            applied.error().applied_patch_count ==
            0U);
        REQUIRE(
            applied.error().applied_record_count ==
            0U);
        REQUIRE(
            applied.error().guest_memory_error.has_value());
        REQUIRE(
            applied.error().guest_memory_error->code ==
            GuestMemoryErrorCode::
                guest_memory_unmapped);

        REQUIRE(
            read_bytes<0x108>(
                memory,
                context) ==
            before);
    }
}

#endif
