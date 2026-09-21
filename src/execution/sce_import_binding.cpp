#include <astraea/execution/sce_import_binding.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::execution {
namespace {

[[nodiscard]] SceImportBindingError binding_error(
    SceImportBindingErrorCode code,
    std::optional<std::size_t> binding_index = std::nullopt,
    std::optional<std::size_t> conflicting_binding_index =
        std::nullopt,
    std::optional<HleFunctionId> function_id =
        std::nullopt) noexcept {
    return SceImportBindingError{
        .code = code,
        .binding_index = binding_index,
        .conflicting_binding_index =
            conflicting_binding_index,
        .function_id = function_id,
    };
}

[[nodiscard]] bool valid_identity(
    const astraea::loader::SceSymbolIdentity& identity)
    noexcept {
    return identity.nid.size() == 11U &&
           !identity.library_id.empty() &&
           !identity.module_id.empty();
}

}  // namespace

SceImportBindingRegistry::CreateResult
SceImportBindingRegistry::create(
    const HleRegistry& hle_registry,
    std::span<const SceImportBinding> bindings) {
    try {
        std::vector<SceImportBinding> owned_bindings{
            bindings.begin(),
            bindings.end()};

        for (std::size_t index = 0;
             index < owned_bindings.size();
             ++index) {
            const auto& binding =
                owned_bindings[index];

            if (!valid_identity(binding.identity)) {
                return CreateResult::failure(
                    binding_error(
                        SceImportBindingErrorCode::
                            invalid_identity,
                        index,
                        std::nullopt,
                        binding.function_id));
            }

            if (hle_registry.find(
                    binding.function_id) == nullptr) {
                return CreateResult::failure(
                    binding_error(
                        SceImportBindingErrorCode::
                            unknown_hle_function,
                        index,
                        std::nullopt,
                        binding.function_id));
            }

            for (std::size_t previous = 0;
                 previous < index;
                 ++previous) {
                if (owned_bindings[previous].identity ==
                    binding.identity) {
                    return CreateResult::failure(
                        binding_error(
                            SceImportBindingErrorCode::
                                duplicate_identity,
                            index,
                            previous,
                            binding.function_id));
                }
            }
        }

        return CreateResult::success(
            SceImportBindingRegistry{
                std::move(owned_bindings)});
    } catch (const std::bad_alloc&) {
        return CreateResult::failure(
            binding_error(
                SceImportBindingErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return CreateResult::failure(
            binding_error(
                SceImportBindingErrorCode::
                    host_allocation_failure));
    }
}

SceImportResolution
SceImportBindingRegistry::resolve(
    const astraea::loader::SceSymbolIdentity& identity)
    const noexcept {
    for (const auto& binding : bindings_) {
        if (binding.identity == identity) {
            return SceImportResolution{
                .kind =
                    SceImportResolutionKind::resolved,
                .function_id = binding.function_id,
            };
        }
    }

    return SceImportResolution{
        .kind =
            SceImportResolutionKind::unresolved,
        .function_id = std::nullopt,
    };
}

}  // namespace astraea::execution
