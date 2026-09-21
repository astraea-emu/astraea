#include <astraea/execution/sce_import_resolution.hpp>

#include <new>
#include <stdexcept>

namespace astraea::execution {
namespace {

[[nodiscard]] SceImportResolutionPlanError plan_error(
    SceImportResolutionPlanErrorCode code,
    std::uint32_t relocation_symbol_index,
    std::uint64_t symbol_record_index) noexcept {
    return SceImportResolutionPlanError{
        .code = code,
        .relocation_symbol_index =
            relocation_symbol_index,
        .symbol_record_index =
            symbol_record_index,
    };
}

}  // namespace

SceImportResolutionPlanResult
plan_sce_import_resolution(
    const astraea::loader::DynamicRelocation& relocation,
    const astraea::loader::SceDynamicSymbolRecord& symbol,
    const SceImportBindingRegistry& bindings) {
    if (relocation.symbol_index !=
        symbol.symbol.index) {
        return SceImportResolutionPlanResult::failure(
            plan_error(
                SceImportResolutionPlanErrorCode::
                    symbol_index_mismatch,
                relocation.symbol_index,
                symbol.symbol.index));
    }

    // Generic ELF SHN_UNDEF is encoded as section index zero.
    if (symbol.symbol.section_index_raw != 0U) {
        return SceImportResolutionPlanResult::failure(
            plan_error(
                SceImportResolutionPlanErrorCode::
                    symbol_not_undefined,
                relocation.symbol_index,
                symbol.symbol.index));
    }

    if (!symbol.name.identity.has_value()) {
        return SceImportResolutionPlanResult::failure(
            plan_error(
                SceImportResolutionPlanErrorCode::
                    missing_sce_identity,
                relocation.symbol_index,
                symbol.symbol.index));
    }

    const auto resolved =
        bindings.resolve(
            symbol.name.identity.value());
    if (resolved.kind !=
            SceImportResolutionKind::resolved ||
        !resolved.function_id.has_value()) {
        return SceImportResolutionPlanResult::failure(
            plan_error(
                SceImportResolutionPlanErrorCode::
                    unresolved_import,
                relocation.symbol_index,
                symbol.symbol.index));
    }

    try {
        return SceImportResolutionPlanResult::success(
            SceImportResolutionPlan{
                .table_kind = relocation.table_kind,
                .table_index = relocation.table_index,
                .relocation_target = relocation.target,
                .raw_relocation_type =
                    relocation.relocation_type,
                .raw_addend = relocation.addend,
                .symbol_index =
                    relocation.symbol_index,
                .raw_symbol_name = symbol.name.raw,
                .identity =
                    symbol.name.identity.value(),
                .function_id =
                    resolved.function_id.value(),
            });
    } catch (const std::bad_alloc&) {
        return SceImportResolutionPlanResult::failure(
            plan_error(
                SceImportResolutionPlanErrorCode::
                    host_allocation_failure,
                relocation.symbol_index,
                symbol.symbol.index));
    } catch (const std::length_error&) {
        return SceImportResolutionPlanResult::failure(
            plan_error(
                SceImportResolutionPlanErrorCode::
                    host_allocation_failure,
                relocation.symbol_index,
                symbol.symbol.index));
    }
}

}  // namespace astraea::execution
