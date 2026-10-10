#include <astraea/loader/gnu_hash.hpp>
#include <astraea/memory/initialized_image_view.hpp>
#include <astraea/memory/mapping.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data, std::size_t size) {
    if (size < 16U || size > 1024U * 1024U) return 0;
    const auto bytes = std::as_bytes(
        std::span<const std::uint8_t>{data, size});
    const auto region = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{0x8000U},
        astraea::memory::GuestSize{size});
    const auto permissions =
        astraea::memory::GuestPermissions::checked_from_bits(4U);
    if (!region.has_value() || !permissions.has_value()) return 0;

    const std::array intents{astraea::memory::MappingIntent{
        .range = region.value(),
        .permissions = permissions.value(),
        .backing = astraea::memory::MappingBacking{
            .kind = astraea::memory::MappingBackingKind::file,
            .file_offset = 0U,
            .byte_count = astraea::memory::GuestSize{size},
        },
        .source_index = 0U,
    }};
    const auto view =
        astraea::memory::InitializedImageView::create(bytes, intents);
    if (!view.has_value()) return 0;

    const astraea::loader::DynamicTable table{
        .entries = {astraea::loader::DynamicEntry{
            .tag = 0x6ffffef5,
            .value = 0x8000U,
            .index = 0U,
        }},
    };
    const auto a = astraea::loader::build_gnu_hash_count_evidence(
        table, view.value());
    const auto b = astraea::loader::build_gnu_hash_count_evidence(
        table, view.value());
    if (a.has_value() != b.has_value()) __builtin_trap();
    if (!a.has_value()) {
        if (a.error().code != b.error().code) __builtin_trap();
    } else if (a->has_value() != b->has_value() ||
               (a->has_value() &&
                a->value().symbol_count != b->value().symbol_count)) {
        __builtin_trap();
    }
    return 0;
}
