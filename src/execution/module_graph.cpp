#include <astraea/execution/module_graph.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace astraea::execution {
namespace {

constexpr std::size_t kMaxModuleCount = 64U;
constexpr std::size_t kMaxPerModuleDependencies = 64U;
constexpr std::size_t kMaxPerModuleExports = 512U;
constexpr std::size_t kMaxKeyBytes = 256U;

[[nodiscard]] bool valid_key(std::string_view key) noexcept {
    return !key.empty() && key.size() <= kMaxKeyBytes;
}

[[nodiscard]] bool valid_identity(
    const astraea::loader::SceSymbolIdentity& identity) noexcept {
    return identity.nid.size() == 11U &&
           valid_key(identity.library_id) &&
           valid_key(identity.module_id);
}

[[nodiscard]] ModuleGraphError make_error(
    ModuleGraphErrorCode code,
    std::size_t module_index,
    std::optional<std::size_t> record = std::nullopt,
    std::optional<std::size_t> previous = std::nullopt) noexcept {
    return ModuleGraphError{
        .code = code,
        .module_index = module_index,
        .record_index = record,
        .conflicting_index = previous,
    };
}

[[nodiscard]] ModuleGraphResolution failure(
    ModuleGraphResolutionKind kind) noexcept {
    return ModuleGraphResolution{
        .kind = kind,
        .guest_address = std::nullopt,
    };
}

}  // namespace

ModuleGraph::CreateResult ModuleGraph::create(
    std::span<const ModuleGraphDeclaration> declarations) {
    if (declarations.size() > kMaxModuleCount) {
        return CreateResult::failure(make_error(
            ModuleGraphErrorCode::too_many_modules, declarations.size()));
    }

    try {
        std::vector<ModuleGraphDeclaration> modules{
            declarations.begin(), declarations.end()};
        for (std::size_t i = 0; i < modules.size(); ++i) {
            const auto& module = modules[i];
            if (!valid_key(module.module_key)) {
                return CreateResult::failure(make_error(
                    ModuleGraphErrorCode::invalid_module_key, i));
            }
            for (std::size_t previous = 0; previous < i; ++previous) {
                if (modules[previous].module_key == module.module_key) {
                    return CreateResult::failure(make_error(
                        ModuleGraphErrorCode::duplicate_module_key,
                        i, std::nullopt, previous));
                }
            }
            if (module.dependency_keys.size() > kMaxPerModuleDependencies) {
                return CreateResult::failure(make_error(
                    ModuleGraphErrorCode::too_many_dependencies, i));
            }
            for (std::size_t j = 0; j < module.dependency_keys.size(); ++j) {
                const auto& dep = module.dependency_keys[j];
                if (!valid_key(dep)) {
                    return CreateResult::failure(make_error(
                        ModuleGraphErrorCode::invalid_dependency_key, i, j));
                }
                for (std::size_t previous = 0; previous < j; ++previous) {
                    if (module.dependency_keys[previous] == dep) {
                        return CreateResult::failure(make_error(
                            ModuleGraphErrorCode::duplicate_dependency_key,
                            i, j, previous));
                    }
                }
            }

            if (module.exports.size() > kMaxPerModuleExports) {
                return CreateResult::failure(make_error(
                    ModuleGraphErrorCode::too_many_exports, i));
            }
            for (std::size_t j = 0; j < module.exports.size(); ++j) {
                const auto& symbol = module.exports[j];
                if (!valid_identity(symbol.identity)) {
                    return CreateResult::failure(make_error(
                        ModuleGraphErrorCode::invalid_export_identity, i, j));
                }
                if (symbol.guest_address.value() == 0U) {
                    return CreateResult::failure(make_error(
                        ModuleGraphErrorCode::invalid_export_address, i, j));
                }
                for (std::size_t previous = 0; previous < j; ++previous) {
                    if (module.exports[previous].identity == symbol.identity) {
                        return CreateResult::failure(make_error(
                            ModuleGraphErrorCode::duplicate_export_identity,
                            i, j, previous));
                    }
                }
            }
        }
        return CreateResult::success(ModuleGraph{std::move(modules)});
    } catch (const std::bad_alloc&) {
        return CreateResult::failure(make_error(
            ModuleGraphErrorCode::host_allocation_failure, 0));
    } catch (const std::length_error&) {
        return CreateResult::failure(make_error(
            ModuleGraphErrorCode::host_allocation_failure, 0));
    }
}

const ModuleGraphDeclaration*
ModuleGraph::find(std::string_view key) const noexcept {
    for (const auto& module : modules_) {
        if (module.module_key == key) {
            return &module;
        }
    }
    return nullptr;
}

ModuleGraphResolution ModuleGraph::resolve(
    std::string_view requester,
    std::string_view provider,
    const astraea::loader::SceSymbolIdentity& identity) const noexcept {
    const auto* source = find(requester);
    if (source == nullptr) {
        return failure(ModuleGraphResolutionKind::unknown_requester);
    }
    if (std::find(
            source->dependency_keys.begin(),
            source->dependency_keys.end(),
            provider) == source->dependency_keys.end()) {
        return failure(ModuleGraphResolutionKind::undeclared_dependency);
    }
    const auto* target = find(provider);
    if (target == nullptr) {
        return failure(ModuleGraphResolutionKind::provider_not_registered);
    }
    if (!valid_identity(identity)) {
        return failure(ModuleGraphResolutionKind::invalid_symbol_identity);
    }
    for (const auto& symbol : target->exports) {
        if (symbol.identity == identity) {
            return ModuleGraphResolution{
                .kind = ModuleGraphResolutionKind::resolved,
                .guest_address = symbol.guest_address,
            };
        }
    }
    return failure(ModuleGraphResolutionKind::unresolved_export);
}

}  // namespace astraea::execution
