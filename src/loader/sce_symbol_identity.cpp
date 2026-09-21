#include <astraea/loader/sce_symbol_identity.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace astraea::loader {
namespace {

[[nodiscard]] SceSymbolIdentityError identity_error(
    SceSymbolIdentityErrorCode code) noexcept {
    return SceSymbolIdentityError{
        .code = code,
    };
}

}  // namespace

SceSymbolIdentityParseResult
parse_sce_dynamic_symbol_name(std::string_view raw) {
    try {
        const auto first_separator = raw.find('#');
        if (first_separator == std::string_view::npos) {
            return SceSymbolIdentityParseResult::success(
                SceDynamicSymbolName{
                    .raw = std::string{raw},
                    .identity = std::nullopt,
                });
        }

        const auto second_separator =
            raw.find('#', first_separator + 1U);
        if (second_separator == std::string_view::npos) {
            return SceSymbolIdentityParseResult::failure(
                identity_error(
                    SceSymbolIdentityErrorCode::
                        malformed_long_form));
        }

        if (raw.find(
                '#',
                second_separator + 1U) !=
            std::string_view::npos) {
            return SceSymbolIdentityParseResult::failure(
                identity_error(
                    SceSymbolIdentityErrorCode::
                        malformed_long_form));
        }

        const auto nid =
            raw.substr(0, first_separator);
        const auto library_id =
            raw.substr(
                first_separator + 1U,
                second_separator -
                    first_separator - 1U);
        const auto module_id =
            raw.substr(second_separator + 1U);

        if (nid.empty() ||
            library_id.empty() ||
            module_id.empty()) {
            return SceSymbolIdentityParseResult::failure(
                identity_error(
                    SceSymbolIdentityErrorCode::
                        malformed_long_form));
        }

        if (nid.size() != 11U) {
            return SceSymbolIdentityParseResult::failure(
                identity_error(
                    SceSymbolIdentityErrorCode::
                        invalid_nid_length));
        }

        return SceSymbolIdentityParseResult::success(
            SceDynamicSymbolName{
                .raw = std::string{raw},
                .identity =
                    SceSymbolIdentity{
                        .nid = std::string{nid},
                        .library_id =
                            std::string{library_id},
                        .module_id =
                            std::string{module_id},
                    },
            });
    } catch (const std::bad_alloc&) {
        return SceSymbolIdentityParseResult::failure(
            identity_error(
                SceSymbolIdentityErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return SceSymbolIdentityParseResult::failure(
            identity_error(
                SceSymbolIdentityErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::loader
