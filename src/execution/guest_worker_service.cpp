#include <astraea/execution/guest_worker_service.hpp>

#include <variant>

namespace astraea::execution {
namespace {

[[nodiscard]] GuestWorkerServiceError error(
    GuestWorkerServiceErrorCode code) noexcept {
    return GuestWorkerServiceError{
        .code = code,
    };
}

}  // namespace

GuestWorkerServiceResult
handle_guest_worker_service_message(
    GuestWorkerServiceState& state,
    const GuestWorkerWireMessage& message) noexcept {
    if (state.phase == GuestWorkerServicePhase::terminated) {
        return GuestWorkerServiceResult::failure(
            error(
                GuestWorkerServiceErrorCode::
                    already_terminated));
    }

    if (state.phase ==
        GuestWorkerServicePhase::awaiting_hello) {
        const auto* hello =
            std::get_if<GuestWorkerHello>(&message);
        if (hello == nullptr) {
            return GuestWorkerServiceResult::failure(
                error(
                    GuestWorkerServiceErrorCode::
                        unexpected_message));
        }

        const GuestWorkerReady ready{
            .protocol_version =
                kGuestWorkerProtocolVersion,
            .worker_id = kOwnedGuestWorkerProofId,
        };
        const auto handshake =
            validate_guest_worker_handshake(
                *hello,
                ready);
        if (!handshake.has_value()) {
            return GuestWorkerServiceResult::failure(
                error(
                    GuestWorkerServiceErrorCode::
                        unsupported_protocol_version));
        }

        state.phase = GuestWorkerServicePhase::ready;
        return GuestWorkerServiceResult::success(
            GuestWorkerServiceTransition{
                .response =
                    GuestWorkerWireMessage{ready},
                .terminate_process = false,
            });
    }

    if (const auto* run =
            std::get_if<GuestWorkerRunRequest>(
                &message);
        run != nullptr) {
        const auto valid =
            validate_guest_worker_run_request(*run);
        if (!valid.has_value()) {
            return GuestWorkerServiceResult::failure(
                error(
                    GuestWorkerServiceErrorCode::
                        invalid_run_request));
        }

        return GuestWorkerServiceResult::success(
            GuestWorkerServiceTransition{
                .response =
                    GuestWorkerWireMessage{
                        GuestWorkerStop{
                            .worker_id =
                                kOwnedGuestWorkerProofId,
                            .thread_id =
                                kOwnedGuestWorkerProofThreadId,
                            .reason =
                                GuestWorkerStopReason::
                                    normal_guest_return,
                            .guest_rip =
                                astraea::memory::GuestAddress{
                                    0U},
                        }},
                .terminate_process = false,
            });
    }

    if (std::holds_alternative<GuestWorkerTerminate>(
            message)) {
        state.phase =
            GuestWorkerServicePhase::terminated;
        return GuestWorkerServiceResult::success(
            GuestWorkerServiceTransition{
                .response = std::nullopt,
                .terminate_process = true,
            });
    }

    return GuestWorkerServiceResult::failure(
        error(
            GuestWorkerServiceErrorCode::
                unexpected_message));
}

}  // namespace astraea::execution
