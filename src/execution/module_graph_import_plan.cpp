#include <astraea/execution/module_graph_import_plan.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace astraea::execution {
namespace {

[[nodiscard]] ModuleGraphImportPlanError make_error(
    ModuleGraphImportPlanErrorCode code,
    const astraea::loader::DynamicRelocation& relocation,
    const astraea::loader::SceDynamicSymbolRecord& symbol,
    std::optional<ModuleGraphResolutionKind> resolution = std::nullopt)
    noexcept {
    return ModuleGraphImportPlanError{
        .code = code,
        .relocation_symbol_index = relocation.symbol_index,
        .dynamic_symbol_record_index = symbol.symbol.index,
        .resolution_failure = resolution,
    };
}

}  // namespace

ModuleGraphImportPlanResult plan_module_graph_import(
    const astraea::loader::DynamicRelocation& relocation,
    const astraea::loader::SceDynamicSymbolRecord& symbol,
    const ModuleGraph& graph,
    std::string_view requester_key,
    std::string_view provider_key) {
    if (relocation.symbol_index != symbol.symbol.index) {
        return ModuleGraphImportPlanResult::failure(make_error(
            ModuleGraphImportPlanErrorCode::symbol_index_mismatch,
            relocation, symbol));
    }
    // A local definition or the ELF null symbol does not magically become
    // a bound module import. The latter has no valid SCE long-form identity.
    if (symbol.symbol.section_index_raw != 0U) {
        return ModuleGraphImportPlanResult::failure(make_error(
            ModuleGraphImportPlanErrorCode::symbol_not_undefined,
            relocation, symbol));
    }
    if (!symbol.name.identity.has_value()) {
        return ModuleGraphImportPlanResult::failure(make_error(
            ModuleGraphImportPlanErrorCode::missing_sce_identity,
            relocation, symbol));
    }

    const auto resolved = graph.resolve(
        requester_key, provider_key, symbol.name.identity.value());
    if (resolved.kind != ModuleGraphResolutionKind::resolved ||
        !resolved.guest_address.has_value()) {
        return ModuleGraphImportPlanResult::failure(make_error(
            ModuleGraphImportPlanErrorCode::module_resolution_failure,
            relocation, symbol, resolved.kind));
    }

    try {
        return ModuleGraphImportPlanResult::success(
            ModuleGraphImportPlan{
                .table_kind = relocation.table_kind,
                .table_index = relocation.table_index,
                .relocation_target = relocation.target,
                .raw_relocation_type = relocation.relocation_type,
                .raw_addend = relocation.addend,
                .symbol_index = relocation.symbol_index,
                .symbol_binding = symbol.symbol.binding(),
                .provider_key = std::string{provider_key},
                .symbol_identity = symbol.name.identity.value(),
                .provider_guest_address = resolved.guest_address.value(),
            });
    } catch (const std::bad_alloc&) {
        return ModuleGraphImportPlanResult::failure(make_error(
            ModuleGraphImportPlanErrorCode::host_allocation_failure,
            relocation, symbol));
    } catch (const std::length_error&) {
        return ModuleGraphImportPlanResult::failure(make_error(
            ModuleGraphImportPlanErrorCode::host_allocation_failure,
            relocation, symbol));
    }
}

}  // namespace astraea::execution
