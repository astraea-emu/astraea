#include <astraea/graphics/shader_program.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::graphics {

ShaderIrProgramResult
lower_rdna2_stream_to_shader_ir(
    std::span<const std::uint32_t> words) {
    std::vector<ShaderIrEmission> emissions;

    try {
        // The instruction count can never exceed the source word count because
        // every successfully decoded instruction consumes at least one dword.
        // Reserve up front so allocation failure cannot occur after provenance
        // from earlier instructions has already been accumulated.
        emissions.reserve(words.size());
    } catch (const std::bad_alloc&) {
        return ShaderIrProgramResult::failure(
            ShaderIrProgramError{
                .code =
                    ShaderIrProgramErrorCode::
                        host_allocation_failure,
                .word_index = 0,
                .lowered_instruction_count = 0,
                .decode_error = std::nullopt,
            });
    } catch (const std::length_error&) {
        return ShaderIrProgramResult::failure(
            ShaderIrProgramError{
                .code =
                    ShaderIrProgramErrorCode::
                        host_allocation_failure,
                .word_index = 0,
                .lowered_instruction_count = 0,
                .decode_error = std::nullopt,
            });
    }

    std::size_t word_index = 0;
    while (word_index < words.size()) {
        auto decoded =
            decode_rdna2_instruction(
                words,
                word_index);
        if (!decoded.has_value()) {
            return ShaderIrProgramResult::failure(
                ShaderIrProgramError{
                    .code =
                        ShaderIrProgramErrorCode::
                            decode_failure,
                    .word_index = word_index,
                    .lowered_instruction_count =
                        emissions.size(),
                    .decode_error = decoded.error(),
                });
        }

        auto instruction =
            std::move(decoded).value();

        const auto remaining_words =
            words.size() - word_index;
        if (instruction.word_count == 0 ||
            instruction.word_count > remaining_words) {
            return ShaderIrProgramResult::failure(
                ShaderIrProgramError{
                    .code =
                        ShaderIrProgramErrorCode::
                            invalid_instruction_extent,
                    .word_index = word_index,
                    .lowered_instruction_count =
                        emissions.size(),
                    .decode_error = std::nullopt,
                });
        }

        const auto consumed_words =
            instruction.word_count;
        emissions.push_back(
            lower_rdna2_to_shader_ir(
                std::move(instruction)));
        word_index += consumed_words;
    }

    return ShaderIrProgramResult::success(
        ShaderIrProgram{
            .source_word_count = words.size(),
            .emissions = std::move(emissions),
        });
}

}  // namespace astraea::graphics
