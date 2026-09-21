#include <astraea/loader/dynamic.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (size > static_cast<std::size_t>(std::numeric_limits<std::uint64_t>::max())) {
            return 0;
        }
    }

    const auto bytes = std::as_bytes(std::span<const std::uint8_t>{data, size});

    const auto segment_size = static_cast<std::uint64_t>(size);
    const astraea::loader::ProgramHeader dynamic_segment{
        .type = 2,
        .flags = 0,
        .offset = 0,
        .virtual_address = 0,
        .physical_address = 0,
        .file_size = segment_size,
        .memory_size = segment_size,
        .alignment = 8,
        .index = 0,
    };

    const auto first = astraea::loader::parse_dynamic_table(bytes, dynamic_segment);
    const auto second = astraea::loader::parse_dynamic_table(bytes, dynamic_segment);

    if (first.has_value() != second.has_value()) {
        __builtin_trap();
    }
    if (!first.has_value() && first.error().code != second.error().code) {
        __builtin_trap();
    }

    return 0;
}
