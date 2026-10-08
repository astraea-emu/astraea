#pragma once

#include <compare>
#include <cstdint>

namespace astraea::loader {

// Experimental identity split for an independently authored public linker
// profile, not an admitted universal PS5 loader ABI.
//
// Observed public source: Rufidj/ps5link-sdk
// linker/dynwriter.c @ ea771e535378740b6a058b8e5419eb8a0e0e0ec8.
// Its DT_SCE_NEEDED_MODULE and DT_SCE_IMPORT_LIB words encode the published
// name's .dynstr offset in low32, version in bits 32..47, local ID in 48..63.
// Keep original raw words, and enable decoding only in an explicit opt-in
// read-only profile with independent source provenance and string validation.
struct PublicScePackedFields {
    std::uint32_t name_offset = 0;
    std::uint16_t version = 0;
    std::uint16_t local_id = 0;

    auto operator<=>(const PublicScePackedFields&) const = default;
};

[[nodiscard]] constexpr PublicScePackedFields
decode_public_sce_packed_fields(std::uint64_t raw) noexcept {
    return PublicScePackedFields{
        .name_offset = static_cast<std::uint32_t>(raw & 0xffffffffULL),
        .version = static_cast<std::uint16_t>((raw >> 32U) & 0xffffULL),
        .local_id = static_cast<std::uint16_t>((raw >> 48U) & 0xffffULL),
    };
}

}  // namespace astraea::loader
