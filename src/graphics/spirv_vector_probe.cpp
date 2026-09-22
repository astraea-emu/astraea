#include <astraea/graphics/spirv_vector_probe.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include <spirv/unified1/spirv.hpp11>

namespace astraea::graphics {
namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203U;
constexpr std::uint32_t kSpirvVersion16 = 0x00010600U;
constexpr std::uint32_t kWordSizeBytes = 4U;
constexpr std::uint32_t kDescriptorSet = 0U;
constexpr std::uint32_t kBinding = 0U;

[[nodiscard]] constexpr std::uint32_t word(
    spv::Op value) noexcept {
    return static_cast<std::uint32_t>(value);
}

template <typename Enum>
[[nodiscard]] constexpr std::uint32_t word(
    Enum value) noexcept {
    return static_cast<std::uint32_t>(value);
}

class SpirvBuilder {
public:
    SpirvBuilder()
        : words_{
              kSpirvMagic,
              kSpirvVersion16,
              0U,
              0U,
              0U,
          } {}

    [[nodiscard]] std::uint32_t allocate_id() noexcept {
        const auto id = next_id_;
        ++next_id_;
        return id;
    }

    void emit(spv::Op opcode) {
        words_.push_back(
            (1U << 16U) | word(opcode));
    }

    void emit(
        spv::Op opcode,
        std::initializer_list<std::uint32_t> operands) {
        const auto word_count =
            static_cast<std::uint32_t>(
                operands.size() + 1U);
        words_.push_back(
            (word_count << 16U) | word(opcode));
        words_.insert(
            words_.end(),
            operands.begin(),
            operands.end());
    }

    void emit(
        spv::Op opcode,
        std::span<const std::uint32_t> operands) {
        const auto word_count =
            static_cast<std::uint32_t>(
                operands.size() + 1U);
        words_.push_back(
            (word_count << 16U) | word(opcode));
        words_.insert(
            words_.end(),
            operands.begin(),
            operands.end());
    }

    [[nodiscard]] std::vector<std::uint32_t> finish() && {
        words_[3] = next_id_;
        return std::move(words_);
    }

private:
    std::vector<std::uint32_t> words_;
    std::uint32_t next_id_ = 1U;
};

[[nodiscard]] SpirvVectorProbeError make_error(
    SpirvVectorProbeErrorCode code,
    std::size_t emission_index = 0,
    std::optional<ShaderIrUnsupportedReason>
        unsupported_reason = std::nullopt) noexcept {
    return SpirvVectorProbeError{
        .code = code,
        .emission_index = emission_index,
        .unsupported_reason = unsupported_reason,
    };
}

[[nodiscard]] std::uint32_t wave_size_words(
    ShaderWaveSize wave_size) noexcept {
    switch (wave_size) {
    case ShaderWaveSize::wave32:
        return 32U;
    case ShaderWaveSize::wave64:
        return 64U;
    case ShaderWaveSize::unspecified:
        return 0U;
    }
    return 0U;
}

void mark_vgpr(
    std::array<bool, kShaderVectorGprCount>& used,
    std::uint8_t index,
    bool& has_vgpr,
    std::uint8_t& max_vgpr) noexcept {
    used[index] = true;
    if (!has_vgpr || index > max_vgpr) {
        max_vgpr = index;
    }
    has_vgpr = true;
}

struct ProgramShape {
    std::array<bool, kShaderVectorGprCount> used_vgprs{};
    bool has_vgpr = false;
    std::uint8_t max_vgpr = 0;
};

using ProgramShapeResult =
    astraea::core::Result<
        ProgramShape,
        SpirvVectorProbeError>;

[[nodiscard]] ProgramShapeResult validate_program_shape(
    const ShaderIrProgram& program) noexcept {
    ProgramShape shape;
    bool saw_end = false;

    for (std::size_t index = 0;
         index < program.emissions.size();
         ++index) {
        const auto& operation =
            program.emissions[index].operation;

        if (saw_end) {
            if (std::holds_alternative<
                    ShaderIrEndProgram>(operation)) {
                return ProgramShapeResult::failure(
                    make_error(
                        SpirvVectorProbeErrorCode::
                            duplicate_end_program,
                        index));
            }
            return ProgramShapeResult::failure(
                make_error(
                    SpirvVectorProbeErrorCode::
                        operation_after_end_program,
                    index));
        }

        if (std::holds_alternative<
                ShaderIrEndProgram>(operation)) {
            saw_end = true;
            continue;
        }

        if (std::holds_alternative<
                ShaderIrNop>(operation)) {
            continue;
        }

        if (const auto* move =
                std::get_if<
                    ShaderIrVectorMove32>(&operation);
            move != nullptr) {
            mark_vgpr(
                shape.used_vgprs,
                move->destination.index,
                shape.has_vgpr,
                shape.max_vgpr);
            mark_vgpr(
                shape.used_vgprs,
                move->source.index,
                shape.has_vgpr,
                shape.max_vgpr);
            continue;
        }

        if (const auto* add =
                std::get_if<
                    ShaderIrVectorAddF32>(&operation);
            add != nullptr) {
            mark_vgpr(
                shape.used_vgprs,
                add->destination.index,
                shape.has_vgpr,
                shape.max_vgpr);
            mark_vgpr(
                shape.used_vgprs,
                add->source0.index,
                shape.has_vgpr,
                shape.max_vgpr);
            mark_vgpr(
                shape.used_vgprs,
                add->source1.index,
                shape.has_vgpr,
                shape.max_vgpr);
            continue;
        }

        if (const auto* unsupported =
                std::get_if<
                    ShaderIrUnsupported>(&operation);
            unsupported != nullptr) {
            return ProgramShapeResult::failure(
                make_error(
                    SpirvVectorProbeErrorCode::
                        unsupported_operation,
                    index,
                    unsupported->reason));
        }

        return ProgramShapeResult::failure(
            make_error(
                SpirvVectorProbeErrorCode::
                    unsupported_operation,
                index));
    }

    if (!saw_end) {
        return ProgramShapeResult::failure(
            make_error(
                SpirvVectorProbeErrorCode::
                    missing_end_program,
                program.emissions.size()));
    }

    return ProgramShapeResult::success(shape);
}

[[nodiscard]] std::vector<std::uint32_t>
encode_string_words(std::string_view value) {
    const auto byte_count = value.size() + 1U;
    const auto word_count =
        (byte_count + 3U) / 4U;

    std::vector<std::uint32_t> words(
        word_count,
        0U);
    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        const auto word_index = index / 4U;
        const auto byte_index = index % 4U;
        words[word_index] |=
            static_cast<std::uint32_t>(
                static_cast<unsigned char>(
                    value[index]))
            << static_cast<unsigned>(
                byte_index * 8U);
    }
    return words;
}

void emit_entry_point(
    SpirvBuilder& builder,
    std::uint32_t function_id,
    std::uint32_t local_index_variable_id,
    std::uint32_t buffer_variable_id) {
    std::vector<std::uint32_t> operands{
        word(spv::ExecutionModel::GLCompute),
        function_id,
    };
    auto name = encode_string_words("main");
    operands.insert(
        operands.end(),
        name.begin(),
        name.end());
    operands.push_back(local_index_variable_id);
    operands.push_back(buffer_variable_id);
    builder.emit(spv::Op::OpEntryPoint, operands);
}

[[nodiscard]] std::uint32_t required_vgpr_count(
    const ProgramShape& shape) noexcept {
    if (!shape.has_vgpr) {
        return 0U;
    }
    return static_cast<std::uint32_t>(
               shape.max_vgpr) +
           1U;
}

}  // namespace

SpirvVectorProbeResult
lower_shader_ir_to_spirv_vector_probe(
    const ShaderIrProgram& program,
    SpirvVectorProbeOptions options) {
    const auto wave_size =
        wave_size_words(options.wave_size);
    if (wave_size == 0U) {
        return SpirvVectorProbeResult::failure(
            make_error(
                SpirvVectorProbeErrorCode::
                    invalid_wave_size));
    }

    auto shape_result =
        validate_program_shape(program);
    if (!shape_result.has_value()) {
        return SpirvVectorProbeResult::failure(
            shape_result.error());
    }
    const auto shape = shape_result.value();

    try {
        SpirvBuilder builder;

        const auto void_type =
            builder.allocate_id();
        const auto uint_type =
            builder.allocate_id();
        const auto float_type =
            builder.allocate_id();
        const auto runtime_array_type =
            builder.allocate_id();
        const auto buffer_struct_type =
            builder.allocate_id();
        const auto storage_buffer_struct_pointer_type =
            builder.allocate_id();
        const auto storage_buffer_uint_pointer_type =
            builder.allocate_id();
        const auto input_uint_pointer_type =
            builder.allocate_id();
        const auto function_type =
            builder.allocate_id();
        const auto zero_constant =
            builder.allocate_id();
        const auto buffer_variable =
            builder.allocate_id();
        const auto local_index_variable =
            builder.allocate_id();
        const auto main_function =
            builder.allocate_id();
        const auto entry_label =
            builder.allocate_id();
        const auto lane_value =
            builder.allocate_id();

        std::array<std::uint32_t, kShaderVectorGprCount>
            base_constant_ids{};
        for (std::size_t vgpr = 0;
             vgpr < shape.used_vgprs.size();
             ++vgpr) {
            if (shape.used_vgprs[vgpr]) {
                base_constant_ids[vgpr] =
                    builder.allocate_id();
            }
        }

        builder.emit(
            spv::Op::OpCapability,
            {
                word(spv::Capability::Shader),
            });
        builder.emit(
            spv::Op::OpMemoryModel,
            {
                word(spv::AddressingModel::Logical),
                word(spv::MemoryModel::GLSL450),
            });

        emit_entry_point(
            builder,
            main_function,
            local_index_variable,
            buffer_variable);
        builder.emit(
            spv::Op::OpExecutionMode,
            {
                main_function,
                word(spv::ExecutionMode::LocalSize),
                wave_size,
                1U,
                1U,
            });

        builder.emit(
            spv::Op::OpDecorate,
            {
                runtime_array_type,
                word(spv::Decoration::ArrayStride),
                kWordSizeBytes,
            });
        builder.emit(
            spv::Op::OpMemberDecorate,
            {
                buffer_struct_type,
                0U,
                word(spv::Decoration::Offset),
                0U,
            });
        builder.emit(
            spv::Op::OpDecorate,
            {
                buffer_struct_type,
                word(spv::Decoration::Block),
            });
        builder.emit(
            spv::Op::OpDecorate,
            {
                buffer_variable,
                word(spv::Decoration::DescriptorSet),
                kDescriptorSet,
            });
        builder.emit(
            spv::Op::OpDecorate,
            {
                buffer_variable,
                word(spv::Decoration::Binding),
                kBinding,
            });
        builder.emit(
            spv::Op::OpDecorate,
            {
                local_index_variable,
                word(spv::Decoration::BuiltIn),
                word(spv::BuiltIn::LocalInvocationIndex),
            });

        builder.emit(
            spv::Op::OpTypeVoid,
            {
                void_type,
            });
        builder.emit(
            spv::Op::OpTypeInt,
            {
                uint_type,
                32U,
                0U,
            });
        builder.emit(
            spv::Op::OpTypeFloat,
            {
                float_type,
                32U,
            });
        builder.emit(
            spv::Op::OpTypeRuntimeArray,
            {
                runtime_array_type,
                uint_type,
            });
        builder.emit(
            spv::Op::OpTypeStruct,
            {
                buffer_struct_type,
                runtime_array_type,
            });
        builder.emit(
            spv::Op::OpTypePointer,
            {
                storage_buffer_struct_pointer_type,
                word(spv::StorageClass::StorageBuffer),
                buffer_struct_type,
            });
        builder.emit(
            spv::Op::OpTypePointer,
            {
                storage_buffer_uint_pointer_type,
                word(spv::StorageClass::StorageBuffer),
                uint_type,
            });
        builder.emit(
            spv::Op::OpTypePointer,
            {
                input_uint_pointer_type,
                word(spv::StorageClass::Input),
                uint_type,
            });
        builder.emit(
            spv::Op::OpTypeFunction,
            {
                function_type,
                void_type,
            });

        builder.emit(
            spv::Op::OpConstant,
            {
                uint_type,
                zero_constant,
                0U,
            });
        for (std::size_t vgpr = 0;
             vgpr < shape.used_vgprs.size();
             ++vgpr) {
            if (!shape.used_vgprs[vgpr]) {
                continue;
            }
            const auto base =
                static_cast<std::uint32_t>(vgpr) *
                wave_size;
            builder.emit(
                spv::Op::OpConstant,
                {
                    uint_type,
                    base_constant_ids[vgpr],
                    base,
                });
        }

        builder.emit(
            spv::Op::OpVariable,
            {
                storage_buffer_struct_pointer_type,
                buffer_variable,
                word(spv::StorageClass::StorageBuffer),
            });
        builder.emit(
            spv::Op::OpVariable,
            {
                input_uint_pointer_type,
                local_index_variable,
                word(spv::StorageClass::Input),
            });

        builder.emit(
            spv::Op::OpFunction,
            {
                void_type,
                main_function,
                word(spv::FunctionControlMask::MaskNone),
                function_type,
            });
        builder.emit(
            spv::Op::OpLabel,
            {
                entry_label,
            });
        builder.emit(
            spv::Op::OpLoad,
            {
                uint_type,
                lane_value,
                local_index_variable,
            });

        std::array<std::uint32_t, kShaderVectorGprCount>
            register_pointer_ids{};
        for (std::size_t vgpr = 0;
             vgpr < shape.used_vgprs.size();
             ++vgpr) {
            if (!shape.used_vgprs[vgpr]) {
                continue;
            }

            const auto flattened_index =
                builder.allocate_id();
            builder.emit(
                spv::Op::OpIAdd,
                {
                    uint_type,
                    flattened_index,
                    base_constant_ids[vgpr],
                    lane_value,
                });

            const auto pointer =
                builder.allocate_id();
            builder.emit(
                spv::Op::OpAccessChain,
                {
                    storage_buffer_uint_pointer_type,
                    pointer,
                    buffer_variable,
                    zero_constant,
                    flattened_index,
                });
            register_pointer_ids[vgpr] = pointer;
        }

        for (const auto& emission : program.emissions) {
            const auto& operation = emission.operation;

            if (const auto* nop =
                    std::get_if<ShaderIrNop>(&operation);
                nop != nullptr) {
                for (std::uint8_t repeat = 0;
                     repeat < nop->repeat_count;
                     ++repeat) {
                    builder.emit(
                        spv::Op::OpNop);
                }
                continue;
            }

            if (const auto* move =
                    std::get_if<
                        ShaderIrVectorMove32>(&operation);
                move != nullptr) {
                const auto value =
                    builder.allocate_id();
                builder.emit(
                    spv::Op::OpLoad,
                    {
                        uint_type,
                        value,
                        register_pointer_ids[
                            move->source.index],
                    });
                builder.emit(
                    spv::Op::OpStore,
                    {
                        register_pointer_ids[
                            move->destination.index],
                        value,
                    });
                continue;
            }

            if (const auto* add =
                    std::get_if<
                        ShaderIrVectorAddF32>(&operation);
                add != nullptr) {
                const auto source0_bits =
                    builder.allocate_id();
                const auto source1_bits =
                    builder.allocate_id();
                builder.emit(
                    spv::Op::OpLoad,
                    {
                        uint_type,
                        source0_bits,
                        register_pointer_ids[
                            add->source0.index],
                    });
                builder.emit(
                    spv::Op::OpLoad,
                    {
                        uint_type,
                        source1_bits,
                        register_pointer_ids[
                            add->source1.index],
                    });

                const auto source0_float =
                    builder.allocate_id();
                const auto source1_float =
                    builder.allocate_id();
                builder.emit(
                    spv::Op::OpBitcast,
                    {
                        float_type,
                        source0_float,
                        source0_bits,
                    });
                builder.emit(
                    spv::Op::OpBitcast,
                    {
                        float_type,
                        source1_float,
                        source1_bits,
                    });

                const auto sum =
                    builder.allocate_id();
                builder.emit(
                    spv::Op::OpFAdd,
                    {
                        float_type,
                        sum,
                        source0_float,
                        source1_float,
                    });

                const auto sum_bits =
                    builder.allocate_id();
                builder.emit(
                    spv::Op::OpBitcast,
                    {
                        uint_type,
                        sum_bits,
                        sum,
                    });
                builder.emit(
                    spv::Op::OpStore,
                    {
                        register_pointer_ids[
                            add->destination.index],
                        sum_bits,
                    });
                continue;
            }

            if (std::holds_alternative<
                    ShaderIrEndProgram>(operation)) {
                builder.emit(
                    spv::Op::OpReturn);
                continue;
            }
        }

        builder.emit(
            spv::Op::OpFunctionEnd);

        const auto vgpr_count =
            required_vgpr_count(shape);
        return SpirvVectorProbeResult::success(
            SpirvVectorProbeModule{
                .words = std::move(builder).finish(),
                .layout =
                    SpirvVectorProbeLayout{
                        .descriptor_set = kDescriptorSet,
                        .binding = kBinding,
                        .word_size_bytes =
                            kWordSizeBytes,
                        .wave_size = wave_size,
                        .vgpr_stride_words =
                            wave_size,
                        .required_vgpr_count =
                            vgpr_count,
                        .required_state_word_count =
                            vgpr_count * wave_size,
                    },
            });
    } catch (const std::bad_alloc&) {
        return SpirvVectorProbeResult::failure(
            make_error(
                SpirvVectorProbeErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return SpirvVectorProbeResult::failure(
            make_error(
                SpirvVectorProbeErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::graphics
