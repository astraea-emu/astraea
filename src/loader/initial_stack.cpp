#include <astraea/loader/initial_stack.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace astraea::loader {
namespace {

static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));

constexpr std::uint64_t kWordSize = 8;
constexpr std::uint64_t kAuxEntrySize = 16;
constexpr std::uint64_t kStackAlignment = 16;

[[nodiscard]] InitialStackError stack_error(
    InitialStackErrorCode code,
    std::optional<InitialStackInputKind> input_kind = std::nullopt,
    std::optional<std::size_t> input_index = std::nullopt) {
    return InitialStackError{
        .code = code,
        .input_kind = input_kind,
        .input_index = input_index,
    };
}

[[nodiscard]] bool checked_add_u64(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::uint64_t& result) noexcept {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

[[nodiscard]] bool checked_mul_u64(
    std::uint64_t lhs,
    std::uint64_t rhs,
    std::uint64_t& result) noexcept {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

[[nodiscard]] bool contains_nul(const std::string& value) noexcept {
    return value.find(static_cast<char>(0)) != std::string::npos;
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) noexcept {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>((value >> (i * 8U)) & 0xffU);
    }
}

[[nodiscard]] astraea::core::Result<
    astraea::memory::GuestAddress,
    InitialStackError>
address_at(
    astraea::memory::GuestAddress base,
    std::uint64_t offset) {
    auto result = astraea::memory::GuestAddress::checked_add(
        base,
        astraea::memory::GuestSize{offset});
    if (!result.has_value()) {
        return astraea::core::Result<
            astraea::memory::GuestAddress,
            InitialStackError>::failure(
            stack_error(InitialStackErrorCode::guest_address_overflow));
    }
    return astraea::core::Result<
        astraea::memory::GuestAddress,
        InitialStackError>::success(result.value());
}

}  // namespace

InitialStackResult build_initial_stack(const InitialStackRequest& request) {
    for (std::size_t i = 0; i < request.arguments.size(); ++i) {
        if (contains_nul(request.arguments[i])) {
            return InitialStackResult::failure(
                stack_error(
                    InitialStackErrorCode::embedded_nul,
                    InitialStackInputKind::argument,
                    i));
        }
    }

    for (std::size_t i = 0; i < request.environment.size(); ++i) {
        if (contains_nul(request.environment[i])) {
            return InitialStackResult::failure(
                stack_error(
                    InitialStackErrorCode::embedded_nul,
                    InitialStackInputKind::environment,
                    i));
        }
    }

    for (std::size_t i = 0; i < request.auxiliary_vector.size(); ++i) {
        if (request.auxiliary_vector[i].type == 0) {
            return InitialStackResult::failure(
                stack_error(
                    InitialStackErrorCode::auxv_contains_terminator,
                    InitialStackInputKind::auxiliary_vector,
                    i));
        }
    }

    std::uint64_t information_size = 0;
    const auto add_string_size = [&information_size](
                                     const std::string& value) -> bool {
        const auto size = static_cast<std::uint64_t>(value.size());
        std::uint64_t terminated_size = 0;
        if (!checked_add_u64(size, 1, terminated_size)) {
            return false;
        }
        std::uint64_t next = 0;
        if (!checked_add_u64(information_size, terminated_size, next)) {
            return false;
        }
        information_size = next;
        return true;
    };

    for (const auto& value : request.arguments) {
        if (!add_string_size(value)) {
            return InitialStackResult::failure(
                stack_error(InitialStackErrorCode::layout_size_overflow));
        }
    }
    for (const auto& value : request.environment) {
        if (!add_string_size(value)) {
            return InitialStackResult::failure(
                stack_error(InitialStackErrorCode::layout_size_overflow));
        }
    }

    const auto argument_count =
        static_cast<std::uint64_t>(request.arguments.size());
    const auto environment_count =
        static_cast<std::uint64_t>(request.environment.size());
    const auto aux_count =
        static_cast<std::uint64_t>(request.auxiliary_vector.size());

    std::uint64_t argument_pointer_bytes = 0;
    std::uint64_t environment_pointer_bytes = 0;
    std::uint64_t aux_bytes = 0;
    std::uint64_t aux_count_with_terminator = 0;

    if (!checked_mul_u64(argument_count, kWordSize, argument_pointer_bytes) ||
        !checked_mul_u64(environment_count, kWordSize, environment_pointer_bytes) ||
        !checked_add_u64(aux_count, 1, aux_count_with_terminator) ||
        !checked_mul_u64(aux_count_with_terminator, kAuxEntrySize, aux_bytes)) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::layout_size_overflow));
    }

    std::uint64_t control_size = kWordSize;
    const std::uint64_t pieces[]{
        argument_pointer_bytes,
        kWordSize,
        environment_pointer_bytes,
        kWordSize,
        aux_bytes,
    };
    for (const auto piece : pieces) {
        std::uint64_t next = 0;
        if (!checked_add_u64(control_size, piece, next)) {
            return InitialStackResult::failure(
                stack_error(InitialStackErrorCode::layout_size_overflow));
        }
        control_size = next;
    }

    const auto capacity = request.storage.size().value();
    if (information_size > capacity) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::stack_too_small));
    }

    const auto information_start = capacity - information_size;
    if (control_size > information_start) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::stack_too_small));
    }

    const auto max_rsp_offset = information_start - control_size;
    auto max_rsp = address_at(request.storage.base(), max_rsp_offset);
    if (!max_rsp.has_value()) {
        return InitialStackResult::failure(max_rsp.error());
    }

    const auto aligned_rsp_value =
        max_rsp->value() & ~(kStackAlignment - 1U);
    if (aligned_rsp_value < request.storage.base().value()) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::stack_too_small));
    }

    const auto rsp_offset =
        aligned_rsp_value - request.storage.base().value();
    if (rsp_offset > max_rsp_offset) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::stack_too_small));
    }

    const auto used_size = capacity - rsp_offset;

    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (used_size >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return InitialStackResult::failure(
                stack_error(InitialStackErrorCode::host_size_unrepresentable));
        }
    }

    const auto host_used_size = static_cast<std::size_t>(used_size);
    std::vector<std::byte> bytes;
    if (host_used_size > bytes.max_size()) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::host_size_unrepresentable));
    }

    try {
        bytes.assign(host_used_size, std::byte{0});
    } catch (const std::bad_alloc&) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::host_allocation_failure));
    } catch (const std::length_error&) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::host_size_unrepresentable));
    }

    auto used_range = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{aligned_rsp_value},
        astraea::memory::GuestSize{used_size});
    if (!used_range.has_value()) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::guest_address_overflow));
    }

    const auto information_output_offset =
        information_start - rsp_offset;
    std::uint64_t information_cursor = information_output_offset;

    std::vector<astraea::memory::GuestAddress> argument_addresses;
    std::vector<astraea::memory::GuestAddress> environment_addresses;

    try {
        argument_addresses.reserve(request.arguments.size());
        environment_addresses.reserve(request.environment.size());
    } catch (const std::bad_alloc&) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::host_allocation_failure));
    } catch (const std::length_error&) {
        return InitialStackResult::failure(
            stack_error(InitialStackErrorCode::host_size_unrepresentable));
    }

    const auto copy_string = [&bytes, &information_cursor, &used_range](
                                 const std::string& value)
        -> astraea::core::Result<
            astraea::memory::GuestAddress,
            InitialStackError> {
        auto address = address_at(used_range.value().base(), information_cursor);
        if (!address.has_value()) {
            return astraea::core::Result<
                astraea::memory::GuestAddress,
                InitialStackError>::failure(address.error());
        }

        const auto output_offset =
            static_cast<std::size_t>(information_cursor);
        for (std::size_t i = 0; i < value.size(); ++i) {
            bytes[output_offset + i] = static_cast<std::byte>(
                static_cast<unsigned char>(value[i]));
        }
        bytes[output_offset + value.size()] = std::byte{0};

        information_cursor +=
            static_cast<std::uint64_t>(value.size()) + 1U;

        return astraea::core::Result<
            astraea::memory::GuestAddress,
            InitialStackError>::success(address.value());
    };

    for (const auto& value : request.arguments) {
        auto address = copy_string(value);
        if (!address.has_value()) {
            return InitialStackResult::failure(address.error());
        }
        argument_addresses.push_back(address.value());
    }

    for (const auto& value : request.environment) {
        auto address = copy_string(value);
        if (!address.has_value()) {
            return InitialStackResult::failure(address.error());
        }
        environment_addresses.push_back(address.value());
    }

    std::size_t cursor = 0;
    write_u64(bytes, cursor, argument_count);
    cursor += static_cast<std::size_t>(kWordSize);

    for (const auto address : argument_addresses) {
        write_u64(bytes, cursor, address.value());
        cursor += static_cast<std::size_t>(kWordSize);
    }
    write_u64(bytes, cursor, 0);
    cursor += static_cast<std::size_t>(kWordSize);

    for (const auto address : environment_addresses) {
        write_u64(bytes, cursor, address.value());
        cursor += static_cast<std::size_t>(kWordSize);
    }
    write_u64(bytes, cursor, 0);
    cursor += static_cast<std::size_t>(kWordSize);

    for (const auto& entry : request.auxiliary_vector) {
        write_u64(bytes, cursor, entry.type);
        cursor += static_cast<std::size_t>(kWordSize);
        write_u64(bytes, cursor, entry.value);
        cursor += static_cast<std::size_t>(kWordSize);
    }
    write_u64(bytes, cursor, 0);
    cursor += static_cast<std::size_t>(kWordSize);
    write_u64(bytes, cursor, 0);

    return InitialStackResult::success(
        InitialStackImage{
            .storage = request.storage,
            .used_range = used_range.value(),
            .rsp = astraea::memory::GuestAddress{aligned_rsp_value},
            .bytes = std::move(bytes),
        });
}

}  // namespace astraea::loader
