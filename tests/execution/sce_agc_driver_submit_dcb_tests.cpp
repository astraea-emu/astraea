#include <astraea/execution/sce_agc_driver_submit_dcb.hpp>
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
using astraea::execution::SceAgcDriverSubmitDcbPlanErrorCode;
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
    std::uint64_t descriptor) {
    return HleCall{
        .function_id = id,
        .gate_slot = 11,
        .guest_rip = 0x12345678U,
        .guest_rsp = 0x87654321U,
        .arguments =
            {
                descriptor,
                0,
                0,
                0,
                0,
                0,
            },
    };
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

template <typename T>
void write_little_endian(
    std::span<std::byte> bytes,
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

std::array<
    std::byte,
    astraea::execution::
        kSceAgcDcbSubmitDescriptionSize>
make_descriptor(
    std::uint64_t words_address,
    std::uint32_t word_count,
    std::uint8_t flag,
    std::array<std::byte, 3> padding = {}) {
    std::array<
        std::byte,
        astraea::execution::
            kSceAgcDcbSubmitDescriptionSize>
        bytes{};
    write_little_endian<std::uint64_t>(
        bytes,
        0x00,
        words_address);
    write_little_endian<std::uint32_t>(
        bytes,
        0x08,
        word_count);
    bytes[0x0c] = std::byte{flag};
    bytes[0x0d] = padding[0];
    bytes[0x0e] = padding[1];
    bytes[0x0f] = padding[2];
    return bytes;
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

#endif

}  // namespace

TEST_CASE(
    "sceAgcDriverSubmitDcb planner rejects wrong HLE identity before guest memory",
    "[execution][agc][submit-dcb]") {
    auto image = empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    HleFunctionId{99},
                    0x1000U),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcDriverSubmitDcbPlanErrorCode::
            unexpected_function);
    REQUIRE(
        result.error().function_id ==
        std::optional<HleFunctionId>{
            HleFunctionId{99}});
    REQUIRE_FALSE(
        result.error().guest_memory_error.has_value());
}

TEST_CASE(
    "sceAgcDriverSubmitDcb planner rejects null descriptor before guest memory",
    "[execution][agc][submit-dcb]") {
    auto image = empty_image();
    astraea::execution::LinuxPreparedMemory prepared;
    GuestMemoryAccess memory{image, prepared};

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    0U),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcDriverSubmitDcbPlanErrorCode::
            null_submit_description);
    REQUIRE_FALSE(
        result.error().guest_memory_error.has_value());
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "sceAgcDriverSubmitDcb captures exact descriptor and command stream without writes",
    "[execution][agc][submit-dcb][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto descriptor_address =
        layout.data_base + 0x40U;
    const auto words_address =
        layout.data_base + 0x100U;

    const std::array command_bytes{
        std::byte{0x11},
        std::byte{0x22},
        std::byte{0x33},
        std::byte{0x44},
        std::byte{0x55},
        std::byte{0x66},
        std::byte{0x77},
        std::byte{0x88},
        std::byte{0x99},
        std::byte{0xaa},
        std::byte{0xbb},
        std::byte{0xcc},
    };
    const std::array padding{
        std::byte{0xa1},
        std::byte{0xb2},
        std::byte{0xc3},
    };
    const auto descriptor =
        make_descriptor(
            words_address,
            3U,
            0x5aU,
            padding);

    REQUIRE(
        memory.write(
            GuestAddress{descriptor_address},
            descriptor)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{words_address},
            command_bytes)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    descriptor_address),
                memory);

    REQUIRE(result.has_value());
    REQUIRE(
        result->submit_description_address ==
        GuestAddress{descriptor_address});
    REQUIRE(
        result->command_words_address ==
        GuestAddress{words_address});
    REQUIRE(result->word_count == 3U);
    REQUIRE(result->flag == 0x5aU);
    REQUIRE(
        result->raw_submit_description ==
        descriptor);
    REQUIRE(result->opaque_padding == padding);
    REQUIRE(
        result->command_buffer_bytes ==
        std::vector<std::byte>{
            command_bytes.begin(),
            command_bytes.end()});
    REQUIRE(
        result->command_buffer_bytes.size() %
            4U ==
        0U);

    std::array<
        std::byte,
        astraea::execution::
            kSceAgcDcbSubmitDescriptionSize>
        descriptor_after{};
    std::array<std::byte, command_bytes.size()>
        commands_after{};
    REQUIRE(
        memory.read(
            GuestAddress{descriptor_address},
            descriptor_after)
            .has_value());
    REQUIRE(
        memory.read(
            GuestAddress{words_address},
            commands_after)
            .has_value());
    REQUIRE(descriptor_after == descriptor);
    REQUIRE(commands_after == command_bytes);
}

TEST_CASE(
    "sceAgcDriverSubmitDcb zero-count capture does not dereference words pointer",
    "[execution][agc][submit-dcb][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto descriptor_address =
        layout.data_base + 0x40U;
    const auto descriptor =
        make_descriptor(
            0xdeadbeefULL,
            0U,
            7U,
            {
                std::byte{1},
                std::byte{2},
                std::byte{3},
            });
    REQUIRE(
        memory.write(
            GuestAddress{descriptor_address},
            descriptor)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    descriptor_address),
                memory);

    REQUIRE(result.has_value());
    REQUIRE(result->word_count == 0U);
    REQUIRE(
        result->command_words_address ==
        GuestAddress{0xdeadbeefULL});
    REQUIRE(result->command_buffer_bytes.empty());
}

TEST_CASE(
    "sceAgcDriverSubmitDcb rejects nonzero count with null words pointer",
    "[execution][agc][submit-dcb][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto descriptor_address =
        layout.data_base + 0x40U;
    const auto descriptor =
        make_descriptor(
            0U,
            1U,
            0U);
    REQUIRE(
        memory.write(
            GuestAddress{descriptor_address},
            descriptor)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    descriptor_address),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcDriverSubmitDcbPlanErrorCode::
            null_command_words);
}

TEST_CASE(
    "sceAgcDriverSubmitDcb rejects profile-overflow count before command read",
    "[execution][agc][submit-dcb][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto descriptor_address =
        layout.data_base + 0x40U;
    const auto descriptor =
        make_descriptor(
            0U,
            astraea::execution::
                kSceAgcDcbSupportedMaximumWordCount +
                1U,
            0U);
    REQUIRE(
        memory.write(
            GuestAddress{descriptor_address},
            descriptor)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    descriptor_address),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcDriverSubmitDcbPlanErrorCode::
            word_count_exceeds_supported_profile);
    REQUIRE_FALSE(
        result.error().guest_memory_error.has_value());
}

TEST_CASE(
    "sceAgcDriverSubmitDcb reports descriptor range crossing unmapped memory",
    "[execution][agc][submit-dcb][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto descriptor_address =
        layout.data_base +
        layout.page -
        8U;
    const std::array<std::byte, 8> prefix{};
    REQUIRE(
        memory.write(
            GuestAddress{descriptor_address},
            prefix)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    descriptor_address),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcDriverSubmitDcbPlanErrorCode::
            guest_memory_failure);
    REQUIRE(
        result.error().guest_memory_error.has_value());
    REQUIRE(
        result.error().guest_memory_error->code ==
        GuestMemoryErrorCode::guest_memory_unmapped);
}

TEST_CASE(
    "sceAgcDriverSubmitDcb reports exact command range crossing unmapped memory",
    "[execution][agc][submit-dcb][linux]") {
    auto layout = make_mapped_fixture();
    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(layout.image);
    REQUIRE(prepared.has_value());
    GuestMemoryAccess memory{
        layout.image,
        prepared.value()};

    const auto descriptor_address =
        layout.data_base + 0x40U;
    const auto words_address =
        layout.data_base +
        layout.page -
        4U;
    const auto descriptor =
        make_descriptor(
            words_address,
            2U,
            0U);
    const std::array<std::byte, 4> one_word{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
    };
    REQUIRE(
        memory.write(
            GuestAddress{descriptor_address},
            descriptor)
            .has_value());
    REQUIRE(
        memory.write(
            GuestAddress{words_address},
            one_word)
            .has_value());

    const auto result =
        astraea::execution::
            plan_sce_agc_driver_submit_dcb(
                make_call(
                    astraea::execution::
                        kSceAgcDriverSubmitDcbHleId,
                    descriptor_address),
                memory);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SceAgcDriverSubmitDcbPlanErrorCode::
            guest_memory_failure);
    REQUIRE(
        result.error().guest_memory_error.has_value());
    REQUIRE(
        result.error().guest_memory_error->code ==
        GuestMemoryErrorCode::guest_memory_unmapped);
}

#endif
