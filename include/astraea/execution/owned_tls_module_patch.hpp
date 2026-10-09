#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint32_t kX86_64Dtpmod64Type = 16U;
inline constexpr std::size_t kOwnedDtpmod64Width = 8U;

// An opaque runtime TLS module index, distinct from a guest address,
// SCE import-library ID, ELF dynamic symbol index, and HLE function ID.
// The caller must source and validate this independently for the module
// defining the TLS symbol. We do not assign module IDs here.
class OwnedTlsModuleId {
public:
    constexpr explicit OwnedTlsModuleId(std::uint64_t value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr std::uint64_t value() const noexcept {
        return value_;
    }

    auto operator<=>(const OwnedTlsModuleId&) const = default;

private:
    std::uint64_t value_;
};

enum class OwnedTlsModulePatchErrorCode {
    unsupported_table_kind,
    unsupported_relocation_type,
    missing_rela_addend,
    invalid_module_id,
    target_range_overflow,
};

struct OwnedTlsModulePatchError {
    OwnedTlsModulePatchErrorCode code =
        OwnedTlsModulePatchErrorCode::unsupported_relocation_type;
    std::uint32_t raw_relocation_type = 0;

    auto operator<=>(const OwnedTlsModulePatchError&) const = default;
};

struct OwnedTlsModulePatch {
    astraea::memory::GuestAddress target;
    std::uint32_t symbol_index = 0;
    OwnedTlsModuleId tls_module_id{0U};
    std::int64_t raw_addend = 0;
    std::array<std::byte, kOwnedDtpmod64Width> bytes{};

    auto operator<=>(const OwnedTlsModulePatch&) const = default;
};

using OwnedTlsModulePatchResult =
    astraea::core::Result<OwnedTlsModulePatch, OwnedTlsModulePatchError>;

// Source-owned standard x86-64 R_X86_64_DTPMOD64 relocation: the patched
// word64 holds the explicit runtime TLS module ID of the defining module.
// RELA addend is kept as provenance, not applied to this module ID.
// No ELF/SCE module identity, provider selection, runtime TLS allocation,
// guest mapping mutation, execution or PS5 firmware behavior is inferred.
[[nodiscard]] OwnedTlsModulePatchResult
build_owned_x86_64_dtpmod64_patch(
    const astraea::loader::DynamicRelocation& relocation,
    OwnedTlsModuleId defining_module_id) noexcept;

}  // namespace astraea::execution
