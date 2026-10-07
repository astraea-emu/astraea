#include "capture.h"

#include <cstddef>
#include <cstdint>

extern "C" {
int sceKernelDebugOutText(int channel, const char* text);
void* sceKernelGetProcParam();
}

namespace {

struct OutputBuffer {
    char bytes[512]{};
    std::size_t size = 0;
    bool overflow = false;
};

void append_char(OutputBuffer& output, char value) noexcept {
    if (output.size + 1U >= sizeof(output.bytes)) {
        output.overflow = true;
        return;
    }
    output.bytes[output.size++] = value;
}

void append_text(OutputBuffer& output, const char* value) noexcept {
    while (*value != '\0') {
        append_char(output, *value++);
    }
}

void append_hex_u64(OutputBuffer& output, std::uint64_t value) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    append_text(output, "0x");
    for (unsigned shift = 60U;; shift -= 4U) {
        append_char(
            output,
            digits[static_cast<unsigned>((value >> shift) & UINT64_C(0xf))]);
        if (shift == 0U) {
            break;
        }
    }
}

void append_hex_bytes(
    OutputBuffer& output,
    const std::uint8_t* bytes,
    std::size_t size) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    for (std::size_t index = 0U; index < size; ++index) {
        const auto value = bytes[index];
        append_char(output, digits[value >> 4U]);
        append_char(output, digits[value & 0x0fU]);
    }
}

void terminate(OutputBuffer& output) noexcept {
    const auto index =
        output.size < sizeof(output.bytes)
            ? output.size
            : sizeof(output.bytes) - 1U;
    output.bytes[index] = '\0';
}

}  // namespace

extern "C" void astraea_emit_ps5_entry_observation_v0() noexcept {
    OutputBuffer output{};

    const auto& capture = astraea_ps5_entry_capture_v0;
    if (capture.magic != ASTRAEA_PS5_ENTRY_CAPTURE_V0_MAGIC ||
        capture.version != ASTRAEA_PS5_ENTRY_CAPTURE_V0_VERSION) {
        append_text(output, "ASTRAEA_ENTRY_V0 status=incomplete\n");
        terminate(output);
        (void)sceKernelDebugOutText(0, output.bytes);
        return;
    }

    const auto* procparam =
        static_cast<const std::uint8_t*>(sceKernelGetProcParam());

    append_text(output, "ASTRAEA_ENTRY_V0 status=complete");

    append_text(output, " capture_runtime=");
    append_hex_u64(
        output,
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(&astraea_ps5_entry_capture_v0)));

    append_text(output, " rdi=");
    append_hex_u64(output, capture.rdi);
    append_text(output, " rsi=");
    append_hex_u64(output, capture.rsi);
    append_text(output, " rbp=");
    append_hex_u64(output, capture.rbp);
    append_text(output, " rsp=");
    append_hex_u64(output, capture.rsp);

    append_text(output, " process_prefix=");
    append_hex_bytes(
        output,
        capture.process_prefix,
        sizeof(capture.process_prefix));

    append_text(output, " procparam_runtime=");
    append_hex_u64(
        output,
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(procparam)));

    append_text(output, " procparam_prefix=");
    if (procparam != nullptr) {
        append_hex_bytes(output, procparam, 16U);
    } else {
        append_text(output, "unavailable");
    }

    append_char(output, '\n');
    terminate(output);

    if (output.overflow) {
        (void)sceKernelDebugOutText(
            0,
            "ASTRAEA_ENTRY_V0 status=formatter_overflow\n");
        return;
    }

    (void)sceKernelDebugOutText(0, output.bytes);
}
