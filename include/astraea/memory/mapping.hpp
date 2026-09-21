#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/elf64.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::memory {

enum class GuestPermission : std::uint8_t {
    read = 1U << 0U,
    write = 1U << 1U,
    execute = 1U << 2U,
};

enum class PermissionErrorCode {
    invalid_permission_bits,
};

struct PermissionError {
    PermissionErrorCode code;
};

class GuestPermissions {
public:
    constexpr GuestPermissions() noexcept = default;

    using Result = astraea::core::Result<GuestPermissions, PermissionError>;

    [[nodiscard]] static Result checked_from_bits(std::uint8_t bits) noexcept {
        if ((bits & static_cast<std::uint8_t>(~kKnownMask)) != 0) {
            return Result::failure(PermissionError{PermissionErrorCode::invalid_permission_bits});
        }
        return Result::success(GuestPermissions{bits});
    }

    [[nodiscard]] static constexpr GuestPermissions from_elf_flags(std::uint32_t flags) noexcept {
        std::uint8_t bits = 0;
        if ((flags & 0x4U) != 0) {
            bits |= static_cast<std::uint8_t>(GuestPermission::read);
        }
        if ((flags & 0x2U) != 0) {
            bits |= static_cast<std::uint8_t>(GuestPermission::write);
        }
        if ((flags & 0x1U) != 0) {
            bits |= static_cast<std::uint8_t>(GuestPermission::execute);
        }
        return GuestPermissions{bits};
    }

    [[nodiscard]] constexpr bool has(GuestPermission permission) const noexcept {
        return (bits_ & static_cast<std::uint8_t>(permission)) != 0;
    }

    [[nodiscard]] constexpr std::uint8_t bits() const noexcept {
        return bits_;
    }

    auto operator<=>(const GuestPermissions&) const = default;

private:
    static constexpr std::uint8_t kKnownMask =
        static_cast<std::uint8_t>(GuestPermission::read) |
        static_cast<std::uint8_t>(GuestPermission::write) |
        static_cast<std::uint8_t>(GuestPermission::execute);

    constexpr explicit GuestPermissions(std::uint8_t bits) noexcept : bits_(bits) {}

    std::uint8_t bits_ = 0;
};

enum class MappingBackingKind : std::uint8_t {
    file,
    zero_fill,
    anonymous,
};

struct MappingBacking {
    MappingBackingKind kind;
    std::uint64_t file_offset = 0;
    GuestSize byte_count{0};

    auto operator<=>(const MappingBacking&) const = default;
};

struct MappingIntent {
    GuestRange range;
    GuestPermissions permissions;
    MappingBacking backing;
    std::size_t source_index;

    auto operator<=>(const MappingIntent&) const = default;
};

enum class MappingErrorCode {
    not_load_segment,
    invalid_load_sizes,
    guest_range_overflow,
    address_addition_overflow,
    mapping_overlap_conflict,
};

struct MappingError {
    MappingErrorCode code;
    std::size_t first_source_index;
    std::size_t second_source_index;
};

enum class OverlapClass {
    none,
    identical_compatible,
    different_backing,
    conflicting_initialized_bytes,
    file_vs_zero_fill,
    permission_disagreement,
};

using MappingResult =
    astraea::core::Result<std::vector<MappingIntent>, MappingError>;

[[nodiscard]] MappingResult mapping_intents_from_load(
    const astraea::loader::ProgramHeader& program_header);

[[nodiscard]] OverlapClass classify_overlap(
    const MappingIntent& lhs,
    const MappingIntent& rhs) noexcept;

[[nodiscard]] bool mapping_intent_less(
    const MappingIntent& lhs,
    const MappingIntent& rhs) noexcept;

}  // namespace astraea::memory
