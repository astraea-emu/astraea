#pragma once

#include <compare>
#include <cstdint>
#include <vector>

namespace astraea::graphics {

struct SpirvRasterProbeModules {
    std::vector<std::uint32_t> vertex_words;
    std::vector<std::uint32_t> fragment_words;

    auto operator<=>(const SpirvRasterProbeModules&) const = default;
};

// Builds a deterministic owned Vulkan-1.3 shader pair:
//
// - vertex: fullscreen triangle derived only from gl_VertexIndex;
// - fragment: constant opaque magenta at location 0.
//
// These modules are a backend oracle, not guest/AGC shader semantics.
[[nodiscard]] SpirvRasterProbeModules
build_spirv_raster_probe_modules();

}  // namespace astraea::graphics
