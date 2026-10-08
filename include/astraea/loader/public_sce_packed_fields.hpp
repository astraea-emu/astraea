#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

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


enum class PublicSceLocalIdRecordResult {
    inserted,
    equivalent_duplicate,
    conflicting_reuse,
};

// This ledger is local to an explicitly selected PUBLIC emitter profile,
// not a Sony dynamic-loader module index. Modules and import libraries must
// maintain independent instances of this ledger.
class PublicSceLocalIdLedger {
public:
    [[nodiscard]] PublicSceLocalIdRecordResult record(
        PublicScePackedFields fields,
        std::string_view published_name) {
        const auto it = records_.find(fields.local_id);
        if (it != records_.end()) {
            if (it->second.first != fields.version ||
                it->second.second != published_name) {
                return PublicSceLocalIdRecordResult::conflicting_reuse;
            }
            return PublicSceLocalIdRecordResult::equivalent_duplicate;
        }
        records_.emplace(
            fields.local_id,
            std::pair{fields.version, std::string{published_name}});
        return PublicSceLocalIdRecordResult::inserted;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return records_.size();
    }

private:
    std::map<std::uint16_t, std::pair<std::uint16_t, std::string>> records_;
};

}  // namespace astraea::loader
