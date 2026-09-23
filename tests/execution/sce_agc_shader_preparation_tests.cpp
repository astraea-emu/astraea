#include <astraea/execution/sce_agc_shader_preparation.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/graphics/agc_shader_binary.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>
#include <astraea/memory/mapping.hpp>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

using astraea::execution::GuestMemoryAccess;
using astraea::execution::HleCall;
using astraea::execution::SceAgcCreateShaderPlan;
using astraea::execution::SceAgcShaderApplyErrorCode;
using astraea::execution::SceAgcShaderPatch;
using astraea::execution::SceAgcShaderPatchKind;
using astraea::execution::SceAgcShaderPreparationErrorCode;
using astraea::execution::SceAgcShaderPreparationPlan;
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

constexpr std::size_t kHeaderSize = 0x160;
constexpr std::size_t kTextSize = 0x80;
constexpr std::size_t kContextOffset = 0xc8;
constexpr std::size_t kShaderOffset = 0x98;
constexpr std::size_t kUserDataOffset = 0x110;
constexpr std::size_t kDirectResourceOffset = 0x148;

template <typename T>
void write_little_endian(
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

template <typename T>
T read_little_endian(
    std::span<const std::byte> bytes) {
    static_assert(std::is_unsigned_v<T>);

    REQUIRE(bytes.size() >= sizeof(T));
    std::uint64_t value = 0;
    for (std::size_t index = 0;
         index < sizeof(T);
         ++index) {
        value |=
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[index]))
            << (index * 8U);
    }
    return static_cast<T>(value);
}

struct ShaderFixture {
    std::vector<std::byte> header;
    std::vector<std::byte> text;
};

ShaderFixture make_public_shape_pixel_fixture() {
    ShaderFixture fixture{
        .header =
            std::vector<std::byte>(
                kHeaderSize,
                std::byte{0}),
        .text =
            std::vector<std::byte>(
                kTextSize,
                std::byte{0}),
    };

    write_little_endian<std::uint32_t>(
        fixture.header,
        0,
        0x34333231U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        4,
        0x18U);

    // Public-sample-shaped self-relative top-level pointers.
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x08,
        kUserDataOffset - 0x08U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x18,
        kContextOffset - 0x18U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x20,
        kShaderOffset - 0x20U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x28,
        0x60U - 0x28U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x30,
        0x90U - 0x30U);
    write_little_endian<std::uint64_t>(
        fixture.header,
        0x38,
        0U);

    write_little_endian<std::uint32_t>(
        fixture.header,
        0x40,
        static_cast<std::uint32_t>(
            fixture.header.size()));
    write_little_endian<std::uint32_t>(
        fixture.header,
        0x44,
        static_cast<std::uint32_t>(
            fixture.text.size()));
    fixture.header[0x5a] = std::byte{1};
    fixture.header[0x5b] = std::byte{1};
    fixture.header[0x5c] = std::byte{2};

    // First two shader-register records are pixel PGM_LO/HI.
    write_little_endian<std::uint16_t>(
        fixture.header,
        kShaderOffset,
        0x0008U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        kShaderOffset + 4U,
        0xaaaaaaaaU);
    write_little_endian<std::uint16_t>(
        fixture.header,
        kShaderOffset + 8U,
        0x0009U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        kShaderOffset + 12U,
        0xbbbbbbbbU);

    write_little_endian<std::uint16_t>(
        fixture.header,
        kContextOffset,
        0x01c4U);
    write_little_endian<std::uint32_t>(
        fixture.header,
        kContextOffset + 4U,
        0x00000004U);

    // Public-sample-shaped nested user-data pointer qwords.
    write_little_endian<std::uint64_t>(
        fixture.header,
        kUserDataOffset + 0x00U,
        kDirectResourceOffset -
            (kUserDataOffset + 0x00U));
    write_little_endian<std::uint64_t>(
        fixture.header,
        kUserDataOffset + 0x08U,
        kHeaderSize -
            (kUserDataOffset + 0x08U));
    write_little_endian<std::uint64_t>(
        fixture.header,
        kUserDataOffset + 0x10U,
        kHeaderSize -
            (kUserDataOffset + 0x10U));
    write_little_endian<std::uint64_t>(
        fixture.header,
        kUserDataOffset + 0x18U,
        kHeaderSize -
            (kUserDataOffset + 0x18U));
    write_little_endian<std::uint64_t>(
        fixture.header,
        kUserDataOffset + 0x20U,
        kHeaderSize -
            (kUserDataOffset + 0x20U));

    // Unknown byte used to prove the planner/apply path does not rewrite
    // unrelated header state.
    fixture.header[0x70] = std::byte{0x5a};

    write_little_endian<std::uint32_t>(
        fixture.text,
        0,
        0xbf800000U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        4,
        0xbf810000U);
    const auto trailer =
        fixture.text.size() - 0x30U;
    write_little_endian<std::uint32_t>(
        fixture.text,
        trailer + 0x14U,
        8U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        trailer + 0x1cU,
        0U);

    return fixture;
}

ShaderFixture make_public_shape_geometry_fixture() {
    auto fixture =
        make_public_shape_pixel_fixture();

    fixture.header[0x5a] = std::byte{2};

    // Evidence-bounded type-2 Geometry/fused-pre-raster profile uses the
    // leading ES PGM_LO/HI register pair.
    write_little_endian<std::uint16_t>(
        fixture.header,
        kShaderOffset,
        0x00c8U);
    write_little_endian<std::uint16_t>(
        fixture.header,
        kShaderOffset + 8U,
        0x00c9U);

    // Distinct unknown byte used by the type-2 apply regression.
    fixture.header[0x71] = std::byte{0x6b};

    return fixture;
}

SceAgcCreateShaderPlan make_create_plan(
    std::uint64_t output_address = 0x00100000U,
    std::uint64_t header_address = 0x00200000U,
    std::uint64_t text_address = 0x00300000U) {
    auto fixture =
        make_public_shape_pixel_fixture();
    auto shader =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);
    REQUIRE(shader.has_value());

    return SceAgcCreateShaderPlan{
        .request =
            {
                .output_pointer_address =
                    GuestAddress{output_address},
                .shader_header_address =
                    GuestAddress{header_address},
                .shader_text_address =
                    GuestAddress{text_address},
            },
        .shader = std::move(shader).value(),
    };
}

SceAgcCreateShaderPlan make_geometry_create_plan(
    std::uint64_t output_address = 0x00100000U,
    std::uint64_t header_address = 0x00200000U,
    std::uint64_t text_address = 0x00300000U) {
    auto fixture =
        make_public_shape_geometry_fixture();
    auto shader =
        astraea::graphics::
            parse_agc_shader_binary(
                fixture.header,
                fixture.text);
    REQUIRE(shader.has_value());

    return SceAgcCreateShaderPlan{
        .request =
            {
                .output_pointer_address =
                    GuestAddress{output_address},
                .shader_header_address =
                    GuestAddress{header_address},
                .shader_text_address =
                    GuestAddress{text_address},
            },
        .shader = std::move(shader).value(),
    };
}

const SceAgcShaderPatch& require_patch(
    const SceAgcShaderPreparationPlan& plan,
    SceAgcShaderPatchKind kind) {
    for (const auto& patch : plan.patches) {
        if (patch.kind == kind) {
            return patch;
        }
    }
    FAIL_CHECK("expected AGC preparation patch kind");
    REQUIRE_FALSE(plan.patches.empty());
    return plan.patches.front();
}

std::uint64_t patch_u64(
    const SceAgcShaderPreparationPlan& plan,
    SceAgcShaderPatchKind kind) {
    const auto& patch =
        require_patch(plan, kind);
    return read_little_endian<std::uint64_t>(
        patch.bytes);
}

std::uint32_t patch_u32(
    const SceAgcShaderPreparationPlan& plan,
    SceAgcShaderPatchKind kind) {
    const auto& patch =
        require_patch(plan, kind);
    return read_little_endian<std::uint32_t>(
        patch.bytes);
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

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
    std::uint64_t code_base = 0;
    std::uint64_t data_base = 0;
    std::uint64_t stack_base = 0;
    std::uint64_t page = 0;
};

MappedFixture make_mapped_fixture() {
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
        page * 5U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 5U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base = base + 4U * page;

    std::vector<std::byte> image_bytes{
        std::byte{0x0f},
        std::byte{0x0b},
    };
    const auto data_file_offset =
        static_cast<std::uint64_t>(
            image_bytes.size());
    image_bytes.resize(
        image_bytes.size() +
        static_cast<std::size_t>(
            page * 3U),
        std::byte{0});

    ElfHeader elf_header{};
    elf_header.entry = code_base;

    const auto stack_pointer =
        GuestAddress{
            stack_base + page - 8U};

    GuestImage image{
        .image_bytes = std::move(image_bytes),
        .elf =
            ElfImage{
                .header = elf_header,
                .program_headers = {},
            },
        .mappings =
            {
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
                    .range =
                        range(
                            data_base,
                            page * 3U),
                    .permissions =
                        permissions(kRead | kWrite),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset =
                                data_file_offset,
                            .byte_count =
                                GuestSize{
                                    page * 3U},
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

    return MappedFixture{
        .image = std::move(image),
        .code_base = code_base,
        .data_base = data_base,
        .stack_base = stack_base,
        .page = page,
    };
}

HleCall make_call(
    std::uint64_t output,
    std::uint64_t header,
    std::uint64_t text) {
    return HleCall{
        .function_id =
            astraea::execution::
                kSceAgcCreateShaderHleId,
        .gate_slot = 0,
        .guest_rip = 0,
        .guest_rsp = 0,
        .arguments =
            {
                output,
                header,
                text,
                0,
                0,
                0,
            },
    };
}

template <typename T>
T read_guest_value(
    const GuestMemoryAccess& memory,
    std::uint64_t address) {
    std::array<std::byte, sizeof(T)> bytes{};
    auto result =
        memory.read(
            GuestAddress{address},
            bytes);
    REQUIRE(result.has_value());
    return read_little_endian<T>(bytes);
}

#endif

}  // namespace

TEST_CASE(
    "pixel AGC preparation plan exposes exact public-shape guest mutations",
    "[execution][agc][prepare][v1]") {
    const auto create = make_create_plan();
    const auto original_header =
        create.shader.shader_header_bytes;

    const auto result =
        astraea::execution::
            plan_sce_agc_shader_preparation(
                create);
    REQUIRE(result.has_value());

    const auto& plan = result.value();
    REQUIRE(
        plan.profile ==
        astraea::execution::SceAgcShaderPreparationProfile::
            v18_pixel_public_shape);
    REQUIRE(
        plan.shader_handle ==
        create.request.shader_header_address);
    REQUIRE(
        plan.create_shader.shader.shader_header_bytes ==
        original_header);
    REQUIRE(plan.patches.size() == 14);
    REQUIRE(
        plan.patches.back().kind ==
        SceAgcShaderPatchKind::output_handle);

    const auto header =
        create.request.shader_header_address.value();
    const auto text =
        create.request.shader_text_address.value();

    REQUIRE(
        require_patch(
            plan,
            SceAgcShaderPatchKind::
                user_data_pointer)
            .address ==
        GuestAddress{header + 0x08U});
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                user_data_pointer) ==
        header + kUserDataOffset);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                code_pointer) ==
        text);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                context_register_pointer) ==
        header + kContextOffset);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                shader_register_pointer) ==
        header + kShaderOffset);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                specials_pointer) ==
        header + 0x60U);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                input_semantics_pointer) ==
        header + 0x90U);

    for (const auto& patch : plan.patches) {
        REQUIRE(
            patch.kind !=
            SceAgcShaderPatchKind::
                output_semantics_pointer);
    }

    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                user_data_direct_resource_pointer) ==
        header + kDirectResourceOffset);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                user_data_sharp_resource_pointer_0) ==
        header + kHeaderSize);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                user_data_sharp_resource_pointer_1) ==
        header + kHeaderSize);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                user_data_sharp_resource_pointer_2) ==
        header + kHeaderSize);
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                user_data_sharp_resource_pointer_3) ==
        header + kHeaderSize);

    REQUIRE(
        require_patch(
            plan,
            SceAgcShaderPatchKind::
                pixel_pgm_lo_value)
            .address ==
        GuestAddress{
            header + kShaderOffset + 4U});
    REQUIRE(
        patch_u32(
            plan,
            SceAgcShaderPatchKind::
                pixel_pgm_lo_value) ==
        static_cast<std::uint32_t>(
            (text >> 8U) & 0xffffffffU));
    REQUIRE(
        patch_u32(
            plan,
            SceAgcShaderPatchKind::
                pixel_pgm_hi_value) ==
        static_cast<std::uint32_t>(
            (text >> 40U) & 0xffU));
    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                output_handle) ==
        header);
}

TEST_CASE(
    "Geometry AGC preparation plan patches the evidenced ES program pair",
    "[execution][agc][prepare][v3][geometry]") {
    const auto create =
        make_geometry_create_plan();
    REQUIRE(
        create.shader.program_type.raw ==
        2U);
    REQUIRE(
        create.shader.program_type.known ==
        std::optional<
            astraea::graphics::AgcShaderStage>{
            astraea::graphics::AgcShaderStage::
                geometry});

    const auto original_header =
        create.shader.shader_header_bytes;
    const auto result =
        astraea::execution::
            plan_sce_agc_shader_preparation(
                create);

    REQUIRE(result.has_value());
    const auto& plan = result.value();
    REQUIRE(
        plan.profile ==
        astraea::execution::SceAgcShaderPreparationProfile::
            v18_geometry_es_public_shape);
    REQUIRE(
        plan.shader_handle ==
        create.request.shader_header_address);
    REQUIRE(
        plan.create_shader.shader.shader_header_bytes ==
        original_header);
    REQUIRE(
        plan.patches.back().kind ==
        SceAgcShaderPatchKind::output_handle);

    const auto header =
        create.request.shader_header_address.value();
    const auto text =
        create.request.shader_text_address.value();

    REQUIRE(
        require_patch(
            plan,
            SceAgcShaderPatchKind::
                geometry_es_pgm_lo_value)
            .address ==
        GuestAddress{
            header + kShaderOffset + 4U});
    REQUIRE(
        patch_u32(
            plan,
            SceAgcShaderPatchKind::
                geometry_es_pgm_lo_value) ==
        static_cast<std::uint32_t>(
            (text >> 8U) & 0xffffffffU));
    REQUIRE(
        require_patch(
            plan,
            SceAgcShaderPatchKind::
                geometry_es_pgm_hi_value)
            .address ==
        GuestAddress{
            header + kShaderOffset + 12U});
    REQUIRE(
        patch_u32(
            plan,
            SceAgcShaderPatchKind::
                geometry_es_pgm_hi_value) ==
        static_cast<std::uint32_t>(
            (text >> 40U) & 0xffU));

    for (const auto& patch : plan.patches) {
        REQUIRE(
            patch.kind !=
            SceAgcShaderPatchKind::
                pixel_pgm_lo_value);
        REQUIRE(
            patch.kind !=
            SceAgcShaderPatchKind::
                pixel_pgm_hi_value);
    }

    REQUIRE(
        patch_u64(
            plan,
            SceAgcShaderPatchKind::
                output_handle) ==
        header);
}

TEST_CASE(
    "Geometry AGC preparation rejects wrong leading program-register pair",
    "[execution][agc][prepare][v3][geometry][negative]") {
    SECTION("pixel pair") {
        auto create =
            make_geometry_create_plan();
        create.shader.shader_registers[0]
            .register_offset = 0x0008U;
        create.shader.shader_registers[1]
            .register_offset = 0x0009U;

        const auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                unsupported_geometry_program_register_pair);
    }

    SECTION("GS pair") {
        auto create =
            make_geometry_create_plan();
        create.shader.shader_registers[0]
            .register_offset = 0x0088U;
        create.shader.shader_registers[1]
            .register_offset = 0x0089U;

        const auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                unsupported_geometry_program_register_pair);
    }
}

TEST_CASE(
    "pixel AGC preparation rejects unsupported or unsafe profiles before mutation",
    "[execution][agc][prepare][v1][errors]") {
    SECTION("null output") {
        auto create =
            make_create_plan(
                0,
                0x00200000U,
                0x00300000U);
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                null_output_pointer);
    }

    SECTION("wrong version") {
        auto create = make_create_plan();
        create.shader.header_version = 0x19U;
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                unsupported_header_version);
    }

    SECTION("wrong stage") {
        auto create = make_create_plan();
        create.shader.program_type.raw = 0;
        create.shader.program_type.known =
            astraea::graphics::AgcShaderStage::
                compute;
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                unsupported_shader_stage);
    }

    SECTION("wrong PGM pair") {
        auto create = make_create_plan();
        create.shader.shader_registers[0]
            .register_offset = 0x000aU;
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                unsupported_pixel_program_register_pair);
    }

    SECTION("misaligned code") {
        auto create =
            make_create_plan(
                0x00100000U,
                0x00200000U,
                0x00300001U);
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                shader_code_address_misaligned);
    }

    SECTION("unrepresentable code") {
        auto create =
            make_create_plan(
                0x00100000U,
                0x00200000U,
                0x0001000000000000ULL);
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                shader_code_address_unrepresentable);
    }

    SECTION("canonical list provenance mismatch") {
        auto create = make_create_plan();
        create.shader
            .shader_register_list_header_offset =
            0x88U;
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                canonical_register_list_mismatch);
    }

    SECTION("top-level pointer target out of header") {
        auto create = make_create_plan();
        write_little_endian<std::uint64_t>(
            create.shader.shader_header_bytes,
            0x28,
            0x1000U);
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                self_relative_pointer_out_of_bounds);
    }

    SECTION("user-data object cannot hold five pointer fields") {
        auto create = make_create_plan();
        write_little_endian<std::uint64_t>(
            create.shader.shader_header_bytes,
            0x08,
            (kHeaderSize - 0x10U) - 0x08U);
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                user_data_too_small);
    }

    SECTION("output aliases shader header") {
        auto create =
            make_create_plan(
                0x00200070U,
                0x00200000U,
                0x00300000U);
        auto result =
            astraea::execution::
                plan_sce_agc_shader_preparation(
                    create);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcShaderPreparationErrorCode::
                output_aliases_shader_input);
    }
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "pixel AGC preparation applies exact guest mutations and preserves unknown bytes",
    "[execution][agc][prepare][apply][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto fixture =
        make_public_shape_pixel_fixture();
    const auto output =
        layout.data_base + 0x20U;
    const auto header =
        layout.data_base + 0x100U;
    const auto text =
        layout.data_base + layout.page;

    REQUIRE((text & 0xffU) == 0U);
    REQUIRE(
        memory.write(
            GuestAddress{header},
            fixture.header)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{text},
            fixture.text)
            .has_value());

    std::array<std::byte, 8> output_sentinel{
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30},
        std::byte{0x40},
        std::byte{0x50},
        std::byte{0x60},
        std::byte{0x70},
        std::byte{0x80},
    };
    REQUIRE(
        memory.write(
            GuestAddress{output},
            output_sentinel)
            .has_value());

    auto captured =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    output,
                    header,
                    text),
                memory);
    REQUIRE(captured.has_value());

    auto plan =
        astraea::execution::
            plan_sce_agc_shader_preparation(
                captured.value());
    REQUIRE(plan.has_value());

    auto applied =
        astraea::execution::
            apply_sce_agc_shader_preparation(
                plan.value(),
                memory);
    REQUIRE(applied.has_value());
    REQUIRE(
        applied->applied_patch_count ==
        plan->patches.size());
    REQUIRE(
        applied->shader_handle ==
        GuestAddress{header});

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
        header + kContextOffset);
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            header + 0x20U) ==
        header + kShaderOffset);
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            header + 0x28U) ==
        header + 0x60U);
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            header + 0x30U) ==
        header + 0x90U);
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            header + 0x38U) ==
        0U);

    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            header + kUserDataOffset) ==
        header + kDirectResourceOffset);
    for (std::size_t index = 1;
         index < 5;
         ++index) {
        REQUIRE(
            read_guest_value<std::uint64_t>(
                memory,
                header +
                    kUserDataOffset +
                    index * 8U) ==
            header + kHeaderSize);
    }

    REQUIRE(
        read_guest_value<std::uint32_t>(
            memory,
            header + kShaderOffset + 4U) ==
        static_cast<std::uint32_t>(
            (text >> 8U) & 0xffffffffU));
    REQUIRE(
        read_guest_value<std::uint32_t>(
            memory,
            header + kShaderOffset + 12U) ==
        static_cast<std::uint32_t>(
            (text >> 40U) & 0xffU));
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            output) ==
        header);

    std::array<std::byte, 1> unknown{};
    REQUIRE(
        memory.read(
            GuestAddress{header + 0x70U},
            unknown)
            .has_value());
    REQUIRE(unknown[0] == std::byte{0x5a});
}

TEST_CASE(
    "Geometry AGC preparation applies ES program address and preserves unknown bytes",
    "[execution][agc][prepare][apply][linux][geometry]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto fixture =
        make_public_shape_geometry_fixture();
    const auto output =
        layout.data_base + 0x20U;
    const auto header =
        layout.data_base + 0x100U;
    const auto text =
        layout.data_base + layout.page;

    REQUIRE((text & 0xffU) == 0U);
    REQUIRE(
        memory.write(
            GuestAddress{header},
            fixture.header)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{text},
            fixture.text)
            .has_value());

    std::array<std::byte, 8> output_sentinel{
        std::byte{0x10},
        std::byte{0x20},
        std::byte{0x30},
        std::byte{0x40},
        std::byte{0x50},
        std::byte{0x60},
        std::byte{0x70},
        std::byte{0x80},
    };
    REQUIRE(
        memory.write(
            GuestAddress{output},
            output_sentinel)
            .has_value());

    auto captured =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    output,
                    header,
                    text),
                memory);
    REQUIRE(captured.has_value());
    REQUIRE(
        captured->shader.program_type.known ==
        std::optional<
            astraea::graphics::AgcShaderStage>{
            astraea::graphics::AgcShaderStage::
                geometry});

    auto plan =
        astraea::execution::
            plan_sce_agc_shader_preparation(
                captured.value());
    REQUIRE(plan.has_value());
    REQUIRE(
        plan->profile ==
        astraea::execution::
            SceAgcShaderPreparationProfile::
                v18_geometry_es_public_shape);

    auto applied =
        astraea::execution::
            apply_sce_agc_shader_preparation(
                plan.value(),
                memory);
    REQUIRE(applied.has_value());
    REQUIRE(
        applied->applied_patch_count ==
        plan->patches.size());
    REQUIRE(
        applied->shader_handle ==
        GuestAddress{header});

    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            header + 0x10U) ==
        text);
    REQUIRE(
        read_guest_value<std::uint32_t>(
            memory,
            header + kShaderOffset + 4U) ==
        static_cast<std::uint32_t>(
            (text >> 8U) & 0xffffffffU));
    REQUIRE(
        read_guest_value<std::uint32_t>(
            memory,
            header + kShaderOffset + 12U) ==
        static_cast<std::uint32_t>(
            (text >> 40U) & 0xffU));
    REQUIRE(
        read_guest_value<std::uint64_t>(
            memory,
            output) ==
        header);

    std::array<std::byte, 1> unknown{};
    REQUIRE(
        memory.read(
            GuestAddress{header + 0x71U},
            unknown)
            .has_value());
    REQUIRE(unknown[0] == std::byte{0x6b});
}

TEST_CASE(
    "AGC preparation preflight failure leaves header byte-identical",
    "[execution][agc][prepare][apply][atomic][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto fixture =
        make_public_shape_pixel_fixture();
    const auto header =
        layout.data_base + 0x100U;
    const auto text =
        layout.data_base + layout.page;

    REQUIRE(
        memory.write(
            GuestAddress{header},
            fixture.header)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{text},
            fixture.text)
            .has_value());

    // The RX code mapping is intentionally not writable. It is the final
    // output-handle patch, so preflight must reject it before any header
    // mutation occurs.
    auto captured =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    layout.code_base,
                    header,
                    text),
                memory);
    REQUIRE(captured.has_value());

    auto plan =
        astraea::execution::
            plan_sce_agc_shader_preparation(
                captured.value());
    REQUIRE(plan.has_value());
    REQUIRE(
        plan->patches.back().kind ==
        SceAgcShaderPatchKind::output_handle);

    std::vector<std::byte> before(
        fixture.header.size());
    REQUIRE(
        memory.read(
            GuestAddress{header},
            before)
            .has_value());

    auto applied =
        astraea::execution::
            apply_sce_agc_shader_preparation(
                plan.value(),
                memory);
    REQUIRE_FALSE(applied.has_value());
    REQUIRE(
        applied.error().code ==
        SceAgcShaderApplyErrorCode::
            guest_memory_preflight_failure);
    REQUIRE(applied.error().applied_count == 0);
    REQUIRE(
        applied.error().patch_index ==
        plan->patches.size() - 1U);

    std::vector<std::byte> after(
        fixture.header.size());
    REQUIRE(
        memory.read(
            GuestAddress{header},
            after)
            .has_value());
    REQUIRE(after == before);
}

#endif
