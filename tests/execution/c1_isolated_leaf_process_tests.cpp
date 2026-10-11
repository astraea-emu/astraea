#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include <array>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
namespace {

void exercise_source_owned_leaf(const char* mode, int expected_exit) {
    std::array<int,2> pipe_fds{-1,-1};
    REQUIRE(::pipe2(pipe_fds.data(), O_CLOEXEC|O_NONBLOCK) == 0);
    struct FdGuard {
        std::array<int,2>& fds;
        int spare = -1;
        ~FdGuard() noexcept {
            for (int fd : fds) if (fd >= 0) (void)::close(fd);
            if (spare >= 0) (void)::close(spare);
        }
    } guard{pipe_fds};
    // Force a real dup2 to fd3, even when the pipe already uses fd3;
    // otherwise an inherited O_CLOEXEC flag could erase the test capability.
    guard.spare = ::fcntl(pipe_fds[1], F_DUPFD_CLOEXEC, 4);
    REQUIRE(guard.spare >= 4);

    posix_spawn_file_actions_t actions{};
    REQUIRE(::posix_spawn_file_actions_init(&actions) == 0);
    struct ActionsGuard {
        posix_spawn_file_actions_t& actions;
        ~ActionsGuard() noexcept {
            (void)::posix_spawn_file_actions_destroy(&actions);
        }
    } cleanup{actions};
    REQUIRE(::posix_spawn_file_actions_adddup2(
        &actions, guard.spare, 3) == 0);
    for (int fd = 0; fd < 3; ++fd)
        REQUIRE(::posix_spawn_file_actions_addclose(&actions, fd) == 0);
    REQUIRE(::posix_spawn_file_actions_addclosefrom_np(&actions, 4) == 0);

    char env_lc[] = "LC_ALL=C";
    char* envp[]{env_lc, nullptr};
    char* argv[]{
        const_cast<char*>(ASTRAEA_C1_LEAF_PROBE_PATH),
        const_cast<char*>(mode),
        nullptr,
    };
    pid_t child = -1;
    REQUIRE(::posix_spawn(&child, ASTRAEA_C1_LEAF_PROBE_PATH,
                          &actions, nullptr, argv, envp) == 0);
    // Parent retains no write end and never receives an executable command.
    (void)::close(pipe_fds[1]);
    pipe_fds[1] = -1;
    (void)::close(guard.spare);
    guard.spare = -1;

    int status = 0;
    pid_t waited = 0;
    for (int i = 0; i < 300; ++i) {
        waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (waited < 0 && errno != EINTR) break;
        (void)::usleep(10000);
    }
    if (waited != child) {
        (void)::kill(child, SIGKILL);
        (void)::waitpid(child, &status, 0);
        FAIL("isolated research leaf exceeded its bounded lifetime");
    }
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == expected_exit);
    std::array<char,8> received{};
    // Child has exited, all other writers have been closed: EOF proves
    // no bytes reached the controller-owned pipe.
    REQUIRE(::read(pipe_fds[0], received.data(), received.size()) == 0);
}
} // namespace

TEST_CASE("separate exec leaf denies source-owned host-IP write without leaking bytes",
          "[execution][c1][leaf][process][negative]") {
    SECTION("same frozen source-owned guest sequence twice") {
        exercise_source_owned_leaf("source-owned-escape", 0);
        exercise_source_owned_leaf("source-owned-escape", 0);
    }
    SECTION("unknown input is refused before any guest entry") {
        exercise_source_owned_leaf("unknown-input", 64);
    }
}
#endif
