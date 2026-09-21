#include <astraea/execution/windows_memory.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32) && defined(_M_X64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#if defined(_WIN32) && defined(_M_X64)

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

GuestPermissions permissions(
    std::uint8_t bits) {
    auto result =
        GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
}

struct WindowsGeometry {
    std::uint64_t page = 0;
    std::uint64_t granularity = 0;
};

WindowsGeometry geometry() {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    REQUIRE(info.dwPageSize != 0);
    REQUIRE(
        info.dwAllocationGranularity != 0);
    return WindowsGeometry{
        .page =
            static_cast<std::uint64_t>(
                info.dwPageSize),
        .granularity =
            static_cast<std::uint64_t>(
                info.dwAllocationGranularity),
    };
}

std::uint64_t find_free_block(
    std::size_t size) {
    void* const reserved =
        ::VirtualAlloc(
            nullptr,
            size,
            MEM_RESERVE,
            PAGE_NOACCESS);
    REQUIRE(reserved != nullptr);
    const auto base =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                reserved));
    REQUIRE(
        ::VirtualFree(
            reserved,
            0,
            MEM_RELEASE) != 0);
    return base;
}

GuestImage make_guest_image(
    std::uint64_t base,
    std::uint64_t page) {
    constexpr auto kRead =
        static_cast<std::uint8_t>(
            GuestPermission::read);
    constexpr auto kWrite =
        static_cast<std::uint8_t>(
            GuestPermission::write);
    constexpr auto kExecute =
        static_cast<std::uint8_t>(
            GuestPermission::execute);

    const std::vector<std::byte> code{
        std::byte{0x0f},
        std::byte{0x0b},
    };
    const std::vector<std::byte> data{
        std::byte{'d'},
        std::byte{'a'},
        std::byte{'t'},
        std::byte{'a'},
    };

    std::vector<std::byte> image_bytes;
    image_bytes.insert(
        image_bytes.end(),
        code.begin(),
        code.end());
    image_bytes.insert(
        image_bytes.end(),
        data.begin(),
        data.end());

    const auto data_base =
        base + page;
    const auto stack_base =
        base + 2U * page;
    const auto stack_used_base =
        stack_base + page - 8U;

    ElfHeader header{};
    header.entry = base;

    return GuestImage{
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
                        range(base, 2),
                    .permissions =
                        permissions(
                            kRead | kExecute),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = 0,
                            .byte_count =
                                GuestSize{2},
                        },
                    .source_index = 0,
                },
                MappingIntent{
                    .range =
                        range(
                            data_base,
                            4),
                    .permissions =
                        permissions(
                            kRead | kWrite),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::file,
                            .file_offset = 2,
                            .byte_count =
                                GuestSize{4},
                        },
                    .source_index = 1,
                },
                MappingIntent{
                    .range =
                        range(
                            data_base + 4U,
                            4),
                    .permissions =
                        permissions(
                            kRead | kWrite),
                    .backing =
                        MappingBacking{
                            .kind =
                                MappingBackingKind::zero_fill,
                            .file_offset = 0,
                            .byte_count =
                                GuestSize{4},
                        },
                    .source_index = 2,
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
                        stack_used_base,
                        8),
                .rsp =
                    GuestAddress{
                        stack_used_base},
                .bytes =
                    std::vector<std::byte>{
                        std::byte{1},
                        std::byte{2},
                        std::byte{3},
                        std::byte{4},
                        std::byte{5},
                        std::byte{6},
                        std::byte{7},
                        std::byte{8},
                    },
            },
    };
}

DWORD protection_at(
    std::uint64_t address) {
    MEMORY_BASIC_INFORMATION info{};
    REQUIRE(
        ::VirtualQuery(
            reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(
                    address)),
            &info,
            sizeof(info)) ==
        sizeof(info));
    return info.Protect;
}

#endif

}  // namespace

TEST_CASE(
    "Windows native memory backend availability matches host",
    "[execution][windows-memory]") {
#if defined(_WIN32) && defined(_M_X64)
    REQUIRE(
        astraea::execution::
            windows_native_memory_backend_available());
#else
    REQUIRE_FALSE(
        astraea::execution::
            windows_native_memory_backend_available());
#endif
}

#if defined(_WIN32) && defined(_M_X64)

TEST_CASE(
    "Windows preparation populates bytes and applies final W^X protections",
    "[execution][windows-memory]") {
    const auto g = geometry();
    REQUIRE(
        g.granularity <=
        static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity));
    auto image =
        make_guest_image(
            base,
            g.page);

    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(image);
    REQUIRE(prepared.has_value());
    REQUIRE(
        prepared->plan().
            host_page_size ==
        g.page);
    REQUIRE_FALSE(prepared->empty());

    const auto* code =
        reinterpret_cast<
            const std::byte*>(
            static_cast<std::uintptr_t>(
                base));
    REQUIRE(code[0] == std::byte{0x0f});
    REQUIRE(code[1] == std::byte{0x0b});

    const auto data_base =
        base + g.page;
    const auto* data =
        reinterpret_cast<
            const std::byte*>(
            static_cast<std::uintptr_t>(
                data_base));
    REQUIRE(data[0] == std::byte{'d'});
    REQUIRE(data[1] == std::byte{'a'});
    REQUIRE(data[2] == std::byte{'t'});
    REQUIRE(data[3] == std::byte{'a'});
    REQUIRE(data[4] == std::byte{0});
    REQUIRE(data[5] == std::byte{0});
    REQUIRE(data[6] == std::byte{0});
    REQUIRE(data[7] == std::byte{0});

    const auto stack_base =
        base + 2U * g.page;
    const auto stack_used =
        stack_base + g.page - 8U;
    const auto* stack =
        reinterpret_cast<
            const std::byte*>(
            static_cast<std::uintptr_t>(
                stack_used));
    REQUIRE(stack[0] == std::byte{1});
    REQUIRE(stack[7] == std::byte{8});

    REQUIRE(
        (protection_at(base) & 0xffU) ==
        PAGE_EXECUTE_READ);
    REQUIRE(
        (protection_at(data_base) & 0xffU) ==
        PAGE_READWRITE);
    REQUIRE(
        (protection_at(stack_base) & 0xffU) ==
        PAGE_READWRITE);
}

TEST_CASE(
    "Windows preparation never replaces an existing reservation",
    "[execution][windows-memory]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity));
    auto image =
        make_guest_image(
            base,
            g.page);

    void* const collision =
        ::VirtualAlloc(
            reinterpret_cast<void*>(
                static_cast<std::uintptr_t>(
                    base)),
            static_cast<std::size_t>(
                g.granularity),
            MEM_RESERVE,
            PAGE_NOACCESS);
    REQUIRE(collision != nullptr);
    REQUIRE(
        collision ==
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(
                base)));

    auto prepared =
        astraea::execution::
            prepare_windows_guest_memory(image);
    REQUIRE_FALSE(prepared.has_value());
    REQUIRE(
        prepared.error().code ==
        astraea::execution::
            NativeBackendErrorCode::
                guest_address_unavailable);

    MEMORY_BASIC_INFORMATION info{};
    REQUIRE(
        ::VirtualQuery(
            collision,
            &info,
            sizeof(info)) ==
        sizeof(info));
    REQUIRE(info.State == MEM_RESERVE);

    REQUIRE(
        ::VirtualFree(
            collision,
            0,
            MEM_RELEASE) != 0);
}

TEST_CASE(
    "Windows preparation can repeat after deterministic teardown",
    "[execution][windows-memory]") {
    const auto g = geometry();
    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                g.granularity));
    auto image =
        make_guest_image(
            base,
            g.page);

    {
        auto first =
            astraea::execution::
                prepare_windows_guest_memory(
                    image);
        REQUIRE(first.has_value());
    }

    {
        auto second =
            astraea::execution::
                prepare_windows_guest_memory(
                    image);
        REQUIRE(second.has_value());
    }
}

#endif
