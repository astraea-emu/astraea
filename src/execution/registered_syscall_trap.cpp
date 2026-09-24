#include <astraea/execution/registered_syscall_trap.hpp>

#include <algorithm>
#include <cstddef>
#include <span>

namespace astraea::execution {
namespace {

[[nodiscard]] RegisteredSyscallTrapError error(
    RegisteredSyscallTrapErrorCode code,
    std::size_t offset = 0U) noexcept {
    return RegisteredSyscallTrapError{
        .code = code,
        .code_byte_offset = offset,
    };
}

[[nodiscard]] bool span_fits(
    std::span<const std::byte> code,
    std::size_t offset) noexcept {
    return offset <= code.size() &&
           kX86SyscallBytes.size() <=
               code.size() - offset;
}

}  // namespace

RegisteredSyscallTrapPlanResult
plan_registered_syscall_trap(
    astraea::memory::GuestAddress code_base,
    std::span<const std::byte> code,
    astraea::memory::GuestAddress guest_rip) noexcept {
    const auto range =
        astraea::memory::GuestRange::create(
            code_base,
            astraea::memory::GuestSize{
                static_cast<std::uint64_t>(
                    code.size())});
    if (!range.has_value()) {
        return RegisteredSyscallTrapPlanResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    code_range_overflow));
    }

    if (guest_rip.value() < code_base.value()) {
        return RegisteredSyscallTrapPlanResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    site_before_code_range));
    }

    const auto offset_u64 =
        guest_rip.value() - code_base.value();
    if (offset_u64 >
        static_cast<std::uint64_t>(
            code.size())) {
        return RegisteredSyscallTrapPlanResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    site_not_fully_in_code_range));
    }

    const auto offset =
        static_cast<std::size_t>(offset_u64);
    if (!span_fits(code, offset)) {
        return RegisteredSyscallTrapPlanResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    site_not_fully_in_code_range,
                offset));
    }

    if (!std::equal(
            kX86SyscallBytes.begin(),
            kX86SyscallBytes.end(),
            code.begin() +
                static_cast<std::ptrdiff_t>(
                    offset))) {
        return RegisteredSyscallTrapPlanResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    unexpected_original_bytes,
                offset));
    }

    return RegisteredSyscallTrapPlanResult::success(
        RegisteredSyscallTrapSite{
            .guest_rip = guest_rip,
            .code_byte_offset = offset,
            .original_bytes =
                kX86SyscallBytes,
            .trap_bytes =
                kX86Ud2Bytes,
        });
}

RegisteredSyscallTrapMutationResult
apply_registered_syscall_trap(
    std::span<std::byte> code,
    const RegisteredSyscallTrapSite& site) noexcept {
    const std::span<const std::byte> read_only{
        code.data(),
        code.size()};
    if (!span_fits(
            read_only,
            site.code_byte_offset)) {
        return RegisteredSyscallTrapMutationResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    site_not_fully_in_code_range,
                site.code_byte_offset));
    }

    const auto begin =
        code.begin() +
        static_cast<std::ptrdiff_t>(
            site.code_byte_offset);
    if (!std::equal(
            site.original_bytes.begin(),
            site.original_bytes.end(),
            begin)) {
        return RegisteredSyscallTrapMutationResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    unexpected_original_bytes,
                site.code_byte_offset));
    }

    std::copy(
        site.trap_bytes.begin(),
        site.trap_bytes.end(),
        begin);
    return RegisteredSyscallTrapMutationResult::success(
        true);
}

RegisteredSyscallTrapMutationResult
restore_registered_syscall_trap(
    std::span<std::byte> code,
    const RegisteredSyscallTrapSite& site) noexcept {
    const std::span<const std::byte> read_only{
        code.data(),
        code.size()};
    if (!span_fits(
            read_only,
            site.code_byte_offset)) {
        return RegisteredSyscallTrapMutationResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    site_not_fully_in_code_range,
                site.code_byte_offset));
    }

    const auto begin =
        code.begin() +
        static_cast<std::ptrdiff_t>(
            site.code_byte_offset);
    if (!std::equal(
            site.trap_bytes.begin(),
            site.trap_bytes.end(),
            begin)) {
        return RegisteredSyscallTrapMutationResult::failure(
            error(
                RegisteredSyscallTrapErrorCode::
                    unexpected_trap_bytes,
                site.code_byte_offset));
    }

    std::copy(
        site.original_bytes.begin(),
        site.original_bytes.end(),
        begin);
    return RegisteredSyscallTrapMutationResult::success(
        true);
}

}  // namespace astraea::execution
