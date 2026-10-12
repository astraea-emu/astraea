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

#include <astraea/execution/owned_relative_patch.hpp>
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
        entry.entry_rip != image.elf.header.entry ||
        entry.initial_rsp != image.initial_stack.rsp.value() ||
        entry.interpreted_instructions != 2U ||
        image.initial_stack.storage.size().value() != 4096U)
        return fail(OwnedCalleeError::unexpected_context);
    if (entry.initial_rsp < 8U ||
        entry.guest_stack_write_address !=
            ((entry.initial_rsp & ~std::uint64_t{0xfU}) - 8U) ||
        entry.guest_stack_write_address >
            std::numeric_limits<std::uint64_t>::max() - 7U ||
        !image.initial_stack.storage.contains(
            m::GuestAddress{entry.guest_stack_write_address}) ||
        !image.initial_stack.storage.contains(
            m::GuestAddress{entry.guest_stack_write_address + 7U}))
        return fail(OwnedCalleeError::stack_out_of_bounds);

    OwnedCalleeEvidence state{};
    // The real executable entry is now fetched by the SAME generic
    // research CPU runner as the subsequent callee. The independent
    // two-instruction entry interpretation is retained only as an oracle.
    state.context = e::make_synthetic_initial_context(image);
    if (state.context.rip != entry.entry_rip ||
        state.context.rsp != entry.initial_rsp)
        return fail(OwnedCalleeError::unexpected_context);
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

    // Complete the ONE genuine source-backed RELATIVE relocation inside
    // virtual guest memory before interpretation. The guest VA layout here
    // uses the original ELF coordinates: explicit synthetic load bias zero,
    // NOT an observed Sony ASLR/process-entry contract. All nineteen
    // unresolved GLOB_DAT words must remain byte-identical to source.
    if (!image.general_relocations.rela.has_value() ||
        !image.dynamic_symbols.has_value() ||
        image.general_relocations.rela->count != 20U)
        return fail(OwnedCalleeError::relocation_refused);
    std::optional<e::OwnedRelativePatch> relative;
    std::array<std::uint64_t, 19U> unresolved_slots{};
    std::size_t imports = 0U;
    for (std::uint64_t i = 0U;
         i < image.general_relocations.rela->count; ++i) {
        const auto record = l::parse_dynamic_relocation(
            image.general_relocations.rela.value(), i,
            image.dynamic_symbols.value(), source.value());
        if (!record.has_value())
            return fail(OwnedCalleeError::relocation_refused);
        if (record->relocation_type == e::kOwnedX86_64RelativeType &&
            record->symbol_index == 0U) {
            if (relative.has_value())
                return fail(OwnedCalleeError::relocation_refused);
            auto patch = e::build_owned_x86_64_relative_patch(
                record.value(), m::GuestAddress{0U});
            if (!patch.has_value())
                return fail(OwnedCalleeError::relocation_refused);
            relative = patch.value();
        } else if (record->relocation_type == 6U &&
                   record->symbol_index != 0U &&
                   imports < unresolved_slots.size()) {
            const auto slot = record->target.value();
            if (std::find(unresolved_slots.begin(),
                          unresolved_slots.begin() + imports, slot) !=
                unresolved_slots.begin() + imports)
                return fail(OwnedCalleeError::relocation_refused);
            unresolved_slots[imports++] = slot;
        } else {
            return fail(OwnedCalleeError::relocation_refused);
        }
    }
    if (!relative.has_value() || imports != unresolved_slots.size())
        return fail(OwnedCalleeError::relocation_refused);

    const auto relative_target = relative->target.value();
    const auto source_relative_range = m::GuestRange::create(
        relative->target, m::GuestSize{relative->bytes.size()});
    std::array<std::byte, 8U> original_relative{};
    std::array<std::byte, 8U> initial_relative{};
    if (!source_relative_range.has_value() ||
        !source->copy_bytes(source_relative_range.value(),
                            original_relative).has_value() ||
        !memory.read(relative_target, initial_relative).has_value() ||
        !memory.preflight_write(relative_target, relative->bytes.size())
             .has_value() ||
        initial_relative != original_relative)
        return fail(OwnedCalleeError::relocation_refused);

    // Preflight every unbound import's source and guest-visible bytes
    // before committing the single relocation. Reject overlapping patch
    // destinations and refuse to manufacture provider addresses.
    for (const auto slot : unresolved_slots) {
        const bool overlaps =
            (relative_target <= slot && slot - relative_target < 8U) ||
            (slot < relative_target && relative_target - slot < 8U);
        const auto range = m::GuestRange::create(
            m::GuestAddress{slot}, m::GuestSize{8U});
        std::array<std::byte, 8U> original{}, staged{};
        if (overlaps || !range.has_value() ||
            !source->copy_bytes(range.value(), original).has_value() ||
            !memory.read(slot, staged).has_value() || staged != original ||
            !memory.preflight_write(slot, 8U).has_value())
            return fail(OwnedCalleeError::relocation_refused);
        ++state.preflighted_unresolved_imports;
    }
    std::array<std::byte, 8U> applied_relative{};
    if (!memory.write(relative_target, relative->bytes).has_value() ||
        !memory.read(relative_target, applied_relative).has_value() ||
        applied_relative != relative->bytes)
        return fail(OwnedCalleeError::relocation_refused);
    state.relative_patch_target = relative_target;
    state.relative_patch_value = relative->relocated_value.value();
    state.applied_relative_patches = 1U;

    // Run from the actual ELF entry in one bounded invocation: checked
    // AND RSP,imm; checked CALL rel32; eleven callee instructions; then
    // refuse the unresolved indirect Sony import before retirement.
    constexpr std::size_t kExpectedEntryRetirements = 2U;
    constexpr std::size_t kExpectedCalleeRetirements = 11U;
    const auto run = run_owned_x86_64(
        state.context, memory, kC1MaxDecodedInstructions +
                               kExpectedEntryRetirements);
    if (run.stop == OwnedCpuRunStopCode::fetch_refused ||
        run.stop == OwnedCpuRunStopCode::invalid_encoding)
        return fail(OwnedCalleeError::decode_refused);
    if (run.retired_instructions !=
            kExpectedEntryRetirements + kExpectedCalleeRetirements ||
        run.guest_writes == 0U)
        return fail(OwnedCalleeError::unsupported_instruction);
    state.entry_retired_instructions = kExpectedEntryRetirements;
    state.retired_instructions =
        run.retired_instructions - kExpectedEntryRetirements;
    state.guest_stack_writes = run.guest_writes - 1U;

    // Compare the real checked CALL's return-stack effect with the
    // separately source-backed two-instruction oracle.
    std::array<std::byte, 8U> saved_return{};
    if (!memory.read(entry.guest_stack_write_address,
                     saved_return).has_value() ||
        saved_return != entry.guest_stack_return_word)
        return fail(OwnedCalleeError::unexpected_context);
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
        // The single real source relocation must still hold after all
        // interpreted guest instructions; this is guest-memory readback,
        // not a trusted leaf report or a native guest effect.
        std::array<std::byte, 8U> post_step_relative{};
        if (!memory.read(relative_target, post_step_relative).has_value() ||
            post_step_relative != relative->bytes)
            return fail(OwnedCalleeError::relocation_refused);
        // No guest instruction or synthetic service is allowed to modify
        // any of the nineteen unresolved import slots before this stop.
        for (const auto slot : unresolved_slots) {
            const auto range = m::GuestRange::create(
                m::GuestAddress{slot}, m::GuestSize{8U});
            std::array<std::byte, 8U> original{}, import_readback{};
            if (!range.has_value() ||
                !source->copy_bytes(range.value(), original).has_value() ||
                !memory.read(slot, import_readback).has_value() ||
                import_readback != original)
                return fail(OwnedCalleeError::missing_import_metadata);
            ++state.untouched_unresolved_imports;
        }

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
