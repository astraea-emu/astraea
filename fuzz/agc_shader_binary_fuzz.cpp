#include <astraea/graphics/agc_shader_binary.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data,
    std::size_t size) {
    std::span<const std::byte> header;
    std::span<const std::byte> text;

    if (size != 0) {
        const auto payload =
            std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(
                    data + 1),
                size - 1};
        const auto split =
            static_cast<std::size_t>(data[0]) %
            (payload.size() + 1U);
        header = payload.first(split);
        text = payload.subspan(split);
    }

    const auto first =
        astraea::graphics::
            parse_agc_shader_binary(
                header,
                text);
    const auto second =
        astraea::graphics::
            parse_agc_shader_binary(
                header,
                text);

    if (first.has_value() != second.has_value()) {
        __builtin_trap();
    }

    if (!first.has_value() &&
        first.error() != second.error()) {
        __builtin_trap();
    }

    if (first.has_value() &&
        first.value() != second.value()) {
        __builtin_trap();
    }

    return 0;
}
