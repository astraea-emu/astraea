// Exact source-owned Linux x86-64 research leaf. Not PS5 retail admission.
// Intentionally unsanitized: the trusted test controller is ASan-instrumented.
#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE 1
#endif
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
constexpr std::array<char, 8> kMarker{'A','S','T','R','A','E','A','1'};
[[gnu::noinline]] long host_write_probe(
    int fd, const void* data, std::size_t length) noexcept {
    return ::syscall(SYS_write, fd, data, length);
}
struct Code {
    std::array<std::uint8_t, 64> bytes{};
    std::size_t size = 0;
    void push(std::uint8_t b) noexcept { bytes[size++] = b; }
    void movabs(std::uint8_t opcode, std::uint64_t value) noexcept {
        push(0x48U); push(opcode);
        for (unsigned i = 0; i != 8; ++i)
            push(static_cast<std::uint8_t>(value >> (8U * i)));
    }
};
[[nodiscard]] bool install_filter() noexcept {
    // No IP-dependent permission: only exit/rt_sigreturn can execute.
    // No open, network, write, ioctl, mmap, clone, or seccomp changes.
    std::array<sock_filter, 13> code{{
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(seccomp_data, arch)),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD|BPF_W|BPF_ABS, offsetof(seccomp_data, nr)),
        BPF_JUMP(BPF_JMP|BPF_JGE|BPF_K, 0x40000000U, 0, 1),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_exit, 0, 1),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_exit_group, 0, 1),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
        BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, SYS_rt_sigreturn, 0, 1),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET|BPF_K, SECCOMP_RET_ERRNO|
                 static_cast<std::uint32_t>(EPERM)),
    }};
    sock_fprog filter{
        .len = static_cast<unsigned short>(code.size()),
        .filter = code.data(),
    };
    if (::prctl(PR_SET_NO_NEW_PRIVS, 1UL, 0UL, 0UL, 0UL) != 0)
        return false;
    return ::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER,
                     0U, &filter) == 0;
}
} // namespace

int main(int argc, char** argv) {
    // Neither caller-supplied guest code nor a second execution mode exists.
    if (argc != 2 || std::strcmp(argv[1], "source-owned-escape") != 0)
        return 64;
    const long page_size = ::sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return 65;
    void* page = ::mmap(nullptr, static_cast<std::size_t>(page_size),
                        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) return 66;
    Code guest;
    guest.push(0x48U); guest.push(0x83U);
    guest.push(0xecU); guest.push(0x08U); // System V stack alignment
    guest.movabs(0xbfU, 3U); // RDI: disposable inherited pipe fd
    guest.movabs(0xbeU, static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(kMarker.data()))); // RSI
    guest.movabs(0xbaU, kMarker.size()); // RDX
    guest.movabs(0xb8U, static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&host_write_probe))); // RAX
    guest.push(0xffU); guest.push(0xd0U); // call host address outside guest
    guest.push(0x48U); guest.push(0x83U);
    guest.push(0xc4U); guest.push(0x08U);
    guest.push(0xc3U); // ret
    std::memcpy(page, guest.bytes.data(), guest.size);
    if (::mprotect(page, static_cast<std::size_t>(page_size),
                   PROT_READ|PROT_EXEC) != 0)
        return 67;
    if (!install_filter()) return 68;
    using Entry = long (*)();
    auto entry = reinterpret_cast<Entry>(page);
    errno = 0;
    const long result = entry();
    ::_exit(result == -1 && errno == EPERM ? 0 : 69);
}
