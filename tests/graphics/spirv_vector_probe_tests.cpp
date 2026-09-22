#include <astraea/graphics/spirv_vector_probe.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <astraea/graphics/shader_cfg.hpp>
#include <astraea/graphics/shader_wave_program_execution.hpp>

#include <catch2/catch_test_macros.hpp>
#include <spirv-tools/libspirv.hpp>

namespace {

using astraea::graphics::ShaderIrConditionalRelativeBranch;
using astraea::graphics::ShaderIrEndProgram;
using astraea::graphics::ShaderIrEmission;
using astraea::graphics::ShaderIrNop;
using astraea::graphics::ShaderIrOperation;
using astraea::graphics::ShaderIrProgram;
using astraea::graphics::ShaderIrRelativeBranch;
using astraea::graphics::ShaderIrScalarMove32;
using astraea::graphics::ShaderIrSgpr;
using astraea::graphics::ShaderIrUnsupported;
using astraea::graphics::ShaderIrUnsupportedReason;
using astraea::graphics::ShaderIrVectorAddF32;
using astraea::graphics::ShaderIrVectorMove32;
using astraea::graphics::ShaderIrVgpr;
using astraea::graphics::ShaderIrWaitCount;
using astraea::graphics::ShaderIrWorkgroupBarrier;
using astraea::graphics::ShaderWaveSize;
using astraea::graphics::SpirvVectorProbeErrorCode;
using astraea::graphics::SpirvVectorProbeModule;
using astraea::graphics::SpirvVectorProbeOptions;

ShaderIrEmission emission(ShaderIrOperation operation) {
    return ShaderIrEmission{
        .operation = std::move(operation),
        .provenance =
            {
                .source_instruction = {},
            },
    };
}

ShaderIrProgram make_program(
    std::initializer_list<ShaderIrOperation> operations) {
    ShaderIrProgram program;
    program.source_word_count = operations.size();
    program.emissions.reserve(operations.size());
    for (const auto& operation : operations) {
        program.emissions.push_back(
            emission(operation));
    }
    return program;
}

SpirvVectorProbeModule require_module(
    const ShaderIrProgram& program,
    ShaderWaveSize wave_size) {
    auto result =
        astraea::graphics::
            lower_shader_ir_to_spirv_vector_probe(
                program,
                SpirvVectorProbeOptions{
                    .wave_size = wave_size,
                });
    REQUIRE(result.has_value());
    return std::move(result).value();
}

std::string validate_and_disassemble(
    const SpirvVectorProbeModule& module) {
    spvtools::SpirvTools tools{
        SPV_ENV_VULKAN_1_3};
    REQUIRE(tools.IsValid());

    std::string diagnostics;
    tools.SetMessageConsumer(
        [&diagnostics](
            spv_message_level_t,
            const char*,
            const spv_position_t&,
            const char* message) {
            if (message != nullptr) {
                diagnostics += message;
                diagnostics.push_back('\n');
            }
        });

    const bool valid =
        tools.Validate(module.words);
    INFO(diagnostics);
    REQUIRE(valid);

    std::string text;
    const bool disassembled =
        tools.Disassemble(
            module.words,
            &text);
    INFO(diagnostics);
    REQUIRE(disassembled);
    return text;
}

std::size_t count_substring(
    std::string_view text,
    std::string_view needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while (true) {
        position = text.find(needle, position);
        if (position == std::string_view::npos) {
            return count;
        }
        ++count;
        position += needle.size();
    }
}

constexpr std::uint32_t kSoppBase = 0xbf800000U;
constexpr std::uint32_t kVop1Base = 0x7e000000U;

constexpr std::uint32_t make_sopp(
    std::uint8_t opcode,
    std::uint16_t simm16) {
    return kSoppBase |
           (static_cast<std::uint32_t>(opcode) << 16U) |
           static_cast<std::uint32_t>(simm16);
}

constexpr std::uint32_t make_vop1(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source) {
    return kVop1Base |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(opcode) << 9U) |
           static_cast<std::uint32_t>(source);
}

constexpr std::uint32_t make_vop2(
    std::uint8_t opcode,
    std::uint8_t destination,
    std::uint16_t source0,
    std::uint8_t source1) {
    return (static_cast<std::uint32_t>(opcode) << 25U) |
           (static_cast<std::uint32_t>(destination) << 17U) |
           (static_cast<std::uint32_t>(source1) << 9U) |
           static_cast<std::uint32_t>(source0);
}

std::vector<std::uint32_t> flatten_probe_state(
    const astraea::graphics::ShaderVectorState& state,
    const astraea::graphics::SpirvVectorProbeLayout& layout) {
    std::vector<std::uint32_t> words(
        layout.required_state_word_count,
        0U);

    for (std::uint32_t vgpr = 0;
         vgpr < layout.required_vgpr_count;
         ++vgpr) {
        for (std::uint32_t lane = 0;
             lane < layout.wave_size;
             ++lane) {
            const auto index =
                vgpr * layout.vgpr_stride_words +
                lane;
            words[index] =
                state.vgprs[vgpr][lane];
        }
    }
    return words;
}

}  // namespace

TEST_CASE(
    "minimal NOP END SPIR-V vector probe validates for Vulkan 1.3",
    "[graphics][spirv][vector-probe][validation]") {
    const auto program =
        make_program({
            ShaderIrNop{
                .repeat_count = 2,
            },
            ShaderIrEndProgram{},
        });

    const auto module =
        require_module(
            program,
            ShaderWaveSize::wave32);
    REQUIRE(module.words.size() > 5);
    REQUIRE(module.words[0] == 0x07230203U);
    REQUIRE(module.words[1] == 0x00010600U);
    REQUIRE(module.layout.descriptor_set == 0U);
    REQUIRE(module.layout.binding == 0U);
    REQUIRE(module.layout.word_size_bytes == 4U);
    REQUIRE(module.layout.wave_size == 32U);
    REQUIRE(module.layout.vgpr_stride_words == 32U);
    REQUIRE(module.layout.required_vgpr_count == 0U);
    REQUIRE(
        module.layout.required_state_word_count ==
        0U);

    const auto text =
        validate_and_disassemble(module);
    REQUIRE(
        text.find("OpCapability Shader") !=
        std::string::npos);
    REQUIRE(
        text.find("LocalSize 32 1 1") !=
        std::string::npos);
    REQUIRE(count_substring(text, "OpNop") == 2U);
    REQUIRE(
        text.find("OpReturn") !=
        std::string::npos);
}

TEST_CASE(
    "V_MOV_B32 lowers to validated uint load and store",
    "[graphics][spirv][vector-probe][move]") {
    const auto program =
        make_program({
            ShaderIrVectorMove32{
                .destination =
                    ShaderIrVgpr{
                        .index = 2,
                    },
                .source =
                    ShaderIrVgpr{
                        .index = 1,
                    },
            },
            ShaderIrEndProgram{},
        });

    const auto module =
        require_module(
            program,
            ShaderWaveSize::wave32);
    REQUIRE(module.layout.required_vgpr_count == 3U);
    REQUIRE(
        module.layout.required_state_word_count ==
        96U);

    const auto text =
        validate_and_disassemble(module);
    REQUIRE(
        text.find("OpAccessChain") !=
        std::string::npos);
    REQUIRE(
        text.find("OpLoad") !=
        std::string::npos);
    REQUIRE(
        text.find("OpStore") !=
        std::string::npos);
    REQUIRE(
        text.find("DescriptorSet 0") !=
        std::string::npos);
    REQUIRE(
        text.find("Binding 0") !=
        std::string::npos);
    REQUIRE(
        text.find("LocalInvocationIndex") !=
        std::string::npos);
}

TEST_CASE(
    "V_ADD_F32 lowers to validated bitcast FAdd bitcast store",
    "[graphics][spirv][vector-probe][add-f32]") {
    const auto program =
        make_program({
            ShaderIrVectorAddF32{
                .destination =
                    ShaderIrVgpr{
                        .index = 4,
                    },
                .source0 =
                    ShaderIrVgpr{
                        .index = 2,
                    },
                .source1 =
                    ShaderIrVgpr{
                        .index = 3,
                    },
            },
            ShaderIrEndProgram{},
        });

    const auto module =
        require_module(
            program,
            ShaderWaveSize::wave64);
    REQUIRE(module.layout.wave_size == 64U);
    REQUIRE(module.layout.required_vgpr_count == 5U);
    REQUIRE(
        module.layout.required_state_word_count ==
        320U);

    const auto text =
        validate_and_disassemble(module);
    REQUIRE(
        count_substring(text, "OpBitcast") >= 3U);
    REQUIRE(
        text.find("OpFAdd") !=
        std::string::npos);
    REQUIRE(
        text.find("OpStore") !=
        std::string::npos);
}

TEST_CASE(
    "mixed vector SPIR-V emission is deterministic",
    "[graphics][spirv][vector-probe][determinism]") {
    const auto program =
        make_program({
            ShaderIrVectorMove32{
                .destination = ShaderIrVgpr{2},
                .source = ShaderIrVgpr{1},
            },
            ShaderIrVectorAddF32{
                .destination = ShaderIrVgpr{3},
                .source0 = ShaderIrVgpr{2},
                .source1 = ShaderIrVgpr{0},
            },
            ShaderIrEndProgram{},
        });

    const auto first =
        require_module(
            program,
            ShaderWaveSize::wave32);
    const auto second =
        require_module(
            program,
            ShaderWaveSize::wave32);

    REQUIRE(first == second);
    validate_and_disassemble(first);
}

TEST_CASE(
    "wave size controls LocalSize and flattened VGPR stride",
    "[graphics][spirv][vector-probe][wave-size]") {
    const auto program =
        make_program({
            ShaderIrVectorMove32{
                .destination = ShaderIrVgpr{7},
                .source = ShaderIrVgpr{5},
            },
            ShaderIrEndProgram{},
        });

    SECTION("wave32") {
        const auto module =
            require_module(
                program,
                ShaderWaveSize::wave32);
        REQUIRE(module.layout.wave_size == 32U);
        REQUIRE(
            module.layout.vgpr_stride_words ==
            32U);
        REQUIRE(
            module.layout.required_vgpr_count ==
            8U);
        REQUIRE(
            module.layout.required_state_word_count ==
            256U);
        const auto text =
            validate_and_disassemble(module);
        REQUIRE(
            text.find("LocalSize 32 1 1") !=
            std::string::npos);
    }

    SECTION("wave64") {
        const auto module =
            require_module(
                program,
                ShaderWaveSize::wave64);
        REQUIRE(module.layout.wave_size == 64U);
        REQUIRE(
            module.layout.vgpr_stride_words ==
            64U);
        REQUIRE(
            module.layout.required_vgpr_count ==
            8U);
        REQUIRE(
            module.layout.required_state_word_count ==
            512U);
        const auto text =
            validate_and_disassemble(module);
        REQUIRE(
            text.find("LocalSize 64 1 1") !=
            std::string::npos);
    }
}

TEST_CASE(
    "SPIR-V vector probe rejects unsupported Shader IR explicitly",
    "[graphics][spirv][vector-probe][unsupported]") {
    const auto require_unsupported =
        [](ShaderIrOperation operation) {
            const auto program =
                make_program({
                    std::move(operation),
                    ShaderIrEndProgram{},
                });
            const auto result =
                astraea::graphics::
                    lower_shader_ir_to_spirv_vector_probe(
                        program,
                        SpirvVectorProbeOptions{
                            .wave_size =
                                ShaderWaveSize::wave32,
                        });
            REQUIRE_FALSE(result.has_value());
            REQUIRE(
                result.error().code ==
                SpirvVectorProbeErrorCode::
                    unsupported_operation);
            REQUIRE(result.error().emission_index == 0U);
        };

    SECTION("scalar") {
        require_unsupported(
            ShaderIrScalarMove32{
                .destination = ShaderIrSgpr{0},
                .source = ShaderIrSgpr{1},
            });
    }

    SECTION("branch") {
        require_unsupported(
            ShaderIrRelativeBranch{
                .byte_delta = 4,
            });
    }

    SECTION("conditional branch") {
        require_unsupported(
            ShaderIrConditionalRelativeBranch{});
    }

    SECTION("wait") {
        require_unsupported(
            ShaderIrWaitCount{
                .vmcnt = 0,
                .expcnt = 0,
                .lgkmcnt = 0,
            });
    }

    SECTION("barrier") {
        require_unsupported(
            ShaderIrWorkgroupBarrier{});
    }
}

TEST_CASE(
    "SPIR-V vector probe preserves ShaderIrUnsupported reason",
    "[graphics][spirv][vector-probe][unsupported][provenance]") {
    const auto program =
        make_program({
            ShaderIrUnsupported{
                .reason =
                    ShaderIrUnsupportedReason::
                        unsupported_vector_operand,
            },
            ShaderIrEndProgram{},
        });

    const auto result =
        astraea::graphics::
            lower_shader_ir_to_spirv_vector_probe(
                program,
                SpirvVectorProbeOptions{
                    .wave_size =
                        ShaderWaveSize::wave32,
                });

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        SpirvVectorProbeErrorCode::
            unsupported_operation);
    REQUIRE(result.error().emission_index == 0U);
    REQUIRE(
        result.error().unsupported_reason ==
        std::optional<ShaderIrUnsupportedReason>{
            ShaderIrUnsupportedReason::
                unsupported_vector_operand});
}

TEST_CASE(
    "SPIR-V vector probe validates terminal END structure",
    "[graphics][spirv][vector-probe][structure]") {
    SECTION("missing END") {
        const auto program =
            make_program({
                ShaderIrNop{},
            });
        const auto result =
            astraea::graphics::
                lower_shader_ir_to_spirv_vector_probe(
                    program,
                    SpirvVectorProbeOptions{
                        .wave_size =
                            ShaderWaveSize::wave32,
                    });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SpirvVectorProbeErrorCode::
                missing_end_program);
    }

    SECTION("duplicate END") {
        const auto program =
            make_program({
                ShaderIrEndProgram{},
                ShaderIrEndProgram{},
            });
        const auto result =
            astraea::graphics::
                lower_shader_ir_to_spirv_vector_probe(
                    program,
                    SpirvVectorProbeOptions{
                        .wave_size =
                            ShaderWaveSize::wave32,
                    });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SpirvVectorProbeErrorCode::
                duplicate_end_program);
        REQUIRE(result.error().emission_index == 1U);
    }

    SECTION("operation after END") {
        const auto program =
            make_program({
                ShaderIrEndProgram{},
                ShaderIrNop{},
            });
        const auto result =
            astraea::graphics::
                lower_shader_ir_to_spirv_vector_probe(
                    program,
                    SpirvVectorProbeOptions{
                        .wave_size =
                            ShaderWaveSize::wave32,
                    });
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SpirvVectorProbeErrorCode::
                operation_after_end_program);
        REQUIRE(result.error().emission_index == 1U);
    }

    SECTION("invalid wave") {
        const auto program =
            make_program({
                ShaderIrEndProgram{},
            });
        const auto result =
            astraea::graphics::
                lower_shader_ir_to_spirv_vector_probe(
                    program,
                    SpirvVectorProbeOptions{});
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            SpirvVectorProbeErrorCode::
                invalid_wave_size);
    }
}

TEST_CASE(
    "V2 probe layout records the exact interpreter oracle for V3",
    "[graphics][spirv][vector-probe][oracle]") {
    const std::array<std::uint32_t, 3> words{
        make_vop1(1, 2, 257),
        make_vop2(3, 3, 258, 0),
        make_sopp(1, 0),
    };
    const auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());

    const auto module =
        require_module(
            program.value(),
            ShaderWaveSize::wave32);
    validate_and_disassemble(module);

    auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(
                program.value());
    REQUIRE(graph.has_value());

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0xffffffffULL;

    astraea::graphics::ShaderVectorState vector_state{};
    vector_state.wave_size = ShaderWaveSize::wave32;
    for (std::size_t lane = 0;
         lane < 32U;
         ++lane) {
        vector_state.vgprs[0][lane] = 0x3f800000U;
        vector_state.vgprs[1][lane] = 0x3f800000U;
    }

    const auto initial =
        flatten_probe_state(
            vector_state,
            module.layout);
    REQUIRE(initial.size() == 128U);
    REQUIRE(initial[0] == 0x3f800000U);
    REQUIRE(initial[32] == 0x3f800000U);
    REQUIRE(initial[64] == 0U);
    REQUIRE(initial[96] == 0U);

    const auto executed =
        astraea::graphics::
            execute_shader_wave_program(
                program.value(),
                graph.value(),
                0,
                1,
                scalar_state,
                vector_state);
    REQUIRE(executed.has_value());

    const auto expected =
        flatten_probe_state(
            vector_state,
            module.layout);
    REQUIRE(expected.size() == initial.size());

    for (std::size_t lane = 0;
         lane < 32U;
         ++lane) {
        REQUIRE(
            expected[0U * 32U + lane] ==
            0x3f800000U);
        REQUIRE(
            expected[1U * 32U + lane] ==
            0x3f800000U);
        REQUIRE(
            expected[2U * 32U + lane] ==
            0x3f800000U);
        REQUIRE(
            expected[3U * 32U + lane] ==
            0x40000000U);
    }
}
