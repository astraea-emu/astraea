#include <astraea/graphics/vulkan_vector_probe_execution.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <astraea/graphics/shader_cfg.hpp>
#include <astraea/graphics/shader_program.hpp>
#include <astraea/graphics/shader_wave_program_execution.hpp>
#include <astraea/graphics/spirv_vector_probe.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::graphics::ShaderVectorState;
using astraea::graphics::ShaderWaveSize;
using astraea::graphics::SpirvVectorProbeModule;
using astraea::graphics::VulkanVectorProbeExecutionErrorCode;
using astraea::graphics::VulkanVectorProbeMemoryCandidate;
using astraea::graphics::VulkanVectorProbeQueueCandidate;

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

[[nodiscard]] bool live_vulkan_required() {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t value_size = 0;
    if (_dupenv_s(
            &value,
            &value_size,
            "ASTRAEA_REQUIRE_VULKAN_PROBE") != 0 ||
        value == nullptr) {
        return false;
    }

    const bool required =
        std::string_view{value} == "1";
    std::free(value);
    return required;
#else
    const auto* value =
        std::getenv("ASTRAEA_REQUIRE_VULKAN_PROBE");
    return value != nullptr &&
           std::string_view{value} == "1";
#endif
}

[[nodiscard]] astraea::graphics::ShaderIrProgram
make_oracle_program() {
    const std::array<std::uint32_t, 3> words{
        make_vop1(1, 2, 257),
        make_vop2(3, 3, 258, 0),
        make_sopp(1, 0),
    };
    auto program =
        astraea::graphics::
            lower_rdna2_stream_to_shader_ir(words);
    REQUIRE(program.has_value());
    return std::move(program).value();
}

[[nodiscard]] SpirvVectorProbeModule
make_oracle_module(
    const astraea::graphics::ShaderIrProgram& program) {
    auto module =
        astraea::graphics::
            lower_shader_ir_to_spirv_vector_probe(
                program,
                astraea::graphics::
                    SpirvVectorProbeOptions{
                        .wave_size =
                            ShaderWaveSize::wave32,
                    });
    REQUIRE(module.has_value());
    return std::move(module).value();
}

[[nodiscard]] std::vector<std::uint32_t>
flatten_probe_state(
    const ShaderVectorState& state,
    const astraea::graphics::SpirvVectorProbeLayout&
        layout) {
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
    "Vulkan vector probe queue selection chooses the first usable compute queue",
    "[graphics][vulkan][vector-probe][selection]") {
    const std::array<VulkanVectorProbeQueueCandidate, 4>
        candidates{
            VulkanVectorProbeQueueCandidate{
                .supports_compute = false,
                .queue_count = 2U,
            },
            VulkanVectorProbeQueueCandidate{
                .supports_compute = true,
                .queue_count = 0U,
            },
            VulkanVectorProbeQueueCandidate{
                .supports_compute = true,
                .queue_count = 1U,
            },
            VulkanVectorProbeQueueCandidate{
                .supports_compute = true,
                .queue_count = 4U,
            },
        };

    const auto selected =
        astraea::graphics::
            select_vulkan_vector_probe_queue_family(
                candidates);
    REQUIRE(selected == std::optional<std::uint32_t>{2U});
}

TEST_CASE(
    "Vulkan vector probe queue selection rejects no compute queue",
    "[graphics][vulkan][vector-probe][selection]") {
    const std::array<VulkanVectorProbeQueueCandidate, 2>
        candidates{
            VulkanVectorProbeQueueCandidate{
                .supports_compute = false,
                .queue_count = 1U,
            },
            VulkanVectorProbeQueueCandidate{
                .supports_compute = true,
                .queue_count = 0U,
            },
        };

    const auto selected =
        astraea::graphics::
            select_vulkan_vector_probe_queue_family(
                candidates);
    REQUIRE_FALSE(selected.has_value());
}

TEST_CASE(
    "Vulkan vector probe memory selection prefers coherent host-visible memory",
    "[graphics][vulkan][vector-probe][memory]") {
    const std::array<VulkanVectorProbeMemoryCandidate, 4>
        candidates{
            VulkanVectorProbeMemoryCandidate{
                .allowed = true,
                .host_visible = false,
                .host_coherent = false,
            },
            VulkanVectorProbeMemoryCandidate{
                .allowed = true,
                .host_visible = true,
                .host_coherent = false,
            },
            VulkanVectorProbeMemoryCandidate{
                .allowed = false,
                .host_visible = true,
                .host_coherent = true,
            },
            VulkanVectorProbeMemoryCandidate{
                .allowed = true,
                .host_visible = true,
                .host_coherent = true,
            },
        };

    const auto selected =
        astraea::graphics::
            select_vulkan_vector_probe_memory_type(
                candidates);
    REQUIRE(selected == std::optional<std::uint32_t>{3U});
}

TEST_CASE(
    "Vulkan vector probe memory selection falls back to noncoherent host-visible memory",
    "[graphics][vulkan][vector-probe][memory]") {
    const std::array<VulkanVectorProbeMemoryCandidate, 3>
        candidates{
            VulkanVectorProbeMemoryCandidate{
                .allowed = false,
                .host_visible = true,
                .host_coherent = true,
            },
            VulkanVectorProbeMemoryCandidate{
                .allowed = true,
                .host_visible = true,
                .host_coherent = false,
            },
            VulkanVectorProbeMemoryCandidate{
                .allowed = true,
                .host_visible = false,
                .host_coherent = false,
            },
        };

    const auto selected =
        astraea::graphics::
            select_vulkan_vector_probe_memory_type(
                candidates);
    REQUIRE(selected == std::optional<std::uint32_t>{1U});
}

TEST_CASE(
    "Vulkan vector probe rejects invalid layout before loading Vulkan",
    "[graphics][vulkan][vector-probe][validation]") {
    SpirvVectorProbeModule module{};
    module.words = {0x07230203U};

    const std::array<std::uint32_t, 1> words{0U};
    const auto result =
        astraea::graphics::
            execute_spirv_vector_probe_on_vulkan(
                module,
                words);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        VulkanVectorProbeExecutionErrorCode::
            invalid_probe_layout);
}

TEST_CASE(
    "Vulkan vector probe rejects input extent before loading Vulkan",
    "[graphics][vulkan][vector-probe][validation]") {
    auto program = make_oracle_program();
    auto module = make_oracle_module(program);
    const std::array<std::uint32_t, 1> words{0U};

    const auto result =
        astraea::graphics::
            execute_spirv_vector_probe_on_vulkan(
                module,
                words);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        result.error().code ==
        VulkanVectorProbeExecutionErrorCode::
            input_word_count_mismatch);
}

TEST_CASE(
    "Vulkan vector probe matches Astraea interpreter bit for bit",
    "[graphics][vulkan][vector-probe][live][oracle]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    auto program = make_oracle_program();
    auto module = make_oracle_module(program);

    auto graph =
        astraea::graphics::
            build_shader_control_flow_graph(program);
    REQUIRE(graph.has_value());

    astraea::graphics::ShaderScalarState scalar_state{};
    scalar_state.exec = 0xffffffffULL;

    ShaderVectorState vector_state{};
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

    const auto interpreter =
        astraea::graphics::
            execute_shader_wave_program(
                program,
                graph.value(),
                0U,
                1U,
                scalar_state,
                vector_state);
    REQUIRE(interpreter.has_value());

    const auto expected =
        flatten_probe_state(
            vector_state,
            module.layout);

    const auto executed =
        astraea::graphics::
            execute_spirv_vector_probe_on_vulkan(
                module,
                initial);
    REQUIRE(executed.has_value());
    REQUIRE(executed->final_words == expected);
    REQUIRE_FALSE(executed->device.name.empty());

    for (std::size_t lane = 0;
         lane < 32U;
         ++lane) {
        REQUIRE(
            executed->final_words[
                2U * 32U + lane] ==
            0x3f800000U);
        REQUIRE(
            executed->final_words[
                3U * 32U + lane] ==
            0x40000000U);
    }
}

TEST_CASE(
    "Vulkan vector probe rejects malformed SPIR-V without fake success",
    "[graphics][vulkan][vector-probe][live][negative]") {
    if (!live_vulkan_required()) {
        SKIP(
            "live Vulkan proof is mandatory only when "
            "ASTRAEA_REQUIRE_VULKAN_PROBE=1");
    }

    auto program = make_oracle_program();
    auto module = make_oracle_module(program);
    module.words = {0U, 0U, 0U, 0U, 0U};

    std::vector<std::uint32_t> initial(
        module.layout.required_state_word_count,
        0U);
    const auto result =
        astraea::graphics::
            execute_spirv_vector_probe_on_vulkan(
                module,
                initial);

    REQUIRE_FALSE(result.has_value());
    REQUIRE(
        (
            result.error().code ==
                VulkanVectorProbeExecutionErrorCode::
                    shader_module_creation_failure ||
            result.error().code ==
                VulkanVectorProbeExecutionErrorCode::
                    compute_pipeline_creation_failure
        ));
}
