#include "source_x86_64_decoder.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <Zydis/Zydis.h>
#include <astraea/memory/mapping.hpp>

namespace astraea::research {
namespace {
namespace m = astraea::memory;

[[nodiscard]] bool executable_byte(
    const astraea::loader::GuestImage& image,
    std::uint64_t address) noexcept {
    for (const auto& mapping : image.mappings) {
        if (mapping.permissions.has(m::GuestPermission::execute) &&
            mapping.range.contains(m::GuestAddress{address}))
            return true;
    }
    return false;
}
} // namespace

SourceDecodeResult decode_source_x86_64(
    const astraea::loader::GuestImage& image,
    std::uint64_t first_rip,
    std::size_t instruction_budget) {
    const auto fail = [](SourceDecodeError e) {
        return SourceDecodeResult::failure(e);
    };
    if (instruction_budget == 0U ||
        instruction_budget > kC1MaxDecodedInstructions)
        return fail(SourceDecodeError::invalid_budget);

    const auto view = image.initialized_image_view();
    if (!view.has_value())
        return fail(SourceDecodeError::source_unavailable);
    ZydisDecoder decoder{};
    if (ZYAN_FAILED(ZydisDecoderInit(
            &decoder, ZYDIS_MACHINE_MODE_LONG_64,
            ZYDIS_STACK_WIDTH_64)))
        return fail(SourceDecodeError::decoder_initialization_failure);

    SourceDecodeReport report{};
    std::uint64_t rip = first_rip;
    for (std::size_t i = 0U; i < instruction_budget; ++i) {
        if (!executable_byte(image, rip))
            return fail(SourceDecodeError::target_not_executable);
        std::array<std::byte, kX86MaximumInstructionBytes> bytes{};
        std::size_t available = 0U;
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            if (rip > std::numeric_limits<std::uint64_t>::max() - n)
                break;
            const auto at = rip + n;
            if (!executable_byte(image, at)) break;
            const auto byte = view->read_byte(m::GuestAddress{at});
            if (!byte.has_value()) break;
            bytes[n] = byte.value();
            ++available;
        }
        if (available == 0U)
            return fail(SourceDecodeError::source_unavailable);

        ZydisDecodedInstruction instruction{};
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT]{};
        if (ZYAN_FAILED(ZydisDecoderDecodeFull(
                &decoder, bytes.data(), available,
                &instruction, operands)))
            return fail(SourceDecodeError::invalid_encoding);
        if (instruction.length == 0U || instruction.length > available ||
            instruction.length > kX86MaximumInstructionBytes)
            return fail(SourceDecodeError::invalid_decoded_length);
        if (rip > std::numeric_limits<std::uint64_t>::max() -
                      instruction.length)
            return fail(SourceDecodeError::guest_rip_overflow);

        report.instructions[i] = DecodedSourceInstruction{
            .rip = rip,
            .length = instruction.length,
            .bytes = bytes,
            .mnemonic = static_cast<std::uint32_t>(instruction.mnemonic),
        };
        ++report.count;
        rip += instruction.length;
    }
    report.next_rip = rip;
    return SourceDecodeResult::success(report);
}

std::string_view source_mnemonic_name(std::uint32_t id) noexcept {
    // Zydis returns a constant static mnemonic name or a null pointer
    // for invalid IDs. The owned report does not rely on formatting.
    const auto* const name = ZydisMnemonicGetString(
        static_cast<ZydisMnemonic>(id));
    return name == nullptr ? std::string_view{"INVALID"} :
        std::string_view{name};
}
} // namespace astraea::research
