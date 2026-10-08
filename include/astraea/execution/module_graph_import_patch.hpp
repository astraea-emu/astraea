#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/execution/module_graph_import_plan.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::size_t kOwnedModuleAbsolutePatchWidth = 8U;

enum class OwnedModuleAbsolutePatchErrorCode {
    unsupported_table_kind,
    unsupported_relocation_type,
    invalid_export_address,
    target_range_overflow,
    missing_rela_addend,
};

struct OwnedModuleAbsolutePatchError {
    OwnedModuleAbsolutePatchErrorCode code =
        OwnedModuleAbsolutePatchErrorCode::unsupported_relocation_type;
    std::uint32_t raw_relocation_type = 0;

    auto operator<=>(const OwnedModuleAbsolutePatchError&) const = default;
};

struct OwnedModuleAbsolutePatch {
    astraea::memory::GuestAddress target;
    astraea::memory::GuestAddress source_symbol_address;
    std::uint32_t raw_relocation_type = 0;
    std::optional<std::int64_t> raw_addend;
    std::array<std::byte, kOwnedModuleAbsolutePatchWidth> bytes{};

    auto operator<=>(const OwnedModuleAbsolutePatch&) const = default;
};

using OwnedModuleAbsolutePatchResult =
    astraea::core::Result<
        OwnedModuleAbsolutePatch, OwnedModuleAbsolutePatchError>;

// For explicitly controlled owned modules only. Implements the standard
// x86-64 R_X86_64_GLOB_DAT / R_X86_64_JUMP_SLOT 64-bit S calculation,
// ignoring the RELA addend for the value (while retaining it as evidence).
// Does NOT write guest memory, approve provider lifetime, imply PS5 module
// identity, or permit a commercial title to execute.
[[nodiscard]] OwnedModuleAbsolutePatchResult
build_owned_x86_64_module_import_patch(
    const ModuleGraphImportPlan& plan) noexcept;

}  // namespace astraea::execution
