#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <span>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/module_graph_import_patch.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

struct OwnedModuleImportApplyResult {
    astraea::memory::GuestAddress target;
    astraea::memory::GuestAddress provider_guest_address;

    auto operator<=>(const OwnedModuleImportApplyResult&) const = default;
};

using OwnedModuleImportApplyResultType =
    astraea::core::Result<
        OwnedModuleImportApplyResult, GuestMemoryError>;

// Apply ONE already validated, independently owned x86-64 absolute import
// patch to prepared guest memory. GuestMemoryAccess enforces both guest
// mappings and prepared host permissions. No retail PS5 loader admission,
// runtime module initialization or provider-lifetime semantics are implied.
[[nodiscard]] OwnedModuleImportApplyResultType
apply_owned_module_import_patch(
    const OwnedModuleAbsolutePatch& patch,
    const GuestMemoryAccess& guest_memory) noexcept;


enum class OwnedModuleImportBatchErrorCode {
    too_many_patches,
    conflicting_target,
    preflight_failure,
    apply_failure,
    host_allocation_failure,
};

struct OwnedModuleImportBatchError {
    OwnedModuleImportBatchErrorCode code =
        OwnedModuleImportBatchErrorCode::host_allocation_failure;
    std::size_t patch_index = 0;
    std::optional<std::size_t> conflicting_patch_index;
    std::size_t applied_count = 0;
    std::optional<GuestMemoryError> memory_error;
};

using OwnedModuleImportBatchResult =
    astraea::core::Result<
        std::vector<OwnedModuleImportApplyResult>,
        OwnedModuleImportBatchError>;

// Bounded owned-module batch. Validates every target and rejects overlaps
// BEFORE the first mutation. A post-preflight write failure can still leave
// earlier patches applied: rollback/transactional atomicity is NOT promised.
[[nodiscard]] OwnedModuleImportBatchResult
apply_owned_module_import_batch(
    std::span<const OwnedModuleAbsolutePatch> patches,
    const GuestMemoryAccess& guest_memory);


enum class OwnedLiveJumpSlotErrorCode {
    too_many_patches,
    unsupported_relocation_type,
    invalid_patch_encoding,
    provider_not_live_executable,
    batch_failure,
};

struct OwnedLiveJumpSlotError {
    OwnedLiveJumpSlotErrorCode code =
        OwnedLiveJumpSlotErrorCode::batch_failure;
    std::size_t patch_index = 0U;
    std::size_t applied_count = 0U;
    std::optional<OwnedModuleImportBatchError> batch_error;
};

using OwnedLiveJumpSlotBatchResult =
    astraea::core::Result<
        std::vector<OwnedModuleImportApplyResult>,
        OwnedLiveJumpSlotError>;

// Narrow opt-in integration policy for *source-owned x86-64 callable*
// JUMP_SLOT exports. Checks each import's exact canonical address bytes,
// relocation class and live executable backing in the prepared guest plan,
// BEFORE delegating target preflight/conflict checks and writes to the
// existing owned batch application. A ModuleGraph export address alone is
// never proof that its provider remains mapped.
//
// This does NOT provide Sony module lifetime rules, GLOB_DAT/data-export
// handling, concurrency-safe unloading, post-write rollback or retail entry.
// As with apply_owned_module_import_batch, a failure after preflight can
// still leave earlier patches applied; inspect batch_error.applied_count.
[[nodiscard]] OwnedLiveJumpSlotBatchResult
apply_live_owned_jump_slot_batch(
    std::span<const OwnedModuleAbsolutePatch> patches,
    const GuestMemoryAccess& guest_memory);


 
// Caller-owned identity of ONE explicitly registered provider generation.
// Generation is not a guest virtual address, ELF TLS module index, or a
// Sony-assigned module ID. Tokens are valid only in their issuing registry.
struct OwnedProviderGeneration {
    std::uint64_t registry_id = 0U;
    std::uint64_t generation = 0U;
    std::string module_key;
    astraea::memory::GuestAddress address{0U};

    auto operator<=>(const OwnedProviderGeneration&) const = default;
};

enum class OwnedProviderRegistryErrorCode {
    invalid_module_key,
    invalid_provider_address,
    duplicate_active_module,
    duplicate_active_address,
    too_many_active_providers,
    generation_exhausted,
    host_allocation_failure,
};

struct OwnedProviderRegistryError {
    OwnedProviderRegistryErrorCode code =
        OwnedProviderRegistryErrorCode::invalid_module_key;
};

using OwnedProviderRegistrationResult =
    astraea::core::Result<
        OwnedProviderGeneration, OwnedProviderRegistryError>;

// No runtime provider discovery or memory mapping is attempted. The
// trusted owned loader/test supplies exact module key/address after mapping;
// the registry alone authorizes the generation epoch. An old generation is
// never revived when another owned provider appears at the same VA.
//
// Single-threaded only: registry mutation and importing must not race
// mapping teardown. No concurrency/unload synchronization is supplied.
class OwnedProviderRegistry {
public:
    OwnedProviderRegistry() noexcept;

    OwnedProviderRegistry(const OwnedProviderRegistry&) = delete;
    OwnedProviderRegistry& operator=(const OwnedProviderRegistry&) = delete;
    OwnedProviderRegistry(OwnedProviderRegistry&&) = delete;
    OwnedProviderRegistry& operator=(OwnedProviderRegistry&&) = delete;

    [[nodiscard]] OwnedProviderRegistrationResult register_provider(
        std::string_view module_key,
        astraea::memory::GuestAddress address,
        const GuestMemoryAccess& guest_memory);

    [[nodiscard]] bool retire(
        const OwnedProviderGeneration& generation) noexcept;

    [[nodiscard]] bool is_current(
        const OwnedProviderGeneration& generation,
        astraea::memory::GuestAddress address) const noexcept;

private:
    struct Entry {
        OwnedProviderGeneration handle;
    };

    // Registry IDs distinguish two independently owned registries as well
    // as generations within a registry. Zero is always invalid.
    std::uint64_t registry_id_ = 0U;
    std::uint64_t next_generation_ = 1U;
    std::vector<Entry> active_;
};

enum class OwnedBoundJumpSlotErrorCode {
    binding_count_mismatch,
    stale_or_foreign_provider_generation,
    live_patch_failure,
};

struct OwnedBoundJumpSlotError {
    OwnedBoundJumpSlotErrorCode code =
        OwnedBoundJumpSlotErrorCode::binding_count_mismatch;
    std::size_t patch_index = 0U;
    std::size_t applied_count = 0U;
    std::optional<OwnedLiveJumpSlotError> live_error;
};

using OwnedBoundJumpSlotBatchResult =
    astraea::core::Result<
        std::vector<OwnedModuleImportApplyResult>,
        OwnedBoundJumpSlotError>;

// Reject every stale/foreign provider generation BEFORE invoking the
// existing live executable-address check and batch write. The handle is
// captured at explicit source-owned provider registration, not inferred
// from PS5 dynamic tags. All patches and handles are position-aligned.
//
// No guarantee against concurrent mapping changes, rogue forged registry
// ownership, post-preflight partial writes, or guest code caching imports
// beyond this binding operation.
[[nodiscard]] OwnedBoundJumpSlotBatchResult
apply_generation_bound_owned_jump_slot_batch(
    std::span<const OwnedModuleAbsolutePatch> patches,
    std::span<const OwnedProviderGeneration> generations,
    const OwnedProviderRegistry& registry,
    const GuestMemoryAccess& guest_memory);

}  // namespace astraea::execution
