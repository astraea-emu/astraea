#include <astraea/loader/dynamic_string.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace astraea::loader {
namespace {

static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));

[[nodiscard]] DynamicStringError string_error(
    DynamicStringErrorCode code,
    const DynamicStringRef& reference,
    std::optional<astraea::memory::GuestAddress> guest_address = std::nullopt,
    std::optional<astraea::memory::InitializedImageError> image_error = std::nullopt) {
    return DynamicStringError{
        .code = code,
        .source_entry_index = reference.source_entry_index,
        .offset = reference.offset,
        .guest_address = guest_address,
        .image_error = std::move(image_error),
    };
}

[[nodiscard]] astraea::core::Result<
    astraea::memory::GuestAddress,
    DynamicStringError>
address_at(
    astraea::memory::GuestAddress base,
    std::uint64_t offset,
    const DynamicStringRef& reference) {
    auto address = astraea::memory::GuestAddress::checked_add(
        base,
        astraea::memory::GuestSize{offset});
    if (!address.has_value()) {
        return astraea::core::Result<
            astraea::memory::GuestAddress,
            DynamicStringError>::failure(
            string_error(
                DynamicStringErrorCode::guest_address_overflow,
                reference,
                base));
    }

    return astraea::core::Result<
        astraea::memory::GuestAddress,
        DynamicStringError>::success(address.value());
}

}  // namespace

DynamicStringResult resolve_dynamic_string(
    const DynamicStringTableDescriptor& table,
    const DynamicStringRef& reference,
    const astraea::memory::InitializedImageView& image_view) {
    const auto table_size = table.range.size().value();
    if (reference.offset >= table_size) {
        return DynamicStringResult::failure(
            string_error(
                DynamicStringErrorCode::offset_out_of_bounds,
                reference,
                table.range.base()));
    }

    auto start = address_at(table.range.base(), reference.offset, reference);
    if (!start.has_value()) {
        return DynamicStringResult::failure(start.error());
    }

    const auto remaining = table_size - reference.offset;
    std::uint64_t length = 0;
    bool terminated = false;

    for (std::uint64_t i = 0; i < remaining; ++i) {
        auto address = address_at(start.value(), i, reference);
        if (!address.has_value()) {
            return DynamicStringResult::failure(address.error());
        }

        auto byte = image_view.read_byte(address.value());
        if (!byte.has_value()) {
            return DynamicStringResult::failure(
                string_error(
                    DynamicStringErrorCode::image_read_failure,
                    reference,
                    address.value(),
                    byte.error()));
        }

        if (byte.value() == std::byte{0}) {
            length = i;
            terminated = true;
            break;
        }
    }

    if (!terminated) {
        return DynamicStringResult::failure(
            string_error(
                DynamicStringErrorCode::unterminated,
                reference,
                start.value()));
    }

    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (length > static_cast<std::uint64_t>(
                         std::numeric_limits<std::size_t>::max())) {
            return DynamicStringResult::failure(
                string_error(
                    DynamicStringErrorCode::host_size_unrepresentable,
                    reference,
                    start.value()));
        }
    }

    const auto host_length = static_cast<std::size_t>(length);
    if (host_length > std::string{}.max_size()) {
        return DynamicStringResult::failure(
            string_error(
                DynamicStringErrorCode::host_size_unrepresentable,
                reference,
                start.value()));
    }

    std::string value;
    try {
        value.resize(host_length);
    } catch (const std::bad_alloc&) {
        return DynamicStringResult::failure(
            string_error(
                DynamicStringErrorCode::host_allocation_failure,
                reference,
                start.value()));
    }

    for (std::size_t i = 0; i < host_length; ++i) {
        auto address = address_at(
            start.value(),
            static_cast<std::uint64_t>(i),
            reference);
        if (!address.has_value()) {
            return DynamicStringResult::failure(address.error());
        }

        auto byte = image_view.read_byte(address.value());
        if (!byte.has_value()) {
            return DynamicStringResult::failure(
                string_error(
                    DynamicStringErrorCode::image_read_failure,
                    reference,
                    address.value(),
                    byte.error()));
        }

        value[i] = static_cast<char>(
            std::to_integer<unsigned char>(byte.value()));
    }

    return DynamicStringResult::success(std::move(value));
}

}  // namespace astraea::loader
