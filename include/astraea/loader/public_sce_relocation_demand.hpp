#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace astraea::loader {

// Only for the explicit public SCE emitter profile. These are static import
// REFERENCES, not dynamic calls, resolved providers, or emulator support.
struct PublicSceDemandImport {
    std::uint16_t module_local_id = 0;
    std::uint16_t library_local_id = 0;
    std::string nid;

    auto operator<=>(const PublicSceDemandImport&) const = default;
};

struct PublicSceDemandReference {
    std::uint32_t symbol_index = 0;
    std::uint32_t relocation_type = 0;
    bool symbol_is_undefined = false;
    std::optional<PublicSceDemandImport> public_import;
};

enum class PublicSceDemandClass {
    null_symbol,
    local_defined_symbol,
    external_unclassified,
    public_external_import,
};

struct PublicSceDemandCounts {
    std::size_t total = 0;
    std::size_t null_symbol = 0;
    std::size_t local_defined = 0;
    std::size_t external_unclassified = 0;
    std::size_t public_external_import = 0;
    std::size_t unique_imported_symbol_indices = 0;
    std::size_t unique_public_import_identities = 0;
};

using PublicSceDemandGroupKey =
    std::tuple<std::uint16_t, std::uint16_t, std::uint32_t>;

// Each group is one (module ID, library ID, numerical relocation type).
// A group count says nothing about loader implementation or HLE coverage.
class PublicSceRelocationDemandLedger {
public:
    [[nodiscard]] PublicSceDemandClass record(
        const PublicSceDemandReference& reference) {
        ++counts_.total;
        if (reference.symbol_index == 0U &&
            !reference.public_import.has_value()) {
            ++counts_.null_symbol;
            return PublicSceDemandClass::null_symbol;
        }
        if (!reference.symbol_is_undefined) {
            ++counts_.local_defined;
            return PublicSceDemandClass::local_defined_symbol;
        }
        if (!reference.public_import.has_value()) {
            ++counts_.external_unclassified;
            return PublicSceDemandClass::external_unclassified;
        }
        ++counts_.public_external_import;
        const auto& name = reference.public_import.value();
        imported_symbol_indices_.insert(reference.symbol_index);
        imported_identities_.insert(
            std::tuple{name.module_local_id, name.library_local_id, name.nid});
        ++groups_[PublicSceDemandGroupKey{
            name.module_local_id, name.library_local_id,
            reference.relocation_type}];
        return PublicSceDemandClass::public_external_import;
    }

    [[nodiscard]] PublicSceDemandCounts counts() const noexcept {
        auto result = counts_;
        result.unique_imported_symbol_indices = imported_symbol_indices_.size();
        result.unique_public_import_identities = imported_identities_.size();
        return result;
    }

    [[nodiscard]] const std::map<PublicSceDemandGroupKey, std::size_t>&
    groups() const noexcept {
        return groups_;
    }

private:
    PublicSceDemandCounts counts_{};
    std::set<std::uint32_t> imported_symbol_indices_;
    std::set<std::tuple<std::uint16_t, std::uint16_t, std::string>>
        imported_identities_;
    std::map<PublicSceDemandGroupKey, std::size_t> groups_;
};

}  // namespace astraea::loader
