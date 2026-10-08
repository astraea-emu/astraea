#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <astraea/core/result.hpp>
#include <astraea/execution/module_graph.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

enum class ModuleGraphImportPlanErrorCode {
    symbol_index_mismatch,
    symbol_not_undefined,
    missing_sce_identity,
    module_resolution_failure,
    host_allocation_failure,
};

struct ModuleGraphImportPlanError {
    ModuleGraphImportPlanErrorCode code =
        ModuleGraphImportPlanErrorCode::host_allocation_failure;
    std::uint32_t relocation_symbol_index = 0;
    std::uint64_t dynamic_symbol_record_index = 0;
    std::optional<ModuleGraphResolutionKind> resolution_failure;

    auto operator<=>(const ModuleGraphImportPlanError&) const = default;
};

// Static association only: an exact caller-approved provider supplies a
// typed guest address for an undefined SCE symbol referenced by a relocation.
// No relocation type is approved, no memory is written, and no HLE is called.
struct ModuleGraphImportPlan {
    astraea::loader::RelocationTableKind table_kind;
    std::uint64_t table_index = 0;
    astraea::memory::GuestAddress relocation_target;
    std::uint32_t raw_relocation_type = 0;
    std::optional<std::int64_t> raw_addend;
    std::uint32_t symbol_index = 0;
    std::uint8_t symbol_binding = 0;
    std::string provider_key;
    astraea::loader::SceSymbolIdentity symbol_identity;
    astraea::memory::GuestAddress provider_guest_address;

    auto operator<=>(const ModuleGraphImportPlan&) const = default;
};

using ModuleGraphImportPlanResult =
    astraea::core::Result<
        ModuleGraphImportPlan, ModuleGraphImportPlanError>;

// The caller is responsible for establishing requester/provider identity
// from independent evidence. No SCE packed module ID mapping is inferred.
// Undefined/weak policy and relocation-type-specific semantics remain
// separate acceptance gates.
[[nodiscard]] ModuleGraphImportPlanResult
plan_module_graph_import(
    const astraea::loader::DynamicRelocation& relocation,
    const astraea::loader::SceDynamicSymbolRecord& symbol,
    const ModuleGraph& graph,
    std::string_view requester_key,
    std::string_view provider_key);

}  // namespace astraea::execution
