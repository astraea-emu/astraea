#include <astraea/graphics/agc_shader_binary.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace astraea::graphics {
namespace {

constexpr std::uint32_t kAgcHeaderMagic = 0x34333231U;
constexpr std::size_t kAgcHeaderMinimumSize = 96;
constexpr std::size_t kAgcContextRegistersOffset = 0x18;
constexpr std::size_t kAgcShaderRegistersOffset = 0x20;
constexpr std::size_t kAgcHeaderSizeOffset = 0x40;
constexpr std::size_t kAgcShaderTextSizeOffset = 0x44;
constexpr std::size_t kAgcProgramTypeOffset = 0x5a;
constexpr std::size_t kAgcContextRegisterCountOffset = 0x5b;
constexpr std::size_t kAgcShaderRegisterCountOffset = 0x5c;
constexpr std::size_t kAgcRegisterRecordSize = 8;

constexpr std::size_t kAgcShaderTextTrailerSize = 0x30;
constexpr std::size_t kAgcTrailerProgramLengthOffset = 0x14;
constexpr std::size_t kAgcTrailerSl00LengthOffset = 0x1c;

template <typename T>
[[nodiscard]] T read_little_endian(
    std::span<const std::byte> bytes,
    std::size_t offset) {
    static_assert(std::is_unsigned_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const auto byte =
            static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(
                    bytes[offset + i]));
        value |= byte << (i * 8U);
    }
    return static_cast<T>(value);
}

[[nodiscard]] bool checked_add(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left >
        std::numeric_limits<std::uint64_t>::max() -
            right) {
        return false;
    }
    result = left + right;
    return true;
}

[[nodiscard]] bool checked_multiply(
    std::uint64_t left,
    std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0 &&
        right >
            std::numeric_limits<std::uint64_t>::max() /
                left) {
        return false;
    }
    result = left * right;
    return true;
}

[[nodiscard]] AgcShaderBinaryError error(
    AgcShaderBinaryErrorCode code,
    AgcShaderBinaryRegion region,
    std::uint64_t byte_offset,
    std::optional<AgcRegisterListKind> register_list_kind =
        std::nullopt) noexcept {
    return AgcShaderBinaryError{
        .code = code,
        .region = region,
        .byte_offset = byte_offset,
        .register_list_kind = register_list_kind,
    };
}

[[nodiscard]] AgcShaderProgramType program_type(
    std::uint8_t raw) noexcept {
    using Stage = AgcShaderStage;

    std::optional<Stage> known;
    switch (raw) {
    case 0:
        known = Stage::compute;
        break;
    case 1:
        known = Stage::pixel;
        break;
    case 2:
        known = Stage::geometry;
        break;
    case 3:
        known = Stage::hull;
        break;
    case 4:
        known = Stage::geometry_front;
        break;
    case 5:
        known = Stage::hull_front;
        break;
    case 6:
        known = Stage::geometry_back;
        break;
    case 7:
        known = Stage::hull_back;
        break;
    case 8:
        known = Stage::function;
        break;
    default:
        break;
    }

    return AgcShaderProgramType{
        .raw = raw,
        .known = known,
    };
}

using RegisterListResult =
    astraea::core::Result<
        std::vector<AgcRegisterWrite>,
        AgcShaderBinaryError>;

[[nodiscard]] RegisterListResult parse_register_list(
    std::span<const std::byte> header,
    std::size_t offset_field,
    std::size_t count_field,
    AgcRegisterListKind kind) {
    const auto count =
        std::to_integer<std::uint8_t>(
            header[count_field]);

    if (count == 0) {
        return RegisterListResult::success({});
    }

    const auto raw_relative_offset =
        read_little_endian<std::uint64_t>(
            header,
            offset_field);

    std::uint64_t list_offset = 0;
    if (!checked_add(
            static_cast<std::uint64_t>(offset_field),
            raw_relative_offset,
            list_offset)) {
        return RegisterListResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    register_table_extent_overflow,
                AgcShaderBinaryRegion::shader_header,
                offset_field,
                kind));
    }

    std::uint64_t list_size = 0;
    if (!checked_multiply(
            static_cast<std::uint64_t>(count),
            kAgcRegisterRecordSize,
            list_size)) {
        return RegisterListResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    register_table_extent_overflow,
                AgcShaderBinaryRegion::shader_header,
                offset_field,
                kind));
    }

    std::uint64_t list_end = 0;
    if (!checked_add(
            list_offset,
            list_size,
            list_end)) {
        return RegisterListResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    register_table_extent_overflow,
                AgcShaderBinaryRegion::shader_header,
                offset_field,
                kind));
    }

    if (list_offset >
            static_cast<std::uint64_t>(
                header.size()) ||
        list_end >
            static_cast<std::uint64_t>(
                header.size())) {
        return RegisterListResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    register_table_extent_out_of_bounds,
                AgcShaderBinaryRegion::shader_header,
                offset_field,
                kind));
    }

    try {
        std::vector<AgcRegisterWrite> writes;
        writes.reserve(count);

        for (std::size_t index = 0;
             index < count;
             ++index) {
            const auto record_offset =
                static_cast<std::size_t>(
                    list_offset +
                    static_cast<std::uint64_t>(index) *
                        kAgcRegisterRecordSize);

            writes.push_back(
                AgcRegisterWrite{
                    .register_offset =
                        read_little_endian<std::uint16_t>(
                            header,
                            record_offset),
                    .value =
                        read_little_endian<std::uint32_t>(
                            header,
                            record_offset + 4),
                });
        }

        return RegisterListResult::success(
            std::move(writes));
    } catch (const std::bad_alloc&) {
        return RegisterListResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    host_allocation_failure,
                AgcShaderBinaryRegion::none,
                0,
                kind));
    } catch (const std::length_error&) {
        return RegisterListResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    host_allocation_failure,
                AgcShaderBinaryRegion::none,
                0,
                kind));
    }
}

}  // namespace

AgcShaderBinaryResult
parse_agc_shader_binary(
    std::span<const std::byte> shader_header,
    std::span<const std::byte> shader_text) {
    if (shader_header.size() <
        kAgcHeaderMinimumSize) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    shader_header_too_small,
                AgcShaderBinaryRegion::shader_header,
                shader_header.size()));
    }

    const auto header_magic =
        read_little_endian<std::uint32_t>(
            shader_header,
            0);
    if (header_magic != kAgcHeaderMagic) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    bad_shader_header_magic,
                AgcShaderBinaryRegion::shader_header,
                0));
    }

    const auto header_version =
        read_little_endian<std::uint32_t>(
            shader_header,
            4);
    const auto declared_header_size =
        read_little_endian<std::uint32_t>(
            shader_header,
            kAgcHeaderSizeOffset);
    const auto declared_shader_text_size =
        read_little_endian<std::uint32_t>(
            shader_header,
            kAgcShaderTextSizeOffset);

    if (static_cast<std::uint64_t>(
            declared_header_size) !=
        static_cast<std::uint64_t>(
            shader_header.size())) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    declared_header_size_mismatch,
                AgcShaderBinaryRegion::shader_header,
                kAgcHeaderSizeOffset));
    }

    if (static_cast<std::uint64_t>(
            declared_shader_text_size) !=
        static_cast<std::uint64_t>(
            shader_text.size())) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    declared_shader_text_size_mismatch,
                AgcShaderBinaryRegion::shader_header,
                kAgcShaderTextSizeOffset));
    }

    auto context_registers =
        parse_register_list(
            shader_header,
            kAgcContextRegistersOffset,
            kAgcContextRegisterCountOffset,
            AgcRegisterListKind::context);
    if (!context_registers.has_value()) {
        return AgcShaderBinaryResult::failure(
            context_registers.error());
    }

    auto shader_registers =
        parse_register_list(
            shader_header,
            kAgcShaderRegistersOffset,
            kAgcShaderRegisterCountOffset,
            AgcRegisterListKind::shader);
    if (!shader_registers.has_value()) {
        return AgcShaderBinaryResult::failure(
            shader_registers.error());
    }

    if (shader_text.size() <
        kAgcShaderTextTrailerSize) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    shader_text_too_small_for_trailer,
                AgcShaderBinaryRegion::shader_text,
                shader_text.size()));
    }

    const auto trailer_offset =
        shader_text.size() -
        kAgcShaderTextTrailerSize;
    const auto program_byte_size =
        read_little_endian<std::uint32_t>(
            shader_text,
            trailer_offset +
                kAgcTrailerProgramLengthOffset);
    const auto sl00_byte_size =
        read_little_endian<std::uint32_t>(
            shader_text,
            trailer_offset +
                kAgcTrailerSl00LengthOffset);

    if (program_byte_size > trailer_offset) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    program_extent_out_of_bounds,
                AgcShaderBinaryRegion::shader_text,
                trailer_offset +
                    kAgcTrailerProgramLengthOffset));
    }

    if ((program_byte_size % 4U) != 0U) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    program_size_not_dword_aligned,
                AgcShaderBinaryRegion::shader_text,
                trailer_offset +
                    kAgcTrailerProgramLengthOffset));
    }

    try {
        std::vector<std::uint32_t> words;
        words.reserve(
            static_cast<std::size_t>(
                program_byte_size / 4U));
        for (std::size_t offset = 0;
             offset <
             static_cast<std::size_t>(
                 program_byte_size);
             offset += 4) {
            words.push_back(
                read_little_endian<std::uint32_t>(
                    shader_text,
                    offset));
        }

        return AgcShaderBinaryResult::success(
            AgcShaderBinary{
                .header_magic = header_magic,
                .header_version = header_version,
                .declared_header_size =
                    declared_header_size,
                .declared_shader_text_size =
                    declared_shader_text_size,
                .program_type =
                    program_type(
                        std::to_integer<std::uint8_t>(
                            shader_header[
                                kAgcProgramTypeOffset])),
                .context_registers =
                    std::move(context_registers).value(),
                .shader_registers =
                    std::move(shader_registers).value(),
                .program_byte_size =
                    program_byte_size,
                .trailer_sl00_byte_size =
                    sl00_byte_size,
                .shader_header_bytes =
                    std::vector<std::byte>(
                        shader_header.begin(),
                        shader_header.end()),
                .shader_text_bytes =
                    std::vector<std::byte>(
                        shader_text.begin(),
                        shader_text.end()),
                .rdna2_words = std::move(words),
            });
    } catch (const std::bad_alloc&) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    host_allocation_failure,
                AgcShaderBinaryRegion::none,
                0));
    } catch (const std::length_error&) {
        return AgcShaderBinaryResult::failure(
            error(
                AgcShaderBinaryErrorCode::
                    host_allocation_failure,
                AgcShaderBinaryRegion::none,
                0));
    }
}

}  // namespace astraea::graphics
