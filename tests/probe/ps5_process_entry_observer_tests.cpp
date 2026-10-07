#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include <capture.h>

#if defined(__linux__) && defined(__x86_64__)
extern "C" void astraea_test_invoke_entry_observer(const void* process_prefix);
extern "C" AstraeaPs5EntryCaptureV0 astraea_ps5_entry_capture_v0;
extern "C" AstraeaPs5EntryCaptureV0 astraea_test_target_capture;
#endif

TEST_CASE(
    "PS5 process-entry capture layout is stable",
    "[probe][c1][process-entry][observer]") {
    REQUIRE(sizeof(AstraeaPs5EntryCaptureV0) == 0xa0U);
    REQUIRE(offsetof(AstraeaPs5EntryCaptureV0, rdi) == 0x38U);
    REQUIRE(offsetof(AstraeaPs5EntryCaptureV0, rsp) == 0x48U);
    REQUIRE(
        offsetof(AstraeaPs5EntryCaptureV0, process_prefix) ==
        0x90U);
}

TEST_CASE(
    "pre-CRT observer preserves entry GPRs and captures the corroborated prefix",
    "[probe][c1][process-entry][observer][linux]") {
#if defined(__linux__) && defined(__x86_64__)
    std::array<std::byte, 16> process_prefix{};
    for (std::size_t index = 0; index < process_prefix.size(); ++index) {
        process_prefix[index] = static_cast<std::byte>(index + 1U);
    }

    std::memset(
        &astraea_ps5_entry_capture_v0,
        0,
        sizeof(astraea_ps5_entry_capture_v0));
    std::memset(
        &astraea_test_target_capture,
        0,
        sizeof(astraea_test_target_capture));

    astraea_test_invoke_entry_observer(process_prefix.data());

    const auto& captured = astraea_ps5_entry_capture_v0;
    const auto& target = astraea_test_target_capture;

    REQUIRE(captured.magic == ASTRAEA_PS5_ENTRY_CAPTURE_V0_MAGIC);
    REQUIRE(captured.version == ASTRAEA_PS5_ENTRY_CAPTURE_V0_VERSION);
    REQUIRE(
        std::memcmp(
            captured.process_prefix,
            process_prefix.data(),
            process_prefix.size()) ==
        0);

    REQUIRE(captured.rax == target.rax);
    REQUIRE(captured.rbx == target.rbx);
    REQUIRE(captured.rcx == target.rcx);
    REQUIRE(captured.rdx == target.rdx);
    REQUIRE(captured.rsi == target.rsi);
    REQUIRE(captured.rdi == target.rdi);
    REQUIRE(captured.rbp == target.rbp);
    REQUIRE(captured.rsp == target.rsp);
    REQUIRE(captured.r8 == target.r8);
    REQUIRE(captured.r9 == target.r9);
    REQUIRE(captured.r10 == target.r10);
    REQUIRE(captured.r11 == target.r11);
    REQUIRE(captured.r12 == target.r12);
    REQUIRE(captured.r13 == target.r13);
    REQUIRE(captured.r14 == target.r14);
    REQUIRE(captured.r15 == target.r15);

    REQUIRE(captured.rsi == UINT64_C(0x5555555555555555));
    REQUIRE(captured.rbp == UINT64_C(0x7777777777777777));
    REQUIRE(
        captured.rdi ==
        reinterpret_cast<std::uintptr_t>(process_prefix.data()));
#else
    SUCCEED(
        "The exact observer assembly is exercised only on Linux x86-64 CI");
#endif
}
