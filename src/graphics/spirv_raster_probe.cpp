#include <astraea/graphics/spirv_raster_probe.hpp>

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <spirv/unified1/spirv.hpp11>

namespace astraea::graphics {
namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203U;
constexpr std::uint32_t kSpirvVersion16 = 0x00010600U;

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
        return next_id_++;
    }

    void emit(
        spv::Op opcode,
        std::initializer_list<std::uint32_t> operands = {}) {
        const auto count =
            static_cast<std::uint32_t>(
                operands.size() + 1U);
        words_.push_back(
            (count << 16U) | word(opcode));
        words_.insert(
            words_.end(),
            operands.begin(),
            operands.end());
    }

    void emit(
        spv::Op opcode,
        std::span<const std::uint32_t> operands) {
        const auto count =
            static_cast<std::uint32_t>(
                operands.size() + 1U);
        words_.push_back(
            (count << 16U) | word(opcode));
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

[[nodiscard]] std::vector<std::uint32_t>
encode_string_words(std::string_view value) {
    const auto byte_count = value.size() + 1U;
    const auto word_count =
        (byte_count + 3U) / 4U;
    std::vector<std::uint32_t> words(
        word_count,
        0U);

    for (std::size_t index = 0U;
         index < value.size();
         ++index) {
        words[index / 4U] |=
            static_cast<std::uint32_t>(
                static_cast<unsigned char>(
                    value[index]))
            << static_cast<unsigned>(
                (index % 4U) * 8U);
    }

    return words;
}

void emit_entry_point(
    SpirvBuilder& builder,
    spv::ExecutionModel model,
    std::uint32_t function,
    std::initializer_list<std::uint32_t> interfaces) {
    std::vector<std::uint32_t> operands{
        word(model),
        function,
    };
    auto name = encode_string_words("main");
    operands.insert(
        operands.end(),
        name.begin(),
        name.end());
    operands.insert(
        operands.end(),
        interfaces.begin(),
        interfaces.end());
    builder.emit(
        spv::Op::OpEntryPoint,
        operands);
}

[[nodiscard]] std::vector<std::uint32_t>
build_vertex_module() {
    SpirvBuilder b;

    const auto void_type = b.allocate_id();
    const auto uint_type = b.allocate_id();
    const auto float_type = b.allocate_id();
    const auto vec4_type = b.allocate_id();
    const auto input_uint_pointer = b.allocate_id();
    const auto output_vec4_pointer = b.allocate_id();
    const auto function_type = b.allocate_id();

    const auto uint_one = b.allocate_id();
    const auto uint_two = b.allocate_id();
    const auto float_zero = b.allocate_id();
    const auto float_one = b.allocate_id();
    const auto float_two = b.allocate_id();

    const auto vertex_index = b.allocate_id();
    const auto position = b.allocate_id();

    const auto main_function = b.allocate_id();
    const auto entry = b.allocate_id();
    const auto loaded_index = b.allocate_id();
    const auto shifted = b.allocate_id();
    const auto x_bits = b.allocate_id();
    const auto y_bits = b.allocate_id();
    const auto x_float = b.allocate_id();
    const auto y_float = b.allocate_id();
    const auto x_scaled = b.allocate_id();
    const auto y_scaled = b.allocate_id();
    const auto x_position = b.allocate_id();
    const auto y_position = b.allocate_id();
    const auto position_value = b.allocate_id();

    b.emit(
        spv::Op::OpCapability,
        {word(spv::Capability::Shader)});
    b.emit(
        spv::Op::OpMemoryModel,
        {
            word(spv::AddressingModel::Logical),
            word(spv::MemoryModel::GLSL450),
        });

    emit_entry_point(
        b,
        spv::ExecutionModel::Vertex,
        main_function,
        {vertex_index, position});

    b.emit(
        spv::Op::OpDecorate,
        {
            vertex_index,
            word(spv::Decoration::BuiltIn),
            word(spv::BuiltIn::VertexIndex),
        });
    b.emit(
        spv::Op::OpDecorate,
        {
            position,
            word(spv::Decoration::BuiltIn),
            word(spv::BuiltIn::Position),
        });

    b.emit(
        spv::Op::OpTypeVoid,
        {void_type});
    b.emit(
        spv::Op::OpTypeInt,
        {uint_type, 32U, 0U});
    b.emit(
        spv::Op::OpTypeFloat,
        {float_type, 32U});
    b.emit(
        spv::Op::OpTypeVector,
        {vec4_type, float_type, 4U});
    b.emit(
        spv::Op::OpTypePointer,
        {
            input_uint_pointer,
            word(spv::StorageClass::Input),
            uint_type,
        });
    b.emit(
        spv::Op::OpTypePointer,
        {
            output_vec4_pointer,
            word(spv::StorageClass::Output),
            vec4_type,
        });
    b.emit(
        spv::Op::OpTypeFunction,
        {function_type, void_type});

    b.emit(
        spv::Op::OpConstant,
        {uint_type, uint_one, 1U});
    b.emit(
        spv::Op::OpConstant,
        {uint_type, uint_two, 2U});
    b.emit(
        spv::Op::OpConstant,
        {float_type, float_zero, 0x00000000U});
    b.emit(
        spv::Op::OpConstant,
        {float_type, float_one, 0x3f800000U});
    b.emit(
        spv::Op::OpConstant,
        {float_type, float_two, 0x40000000U});

    b.emit(
        spv::Op::OpVariable,
        {
            input_uint_pointer,
            vertex_index,
            word(spv::StorageClass::Input),
        });
    b.emit(
        spv::Op::OpVariable,
        {
            output_vec4_pointer,
            position,
            word(spv::StorageClass::Output),
        });

    b.emit(
        spv::Op::OpFunction,
        {
            void_type,
            main_function,
            word(spv::FunctionControlMask::MaskNone),
            function_type,
        });
    b.emit(
        spv::Op::OpLabel,
        {entry});

    b.emit(
        spv::Op::OpLoad,
        {
            uint_type,
            loaded_index,
            vertex_index,
        });
    b.emit(
        spv::Op::OpShiftLeftLogical,
        {
            uint_type,
            shifted,
            loaded_index,
            uint_one,
        });
    b.emit(
        spv::Op::OpBitwiseAnd,
        {
            uint_type,
            x_bits,
            shifted,
            uint_two,
        });
    b.emit(
        spv::Op::OpBitwiseAnd,
        {
            uint_type,
            y_bits,
            loaded_index,
            uint_two,
        });
    b.emit(
        spv::Op::OpConvertUToF,
        {
            float_type,
            x_float,
            x_bits,
        });
    b.emit(
        spv::Op::OpConvertUToF,
        {
            float_type,
            y_float,
            y_bits,
        });
    b.emit(
        spv::Op::OpFMul,
        {
            float_type,
            x_scaled,
            x_float,
            float_two,
        });
    b.emit(
        spv::Op::OpFMul,
        {
            float_type,
            y_scaled,
            y_float,
            float_two,
        });
    b.emit(
        spv::Op::OpFSub,
        {
            float_type,
            x_position,
            x_scaled,
            float_one,
        });
    b.emit(
        spv::Op::OpFSub,
        {
            float_type,
            y_position,
            y_scaled,
            float_one,
        });
    b.emit(
        spv::Op::OpCompositeConstruct,
        {
            vec4_type,
            position_value,
            x_position,
            y_position,
            float_zero,
            float_one,
        });
    b.emit(
        spv::Op::OpStore,
        {
            position,
            position_value,
        });
    b.emit(spv::Op::OpReturn);
    b.emit(spv::Op::OpFunctionEnd);

    return std::move(b).finish();
}

[[nodiscard]] std::vector<std::uint32_t>
build_fragment_module() {
    SpirvBuilder b;

    const auto void_type = b.allocate_id();
    const auto float_type = b.allocate_id();
    const auto vec4_type = b.allocate_id();
    const auto output_vec4_pointer = b.allocate_id();
    const auto function_type = b.allocate_id();

    const auto float_zero = b.allocate_id();
    const auto float_one = b.allocate_id();
    const auto magenta = b.allocate_id();

    const auto output_color = b.allocate_id();
    const auto main_function = b.allocate_id();
    const auto entry = b.allocate_id();

    b.emit(
        spv::Op::OpCapability,
        {word(spv::Capability::Shader)});
    b.emit(
        spv::Op::OpMemoryModel,
        {
            word(spv::AddressingModel::Logical),
            word(spv::MemoryModel::GLSL450),
        });

    emit_entry_point(
        b,
        spv::ExecutionModel::Fragment,
        main_function,
        {output_color});

    b.emit(
        spv::Op::OpExecutionMode,
        {
            main_function,
            word(spv::ExecutionMode::OriginUpperLeft),
        });
    b.emit(
        spv::Op::OpDecorate,
        {
            output_color,
            word(spv::Decoration::Location),
            0U,
        });

    b.emit(
        spv::Op::OpTypeVoid,
        {void_type});
    b.emit(
        spv::Op::OpTypeFloat,
        {float_type, 32U});
    b.emit(
        spv::Op::OpTypeVector,
        {vec4_type, float_type, 4U});
    b.emit(
        spv::Op::OpTypePointer,
        {
            output_vec4_pointer,
            word(spv::StorageClass::Output),
            vec4_type,
        });
    b.emit(
        spv::Op::OpTypeFunction,
        {function_type, void_type});

    b.emit(
        spv::Op::OpConstant,
        {float_type, float_zero, 0x00000000U});
    b.emit(
        spv::Op::OpConstant,
        {float_type, float_one, 0x3f800000U});
    b.emit(
        spv::Op::OpConstantComposite,
        {
            vec4_type,
            magenta,
            float_one,
            float_zero,
            float_one,
            float_one,
        });

    b.emit(
        spv::Op::OpVariable,
        {
            output_vec4_pointer,
            output_color,
            word(spv::StorageClass::Output),
        });

    b.emit(
        spv::Op::OpFunction,
        {
            void_type,
            main_function,
            word(spv::FunctionControlMask::MaskNone),
            function_type,
        });
    b.emit(
        spv::Op::OpLabel,
        {entry});
    b.emit(
        spv::Op::OpStore,
        {
            output_color,
            magenta,
        });
    b.emit(spv::Op::OpReturn);
    b.emit(spv::Op::OpFunctionEnd);

    return std::move(b).finish();
}

}  // namespace

SpirvRasterProbeModules
build_spirv_raster_probe_modules() {
    return SpirvRasterProbeModules{
        .vertex_words = build_vertex_module(),
        .fragment_words = build_fragment_module(),
    };
}

}  // namespace astraea::graphics
