#include <astraea/graphics/spirv_raster_probe.hpp>

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <spirv-tools/libspirv.hpp>

namespace {

std::string validate_and_disassemble(
    const std::vector<std::uint32_t>& words) {
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

    const bool valid = tools.Validate(words);
    INFO(diagnostics);
    REQUIRE(valid);

    std::string text;
    const bool disassembled =
        tools.Disassemble(
            words,
            &text);
    INFO(diagnostics);
    REQUIRE(disassembled);
    return text;
}

}  // namespace

TEST_CASE(
    "owned fullscreen raster probe shaders validate for Vulkan 1.3",
    "[graphics][spirv][raster-probe][validation]") {
    const auto modules =
        astraea::graphics::
            build_spirv_raster_probe_modules();

    REQUIRE(modules.vertex_words.size() > 5U);
    REQUIRE(modules.fragment_words.size() > 5U);
    REQUIRE(
        modules.vertex_words.front() ==
        0x07230203U);
    REQUIRE(
        modules.fragment_words.front() ==
        0x07230203U);

    const auto vertex =
        validate_and_disassemble(
            modules.vertex_words);
    const auto fragment =
        validate_and_disassemble(
            modules.fragment_words);

    REQUIRE(
        vertex.find("OpEntryPoint Vertex") !=
        std::string::npos);
    REQUIRE(
        vertex.find("BuiltIn VertexIndex") !=
        std::string::npos);
    REQUIRE(
        vertex.find("BuiltIn Position") !=
        std::string::npos);
    REQUIRE(
        vertex.find("OpShiftLeftLogical") !=
        std::string::npos);

    REQUIRE(
        fragment.find("OpEntryPoint Fragment") !=
        std::string::npos);
    REQUIRE(
        fragment.find("OriginUpperLeft") !=
        std::string::npos);
    REQUIRE(
        fragment.find("Location 0") !=
        std::string::npos);
}
