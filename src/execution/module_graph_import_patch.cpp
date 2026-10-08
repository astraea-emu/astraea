#include <astraea/execution/module_graph_import_patch.hpp>

#include <cstddef>
#include <cstdint>

namespace astraea::execution {
namespace {

constexpr std::uint32_t kGlobDat = 6U;
constexpr std::uint32_t kJumpSlot = 7U;

[[nodiscard]] OwnedModuleAbsolutePatchError make_error(
    OwnedModuleAbsolutePatchErrorCode code,
    std::uint32_t type) noexcept {
    return OwnedModuleAbsolutePatchError{
        .code = code,
        .raw_relocation_type = type,
    };
}

[[nodiscard]] std::array<std::byte, kOwnedModuleAbsolutePatchWidth>
encode_le64(std::uint64_t value) noexcept {
    std::array<std::byte, kOwnedModuleAbsolutePatchWidth> bytes{};
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        bytes[i] =
            static_cast<std::byte>((value >> (8U * i)) & 0xffU);
    }
    return bytes;
}

}  // namespace

OwnedModuleAbsolutePatchResult
build_owned_x86_64_module_import_patch(
    const ModuleGraphImportPlan& plan) noexcept {
    const bool is_glob_dat = plan.raw_relocation_type == kGlobDat;
    const bool is_jump_slot = plan.raw_relocation_type == kJumpSlot;
    if (!is_glob_dat && !is_jump_slot) {
        return OwnedModuleAbsolutePatchResult::failure(
            make_error(
                OwnedModuleAbsolutePatchErrorCode::
                    unsupported_relocation_type,
                plan.raw_relocation_type));
    }

    const bool expected_table =
        (is_glob_dat &&
         plan.table_kind == astraea::loader::RelocationTableKind::rela) ||
        (is_jump_slot &&
         plan.table_kind ==
             astraea::loader::RelocationTableKind::plt_rela);
    if (!expected_table) {
        return OwnedModuleAbsolutePatchResult::failure(
            make_error(
                OwnedModuleAbsolutePatchErrorCode::
                    unsupported_table_kind,
                plan.raw_relocation_type));
    }
    if (!plan.raw_addend.has_value()) {
        return OwnedModuleAbsolutePatchResult::failure(
            make_error(
                OwnedModuleAbsolutePatchErrorCode::
                    missing_rela_addend,
                plan.raw_relocation_type));
    }
    if (plan.provider_guest_address.value() == 0U) {
        return OwnedModuleAbsolutePatchResult::failure(
            make_error(
                OwnedModuleAbsolutePatchErrorCode::
                    invalid_export_address,
                plan.raw_relocation_type));
    }
    // This is only the address arithmetic gate. The later apply stage
    // must also prove mapped extent, permissions, aliasing and lifetime.
    const auto target = astraea::memory::GuestRange::create(
        plan.relocation_target,
        astraea::memory::GuestSize{kOwnedModuleAbsolutePatchWidth});
    if (!target.has_value()) {
        return OwnedModuleAbsolutePatchResult::failure(
            make_error(
                OwnedModuleAbsolutePatchErrorCode::
                    target_range_overflow,
                plan.raw_relocation_type));
    }

    return OwnedModuleAbsolutePatchResult::success(
        OwnedModuleAbsolutePatch{
            .target = plan.relocation_target,
            .source_symbol_address = plan.provider_guest_address,
            .raw_relocation_type = plan.raw_relocation_type,
            .raw_addend = plan.raw_addend,
            .bytes = encode_le64(
                plan.provider_guest_address.value()),
        });
}

}  // namespace astraea::execution
