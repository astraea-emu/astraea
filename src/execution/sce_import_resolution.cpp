#include <astraea/execution/sce_import_resolution.hpp>

#include <new>
#include <stdexcept>
#include <utility>

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

SceImportBatchPlansResult
plan_sce_imports(
    const astraea::loader::DynamicRelocationTableDescriptor& relocations,
    const astraea::loader::DynamicSymbolTableDescriptor& symbols,
    const astraea::loader::DynamicStringTableDescriptor& strings,
    const astraea::memory::InitializedImageView& image_view,
    const SceImportBindingRegistry& bindings) {
    std::vector<SceImportResolutionPlan> plans;

    for (std::uint64_t index = 0;
         index < relocations.count;
         ++index) {
        try {
            auto relocation =
                astraea::loader::parse_dynamic_relocation(
                    relocations,
                    index,
                    symbols,
                    image_view);
            if (!relocation.has_value()) {
                return SceImportBatchPlansResult::failure(
                    SceImportBatchPlanError{
                        .code =
                            SceImportBatchPlanErrorCode::
                                relocation_failure,
                        .relocation_index = index,
                        .relocation_error =
                            relocation.error(),
                        .symbol_error = std::nullopt,
                        .resolution_error =
                            std::nullopt,
                    });
            }

            auto symbol =
                astraea::loader::
                    materialize_sce_dynamic_symbol(
                        symbols,
                        strings,
                        relocation->symbol_index,
                        image_view);
            if (!symbol.has_value()) {
                return SceImportBatchPlansResult::failure(
                    SceImportBatchPlanError{
                        .code =
                            SceImportBatchPlanErrorCode::
                                symbol_failure,
                        .relocation_index = index,
                        .relocation_error =
                            std::nullopt,
                        .symbol_error =
                            symbol.error(),
                        .resolution_error =
                            std::nullopt,
                    });
            }

            auto plan =
                plan_sce_import_resolution(
                    relocation.value(),
                    symbol.value(),
                    bindings);
            if (!plan.has_value()) {
                return SceImportBatchPlansResult::failure(
                    SceImportBatchPlanError{
                        .code =
                            SceImportBatchPlanErrorCode::
                                resolution_failure,
                        .relocation_index = index,
                        .relocation_error =
                            std::nullopt,
                        .symbol_error =
                            std::nullopt,
                        .resolution_error =
                            plan.error(),
                    });
            }

            plans.push_back(
                std::move(plan.value()));
        } catch (const std::bad_alloc&) {
            return SceImportBatchPlansResult::failure(
                SceImportBatchPlanError{
                    .code =
                        SceImportBatchPlanErrorCode::
                            host_allocation_failure,
                    .relocation_index = index,
                    .relocation_error = std::nullopt,
                    .symbol_error = std::nullopt,
                    .resolution_error = std::nullopt,
                });
        } catch (const std::length_error&) {
            return SceImportBatchPlansResult::failure(
                SceImportBatchPlanError{
                    .code =
                        SceImportBatchPlanErrorCode::
                            host_allocation_failure,
                    .relocation_index = index,
                    .relocation_error = std::nullopt,
                    .symbol_error = std::nullopt,
                    .resolution_error = std::nullopt,
                });
        }
    }

    return SceImportBatchPlansResult::success(
        std::move(plans));
}

ScePltImportPlansResult
plan_sce_plt_imports(
    const astraea::loader::DynamicRelocationTableDescriptor& relocations,
    const astraea::loader::DynamicSymbolTableDescriptor& symbols,
    const astraea::loader::DynamicStringTableDescriptor& strings,
    const astraea::memory::InitializedImageView& image_view,
    const SceImportBindingRegistry& bindings) {
    return plan_sce_imports(
        relocations,
        symbols,
        strings,
        image_view,
        bindings);
}

}  // namespace astraea::execution
