#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>

namespace astraea::execution {

enum class SceImportBindingErrorCode {
    invalid_identity,
    unknown_hle_function,
    duplicate_identity,
    host_allocation_failure,
};

struct SceImportBindingError {
    SceImportBindingErrorCode code =
        SceImportBindingErrorCode::host_allocation_failure;
    std::optional<std::size_t> binding_index;
    std::optional<std::size_t> conflicting_binding_index;
    std::optional<HleFunctionId> function_id;

    auto operator<=>(const SceImportBindingError&) const = default;
};

struct SceImportBinding {
    astraea::loader::SceSymbolIdentity identity;
    HleFunctionId function_id;

    auto operator<=>(const SceImportBinding&) const = default;
};

enum class SceImportResolutionKind {
    resolved,
    unresolved,
};

struct SceImportResolution {
    SceImportResolutionKind kind =
        SceImportResolutionKind::unresolved;
    std::optional<HleFunctionId> function_id;

    auto operator<=>(const SceImportResolution&) const = default;
};

class SceImportBindingRegistry {
public:
    using CreateResult =
        astraea::core::Result<
            SceImportBindingRegistry,
            SceImportBindingError>;

    [[nodiscard]] static CreateResult create(
        const HleRegistry& hle_registry,
        std::span<const SceImportBinding> bindings);

    [[nodiscard]] SceImportResolution resolve(
        const astraea::loader::SceSymbolIdentity& identity)
        const noexcept;

    [[nodiscard]] std::span<const SceImportBinding>
    bindings() const noexcept {
        return bindings_;
    }

private:
    explicit SceImportBindingRegistry(
        std::vector<SceImportBinding> bindings)
        : bindings_(std::move(bindings)) {}

    std::vector<SceImportBinding> bindings_;
};

}  // namespace astraea::execution
