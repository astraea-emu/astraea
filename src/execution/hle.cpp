#include <astraea/execution/hle.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace astraea::execution {
namespace {

[[nodiscard]] HleSetupError setup_error(
    HleSetupErrorCode code,
    bool has_function_id = false,
    HleFunctionId function_id = {},
    bool has_gate_slot = false,
    std::uint32_t gate_slot = 0,
    bool has_guest_address = false,
    std::uint64_t guest_address = 0) noexcept {
    return HleSetupError{
        .code = code,
        .has_function_id = has_function_id,
        .function_id = function_id,
        .has_gate_slot = has_gate_slot,
        .gate_slot = gate_slot,
        .has_guest_address = has_guest_address,
        .guest_address = guest_address,
    };
}

[[nodiscard]] bool overlaps_image(
    astraea::memory::GuestRange gate_range,
    const astraea::loader::GuestImage& image,
    std::span<const astraea::memory::GuestRange>
        additional_reserved_ranges) noexcept {
    for (const auto& mapping : image.mappings) {
        if (gate_range.overlaps(mapping.range)) {
            return true;
        }
    }

    if (gate_range.overlaps(image.initial_stack.storage)) {
        return true;
    }

    for (const auto& reserved : additional_reserved_ranges) {
        if (gate_range.overlaps(reserved)) {
            return true;
        }
    }

    return false;
}

}  // namespace

HleRegistry::CreateResult HleRegistry::create(
    std::vector<HleFunctionDescriptor> descriptors) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.id.value == 0) {
            return CreateResult::failure(
                setup_error(
                    HleSetupErrorCode::invalid_function_id,
                    true,
                    descriptor.id));
        }

        if (descriptor.canonical_name.empty()) {
            return CreateResult::failure(
                setup_error(
                    HleSetupErrorCode::invalid_function_name,
                    true,
                    descriptor.id));
        }

        if (descriptor.argument_count >
            kMaxSyntheticGateArguments) {
            return CreateResult::failure(
                setup_error(
                    HleSetupErrorCode::invalid_argument_count,
                    true,
                    descriptor.id));
        }
    }

    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        for (std::size_t j = i + 1; j < descriptors.size(); ++j) {
            if (descriptors[i].id == descriptors[j].id) {
                return CreateResult::failure(
                    setup_error(
                        HleSetupErrorCode::duplicate_function_id,
                        true,
                        descriptors[i].id));
            }

            if (descriptors[i].canonical_name ==
                descriptors[j].canonical_name) {
                return CreateResult::failure(
                    setup_error(
                        HleSetupErrorCode::duplicate_function_name,
                        true,
                        descriptors[j].id));
            }
        }
    }

    std::sort(
        descriptors.begin(),
        descriptors.end(),
        [](const HleFunctionDescriptor& lhs,
           const HleFunctionDescriptor& rhs) {
            return lhs.id.value < rhs.id.value;
        });

    return CreateResult::success(
        HleRegistry{std::move(descriptors)});
}

const HleFunctionDescriptor* HleRegistry::find(
    HleFunctionId id) const noexcept {
    for (const auto& descriptor : descriptors_) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }

    return nullptr;
}

std::optional<std::uint32_t> SyntheticGateRegion::recognize_slot(
    astraea::memory::GuestAddress instruction_pointer) const noexcept {
    if (!range_.contains(instruction_pointer)) {
        return std::nullopt;
    }

    const std::uint64_t offset =
        instruction_pointer.value() - range_.base().value();
    if ((offset % kSyntheticGateStride) != 0) {
        return std::nullopt;
    }

    const std::uint64_t slot = offset / kSyntheticGateStride;
    if (slot >= slot_count_) {
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(slot);
}

std::optional<astraea::memory::GuestAddress>
SyntheticGateRegion::slot_address(
    std::uint32_t slot) const noexcept {
    if (slot >= slot_count_) {
        return std::nullopt;
    }

    const auto offset = astraea::memory::GuestSize{
        static_cast<std::uint64_t>(slot) *
        kSyntheticGateStride};
    auto address = astraea::memory::GuestAddress::checked_add(
        range_.base(),
        offset);
    if (!address.has_value()) {
        return std::nullopt;
    }

    return address.value();
}

const GateBinding* SyntheticGateRegion::binding_for_slot(
    std::uint32_t slot) const noexcept {
    for (const auto& binding : bindings_) {
        if (binding.slot == slot) {
            return &binding;
        }
    }

    return nullptr;
}

SyntheticGateRegionResult build_synthetic_gate_region(
    const HleRegistry& registry,
    astraea::memory::GuestAddress base,
    std::uint32_t slot_count,
    std::vector<GateBinding> bindings,
    const astraea::loader::GuestImage& image,
    std::span<const astraea::memory::GuestRange>
        additional_reserved_ranges) {
    if (slot_count == 0) {
        return SyntheticGateRegionResult::failure(
            setup_error(
                HleSetupErrorCode::invalid_gate_slot_count,
                false,
                {},
                false,
                0,
                true,
                base.value()));
    }

    const std::uint64_t byte_count =
        static_cast<std::uint64_t>(slot_count) *
        kSyntheticGateStride;

    auto gate_range = astraea::memory::GuestRange::create(
        base,
        astraea::memory::GuestSize{byte_count});
    if (!gate_range.has_value()) {
        return SyntheticGateRegionResult::failure(
            setup_error(
                HleSetupErrorCode::gate_range_overflow,
                false,
                {},
                false,
                0,
                true,
                base.value()));
    }

    if (overlaps_image(
            gate_range.value(),
            image,
            additional_reserved_ranges)) {
        return SyntheticGateRegionResult::failure(
            setup_error(
                HleSetupErrorCode::gate_region_overlap,
                false,
                {},
                false,
                0,
                true,
                base.value()));
    }

    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];

        if (binding.slot >= slot_count) {
            return SyntheticGateRegionResult::failure(
                setup_error(
                    HleSetupErrorCode::gate_slot_out_of_bounds,
                    true,
                    binding.function_id,
                    true,
                    binding.slot));
        }

        if (registry.find(binding.function_id) == nullptr) {
            return SyntheticGateRegionResult::failure(
                setup_error(
                    HleSetupErrorCode::unknown_function,
                    true,
                    binding.function_id,
                    true,
                    binding.slot));
        }

        for (std::size_t j = i + 1; j < bindings.size(); ++j) {
            if (binding.slot == bindings[j].slot) {
                return SyntheticGateRegionResult::failure(
                    setup_error(
                        HleSetupErrorCode::duplicate_gate_binding,
                        true,
                        bindings[j].function_id,
                        true,
                        binding.slot));
            }
        }
    }

    std::sort(
        bindings.begin(),
        bindings.end(),
        [](const GateBinding& lhs, const GateBinding& rhs) {
            return lhs.slot < rhs.slot;
        });

    if (byte_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        return SyntheticGateRegionResult::failure(
            setup_error(
                HleSetupErrorCode::host_size_unrepresentable,
                false,
                {},
                false,
                0,
                true,
                base.value()));
    }

    try {
        std::vector<std::byte> bytes(
            static_cast<std::size_t>(byte_count),
            kSyntheticGatePaddingByte);

        for (std::uint32_t slot = 0; slot < slot_count; ++slot) {
            const std::size_t offset =
                static_cast<std::size_t>(
                    static_cast<std::uint64_t>(slot) *
                    kSyntheticGateStride);
            bytes[offset] = kSyntheticGateUd2Byte0;
            bytes[offset + 1U] = kSyntheticGateUd2Byte1;
        }

        return SyntheticGateRegionResult::success(
            SyntheticGateRegion{
                gate_range.value(),
                slot_count,
                std::move(bindings),
                std::move(bytes)});
    } catch (const std::bad_alloc&) {
        return SyntheticGateRegionResult::failure(
            setup_error(
                HleSetupErrorCode::host_allocation_failure));
    } catch (const std::length_error&) {
        return SyntheticGateRegionResult::failure(
            setup_error(
                HleSetupErrorCode::host_size_unrepresentable));
    }
}

}  // namespace astraea::execution
