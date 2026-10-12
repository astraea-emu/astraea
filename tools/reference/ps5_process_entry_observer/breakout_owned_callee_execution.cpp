#include "breakout_owned_callee_execution.hpp"
#include "owned_cpu_step.hpp"
#include "owned_guest_address_space.hpp"
#include "owned_indirect_call_observation.hpp"
#include "source_x86_64_decoder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::research {
namespace {
namespace l = astraea::loader;
namespace m = astraea::memory;
namespace e = astraea::execution;

// Source-authenticated, checked virtual observation is performed by the
// reusable non-retiring indirect-CALL observer. Import identity remains a
// separate Breakout/source-backed relocation check, never a Sony binding.
[[nodiscard]] bool import_stop(
    const l::GuestImage& image,
    const OwnedIndirectCallObservation& observation,
    OwnedCalleeEvidence& state) {
    if (observation.call_rip != state.context.rip ||
        !image.general_relocations.rela.has_value() ||
        !image.dynamic_symbols.has_value() ||
        !image.dynamic_strings.has_value() ||
        !image.dynamic_strings->string_table.has_value())
        return false;

    const auto slot = observation.pointer_guest_address;
    const auto view = image.initialized_image_view();
    if (!view.has_value()) return false;
    // Demand a unique verified external import relocation for the actual
    // RIP-relative GOT operand; do NOT load a GOT pointer or follow it.
    const auto view = image.initialized_image_view();
    if (!view.has_value()) return false;
    std::size_t matches = 0U;
    std::uint32_t symbol_index = 0U;
    const auto count = image.general_relocations.rela->count;
    for (std::uint64_t i = 0U; i < count; ++i) {
        auto record = l::parse_dynamic_relocation(
            image.general_relocations.rela.value(), i,
            image.dynamic_symbols.value(), view.value());
        if (!record.has_value()) return false;
        if (record->target.value() != slot) continue;
        if (record->relocation_type != 6U ||
            record->symbol_index == 0U) return false;
        ++matches;
        symbol_index = record->symbol_index;
    }
    if (matches != 1U) return false;
    const auto symbol = l::materialize_sce_dynamic_symbol(
        image.dynamic_symbols.value(),
        image.dynamic_strings->string_table.value(),
        symbol_index, view.value());
    if (!symbol.has_value() || symbol->symbol.section_index_raw != 0U ||
        symbol->symbol.type() != 2U ||
        symbol->name.raw.size() != 11U ||
        symbol->name.identity.has_value())
        return false;
    state.stopped_call_rip = observation.call_rip;
    state.unresolved_got_raw_word = observation.pointer_word_le;
    state.unresolved_got_slot = slot;
    state.unresolved_symbol_index = symbol_index;
    state.unresolved_bare_nid = symbol->name.raw;
    return true;
}
} // namespace

OwnedCalleeResult run_owned_breakout_callee(
    const l::GuestImage& image, const BreakoutEntryStepEvidence& entry) {
    const auto fail = [](OwnedCalleeError code) {
        return OwnedCalleeResult::failure(code);
    };
    if (entry.context.rip != entry.branch_target ||
        entry.context.rsp != entry.guest_stack_write_address ||
        entry.context.rsp > std::numeric_limits<std::uint64_t>::max() - 7U ||
        image.initial_stack.storage.size().value() != 4096U)
        return fail(OwnedCalleeError::unexpected_context);

    OwnedCalleeEvidence state{};
    state.context = entry.context;
    state.initial_callee_rsp = entry.context.rsp;
    state.guest_stack_base = image.initial_stack.storage.base().value();

    const auto perms = m::GuestPermissions::checked_from_bits(
        static_cast<std::uint8_t>(m::GuestPermission::read) |
        static_cast<std::uint8_t>(m::GuestPermission::write));
    if (!perms.has_value())
        return fail(OwnedCalleeError::stack_out_of_bounds);
    OwnedGuestAddressSpace memory;
    if (!memory.map(state.guest_stack_base, state.guest_stack_bytes,
                    perms.value()).has_value())
        return fail(OwnedCalleeError::stack_out_of_bounds);

    // The entry CALL was already retired in #432. Reconstruct its exact
    // owned simulated return word but do not count it among callee writes.
    if (!memory.write(
            entry.guest_stack_write_address,
            entry.guest_stack_return_word).has_value())
        return fail(OwnedCalleeError::stack_out_of_bounds);

    // Stage ORIGINAL GuestImage mapping fragments into ordinary, separately
    // owned host buffers. Preserve the exact guest virtual ranges and source
    // permissions, including virtual W+X, without making host pages executable.
    // Caller-owned buffers must remain alive for the entire fetch/step run.
    constexpr std::uint64_t kMaxOwnedSourceBytes = 16U * 1024U * 1024U;
    const auto source = image.initialized_image_view();
    if (!source.has_value() ||
        image.mappings.size() >= OwnedGuestAddressSpace::kMaxRegions)
        return fail(OwnedCalleeError::guest_source_unavailable);
    std::array<std::vector<std::byte>,
               OwnedGuestAddressSpace::kMaxRegions> source_buffers{};
    std::uint64_t source_bytes = 0U;
    for (std::size_t i = 0U; i < image.mappings.size(); ++i) {
        const auto& mapping = image.mappings[i];
        const auto length = mapping.range.size().value();
        if (length == 0U || length > kMaxOwnedSourceBytes - source_bytes)
            return fail(OwnedCalleeError::guest_source_unavailable);
        auto& backing = source_buffers[i];
        backing.resize(static_cast<std::size_t>(length));
        if (!source->copy_bytes(mapping.range, backing).has_value() ||
            !memory.map(mapping.range.base().value(), backing,
                        mapping.permissions).has_value())
            return fail(OwnedCalleeError::guest_source_unavailable);
        source_bytes += length;
    }

    // Decode and step one fresh instruction at a time from the actual
    // source-backed virtual mappings. The entry CALL was already modeled
    // by interpret_pinned_breakout_entry; no new Sony startup is assumed.
    const auto run = run_owned_x86_64(
        state.context, memory, kC1MaxDecodedInstructions);
    state.retired_instructions = run.retired_instructions;
    state.guest_stack_writes = run.guest_writes;
    if (run.stop == OwnedCpuRunStopCode::fetch_refused ||
        run.stop == OwnedCpuRunStopCode::invalid_encoding)
        return fail(OwnedCalleeError::decode_refused);
    if (run.stop != OwnedCpuRunStopCode::step_refused ||
        !run.step_stop.has_value())
        return fail(OwnedCalleeError::unsupported_instruction);

    const auto& stop = run.step_stop.value();
    if (stop.code == OwnedCpuStepStopCode::unresolved_control_transfer) {
        // Recheck that the stopped instruction's *actual staged virtual
        // bytes* agree with the current immutable GuestImage source. An
        // unresolved control transfer must not follow a GOT pointer.
        const auto decoded = decode_source_x86_64(
            image, state.context.rip, 1U);
        if (!decoded.has_value() || decoded->count != 1U)
            return fail(OwnedCalleeError::decode_refused);
        const auto& record = decoded->instructions[0];
        std::array<std::byte, kX86MaximumInstructionBytes> staged{};
        if (!memory.fetch(record.rip,
                          std::span<std::byte>{staged.data(),
                                               record.length}).has_value() ||
            !std::equal(record.bytes.begin(),
                        record.bytes.begin() + record.length,
                        staged.begin()))
            return fail(OwnedCalleeError::decode_refused);
        const auto observation = observe_owned_indirect_call(
            record, state.context, memory);
        if (!observation.has_value()) {
            if (observation.error().code ==
                    OwnedIndirectCallErrorCode::source_fetch_refused ||
                observation.error().code ==
                    OwnedIndirectCallErrorCode::source_bytes_mismatch ||
                observation.error().code ==
                    OwnedIndirectCallErrorCode::invalid_encoding)
                return fail(OwnedCalleeError::decode_refused);
            if (observation.error().code ==
                    OwnedIndirectCallErrorCode::operand_read_refused)
                return fail(OwnedCalleeError::missing_import_metadata);
            return fail(OwnedCalleeError::unsupported_instruction);
        }
        if (!import_stop(image, observation.value(), state))
            return fail(OwnedCalleeError::missing_import_metadata);

        // No GLOB_DAT import has been resolved. The original GOT bytes
        // must still be identical to their source-backed initialized view.
        const auto target_range = m::GuestRange::create(
            m::GuestAddress{state.unresolved_got_slot}, m::GuestSize{8U});
        std::array<std::byte, 8U> source_word{};
        if (!target_range.has_value() ||
            !source->copy_bytes(target_range.value(),
                                source_word).has_value() ||
            observation->pointer_word_bytes != source_word)
            return fail(OwnedCalleeError::missing_import_metadata);
        return OwnedCalleeResult::success(std::move(state));
    }
    if (stop.code == OwnedCpuStepStopCode::decode_refused)
        return fail(OwnedCalleeError::decode_refused);
    if (stop.code == OwnedCpuStepStopCode::guest_memory_refused)
        return fail(OwnedCalleeError::stack_out_of_bounds);
    if (stop.code == OwnedCpuStepStopCode::unsupported_operand ||
        stop.code == OwnedCpuStepStopCode::address_overflow)
        return fail(OwnedCalleeError::unsupported_operand);
    return fail(OwnedCalleeError::unsupported_instruction);
}
} // namespace astraea::research
