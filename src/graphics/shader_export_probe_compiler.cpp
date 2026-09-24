#include <astraea/graphics/shader_export_probe_compiler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <spirv/unified1/spirv.hpp11>

namespace astraea::graphics {
namespace {

constexpr std::uint32_t kFloatZero = 0x00000000U;
constexpr std::uint32_t kFloatOne = 0x3f800000U;
constexpr std::uint32_t kSpirvMagic = 0x07230203U;
constexpr std::uint32_t kSpirvVersion16 = 0x00010600U;

[[nodiscard]] ShaderExportProbeCompilerError error(
    ShaderExportProbeCompilerErrorCode code,
    std::size_t component_index = 0U,
    std::uint8_t vgpr_index = 0U) noexcept {
    return ShaderExportProbeCompilerError{
        .code = code,
        .component_index = component_index,
        .vgpr_index = vgpr_index,
    };
}

[[nodiscard]] bool bindings_are_unique(
    const ShaderExportProbeLaunchAbi& launch_abi) noexcept {
    for (std::size_t left = 0U;
         left < launch_abi.bindings.size();
         ++left) {
        for (std::size_t right = left + 1U;
             right < launch_abi.bindings.size();
             ++right) {
            if (launch_abi.bindings[left].vgpr ==
                launch_abi.bindings[right].vgpr) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] const ShaderExportProbeVgprBinding*
find_binding(
    const ShaderExportProbeLaunchAbi& launch_abi,
    ShaderIrVgpr vgpr) noexcept {
    for (const auto& binding : launch_abi.bindings) {
        if (binding.vgpr == vgpr) {
            return &binding;
        }
    }
    return nullptr;
}

[[nodiscard]] bool valid_exact_probe_components(
    const ShaderExportProbeCompilerIr& ir) noexcept {
    using Kind = ShaderExportProbeValueSourceKind;

    if (ir.stage == ShaderExportProbeStage::pre_raster) {
        return
            ir.components[0].source.kind ==
                Kind::vertex_index_position_x &&
            ir.components[1].source.kind ==
                Kind::vertex_index_position_y &&
            ir.components[2].source.kind ==
                Kind::constant_f32_bits &&
            ir.components[2].source.constant_bits ==
                kFloatZero &&
            ir.components[3].source.kind ==
                Kind::constant_f32_bits &&
            ir.components[3].source.constant_bits ==
                kFloatOne;
    }

    return
        ir.components[0].source.kind ==
            Kind::constant_f32_bits &&
        ir.components[0].source.constant_bits ==
            kFloatOne &&
        ir.components[1].source.kind ==
            Kind::constant_f32_bits &&
        ir.components[1].source.constant_bits ==
            kFloatZero &&
        ir.components[2].source.kind ==
            Kind::constant_f32_bits &&
        ir.components[2].source.constant_bits ==
            kFloatOne &&
        ir.components[3].source.kind ==
            Kind::constant_f32_bits &&
        ir.components[3].source.constant_bits ==
            kFloatOne;
}

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
build_vertex_module(
    const ShaderExportProbeCompilerIr& ir) {
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

    b.emit(spv::Op::OpTypeVoid, {void_type});
    b.emit(spv::Op::OpTypeInt, {uint_type, 32U, 0U});
    b.emit(spv::Op::OpTypeFloat, {float_type, 32U});
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

    b.emit(spv::Op::OpConstant, {uint_type, uint_one, 1U});
    b.emit(spv::Op::OpConstant, {uint_type, uint_two, 2U});
    b.emit(
        spv::Op::OpConstant,
        {float_type, float_zero, kFloatZero});
    b.emit(
        spv::Op::OpConstant,
        {float_type, float_one, kFloatOne});
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
    b.emit(spv::Op::OpLabel, {entry});
    b.emit(
        spv::Op::OpLoad,
        {uint_type, loaded_index, vertex_index});
    b.emit(
        spv::Op::OpShiftLeftLogical,
        {uint_type, shifted, loaded_index, uint_one});
    b.emit(
        spv::Op::OpBitwiseAnd,
        {uint_type, x_bits, shifted, uint_two});
    b.emit(
        spv::Op::OpBitwiseAnd,
        {uint_type, y_bits, loaded_index, uint_two});
    b.emit(
        spv::Op::OpConvertUToF,
        {float_type, x_float, x_bits});
    b.emit(
        spv::Op::OpConvertUToF,
        {float_type, y_float, y_bits});
    b.emit(
        spv::Op::OpFMul,
        {float_type, x_scaled, x_float, float_two});
    b.emit(
        spv::Op::OpFMul,
        {float_type, y_scaled, y_float, float_two});
    b.emit(
        spv::Op::OpFSub,
        {float_type, x_position, x_scaled, float_one});
    b.emit(
        spv::Op::OpFSub,
        {float_type, y_position, y_scaled, float_one});

    const std::array<std::uint32_t, 4> component_ids{
        ir.components[0].source.kind ==
                ShaderExportProbeValueSourceKind::
                    vertex_index_position_x
            ? x_position
            : float_zero,
        ir.components[1].source.kind ==
                ShaderExportProbeValueSourceKind::
                    vertex_index_position_y
            ? y_position
            : float_zero,
        ir.components[2].source.constant_bits == kFloatOne
            ? float_one
            : float_zero,
        ir.components[3].source.constant_bits == kFloatOne
            ? float_one
            : float_zero,
    };
    b.emit(
        spv::Op::OpCompositeConstruct,
        {
            vec4_type,
            position_value,
            component_ids[0],
            component_ids[1],
            component_ids[2],
            component_ids[3],
        });
    b.emit(
        spv::Op::OpStore,
        {position, position_value});
    b.emit(spv::Op::OpReturn);
    b.emit(spv::Op::OpFunctionEnd);

    return std::move(b).finish();
}

[[nodiscard]] std::vector<std::uint32_t>
build_fragment_module(
    const ShaderExportProbeCompilerIr& ir) {
    SpirvBuilder b;

    const auto void_type = b.allocate_id();
    const auto float_type = b.allocate_id();
    const auto vec4_type = b.allocate_id();
    const auto output_vec4_pointer = b.allocate_id();
    const auto function_type = b.allocate_id();

    std::array<std::uint32_t, 4> constants{};
    for (auto& constant : constants) {
        constant = b.allocate_id();
    }
    const auto color = b.allocate_id();
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

    b.emit(spv::Op::OpTypeVoid, {void_type});
    b.emit(spv::Op::OpTypeFloat, {float_type, 32U});
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

    for (std::size_t component = 0U;
         component < constants.size();
         ++component) {
        b.emit(
            spv::Op::OpConstant,
            {
                float_type,
                constants[component],
                ir.components[component].source.constant_bits,
            });
    }
    b.emit(
        spv::Op::OpConstantComposite,
        {
            vec4_type,
            color,
            constants[0],
            constants[1],
            constants[2],
            constants[3],
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
    b.emit(spv::Op::OpLabel, {entry});
    b.emit(spv::Op::OpStore, {output_color, color});
    b.emit(spv::Op::OpReturn);
    b.emit(spv::Op::OpFunctionEnd);

    return std::move(b).finish();
}

}  // namespace

ShaderExportProbeLaunchAbi
make_fullscreen_position_probe_launch_abi() noexcept {
    using Kind = ShaderExportProbeValueSourceKind;
    return ShaderExportProbeLaunchAbi{
        .stage = ShaderExportProbeStage::pre_raster,
        .bindings = {
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 0U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::vertex_index_position_x,
                        .constant_bits = 0U,
                    },
            },
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 1U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::vertex_index_position_y,
                        .constant_bits = 0U,
                    },
            },
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 2U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::constant_f32_bits,
                        .constant_bits = kFloatZero,
                    },
            },
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 3U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::constant_f32_bits,
                        .constant_bits = kFloatOne,
                    },
            },
        },
    };
}

ShaderExportProbeLaunchAbi
make_magenta_fragment_probe_launch_abi() noexcept {
    using Kind = ShaderExportProbeValueSourceKind;
    return ShaderExportProbeLaunchAbi{
        .stage = ShaderExportProbeStage::fragment,
        .bindings = {
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 0U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::constant_f32_bits,
                        .constant_bits = kFloatOne,
                    },
            },
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 1U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::constant_f32_bits,
                        .constant_bits = kFloatZero,
                    },
            },
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 2U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::constant_f32_bits,
                        .constant_bits = kFloatOne,
                    },
            },
            ShaderExportProbeVgprBinding{
                .vgpr = ShaderIrVgpr{.index = 3U},
                .source =
                    ShaderExportProbeValueSource{
                        .kind = Kind::constant_f32_bits,
                        .constant_bits = kFloatOne,
                    },
            },
        },
    };
}

ShaderExportProbeCompilerResult
compile_shader_export_probe(
    const ShaderIrProgram& program,
    const ShaderExportProbeLaunchAbi& launch_abi) {
    if (program.emissions.size() != 2U ||
        !std::holds_alternative<ShaderIrEndProgram>(
            program.emissions[1].operation)) {
        return ShaderExportProbeCompilerResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    invalid_program_shape));
    }

    const auto* export_op =
        std::get_if<ShaderIrExport>(
            &program.emissions[0].operation);
    if (export_op == nullptr) {
        return ShaderExportProbeCompilerResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    invalid_program_shape));
    }

    if (export_op->enable_mask != 0x0fU ||
        export_op->compressed ||
        !export_op->done ||
        export_op->valid_mask) {
        return ShaderExportProbeCompilerResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    unsupported_export_form));
    }

    const bool target_matches =
        (launch_abi.stage ==
             ShaderExportProbeStage::pre_raster &&
         export_op->target_kind ==
             ShaderIrExportTargetKind::position &&
         export_op->target_index == 0U) ||
        (launch_abi.stage ==
             ShaderExportProbeStage::fragment &&
         export_op->target_kind ==
             ShaderIrExportTargetKind::mrt &&
         export_op->target_index == 0U);
    if (!target_matches) {
        return ShaderExportProbeCompilerResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    unsupported_export_target));
    }

    if (!bindings_are_unique(launch_abi)) {
        return ShaderExportProbeCompilerResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    duplicate_binding));
    }

    ShaderExportProbeCompilerIr ir{
        .stage = launch_abi.stage,
        .components = {},
        .export_provenance =
            program.emissions[0].provenance,
    };

    for (std::size_t component = 0U;
         component < export_op->sources.size();
         ++component) {
        const auto vgpr = export_op->sources[component];
        const auto* binding =
            find_binding(
                launch_abi,
                vgpr);
        if (binding == nullptr) {
            return ShaderExportProbeCompilerResult::failure(
                error(
                    ShaderExportProbeCompilerErrorCode::
                        missing_binding,
                    component,
                    vgpr.index));
        }
        ir.components[component] =
            ShaderExportProbeCompilerValue{
                .source_vgpr = vgpr,
                .source = binding->source,
            };
    }

    if (!valid_exact_probe_components(ir)) {
        return ShaderExportProbeCompilerResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    unsupported_probe_binding));
    }

    return ShaderExportProbeCompilerResult::success(
        ir);
}

ShaderExportProbeSpirvResult
lower_shader_export_probe_to_spirv(
    const ShaderExportProbeCompilerIr& compiler_ir) {
    if (!valid_exact_probe_components(compiler_ir)) {
        return ShaderExportProbeSpirvResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    unsupported_probe_binding));
    }

    try {
        auto words =
            compiler_ir.stage ==
                    ShaderExportProbeStage::pre_raster
                ? build_vertex_module(compiler_ir)
                : build_fragment_module(compiler_ir);
        return ShaderExportProbeSpirvResult::success(
            ShaderExportProbeSpirvModule{
                .stage = compiler_ir.stage,
                .words = std::move(words),
            });
    } catch (const std::bad_alloc&) {
        return ShaderExportProbeSpirvResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return ShaderExportProbeSpirvResult::failure(
            error(
                ShaderExportProbeCompilerErrorCode::
                    host_allocation_failure));
    }
}

}  // namespace astraea::graphics
