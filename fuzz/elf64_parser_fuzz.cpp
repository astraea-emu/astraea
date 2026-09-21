#include <astraea/loader/elf64.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes =
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(data), size};

    const auto first = astraea::loader::parse_elf64(bytes);
    const auto second = astraea::loader::parse_elf64(bytes);

    if (first.has_value() != second.has_value()) {
        __builtin_trap();
    }
    if (!first.has_value() && first.error().code != second.error().code) {
        __builtin_trap();
    }

    return 0;
}
