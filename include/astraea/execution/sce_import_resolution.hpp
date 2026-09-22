#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/sce_import_binding.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

enum class SceImportResolutionPlanErrorCode {
    symbol_index_mismatch,
    symbol_not_undefined,
    missing_sce_identity,
    unresolved_import,
    host_allocation_failure,
};

struct SceImportResolutionPlanError {
    SceImportResolutionPlanErrorCode code =
        SceImportResolutionPlanErrorCode::host_allocation_failure;
    std::uint32_t relocation_symbol_index = 0;
    std::uint64_t symbol_record_index = 0;

    auto operator<=>(const SceImportResolutionPlanError&) const = default;
};

struct SceImportResolutionPlan {
    astraea::loader::RelocationTableKind table_kind;
    std::uint64_t table_index = 0;
    astraea::memory::GuestAddress relocation_target;
    std::uint32_t raw_relocation_type = 0;
    std::optional<std::int64_t> raw_addend;
    std::uint32_t symbol_index = 0;
    std::string raw_symbol_name;
    astraea::loader::SceSymbolIdentity identity;
    HleFunctionId function_id;

    auto operator<=>(const SceImportResolutionPlan&) const = default;
};

using SceImportResolutionPlanResult =
    astraea::core::Result<
        SceImportResolutionPlan,
        SceImportResolutionPlanError>;

enum class SceImportBatchPlanErrorCode {
    relocation_failure,
    symbol_failure,
    resolution_failure,
    host_allocation_failure,
};

struct SceImportBatchPlanError {
    SceImportBatchPlanErrorCode code =
        SceImportBatchPlanErrorCode::host_allocation_failure;
    std::uint64_t relocation_index = 0;
    std::optional<astraea::loader::DynamicRelocationError>
        relocation_error;
    std::optional<astraea::loader::SceDynamicSymbolError>
        symbol_error;
    std::optional<SceImportResolutionPlanError>
        resolution_error;
};

using SceImportBatchPlansResult =
    astraea::core::Result<
        std::vector<SceImportResolutionPlan>,
        SceImportBatchPlanError>;

// Compatibility aliases for the original PLT-specific API.
using ScePltImportPlanErrorCode =
    SceImportBatchPlanErrorCode;
using ScePltImportPlanError =
    SceImportBatchPlanError;
using ScePltImportPlansResult =
    SceImportBatchPlansResult;

// Composes already-validated loader/HLE data without applying relocation
// semantics. Relocation type and addend are preserved as raw evidence only.
[[nodiscard]] SceImportResolutionPlanResult
plan_sce_import_resolution(
    const astraea::loader::DynamicRelocation& relocation,
    const astraea::loader::SceDynamicSymbolRecord& symbol,
    const SceImportBindingRegistry& bindings);

// Plans every already-validated relocation in table order. This composes
// parsing, exact SCE symbol materialization, and exact HLE binding only.
// Table kind, relocation type, and addend are preserved as evidence.
// Relocation semantics, gate selection, patch construction, and guest-memory
// writes remain separate later stages.
[[nodiscard]] SceImportBatchPlansResult
plan_sce_imports(
    const astraea::loader::DynamicRelocationTableDescriptor& relocations,
    const astraea::loader::DynamicSymbolTableDescriptor& symbols,
    const astraea::loader::DynamicStringTableDescriptor& strings,
    const astraea::memory::InitializedImageView& image_view,
    const SceImportBindingRegistry& bindings);

// Compatibility wrapper for the original PLT-oriented entry point.
[[nodiscard]] ScePltImportPlansResult
plan_sce_plt_imports(
    const astraea::loader::DynamicRelocationTableDescriptor& relocations,
    const astraea::loader::DynamicSymbolTableDescriptor& symbols,
    const astraea::loader::DynamicStringTableDescriptor& strings,
    const astraea::memory::InitializedImageView& image_view,
    const SceImportBindingRegistry& bindings);

}  // namespace astraea::execution
