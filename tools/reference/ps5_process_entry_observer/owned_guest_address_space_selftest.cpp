// Source-owned, opt-in-only conformance and rejection checks for the
// virtual-memory boundary. No host protection change and no guest execution.
#include "owned_guest_address_space.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>

namespace {
namespace m = astraea::memory;
namespace r = astraea::research;
using Code = r::OwnedGuestMemoryError;

[[nodiscard]] bool is_error(
    const r::OwnedGuestAddressSpace::Result& result, Code code) {
    return !result.has_value() && result.error().code == code;
}

[[nodiscard]] bool verify() {
    const auto perms = [](std::uint8_t bits) {
        return m::GuestPermissions::checked_from_bits(bits);
    };
    const auto read_exec = perms(5U);
    const auto read_write = perms(3U);
    const auto write_only = perms(2U);
    const auto all = perms(7U);
    if (!read_exec.has_value() || !read_write.has_value() ||
        !write_only.has_value() || !all.has_value()) return false;

    r::OwnedGuestAddressSpace model;
    std::array<std::byte, 16U> source{
        std::byte{0x48}, std::byte{0x83},
        std::byte{0xe4}, std::byte{0xf0},
    };
    std::array<std::byte, 32U> stack{};
    std::array<std::byte, 8U> inaccessible{};
    std::array<std::byte, 4U> virtual_wx{};
    if (!model.map(0x1000U, source, read_exec.value()).has_value() ||
        !model.map(0x2000U, stack, read_write.value()).has_value() ||
        !model.map(0x3000U, inaccessible, write_only.value()).has_value() ||
        !model.map(0x4000U, virtual_wx, all.value()).has_value() ||
        model.region_count() != 4U) return false;

    std::array<std::byte, 4U> copied{};
    if (!model.fetch(0x1000U, copied).has_value() ||
        copied[0] != std::byte{0x48} ||
        !model.read(0x1000U, copied).has_value())
        return false;

    const std::array<std::byte, 8U> payload{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
    };
    if (!model.write(0x2010U, payload).has_value() ||
        stack[16U] != payload[0] || stack[23U] != payload[7])
        return false;
    std::array<std::byte, 8U> copied_stack{};
    if (!model.read(0x2010U, copied_stack).has_value() ||
        copied_stack != payload) return false;

    // Overlapping virtual read/write transfers must use memmove semantics
    // when their caller-owned spans refer to the same allocation.
    for (std::size_t i = 0U; i < 8U; ++i)
        stack[i] = static_cast<std::byte>(i + 1U);
    if (!model.write(0x2004U,
            std::span<const std::byte>{stack.data(), 8U}).has_value())
        return false;
    for (std::size_t i = 0U; i < 8U; ++i)
        if (stack[i + 4U] != static_cast<std::byte>(i + 1U))
            return false;
    std::array<std::byte, 8U> snapshot{};
    for (std::size_t i = 0U; i < 8U; ++i)
        snapshot[i] = stack[i + 4U];
    if (!model.read(0x2004U,
            std::span<std::byte>{stack.data() + 6U, 8U}).has_value())
        return false;
    for (std::size_t i = 0U; i < 8U; ++i)
        if (stack[i + 6U] != snapshot[i])
            return false;

    // Rejecting a cross-boundary access must not write its valid prefix.
    const auto stack_before = stack;
    if (!is_error(model.write(0x201cU, payload), Code::unmapped) ||
        stack != stack_before ||
        !is_error(model.write(0x1008U, payload), Code::permission_denied) ||
        source[8U] != std::byte{0} ||
        !is_error(model.fetch(0x2000U, copied), Code::permission_denied))
        return false;
    copied.fill(std::byte{0x7f});
    if (!is_error(model.read(0x3000U, copied), Code::permission_denied) ||
        copied != std::array<std::byte, 4U>{
            std::byte{0x7f}, std::byte{0x7f},
            std::byte{0x7f}, std::byte{0x7f}})
        return false;

    // Guest W+X permissions can be represented without allocating any
    // host-executable page; this is a model, not native mmap admission.
    if (!model.write(0x4000U,
            std::span<const std::byte>{payload.data(), 4U}).has_value() ||
        !model.fetch(0x4000U, copied).has_value() ||
        copied[0] != std::byte{1})
        return false;

    if (!is_error(model.write(0x5000U, payload), Code::unmapped) ||
        !is_error(model.write(
            std::numeric_limits<std::uint64_t>::max() - 3U,
            payload), Code::address_overflow) ||
        !is_error(model.write(0x2000U, {}), Code::zero_length) ||
        !is_error(model.map(0x8000U,
            std::span<std::byte>{}, read_write.value()), Code::zero_length))
        return false;

    // Independently check overlap and alternate virtual alias without
    // exhausting a region table; a fifth disjoint map hits the region cap.
    r::OwnedGuestAddressSpace overlap;
    std::array<std::byte, 16U> owned{};
    std::array<std::byte, 16U> other{};
    if (!overlap.map(0x7000U, owned, read_write.value()).has_value() ||
        !is_error(overlap.map(0x7008U, other, read_write.value()),
                  Code::region_overlap) ||
        !is_error(overlap.map(0x8000U, owned, read_write.value()),
                  Code::backing_alias) ||
        !is_error(overlap.map(
            std::numeric_limits<std::uint64_t>::max() - 7U,
            other, read_write.value()), Code::address_overflow) ||
        overlap.region_count() != 1U)
        return false;
    std::array<std::byte, 4U> extra{};
    if (!is_error(model.map(0x9000U, extra, read_write.value()),
                  Code::region_limit) ||
        model.region_count() != 4U)
        return false;
    return true;
}
} // namespace

int main() {
    if (!verify()) return 1;
    std::cout << "C1 OWNED VIRTUAL GUEST MEMORY PASS\n"
              << "checked_regions=4\n"
              << "checked_guest_rw_stack=pass\n"
              << "write_readonly=refused\n"
              << "execute_nonexec=refused\n"
              << "read_no_read_permission=refused\n"
              << "cross_region_write=refused_without_partial_mutation\n"
              << "overlapping_caller_spans=memmove_safe\n"
              << "overflow_unmapped_empty=refused\n"
              << "overlap_host_alias=refused\n"
              << "guest_virtual_wx=represented_without_host_executable_mapping\n"
              << "native_hostile_code_containment=not_established\n";
}
