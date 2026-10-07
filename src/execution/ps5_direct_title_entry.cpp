#include <astraea/execution/ps5_direct_title_entry.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace astraea::execution {
namespace {

[[nodiscard]] std::uint32_t read_u32_le(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value |=
            static_cast<std::uint32_t>(
                std::to_integer<unsigned char>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64_le(
    std::span<const std::byte> bytes,
    std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value |=
            static_cast<std::uint64_t>(
                std::to_integer<unsigned char>(
                    bytes[offset + index]))
            << (index * 8U);
    }
    return value;
}

}  // namespace

Ps5DirectTitleEntryPrefixResult
validate_ps5_direct_title_entry_prefix(
    const Ps5DirectTitleEntryPrefixObservation& observation) noexcept {
    if (observation.process_parameters.value() == 0U) {
        return Ps5DirectTitleEntryPrefixResult::failure(
            Ps5DirectTitleEntryPrefixError{
                .code =
                    Ps5DirectTitleEntryPrefixErrorCode::
                        zero_process_parameter_address,
                .expected_size =
                    kPs5DirectTitleEntryPrefixSize,
                .actual_size =
                    observation.process_prefix.size(),
            });
    }

    if (observation.process_prefix.size() <
        kPs5DirectTitleEntryPrefixSize) {
        return Ps5DirectTitleEntryPrefixResult::failure(
            Ps5DirectTitleEntryPrefixError{
                .code =
                    Ps5DirectTitleEntryPrefixErrorCode::
                        prefix_too_small,
                .expected_size =
                    kPs5DirectTitleEntryPrefixSize,
                .actual_size =
                    observation.process_prefix.size(),
            });
    }

    const auto argv =
        astraea::memory::GuestAddress::checked_add(
            observation.process_parameters,
            astraea::memory::GuestSize{
                kPs5DirectTitleArgvOffset});
    if (!argv.has_value()) {
        return Ps5DirectTitleEntryPrefixResult::failure(
            Ps5DirectTitleEntryPrefixError{
                .code =
                    Ps5DirectTitleEntryPrefixErrorCode::
                        argv_address_overflow,
                .expected_size =
                    kPs5DirectTitleEntryPrefixSize,
                .actual_size =
                    observation.process_prefix.size(),
            });
    }

    return Ps5DirectTitleEntryPrefixResult::success(
        Ps5DirectTitleEntryPrefix{
            .process_parameters =
                observation.process_parameters,
            .argc =
                read_u32_le(
                    observation.process_prefix,
                    static_cast<std::size_t>(
                        kPs5DirectTitleArgcOffset)),
            .argv =
                argv.value(),
            .first_argv_pointer =
                astraea::memory::GuestAddress{
                    read_u64_le(
                        observation.process_prefix,
                        static_cast<std::size_t>(
                            kPs5DirectTitleArgvOffset))},
        });
}

Ps5DirectTitleEntryReadiness
assess_ps5_direct_title_entry_readiness(
    const Ps5DirectTitleEntryReadinessRequest& request) noexcept {
    Ps5DirectTitleEntryReadiness result{};

    const auto add_blocker =
        [&result](
            Ps5DirectTitleEntryBlocker blocker) noexcept {
            result.blockers[result.blocker_count] = blocker;
            ++result.blocker_count;
        };

    if (!request.loader_teardown_contract_established) {
        add_blocker(
            Ps5DirectTitleEntryBlocker::
                loader_teardown_contract);
    }
    if (!request.initial_rsp_contract_established) {
        add_blocker(
            Ps5DirectTitleEntryBlocker::
                initial_rsp_contract);
    }
    if (!request.process_metadata_contract_established) {
        add_blocker(
            Ps5DirectTitleEntryBlocker::
                process_metadata_contract);
    }
    if (!request.primary_thread_tls_contract_established) {
        add_blocker(
            Ps5DirectTitleEntryBlocker::
                primary_thread_tls_contract);
    }
    if (!request.bootstrap_contract_established) {
        add_blocker(
            Ps5DirectTitleEntryBlocker::
                bootstrap_contract);
    }

    return result;
}

}  // namespace astraea::execution
