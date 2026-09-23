#pragma once

#include <compare>
#include <cstdint>

namespace astraea::graphics {

// Backend-independent guest GPU virtual address.
//
// This is neither a CPU guest pointer nor a host/Vulkan address. Mapping and
// resource validity belong to the guest GPU address-space layer, not to this
// value type.
struct GpuVirtualAddress {
    std::uint64_t value = 0;

    auto operator<=>(const GpuVirtualAddress&) const = default;
};

}  // namespace astraea::graphics
