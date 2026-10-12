#include "owned_indirect_call_observation.hpp"
#include "owned_cpu_step.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include <Zydis/Zydis.h>

namespace astraea::research {
namespace {
[[nodiscard]] OwnedIndirectCallResult refuse(
    OwnedIndirectCallErrorCode code, std::uint64_t rip,
    std::optional<OwnedGuestMemoryFault> fault = std::nullopt) {
    return OwnedIndirectCallResult::failure(
        OwnedIndirectCallError{code, rip, fault});
}
} // namespace

OwnedIndirectCallResult observe_owned_indirect_call(
    const DecodedSourceInstruction& source_record,
    const astraea::execution::GuestCpuContext& cpu,
    const OwnedGuestAddressSpace& memory) {
    using Error = OwnedIndirectCallErrorCode;
    const auto rip = cpu.rip;
    if (source_record.rip != rip)
        return refuse(Error::context_mismatch, rip);
    if (source_record.length == 0U ||
        source_record.length > source_record.bytes.size())
        return refuse(Error::invalid_encoding, rip);
    if (rip > std::numeric_limits<std::uint64_t>::max() -
                  source_record.length)
        return refuse(Error::address_overflow, rip);

    // Verify every *current* instruction byte through virtual execute
    // permission before trusting any caller-supplied decode record.
    std::array<std::byte, kX86MaximumInstructionBytes> actual{};
    for (std::size_t i = 0U; i < source_record.length; ++i) {
        const auto fetched = memory.fetch(
            rip + i, std::span<std::byte>{actual.data() + i, 1U});
        if (!fetched.has_value())
            return refuse(Error::source_fetch_refused, rip, fetched.error());
    }
    if (!std::equal(source_record.bytes.begin(),
                    source_record.bytes.begin() + source_record.length,
                    actual.begin()))
        return refuse(Error::source_bytes_mismatch, rip);

    ZydisDecoder decoder{};
    ZydisDecodedInstruction instruction{};
    std::array<ZydisDecodedOperand, ZYDIS_MAX_OPERAND_COUNT> operands{};
    if (ZYAN_FAILED(ZydisDecoderInit(
            &decoder, ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64)) ||
        ZYAN_FAILED(ZydisDecoderDecodeFull(
            &decoder, actual.data(), source_record.length,
            &instruction, operands.data())) ||
        instruction.length != source_record.length ||
        instruction.mnemonic !=
            static_cast<ZydisMnemonic>(source_record.mnemonic))
        return refuse(Error::invalid_encoding, rip);

    const auto& op = operands[0];
    if (instruction.mnemonic != ZYDIS_MNEMONIC_CALL ||
        instruction.operand_count_visible != 1U ||
        op.type != ZYDIS_OPERAND_TYPE_MEMORY || op.size != 64U ||
        op.mem.base != ZYDIS_REGISTER_RIP ||
        op.mem.index != ZYDIS_REGISTER_NONE ||
        !op.mem.disp.has_displacement)
        return refuse(Error::unsupported_call_form, rip);

    const auto return_rip = rip + instruction.length;
    const auto address = owned_cpu_memory_address(cpu, op, return_rip);
    if (!address.has_value())
        return refuse(Error::address_overflow, rip);
    std::array<std::byte, 8U> word{};
    const auto read = memory.read(address.value(), word);
    if (!read.has_value())
        return refuse(Error::operand_read_refused, rip, read.error());
    std::uint64_t raw = 0U;
    for (std::size_t i = 0U; i < word.size(); ++i)
        raw |= std::to_integer<std::uint64_t>(word[i]) << (i * 8U);
    return OwnedIndirectCallResult::success(OwnedIndirectCallObservation{
        .call_rip = rip,
        .return_rip = return_rip,
        .pointer_guest_address = address.value(),
        .pointer_word_bytes = word,
        .pointer_word_le = raw,
    });
}
} // namespace astraea::research
