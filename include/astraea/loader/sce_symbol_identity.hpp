#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>

#include <astraea/core/result.hpp>

namespace astraea::loader {

enum class SceSymbolIdentityErrorCode {
    malformed_long_form,
    invalid_nid_length,
    host_allocation_failure,
};

struct SceSymbolIdentityError {
    SceSymbolIdentityErrorCode code =
        SceSymbolIdentityErrorCode::malformed_long_form;

    auto operator<=>(const SceSymbolIdentityError&) const = default;
};

struct SceSymbolIdentity {
    std::string nid;
    std::string library_id;
    std::string module_id;

    auto operator<=>(const SceSymbolIdentity&) const = default;
};

struct SceDynamicSymbolName {
    std::string raw;
    std::optional<SceSymbolIdentity> identity;

    auto operator<=>(const SceDynamicSymbolName&) const = default;
};

using SceSymbolIdentityParseResult =
    astraea::core::Result<
        SceDynamicSymbolName,
        SceSymbolIdentityError>;

// Parses only the observed structural spelling:
//
//   <11-character-nid>#<library-id>#<module-id>
//
// The three components remain opaque strings. This function does not generate
// NIDs, decode module/library IDs, substitute plain names, or resolve imports.
// Names without '#' remain unclassified raw strings.
[[nodiscard]] SceSymbolIdentityParseResult
parse_sce_dynamic_symbol_name(std::string_view raw);

}  // namespace astraea::loader
