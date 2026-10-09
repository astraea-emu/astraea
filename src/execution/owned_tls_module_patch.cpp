#include <astraea/execution/owned_tls_module_patch.hpp>

#include <cstddef>
#include <cstdint>

namespace astraea::execution {
namespace {

[[nodiscard]] OwnedTlsModulePatchError make_error(
    OwnedTlsModulePatchErrorCode code,
    std::uint32_t raw_type) noexcept {
    return OwnedTlsModulePatchError{
        .code = code,
        .raw_relocation_type = raw_type,
    };
}

[[nodiscard]] std::array<std::byte, kOwnedDtpmod64Width>
encode_le64(std::uint64_t value) noexcept {
    std::array<std::byte, kOwnedDtpmod64Width> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
    return bytes;
}

}  // namespace

OwnedTlsModulePatchResult build_owned_x86_64_dtpmod64_patch(
    const astraea::loader::DynamicRelocation& relocation,
    OwnedTlsModuleId defining_module_id) noexcept {
    const auto type = relocation.relocation_type;
    if (relocation.table_kind != astraea::loader::RelocationTableKind::rela) {
        return OwnedTlsModulePatchResult::failure(make_error(
            OwnedTlsModulePatchErrorCode::unsupported_table_kind, type));
    }
    if (type != kX86_64Dtpmod64Type) {
        return OwnedTlsModulePatchResult::failure(make_error(
            OwnedTlsModulePatchErrorCode::unsupported_relocation_type, type));
    }
    if (!relocation.addend.has_value()) {
        return OwnedTlsModulePatchResult::failure(make_error(
            OwnedTlsModulePatchErrorCode::missing_rela_addend, type));
    }
    if (defining_module_id.value() == 0U) {
        return OwnedTlsModulePatchResult::failure(make_error(
            OwnedTlsModulePatchErrorCode::invalid_module_id, type));
    }
    const auto target = astraea::memory::GuestRange::create(
        relocation.target,
        astraea::memory::GuestSize{kOwnedDtpmod64Width});
    if (!target.has_value()) {
        return OwnedTlsModulePatchResult::failure(make_error(
            OwnedTlsModulePatchErrorCode::target_range_overflow, type));
    }

    return OwnedTlsModulePatchResult::success(OwnedTlsModulePatch{
        .target = relocation.target,
        .symbol_index = relocation.symbol_index,
        .tls_module_id = defining_module_id,
        .raw_addend = relocation.addend.value(),
        .bytes = encode_le64(defining_module_id.value()),
    });
}

}  // namespace astraea::execution
