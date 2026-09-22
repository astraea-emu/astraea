#include <astraea/execution/guest_memory.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace astraea::execution {
namespace {

[[nodiscard]] GuestMemoryError memory_error(
    GuestMemoryErrorCode code,
    bool has_guest_address = false,
    std::uint64_t guest_address = 0) noexcept {
    return GuestMemoryError{
        .code = code,
        .has_guest_address = has_guest_address,
        .guest_address = guest_address,
    };
}

[[nodiscard]] bool mapping_allows(
    const astraea::memory::MappingIntent& mapping,
    bool write) noexcept {
    return mapping.permissions.has(
        write
            ? astraea::memory::GuestPermission::write
            : astraea::memory::GuestPermission::read);
}

}  // namespace

GuestMemoryAccess::CopyResult GuestMemoryAccess::validate(
    astraea::memory::GuestAddress address,
    std::size_t byte_count,
    AccessKind access) const noexcept {
    if (image_ == nullptr ||
        prepared_plan_ == nullptr ||
        !prepared_memory_available_) {
        return CopyResult::failure(
            memory_error(
                GuestMemoryErrorCode::
                    prepared_memory_unavailable));
    }

    if (byte_count == 0) {
        return CopyResult::success(0);
    }

    if constexpr (
        sizeof(std::uintptr_t) < sizeof(std::uint64_t)) {
        if (address.value() >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uintptr_t>::max())) {
            return CopyResult::failure(
                memory_error(
                    GuestMemoryErrorCode::
                        host_size_unrepresentable,
                    true,
                    address.value()));
        }
    }

    if constexpr (
        sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (byte_count >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max())) {
            return CopyResult::failure(
                memory_error(
                    GuestMemoryErrorCode::
                        host_size_unrepresentable,
                    true,
                    address.value()));
        }
    }

    const auto requested =
        static_cast<std::uint64_t>(byte_count);
    if (address.value() >
        std::numeric_limits<std::uint64_t>::max() -
            (requested - 1U)) {
        return CopyResult::failure(
            memory_error(
                GuestMemoryErrorCode::
                    guest_memory_range_overflow,
                true,
                address.value()));
    }

    std::uint64_t cursor = address.value();
    std::uint64_t remaining = requested;
    const bool write = access == AccessKind::write;

    while (remaining != 0) {
        std::uint64_t best_coverage = 0;
        bool covered_without_permission = false;

        for (const auto& mapping : image_->mappings) {
            const auto cursor_address =
                astraea::memory::GuestAddress{cursor};
            if (!mapping.range.contains(cursor_address)) {
                continue;
            }

            if (!mapping_allows(mapping, write)) {
                covered_without_permission = true;
                continue;
            }

            const std::uint64_t offset =
                cursor -
                mapping.range.base().value();
            const std::uint64_t available =
                mapping.range.size().value() -
                offset;
            best_coverage =
                std::max(best_coverage, available);
        }

        const auto cursor_address =
            astraea::memory::GuestAddress{cursor};
        if (image_->initial_stack.storage.contains(
                cursor_address)) {
            const std::uint64_t offset =
                cursor -
                image_->initial_stack.storage.base().value();
            const std::uint64_t available =
                image_->initial_stack.storage.size().value() -
                offset;
            best_coverage =
                std::max(best_coverage, available);
        }

        if (best_coverage == 0) {
            return CopyResult::failure(
                memory_error(
                    covered_without_permission
                        ? GuestMemoryErrorCode::
                              guest_memory_permission_denied
                        : GuestMemoryErrorCode::
                              guest_memory_unmapped,
                    true,
                    cursor));
        }

        std::uint64_t prepared_coverage = 0;
        bool prepared_without_permission = false;
        for (const auto& region :
             prepared_plan_->regions) {
            if (!region.range.contains(cursor_address)) {
                continue;
            }

            const bool region_allows =
                region.permissions.has(
                    write
                        ? astraea::memory::
                              GuestPermission::write
                        : astraea::memory::
                              GuestPermission::read);
            if (!region_allows) {
                prepared_without_permission = true;
                continue;
            }

            const std::uint64_t offset =
                cursor -
                region.range.base().value();
            prepared_coverage =
                std::max(
                    prepared_coverage,
                    region.range.size().value() -
                        offset);
        }

        if (prepared_coverage == 0) {
            return CopyResult::failure(
                memory_error(
                    prepared_without_permission
                        ? GuestMemoryErrorCode::
                              guest_memory_permission_denied
                        : GuestMemoryErrorCode::
                              prepared_memory_unavailable,
                    true,
                    cursor));
        }

        const std::uint64_t chunk =
            std::min(
                remaining,
                std::min(
                    best_coverage,
                    prepared_coverage));
        remaining -= chunk;
        if (remaining == 0) {
            break;
        }

        if (cursor >
            std::numeric_limits<std::uint64_t>::max() -
                chunk) {
            return CopyResult::failure(
                memory_error(
                    GuestMemoryErrorCode::
                        guest_memory_range_overflow,
                    true,
                    cursor));
        }
        cursor += chunk;
    }

    return CopyResult::success(byte_count);
}

GuestMemoryAccess::CopyResult GuestMemoryAccess::read(
    astraea::memory::GuestAddress address,
    std::span<std::byte> output) const noexcept {
    auto validated =
        validate(
            address,
            output.size(),
            AccessKind::read);
    if (!validated.has_value()) {
        return CopyResult::failure(
            validated.error());
    }

    if (output.empty()) {
        return CopyResult::success(0);
    }

    const auto* source =
        reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(
                address.value()));
    std::memcpy(
        output.data(),
        source,
        output.size());
    return CopyResult::success(output.size());
}

GuestMemoryAccess::CopyResult GuestMemoryAccess::write(
    astraea::memory::GuestAddress address,
    std::span<const std::byte> input) const noexcept {
    auto validated =
        validate(
            address,
            input.size(),
            AccessKind::write);
    if (!validated.has_value()) {
        return CopyResult::failure(
            validated.error());
    }

    if (input.empty()) {
        return CopyResult::success(0);
    }

    auto* destination =
        reinterpret_cast<void*>(
            static_cast<std::uintptr_t>(
                address.value()));
    std::memcpy(
        destination,
        input.data(),
        input.size());
    return CopyResult::success(input.size());
}

GuestMemoryAccess::CopyResult
GuestMemoryAccess::preflight_write(
    astraea::memory::GuestAddress address,
    std::size_t byte_count) const noexcept {
    return validate(
        address,
        byte_count,
        AccessKind::write);
}

GuestMemoryAccess::StringResult
GuestMemoryAccess::read_c_string(
    astraea::memory::GuestAddress address,
    std::size_t max_bytes) const {
    try {
        std::string result;
        result.reserve(max_bytes);

        auto cursor = address;
        for (std::size_t i = 0; i < max_bytes; ++i) {
            std::byte value{};
            auto copied =
                read(
                    cursor,
                    std::span<std::byte>{&value, 1});
            if (!copied.has_value()) {
                return StringResult::failure(
                    copied.error());
            }

            if (value == std::byte{0}) {
                return StringResult::success(
                    std::move(result));
            }

            result.push_back(
                static_cast<char>(
                    std::to_integer<unsigned char>(
                        value)));

            auto next =
                astraea::memory::GuestAddress::checked_add(
                    cursor,
                    astraea::memory::GuestSize{1});
            if (!next.has_value()) {
                return StringResult::failure(
                    memory_error(
                        GuestMemoryErrorCode::
                            guest_memory_range_overflow,
                        true,
                        cursor.value()));
            }
            cursor = next.value();
        }

        return StringResult::failure(
            memory_error(
                GuestMemoryErrorCode::
                    unterminated_string,
                true,
                address.value()));
    } catch (const std::bad_alloc&) {
        return StringResult::failure(
            memory_error(
                GuestMemoryErrorCode::
                    host_allocation_failure,
                true,
                address.value()));
    } catch (const std::length_error&) {
        return StringResult::failure(
            memory_error(
                GuestMemoryErrorCode::
                    host_size_unrepresentable,
                true,
                address.value()));
    }
}

bool GuestMemoryAccess::is_exact_executable_address(
    astraea::memory::GuestAddress address) const noexcept {
    if (image_ == nullptr ||
        prepared_plan_ == nullptr ||
        !prepared_memory_available_) {
        return false;
    }

    for (const auto& mapping : image_->mappings) {
        if (mapping.permissions.has(
                astraea::memory::GuestPermission::execute) &&
            mapping.range.contains(address)) {
            return true;
        }
    }
    return false;
}

}  // namespace astraea::execution
