#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <capture.h>

#if defined(__linux__) && defined(__x86_64__)
extern "C" void astraea_test_invoke_entry_observer(const void* process_prefix);
extern "C" AstraeaPs5EntryCaptureV0 astraea_ps5_entry_capture_v0;
extern "C" AstraeaPs5EntryCaptureV0 astraea_test_target_capture;
extern "C" std::uint64_t astraea_test_target_rflags;
extern "C" void astraea_emit_ps5_entry_observation_v0() noexcept;

namespace {
const std::uint8_t* g_test_procparam = nullptr;
std::string g_test_debug_text;

std::string hex_u64(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x"
           << std::hex
           << std::nouppercase
           << std::setfill('0')
           << std::setw(16)
           << value;
    return stream.str();
}
}  // namespace

extern "C" void* sceKernelGetProcParam() {
    return const_cast<std::uint8_t*>(g_test_procparam);
}

extern "C" int sceKernelDebugOutText(int channel, const char* text) {
    REQUIRE(channel == 0);
    g_test_debug_text = text != nullptr ? text : "";
    return 0;
}
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
    "post-init PS5 observer emitter serializes C1A and C1B fields",
    "[probe][c1][process-entry][observer][emitter]") {
#if defined(__linux__) && defined(__x86_64__)
    auto& capture = astraea_ps5_entry_capture_v0;
    std::memset(&capture, 0, sizeof(capture));
    capture.magic = ASTRAEA_PS5_ENTRY_CAPTURE_V0_MAGIC;
    capture.version = ASTRAEA_PS5_ENTRY_CAPTURE_V0_VERSION;
    capture.rdi = UINT64_C(0x1111222233334444);
    capture.rsi = UINT64_C(0x5555666677778888);
    capture.rbp = UINT64_C(0x9999aaaabbbbcccc);
    capture.rsp = UINT64_C(0xddddeeeeffff0008);

    for (std::size_t index = 0U;
         index < sizeof(capture.process_prefix);
         ++index) {
        capture.process_prefix[index] =
            static_cast<std::uint8_t>(index);
    }

    std::array<std::uint8_t, 16> procparam{};
    procparam[0] = 0x60U;
    procparam[8] = static_cast<std::uint8_t>('O');
    procparam[9] = static_cast<std::uint8_t>('R');
    procparam[10] = static_cast<std::uint8_t>('B');
    procparam[11] = static_cast<std::uint8_t>('I');

    g_test_procparam = procparam.data();
    g_test_debug_text.clear();

    astraea_emit_ps5_entry_observation_v0();

    REQUIRE(g_test_debug_text.starts_with(
        "ASTRAEA_ENTRY_V0 status=complete"));
    REQUIRE(
        g_test_debug_text.find(
            " capture_runtime=" +
            hex_u64(
                static_cast<std::uint64_t>(
                    reinterpret_cast<std::uintptr_t>(&capture)))) !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " rdi=0x1111222233334444") !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " rsi=0x5555666677778888") !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " rbp=0x9999aaaabbbbcccc") !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " rsp=0xddddeeeeffff0008") !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " process_prefix=000102030405060708090a0b0c0d0e0f") !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " procparam_runtime=" +
            hex_u64(
                static_cast<std::uint64_t>(
                    reinterpret_cast<std::uintptr_t>(
                        procparam.data())))) !=
        std::string::npos);
    REQUIRE(
        g_test_debug_text.find(
            " procparam_prefix=60000000000000004f52424900000000") !=
        std::string::npos);
    REQUIRE(g_test_debug_text.ends_with("\n"));
#else
    SUCCEED(
        "The post-init observer emitter is exercised on Linux x86-64 CI");
#endif
}

TEST_CASE(
    "post-init PS5 observer emitter rejects incomplete capture",
    "[probe][c1][process-entry][observer][emitter][negative]") {
#if defined(__linux__) && defined(__x86_64__)
    std::memset(
        &astraea_ps5_entry_capture_v0,
        0,
        sizeof(astraea_ps5_entry_capture_v0));
    g_test_debug_text.clear();

    astraea_emit_ps5_entry_observation_v0();

    REQUIRE(
        g_test_debug_text ==
        "ASTRAEA_ENTRY_V0 status=incomplete\n");
#else
    SUCCEED(
        "The post-init observer emitter is exercised on Linux x86-64 CI");
#endif
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
    astraea_test_target_rflags = 0U;

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

    constexpr std::uint64_t arithmetic_flags_mask =
        (UINT64_C(1) << 0U) |
        (UINT64_C(1) << 2U) |
        (UINT64_C(1) << 4U) |
        (UINT64_C(1) << 6U) |
        (UINT64_C(1) << 7U) |
        (UINT64_C(1) << 11U);
    constexpr std::uint64_t expected_arithmetic_flags =
        (UINT64_C(1) << 2U) |
        (UINT64_C(1) << 6U);

    REQUIRE(
        (astraea_test_target_rflags &
         arithmetic_flags_mask) ==
        expected_arithmetic_flags);
#else
    SUCCEED(
        "The exact observer assembly is exercised only on Linux x86-64 CI");
#endif
}
