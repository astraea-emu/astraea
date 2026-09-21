#include <astraea/probe/reference.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace astraea::probe {

ReferenceEchoRunResult run_reference_echo(
    const ReferenceEchoRequest& request) {
    if constexpr (
        sizeof(std::size_t) >
        sizeof(std::uint64_t)) {
        if (request.message.size() >
            static_cast<std::size_t>(
                std::numeric_limits<
                    std::uint64_t>::max())) {
            return ReferenceEchoRunResult::failure(
                ReferenceProbeError{
                    .code =
                        ReferenceProbeErrorCode::
                            host_size_unrepresentable,
                });
        }
    }

    try {
        return ReferenceEchoRunResult::success(
            ReferenceEchoResult{
                .output = request.message,
                .bytes_consumed =
                    static_cast<std::uint64_t>(
                        request.message.size()),
                .exit_code = request.exit_code,
            });
    } catch (const std::bad_alloc&) {
        return ReferenceEchoRunResult::failure(
            ReferenceProbeError{
                .code =
                    ReferenceProbeErrorCode::
                        host_allocation_failure,
            });
    } catch (const std::length_error&) {
        return ReferenceEchoRunResult::failure(
            ReferenceProbeError{
                .code =
                    ReferenceProbeErrorCode::
                        host_size_unrepresentable,
            });
    }
}

}  // namespace astraea::probe
