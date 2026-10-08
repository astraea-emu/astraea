#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

// Opaque, explicitly supplied keys for independently controlled modules.
// Do not infer these mappings from the packed SCE dynamic tags.
struct ModuleGraphExport {
    astraea::loader::SceSymbolIdentity identity;
    astraea::memory::GuestAddress guest_address;

    auto operator<=>(const ModuleGraphExport&) const = default;
};

struct ModuleGraphDeclaration {
    std::string module_key;
    std::vector<std::string> dependency_keys;
    std::vector<ModuleGraphExport> exports;
};

enum class ModuleGraphErrorCode {
    too_many_modules,
    invalid_module_key,
    duplicate_module_key,
    too_many_dependencies,
    invalid_dependency_key,
    duplicate_dependency_key,
    too_many_exports,
    invalid_export_identity,
    invalid_export_address,
    duplicate_export_identity,
    host_allocation_failure,
};

struct ModuleGraphError {
    ModuleGraphErrorCode code = ModuleGraphErrorCode::host_allocation_failure;
    std::size_t module_index = 0;
    std::optional<std::size_t> record_index;
    std::optional<std::size_t> conflicting_index;

    auto operator<=>(const ModuleGraphError&) const = default;
};

enum class ModuleGraphResolutionKind {
    resolved,
    unknown_requester,
    undeclared_dependency,
    provider_not_registered,
    invalid_symbol_identity,
    unresolved_export,
};

struct ModuleGraphResolution {
    ModuleGraphResolutionKind kind =
        ModuleGraphResolutionKind::unresolved_export;
    std::optional<astraea::memory::GuestAddress> guest_address;

    auto operator<=>(const ModuleGraphResolution&) const = default;
};

// Data-only and fail-closed. No relocations are patched and no guest, HLE or
// system code executes. Exact caller-provided module/identity values are used;
// mapping a real SCE module dynamic record to these keys requires later evidence.
class ModuleGraph {
public:
    using CreateResult =
        astraea::core::Result<ModuleGraph, ModuleGraphError>;

    [[nodiscard]] static CreateResult create(
        std::span<const ModuleGraphDeclaration> declarations);

    [[nodiscard]] ModuleGraphResolution resolve(
        std::string_view requester,
        std::string_view provider,
        const astraea::loader::SceSymbolIdentity& identity)
        const noexcept;

    [[nodiscard]] std::size_t module_count() const noexcept {
        return modules_.size();
    }

private:
    explicit ModuleGraph(std::vector<ModuleGraphDeclaration> modules)
        : modules_(std::move(modules)) {}

    [[nodiscard]] const ModuleGraphDeclaration* find(
        std::string_view key) const noexcept;

    std::vector<ModuleGraphDeclaration> modules_;
};

}  // namespace astraea::execution
