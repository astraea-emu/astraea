#include "breakout_offline_entry_interpreter.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <astraea/memory/mapping.hpp>

namespace astraea::research {
namespace {
namespace m = astraea::memory;

[[nodiscard]] bool source_is_executable(
    const astraea::loader::GuestImage& image,
    std::uint64_t address,
    std::uint64_t byte_count) {
    if (byte_count == 0U ||
        address > std::numeric_limits<std::uint64_t>::max() - (byte_count - 1U))
        return false;
    const auto last = m::GuestAddress{address + byte_count - 1U};
    for (const auto& mapping : image.mappings) {
        if (mapping.permissions.has(m::GuestPermission::execute) &&
            mapping.range.contains(m::GuestAddress{address}) &&
            mapping.range.contains(last))
            return true;
    }
    return false;
}

[[nodiscard]] std::uint64_t flags_after_and(std::uint64_t value) noexcept {
    // AND clears CF/OF, sets PF/ZF/SF. AF is architecturally undefined
    // and deliberately excluded from known_rflags_mask.
    constexpr std::uint64_t kPF = 1U << 2U;
    constexpr std::uint64_t kZF = 1U << 6U;
    constexpr std::uint64_t kSF = 1U << 7U;
    const bool parity_even =
        (std::popcount(static_cast<unsigned>(value & 0xffU)) & 1) == 0;
    return (parity_even ? kPF : 0U) |
           (value == 0U ? kZF : 0U) |
           ((value & (1ULL << 63U)) != 0U ? kSF : 0U);
}

[[nodiscard]] bool signed_relative_target(
    std::uint64_t next_rip, std::int32_t displacement,
    std::uint64_t& target) noexcept {
    const std::int64_t delta = displacement;
    if (delta < 0) {
        const auto magnitude = static_cast<std::uint64_t>(-delta);
        if (next_rip < magnitude) return false;
        target = next_rip - magnitude;
    } else {
        const auto magnitude = static_cast<std::uint64_t>(delta);
        if (next_rip > std::numeric_limits<std::uint64_t>::max() - magnitude)
            return false;
        target = next_rip + magnitude;
    }
    return true;
}
} // namespace

BreakoutEntryStepResult interpret_pinned_breakout_entry(
    const astraea::loader::GuestImage& image) {
    using Code = BreakoutEntryStopError;
    const auto fail = [](Code code) {
        return BreakoutEntryStepResult::failure(code);
    };
    const auto entry = image.elf.header.entry;
    if (!source_is_executable(image, entry, 9U))
        return fail(Code::unavailable_source_bytes);

    const auto source_view = image.initialized_image_view();
    const auto entry_span = m::GuestRange::create(
        m::GuestAddress{entry}, m::GuestSize{9U});
    std::array<std::byte, 9U> bytes{};
    if (!source_view.has_value() || !entry_span.has_value() ||
        !source_view->copy_bytes(entry_span.value(), bytes).has_value())
        return fail(Code::unavailable_source_bytes);

    constexpr std::array<std::byte, 4U> kAndRsp{
        std::byte{0x48}, std::byte{0x83},
        std::byte{0xe4}, std::byte{0xf0},
    };
    if (!std::equal(kAndRsp.begin(), kAndRsp.end(), bytes.begin()) ||
        bytes[4U] != std::byte{0xe8})
        return fail(Code::unexpected_entry_encoding);

    auto context = astraea::execution::make_synthetic_initial_context(image);
    if (context.rip != entry ||
        context.rsp != image.initial_stack.rsp.value())
        return fail(Code::incorrect_synthetic_context);
    const auto initial_rsp = context.rsp;
    if (entry > std::numeric_limits<std::uint64_t>::max() - 9U)
        return fail(Code::entry_address_overflow);
    const auto return_rip = entry + 9U;

    // The first decoded real instruction: AND RSP, sign-extended -16.
    // The caller-supplied synthetic stack is explicit and not the PS5 ABI.
    context.rsp &= ~std::uint64_t{0xfU};
    constexpr std::uint64_t kFlagMask =
        (1U << 0U) | (1U << 2U) | (1U << 6U) |
        (1U << 7U) | (1U << 11U);
    const auto known_flags = flags_after_and(context.rsp);
    context.rflags = (context.rflags & ~kFlagMask) | known_flags;
    context.rip = entry + 4U;

    // Decode CALL rel32 from the actual source bytes, not the C source
    // predicted symbol name. Branch target and return slot must be proven
    // representable *before* committing to either state change.
    std::uint32_t encoded_rel = 0U;
    for (std::size_t i = 0U; i < 4U; ++i)
        encoded_rel |= std::to_integer<std::uint32_t>(bytes[5U + i])
            << (8U * i);
    const auto displacement = std::bit_cast<std::int32_t>(encoded_rel);
    std::uint64_t target = 0U;
    if (!signed_relative_target(return_rip, displacement, target))
        return fail(Code::target_address_overflow);
    if (!source_is_executable(image, target, 1U))
        return fail(Code::target_not_executable);
    if (!source_view->read_byte(m::GuestAddress{target}).has_value())
        return fail(Code::target_source_unavailable);

    if (context.rsp < 8U)
        return fail(Code::invalid_guest_stack);
    const auto return_slot = context.rsp - 8U;
    if (!image.initial_stack.storage.contains(m::GuestAddress{return_slot}) ||
        !image.initial_stack.storage.contains(
            m::GuestAddress{return_slot + 7U}))
        return fail(Code::invalid_guest_stack);
    std::array<std::byte, 8U> return_word{};
    for (std::size_t i = 0U; i < return_word.size(); ++i)
        return_word[i] = static_cast<std::byte>(
            (return_rip >> (8U * i)) & 0xffU);

    // The second decoded real instruction: push *owned simulated guest*
    // return address and transfer RIP. No host memory write or guest entry.
    context.rsp = return_slot;
    context.rip = target;
    const auto next_byte = source_view->read_byte(m::GuestAddress{target});
    if (!next_byte.has_value())
        return fail(Code::target_source_unavailable);
    return BreakoutEntryStepResult::success(BreakoutEntryStepEvidence{
        .context = context,
        .entry_rip = entry,
        .initial_rsp = initial_rsp,
        .return_rip = return_rip,
        .branch_target = target,
        .guest_stack_write_address = return_slot,
        .guest_stack_return_word = return_word,
        .source_instruction_bytes = bytes,
        .first_uninterpreted_byte = next_byte.value(),
        .known_rflags_mask = kFlagMask,
        .known_rflags_value = known_flags,
        .interpreted_instructions = 2U,
    });
}
} // namespace astraea::research
