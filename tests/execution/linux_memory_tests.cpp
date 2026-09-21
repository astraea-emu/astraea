#include <astraea/execution/linux_memory.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

#if defined(__linux__) && defined(__x86_64__)

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

GuestRange range(std::uint64_t base, std::uint64_t size) {
    auto result = GuestRange::create(GuestAddress{base}, GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

GuestPermissions permissions(std::uint8_t bits) {
    auto result = GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
}

std::uint64_t page_size() {
    const long value = ::sysconf(_SC_PAGESIZE);
    REQUIRE(value > 0);
    return static_cast<std::uint64_t>(value);
}

void* reserve_any(std::size_t size, int protection = PROT_NONE) {
    void* const mapped = ::mmap(
        nullptr,
        size,
        protection,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0);
    REQUIRE(mapped != MAP_FAILED);
    return mapped;
}

void release(void* address, std::size_t size) {
    REQUIRE(::munmap(address, size) == 0);
}

void* map_exact(
    std::uint64_t address,
    std::size_t size,
    int protection) {
#if defined(MAP_FIXED_NOREPLACE)
    return ::mmap(
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(address)),
        size,
        protection,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
        -1,
        0);
#else
    static_cast<void>(address);
    static_cast<void>(size);
    static_cast<void>(protection);
    return MAP_FAILED;
#endif
}

std::uint64_t find_free_block(std::size_t size) {
    void* const mapped = reserve_any(size);
    const auto address =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(mapped));
    release(mapped, size);
    return address;
}

GuestImage make_guest_image(
    std::uint64_t base,
    std::uint64_t host_page_size) {
    constexpr std::uint8_t kRead =
        static_cast<std::uint8_t>(GuestPermission::read);
    constexpr std::uint8_t kWrite =
        static_cast<std::uint8_t>(GuestPermission::write);
    constexpr std::uint8_t kExecute =
        static_cast<std::uint8_t>(GuestPermission::execute);

    std::vector<std::byte> image_bytes{
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0x90},
        std::byte{0xc3},
    };

    ElfHeader header{};
    header.entry = base;

    std::vector<MappingIntent> mappings{
        MappingIntent{
            .range = range(base, 4),
            .permissions = permissions(kRead | kExecute),
            .backing =
                MappingBacking{
                    .kind = MappingBackingKind::file,
                    .file_offset = 0,
                    .byte_count = GuestSize{4},
                },
            .source_index = 0,
        },
        MappingIntent{
            .range = range(base + host_page_size, 32),
            .permissions = permissions(kRead | kWrite),
            .backing =
                MappingBacking{
                    .kind = MappingBackingKind::zero_fill,
                    .file_offset = 0,
                    .byte_count = GuestSize{32},
                },
            .source_index = 1,
        },
    };

    const auto stack_base =
        base + 2U * host_page_size;
    const auto used_base =
        stack_base + host_page_size - 16U;
    std::vector<std::byte> stack_bytes(16);
    for (std::size_t i = 0; i < stack_bytes.size(); ++i) {
        stack_bytes[i] =
            static_cast<std::byte>(
                static_cast<unsigned char>(0xa0U + i));
    }

    return GuestImage{
        .image_bytes = std::move(image_bytes),
        .elf =
            ElfImage{
                .header = header,
                .program_headers = {},
            },
        .mappings = std::move(mappings),
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
                        host_page_size),
                .used_range =
                    range(
                        used_base,
                        stack_bytes.size()),
                .rsp = GuestAddress{used_base},
                .bytes = std::move(stack_bytes),
            },
    };
}

std::string permissions_for_address(
    std::uint64_t target) {
    std::ifstream maps{"/proc/self/maps"};
    REQUIRE(maps.is_open());

    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char perms[5] = {};
        if (std::sscanf(
                line.c_str(),
                "%llx-%llx %4s",
                &start,
                &end,
                perms) != 3) {
            continue;
        }

        if (target >= start && target < end) {
            return std::string{perms};
        }
    }

    return {};
}

#endif

}  // namespace

TEST_CASE("Linux native memory backend availability matches host", "[execution][linux-memory]") {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
    REQUIRE(astraea::execution::linux_native_memory_backend_available());
#else
    REQUIRE_FALSE(astraea::execution::linux_native_memory_backend_available());
#endif
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE("Linux preparation populates bytes and applies final W^X protections", "[execution][linux-memory]") {
    const auto page = page_size();
    REQUIRE(page <= std::numeric_limits<std::size_t>::max());
    const auto total =
        static_cast<std::size_t>(page * 3U);
    const auto base = find_free_block(total);
    auto image = make_guest_image(base, page);

    {
        auto prepared =
            astraea::execution::prepare_linux_guest_memory(image);
        REQUIRE(prepared.has_value());
        REQUIRE_FALSE(prepared->empty());

        const auto* code =
            reinterpret_cast<const std::byte*>(
                static_cast<std::uintptr_t>(base));
        REQUIRE(code[0] == std::byte{0x90});
        REQUIRE(code[1] == std::byte{0x90});
        REQUIRE(code[2] == std::byte{0x90});
        REQUIRE(code[3] == std::byte{0xc3});

        const auto* data =
            reinterpret_cast<const std::byte*>(
                static_cast<std::uintptr_t>(base + page));
        for (std::size_t i = 0; i < 32; ++i) {
            REQUIRE(data[i] == std::byte{0});
        }

        const auto& stack = image.initial_stack;
        const auto* stack_data =
            reinterpret_cast<const std::byte*>(
                static_cast<std::uintptr_t>(
                    stack.used_range.base().value()));
        REQUIRE(
            std::memcmp(
                stack_data,
                stack.bytes.data(),
                stack.bytes.size()) == 0);

        const auto code_perms =
            permissions_for_address(base);
        const auto data_perms =
            permissions_for_address(base + page);
        const auto stack_perms =
            permissions_for_address(base + 2U * page);

        REQUIRE(code_perms.size() >= 3);
        REQUIRE(code_perms[0] == 'r');
        REQUIRE(code_perms[1] == '-');
        REQUIRE(code_perms[2] == 'x');

        REQUIRE(data_perms.size() >= 3);
        REQUIRE(data_perms[0] == 'r');
        REQUIRE(data_perms[1] == 'w');
        REQUIRE(data_perms[2] == '-');

        REQUIRE(stack_perms.size() >= 3);
        REQUIRE(stack_perms[0] == 'r');
        REQUIRE(stack_perms[1] == 'w');
        REQUIRE(stack_perms[2] == '-');
    }

    void* const reclaimed =
        map_exact(
            base,
            static_cast<std::size_t>(page),
            PROT_READ | PROT_WRITE);
    REQUIRE(reclaimed != MAP_FAILED);
    REQUIRE(
        reinterpret_cast<std::uintptr_t>(reclaimed) ==
        static_cast<std::uintptr_t>(base));
    release(
        reclaimed,
        static_cast<std::size_t>(page));
}

TEST_CASE("Linux exact-address collision never clobbers existing mapping", "[execution][linux-memory]") {
    const auto page = page_size();
    const auto total =
        static_cast<std::size_t>(page * 3U);
    void* const occupied =
        reserve_any(total, PROT_READ | PROT_WRITE);
    const auto base =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(occupied));

    auto* sentinel =
        static_cast<std::byte*>(occupied);
    sentinel[0] = std::byte{0x5a};

    auto image = make_guest_image(base, page);
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(image);

    REQUIRE_FALSE(prepared.has_value());
    REQUIRE(
        prepared.error().code ==
        astraea::execution::NativeBackendErrorCode::guest_address_unavailable);
    REQUIRE(sentinel[0] == std::byte{0x5a});

    release(occupied, total);
}

TEST_CASE("Linux partial preparation failure rolls back earlier regions", "[execution][linux-memory]") {
    const auto page = page_size();
    const auto page_host =
        static_cast<std::size_t>(page);
    const auto total =
        static_cast<std::size_t>(page * 3U);
    const auto base = find_free_block(total);

    void* const collision =
        map_exact(
            base + page,
            page_host,
            PROT_READ | PROT_WRITE);
    REQUIRE(collision != MAP_FAILED);

    auto image = make_guest_image(base, page);
    auto prepared =
        astraea::execution::prepare_linux_guest_memory(image);

    REQUIRE_FALSE(prepared.has_value());
    REQUIRE(
        prepared.error().code ==
        astraea::execution::NativeBackendErrorCode::guest_address_unavailable);

    void* const first_page =
        map_exact(
            base,
            page_host,
            PROT_READ | PROT_WRITE);
    REQUIRE(first_page != MAP_FAILED);

    release(first_page, page_host);
    release(collision, page_host);
}

TEST_CASE("Linux preparation rejects mutated stack byte extent", "[execution][linux-memory]") {
    const auto page = page_size();
    const auto total =
        static_cast<std::size_t>(page * 3U);
    const auto base = find_free_block(total);
    auto image = make_guest_image(base, page);

    image.initial_stack.bytes.push_back(std::byte{0xff});

    auto prepared =
        astraea::execution::prepare_linux_guest_memory(image);

    REQUIRE_FALSE(prepared.has_value());
    REQUIRE(
        prepared.error().code ==
        astraea::execution::NativeBackendErrorCode::stack_mapping_failure);

    void* const reclaimed =
        map_exact(
            base,
            static_cast<std::size_t>(page),
            PROT_READ | PROT_WRITE);
    REQUIRE(reclaimed != MAP_FAILED);
    release(
        reclaimed,
        static_cast<std::size_t>(page));
}

TEST_CASE("Linux preparation can be repeated after deterministic teardown", "[execution][linux-memory]") {
    const auto page = page_size();
    const auto total =
        static_cast<std::size_t>(page * 3U);
    const auto base = find_free_block(total);
    auto image = make_guest_image(base, page);

    {
        auto first =
            astraea::execution::prepare_linux_guest_memory(image);
        REQUIRE(first.has_value());
    }

    {
        auto second =
            astraea::execution::prepare_linux_guest_memory(image);
        REQUIRE(second.has_value());
    }
}

#endif
