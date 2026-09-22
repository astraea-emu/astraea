#include <astraea/execution/sce_agc_create_shader.hpp>
#include <astraea/execution/windows_memory.hpp>
#include <astraea/loader/guest_image.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
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
using astraea::execution::HleCall;
using astraea::execution::HleFunctionId;
using astraea::execution::SceAgcCreateShaderPlanErrorCode;
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

GuestImage empty_image() {
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

HleCall make_call(
    HleFunctionId id,
    std::uint64_t output,
    std::uint64_t header,
    std::uint64_t text) {
    return HleCall{
        .function_id = id,
        .gate_slot = 7,
        .guest_rip = 0x12345678U,
        .guest_rsp = 0x87654321U,
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

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

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
            std::byte{
                static_cast<unsigned char>(
                    (static_cast<std::uint64_t>(value) >>
                     (index * 8U)) &
                    0xffU)};
    }
}

struct RuntimeShaderFixture {
    std::vector<std::byte> header;
    std::vector<std::byte> text;
};

RuntimeShaderFixture make_shader_fixture() {
    constexpr std::size_t kHeaderSize = 0x80;
    constexpr std::size_t kTextSize = 0x60;
    constexpr std::size_t kTrailerOffset =
        kTextSize - 0x30;

    RuntimeShaderFixture fixture{
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
    write_little_endian<std::uint32_t>(
        fixture.header,
        0x40,
        static_cast<std::uint32_t>(kHeaderSize));
    write_little_endian<std::uint32_t>(
        fixture.header,
        0x44,
        static_cast<std::uint32_t>(kTextSize));
    fixture.header[0x5a] = std::byte{1};

    write_little_endian<std::uint32_t>(
        fixture.text,
        0,
        0xbf800000U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        4,
        0xbf810000U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        kTrailerOffset + 0x14,
        8U);
    write_little_endian<std::uint32_t>(
        fixture.text,
        kTrailerOffset + 0x1c,
        0U);

    return fixture;
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
        page * 4U <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                page * 4U));
    const auto code_base = base;
    const auto data_base = base + page;
    const auto stack_base = base + 3U * page;

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
        std::byte{0});

    ElfHeader header{};
    header.entry = code_base;

    const auto stack_pointer =
        GuestAddress{
            stack_base + page - 8U};

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
                        permissions(kRead | kWrite),
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
        .data_base = data_base,
        .stack_base = stack_base,
        .page = page,
    };
}

#endif

}  // namespace

TEST_CASE(
    "sceAgcCreateShader planner rejects the wrong HLE identity before guest memory",
    "[execution][agc][create-shader]") {
    auto image = empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    const auto result =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    HleFunctionId{99},
                    0x1000,
                    0,
                    0),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcCreateShaderPlanErrorCode::
            unexpected_function);
    REQUIRE(
        result.error().function_id ==
        std::optional<HleFunctionId>{
            HleFunctionId{99}});
    REQUIRE_FALSE(
        result.error().guest_memory_error.has_value());
}

TEST_CASE(
    "sceAgcCreateShader planner rejects null input pointers before guest memory",
    "[execution][agc][create-shader]") {
    auto image = empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    SECTION("null shader header") {
        const auto result =
            astraea::execution::
                plan_sce_agc_create_shader(
                    make_call(
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                        0x1000,
                        0,
                        0x3000),
                    memory);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcCreateShaderPlanErrorCode::
                null_shader_header);
        REQUIRE_FALSE(
            result.error().guest_memory_error.has_value());
    }

    SECTION("null shader text") {
        const auto result =
            astraea::execution::
                plan_sce_agc_create_shader(
                    make_call(
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                        0x1000,
                        0x2000,
                        0),
                    memory);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcCreateShaderPlanErrorCode::
                null_shader_text);
        REQUIRE_FALSE(
            result.error().guest_memory_error.has_value());
    }
}

TEST_CASE(
    "sceAgcCreateShader planner preserves fixed-header guest-memory failure",
    "[execution][agc][create-shader]") {
    auto image = empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    const auto result =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    astraea::execution::
                        kSceAgcCreateShaderHleId,
                    0x1000,
                    0x2000,
                    0x3000),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcCreateShaderPlanErrorCode::
            guest_memory_failure);
    REQUIRE(
        result.error().guest_memory_error.has_value());
    REQUIRE(
        result.error().guest_memory_error->code ==
        GuestMemoryErrorCode::
            prepared_memory_unavailable);
}

TEST_CASE(
    "GuestMemoryAccess accepts both prepared-memory backend types",
    "[execution][guest-memory][portability]") {
    STATIC_REQUIRE(
        std::is_constructible_v<
            GuestMemoryAccess,
            const GuestImage&,
            const astraea::execution::
                LinuxPreparedMemory&>);
    STATIC_REQUIRE(
        std::is_constructible_v<
            GuestMemoryAccess,
            const GuestImage&,
            const astraea::execution::
                WindowsPreparedMemory&>);
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "sceAgcCreateShader planner materializes the exact canonical runtime shader pair",
    "[execution][agc][create-shader][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto shader = make_shader_fixture();
    const auto output_address =
        layout.data_base + 0x20U;
    const auto header_address =
        layout.data_base + 0x100U;
    const auto text_address =
        layout.data_base + 0x300U;

    REQUIRE(
        memory.write(
            GuestAddress{header_address},
            shader.header)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{text_address},
            shader.text)
            .has_value());

    const std::array sentinel{
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
            GuestAddress{output_address},
            sentinel)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    astraea::execution::
                        kSceAgcCreateShaderHleId,
                    output_address,
                    header_address,
                    text_address),
                memory);

    REQUIRE(result.has_value());
    REQUIRE(
        result->request.output_pointer_address ==
        GuestAddress{output_address});
    REQUIRE(
        result->request.shader_header_address ==
        GuestAddress{header_address});
    REQUIRE(
        result->request.shader_text_address ==
        GuestAddress{text_address});
    REQUIRE(
        result->shader.shader_header_bytes ==
        shader.header);
    REQUIRE(
        result->shader.shader_text_bytes ==
        shader.text);
    REQUIRE(result->shader.rdna2_words.size() == 2);
    REQUIRE(
        result->shader.rdna2_words[0] ==
        0xbf800000U);
    REQUIRE(
        result->shader.rdna2_words[1] ==
        0xbf810000U);

    std::array<std::byte, 8> after{};
    REQUIRE(
        memory.read(
            GuestAddress{output_address},
            after)
            .has_value());
    REQUIRE(after == sentinel);
}

TEST_CASE(
    "sceAgcCreateShader planner rejects undersized declared extents before allocation",
    "[execution][agc][create-shader][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto header_address =
        layout.data_base + 0x100U;
    const auto text_address =
        layout.data_base + 0x300U;

    SECTION("header") {
        auto shader = make_shader_fixture();
        write_little_endian<std::uint32_t>(
            shader.header,
            0x40,
            95U);
        REQUIRE(
            memory.write(
                GuestAddress{header_address},
                shader.header)
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_create_shader(
                    make_call(
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                        layout.data_base + 0x20U,
                        header_address,
                        text_address),
                    memory);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcCreateShaderPlanErrorCode::
                declared_header_size_too_small);
    }

    SECTION("shader text") {
        auto shader = make_shader_fixture();
        write_little_endian<std::uint32_t>(
            shader.header,
            0x44,
            0x2fU);
        REQUIRE(
            memory.write(
                GuestAddress{header_address},
                shader.header)
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_create_shader(
                    make_call(
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                        layout.data_base + 0x20U,
                        header_address,
                        text_address),
                    memory);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcCreateShaderPlanErrorCode::
                declared_shader_text_size_too_small);
    }
}

TEST_CASE(
    "sceAgcCreateShader planner reports exact declared ranges that cross unmapped memory",
    "[execution][agc][create-shader][linux]") {
    SECTION("header range") {
        auto layout = make_mapped_fixture();
        auto prepared =
            astraea::execution::
                prepare_linux_guest_memory(
                    layout.image);
        REQUIRE(prepared.has_value());
        GuestMemoryAccess memory{
            layout.image,
            prepared.value()};

        auto shader = make_shader_fixture();
        const auto header_address =
            layout.data_base +
            layout.page -
            96U;
        REQUIRE(
            memory.write(
                GuestAddress{header_address},
                std::span<const std::byte>{
                    shader.header.data(),
                    96})
                .has_value());

        const auto result =
            astraea::execution::
                plan_sce_agc_create_shader(
                    make_call(
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                        layout.data_base + 0x20U,
                        header_address,
                        layout.data_base + 0x300U),
                    memory);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcCreateShaderPlanErrorCode::
                guest_memory_failure);
        REQUIRE(
            result.error().guest_memory_error.has_value());
        REQUIRE(
            result.error().guest_memory_error->code ==
            GuestMemoryErrorCode::
                guest_memory_unmapped);
    }

    SECTION("shader-text range") {
        auto layout = make_mapped_fixture();
        auto prepared =
            astraea::execution::
                prepare_linux_guest_memory(
                    layout.image);
        REQUIRE(prepared.has_value());
        GuestMemoryAccess memory{
            layout.image,
            prepared.value()};

        auto shader = make_shader_fixture();
        const auto header_address =
            layout.data_base + 0x100U;
        REQUIRE(
            memory.write(
                GuestAddress{header_address},
                shader.header)
                .has_value());

        const auto text_address =
            layout.data_base +
            layout.page -
            0x30U;
        const auto result =
            astraea::execution::
                plan_sce_agc_create_shader(
                    make_call(
                        astraea::execution::
                            kSceAgcCreateShaderHleId,
                        layout.data_base + 0x20U,
                        header_address,
                        text_address),
                    memory);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SceAgcCreateShaderPlanErrorCode::
                guest_memory_failure);
        REQUIRE(
            result.error().guest_memory_error.has_value());
        REQUIRE(
            result.error().guest_memory_error->code ==
            GuestMemoryErrorCode::
                guest_memory_unmapped);
    }
}

TEST_CASE(
    "sceAgcCreateShader planner preserves nested canonical AGC parser failures",
    "[execution][agc][create-shader][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(
                layout.image);
    REQUIRE(prepared.has_value());

    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    auto shader = make_shader_fixture();
    write_little_endian<std::uint32_t>(
        shader.header,
        0,
        0x12345678U);

    const auto header_address =
        layout.data_base + 0x100U;
    const auto text_address =
        layout.data_base + 0x300U;
    REQUIRE(
        memory.write(
            GuestAddress{header_address},
            shader.header)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{text_address},
            shader.text)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_create_shader(
                make_call(
                    astraea::execution::
                        kSceAgcCreateShaderHleId,
                    layout.data_base + 0x20U,
                    header_address,
                    text_address),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcCreateShaderPlanErrorCode::
            invalid_shader_binary);
    REQUIRE(
        result.error().shader_binary_error.has_value());
    REQUIRE(
        result.error().shader_binary_error->code ==
        astraea::graphics::
            AgcShaderBinaryErrorCode::
                bad_shader_header_magic);
}

#endif
