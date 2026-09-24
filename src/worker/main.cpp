#include <astraea/execution/guest_worker_stdio.hpp>

#include <string_view>

namespace {

astraea::execution::GuestWorkerOwnedFixtureMode
mode_from_args(
    int argc,
    char** argv,
    bool& valid) noexcept {
    using Mode =
        astraea::execution::GuestWorkerOwnedFixtureMode;

    valid = true;
    if (argc == 1) {
        return Mode::normal;
    }
    if (argc != 2 || argv[1] == nullptr) {
        valid = false;
        return Mode::normal;
    }

    const std::string_view arg{argv[1]};
    if (arg == "--exit-before-ready") {
        return Mode::exit_before_ready;
    }
    if (arg == "--invalid-frame-before-ready") {
        return Mode::invalid_frame_before_ready;
    }
    if (arg == "--stall-before-ready") {
        return Mode::stall_before_ready;
    }

    valid = false;
    return Mode::normal;
}

}  // namespace

int main(int argc, char** argv) {
    bool valid = false;
    const auto mode =
        mode_from_args(
            argc,
            argv,
            valid);
    if (!valid) {
        return 64;
    }

    const auto result =
        astraea::execution::
            run_guest_worker_stdio(mode);
    return result.has_value() ? 0 : 65;
}
