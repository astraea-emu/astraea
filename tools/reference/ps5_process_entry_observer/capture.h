#pragma once

#include <stddef.h>
#include <stdint.h>

#define ASTRAEA_PS5_ENTRY_CAPTURE_V0_MAGIC UINT64_C(0x3056455052545341)
#define ASTRAEA_PS5_ENTRY_CAPTURE_V0_VERSION UINT64_C(0)

typedef struct AstraeaPs5EntryCaptureV0 {
    uint64_t magic;
    uint64_t version;

    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;

    uint8_t process_prefix[16];
} AstraeaPs5EntryCaptureV0;

#if defined(__cplusplus)
#define ASTRAEA_PS5_ENTRY_STATIC_ASSERT static_assert
#else
#define ASTRAEA_PS5_ENTRY_STATIC_ASSERT _Static_assert
#endif

ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, magic) == 0x00);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, version) == 0x08);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, rax) == 0x10);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, rsi) == 0x30);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, rdi) == 0x38);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, rbp) == 0x40);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, rsp) == 0x48);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, r10) == 0x60);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, r15) == 0x88);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(offsetof(AstraeaPs5EntryCaptureV0, process_prefix) == 0x90);
ASTRAEA_PS5_ENTRY_STATIC_ASSERT(sizeof(AstraeaPs5EntryCaptureV0) == 0xa0);

#undef ASTRAEA_PS5_ENTRY_STATIC_ASSERT
