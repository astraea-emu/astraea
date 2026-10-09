#pragma once

#include <compare>
#include <cstdint>
#include <cstddef>
#include <map>
#include <optional>
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

    [[nodiscard]] const std::pair<std::uint16_t, std::string>* find(
        std::uint16_t local_id) const noexcept {
        const auto it = records_.find(local_id);
        return it == records_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return records_.size();
    }

private:
    std::map<std::uint16_t, std::pair<std::uint16_t, std::string>> records_;
};

// Both reviewed public native-title linkers encode their *numeric local*
// IDs in SCE symbol suffixes using this alphabet. This is deliberately
// separate from an 11-character NID, which has different semantics.
// Source: BlackBear SCE writer at 2f672d1c; ps5link-sdk at ea771e53.
// Only the explicitly selected public-toolchain profile may use this rule.
[[nodiscard]] constexpr std::optional<std::uint16_t>
decode_public_sce_symbol_local_id(std::string_view encoded) noexcept {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    // A special single A represents zero. Leading zero digits are not
    // canonical; local IDs in this public profile are only 16 bits.
    if (encoded.empty() || encoded.size() > 3U ||
        (encoded.size() > 1U && encoded.front() == 'A')) {
        return std::nullopt;
    }

    std::uint32_t value = 0U;
    for (const char letter : encoded) {
        const auto digit = alphabet.find(letter);
        if (digit == std::string_view::npos) {
            return std::nullopt;
        }
        value = (value * 64U) + static_cast<std::uint32_t>(digit);
        if (value > 0xffffU) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint16_t>(value);
}

enum class PublicSceSymbolLinkCode {
    matched,
    malformed_id,
    module_unregistered,
    library_unregistered,
};

struct PublicSceSymbolLink {
    PublicSceSymbolLinkCode code = PublicSceSymbolLinkCode::malformed_id;
    std::optional<std::uint16_t> module_local_id;
    std::optional<std::uint16_t> library_local_id;

    auto operator<=>(const PublicSceSymbolLink&) const = default;
};

// Structural consistency ONLY. The public source describes the encoded
// suffixes as local IDs. A matching ledger name/version does not prove that
// the named Sony module exists, that it exports the requested function, or
// that Astraea has implemented HLE behavior.
[[nodiscard]] inline PublicSceSymbolLink check_public_sce_symbol_link(
    std::string_view encoded_library_id,
    std::string_view encoded_module_id,
    const PublicSceLocalIdLedger& libraries,
    const PublicSceLocalIdLedger& modules) noexcept {
    const auto library_id =
        decode_public_sce_symbol_local_id(encoded_library_id);
    const auto module_id =
        decode_public_sce_symbol_local_id(encoded_module_id);
    if (!library_id.has_value() || !module_id.has_value()) {
        return {
            .code = PublicSceSymbolLinkCode::malformed_id,
            .module_local_id = std::nullopt,
            .library_local_id = std::nullopt,
        };
    }
    if (modules.find(*module_id) == nullptr) {
        return {
            .code = PublicSceSymbolLinkCode::module_unregistered,
            .module_local_id = *module_id,
            .library_local_id = *library_id,
        };
    }
    if (libraries.find(*library_id) == nullptr) {
        return {
            .code = PublicSceSymbolLinkCode::library_unregistered,
            .module_local_id = *module_id,
            .library_local_id = *library_id,
        };
    }
    return {
        .code = PublicSceSymbolLinkCode::matched,
        .module_local_id = *module_id,
        .library_local_id = *library_id,
    };
}

}  // namespace astraea::loader
