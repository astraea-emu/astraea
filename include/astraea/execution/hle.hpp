#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

inline constexpr std::uint64_t kSyntheticGateStride = 16;
inline constexpr std::uint8_t kMaxSyntheticGateArguments = 6;
inline constexpr std::byte kSyntheticGateUd2Byte0{0x0f};
inline constexpr std::byte kSyntheticGateUd2Byte1{0x0b};
inline constexpr std::byte kSyntheticGatePaddingByte{0xcc};

struct HleFunctionId {
    std::uint32_t value = 0;

    auto operator<=>(const HleFunctionId&) const = default;
};

// Captured SysV x86-64 HLE call state. This lives at the generic HLE layer so
// concrete services can consume guest arguments without depending on the
// runtime dispatch implementation.
struct HleCall {
    HleFunctionId function_id;
    std::uint32_t gate_slot = 0;
    std::uint64_t guest_rip = 0;
    std::uint64_t guest_rsp = 0;
    std::array<std::uint64_t, 6> arguments{};

    auto operator<=>(const HleCall&) const = default;
};

struct HleFunctionDescriptor {
    HleFunctionId id;
    std::string canonical_name;
    std::uint8_t argument_count = 0;

    auto operator<=>(const HleFunctionDescriptor&) const = default;
};

enum class HleSetupErrorCode {
    invalid_function_id,
    invalid_function_name,
    invalid_argument_count,
    duplicate_function_id,
    duplicate_function_name,
    invalid_gate_slot_count,
    gate_slot_out_of_bounds,
    duplicate_gate_binding,
    unknown_function,
    gate_range_overflow,
    gate_region_overlap,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct HleSetupError {
    HleSetupErrorCode code = HleSetupErrorCode::host_allocation_failure;
    bool has_function_id = false;
    HleFunctionId function_id;
    bool has_gate_slot = false;
    std::uint32_t gate_slot = 0;
    bool has_guest_address = false;
    std::uint64_t guest_address = 0;

    auto operator<=>(const HleSetupError&) const = default;
};

class HleRegistry {
public:
    HleRegistry(const HleRegistry&) = default;
    HleRegistry& operator=(const HleRegistry&) = default;
    HleRegistry(HleRegistry&&) noexcept = default;
    HleRegistry& operator=(HleRegistry&&) noexcept = default;

    using CreateResult =
        astraea::core::Result<HleRegistry, HleSetupError>;

    [[nodiscard]] static CreateResult create(
        std::vector<HleFunctionDescriptor> descriptors);

    [[nodiscard]] const HleFunctionDescriptor* find(
        HleFunctionId id) const noexcept;

    [[nodiscard]] std::span<const HleFunctionDescriptor>
    descriptors() const noexcept {
        return descriptors_;
    }

private:
    explicit HleRegistry(
        std::vector<HleFunctionDescriptor> descriptors)
        : descriptors_(std::move(descriptors)) {}

    std::vector<HleFunctionDescriptor> descriptors_;
};

struct GateBinding {
    std::uint32_t slot = 0;
    HleFunctionId function_id;

    auto operator<=>(const GateBinding&) const = default;
};

class SyntheticGateRegion {
public:
    SyntheticGateRegion(const SyntheticGateRegion&) = default;
    SyntheticGateRegion& operator=(const SyntheticGateRegion&) = default;
    SyntheticGateRegion(SyntheticGateRegion&&) noexcept = default;
    SyntheticGateRegion& operator=(SyntheticGateRegion&&) noexcept = default;

    [[nodiscard]] astraea::memory::GuestRange range() const noexcept {
        return range_;
    }

    [[nodiscard]] std::uint32_t slot_count() const noexcept {
        return slot_count_;
    }

    [[nodiscard]] std::span<const GateBinding> bindings() const noexcept {
        return bindings_;
    }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] std::optional<std::uint32_t> recognize_slot(
        astraea::memory::GuestAddress instruction_pointer) const noexcept;

    [[nodiscard]] std::optional<astraea::memory::GuestAddress> slot_address(
        std::uint32_t slot) const noexcept;

    [[nodiscard]] const GateBinding* binding_for_slot(
        std::uint32_t slot) const noexcept;

private:
    friend astraea::core::Result<SyntheticGateRegion, HleSetupError>
    build_synthetic_gate_region(
        const HleRegistry& registry,
        astraea::memory::GuestAddress base,
        std::uint32_t slot_count,
        std::vector<GateBinding> bindings,
        const astraea::loader::GuestImage& image,
        std::span<const astraea::memory::GuestRange>
            additional_reserved_ranges);

    SyntheticGateRegion(
        astraea::memory::GuestRange range,
        std::uint32_t slot_count,
        std::vector<GateBinding> bindings,
        std::vector<std::byte> bytes)
        : range_(range),
          slot_count_(slot_count),
          bindings_(std::move(bindings)),
          bytes_(std::move(bytes)) {}

    astraea::memory::GuestRange range_;
    std::uint32_t slot_count_ = 0;
    std::vector<GateBinding> bindings_;
    std::vector<std::byte> bytes_;
};

using SyntheticGateRegionResult =
    astraea::core::Result<SyntheticGateRegion, HleSetupError>;

[[nodiscard]] SyntheticGateRegionResult build_synthetic_gate_region(
    const HleRegistry& registry,
    astraea::memory::GuestAddress base,
    std::uint32_t slot_count,
    std::vector<GateBinding> bindings,
    const astraea::loader::GuestImage& image,
    std::span<const astraea::memory::GuestRange>
        additional_reserved_ranges = {});

}  // namespace astraea::execution
