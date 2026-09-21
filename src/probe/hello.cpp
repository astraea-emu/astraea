#include <astraea/probe/hello.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace astraea::probe {
namespace {

constexpr std::size_t kElfHeaderSize = 64;
constexpr std::size_t kProgramHeaderSize = 56;
constexpr std::size_t kProgramHeaderOffset = 64;
constexpr std::uint64_t kSyntheticGateStride = 16;

[[nodiscard]] ProbeHelloError probe_error(
    ProbeHelloErrorCode code,
    bool has_guest_address = false,
    std::uint64_t guest_address = 0) noexcept {
    return ProbeHelloError{
        .code = code,
        .has_guest_address = has_guest_address,
        .guest_address = guest_address,
    };
}

void write_u16(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint16_t value) {
    const auto widened =
        static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 2; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (widened >> (i * 8U)) &
                0xffU);
    }
}

void write_u32(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint32_t value) {
    const auto widened =
        static_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (widened >> (i * 8U)) &
                0xffU);
    }
}

void write_u64(
    std::vector<std::byte>& bytes,
    std::size_t offset,
    std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        bytes[offset + i] =
            static_cast<std::byte>(
                (value >> (i * 8U)) &
                0xffU);
    }
}

void write_program_header(
    std::vector<std::byte>& bytes,
    std::size_t index,
    std::uint32_t flags,
    std::uint64_t file_offset,
    std::uint64_t virtual_address,
    std::uint64_t file_size,
    std::uint64_t alignment) {
    const auto offset =
        kProgramHeaderOffset +
        index * kProgramHeaderSize;

    write_u32(bytes, offset, 1);
    write_u32(bytes, offset + 4, flags);
    write_u64(bytes, offset + 8, file_offset);
    write_u64(
        bytes,
        offset + 16,
        virtual_address);
    write_u64(bytes, offset + 24, 0);
    write_u64(bytes, offset + 32, file_size);
    write_u64(bytes, offset + 40, file_size);
    write_u64(bytes, offset + 48, alignment);
}

void append_u32(
    std::vector<std::byte>& bytes,
    std::uint32_t value) {
    for (unsigned shift = 0;
         shift < 32;
         shift += 8) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) &
                    0xffU)));
    }
}

void append_u64(
    std::vector<std::byte>& bytes,
    std::uint64_t value) {
    for (unsigned shift = 0;
         shift < 64;
         shift += 8) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    (value >> shift) &
                    0xffU)));
    }
}

void append_mov_rdi_imm64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbf});
    append_u64(code, value);
}

void append_mov_rsi_imm64(
    std::vector<std::byte>& code,
    std::uint64_t value) {
    code.push_back(std::byte{0x48});
    code.push_back(std::byte{0xbe});
    append_u64(code, value);
}

[[nodiscard]] bool append_call_rel32(
    std::vector<std::byte>& code,
    std::uint64_t code_base,
    std::uint64_t target) {
    const auto code_size =
        static_cast<std::uint64_t>(
            code.size());
    if (code_base >
        std::numeric_limits<std::uint64_t>::max() -
            code_size -
            5U) {
        return false;
    }

    const std::uint64_t next_rip =
        code_base + code_size + 5U;

    std::int32_t relative = 0;
    if (target >= next_rip) {
        const std::uint64_t distance =
            target - next_rip;
        if (distance >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int32_t>::
                    max())) {
            return false;
        }
        relative =
            static_cast<std::int32_t>(
                distance);
    } else {
        const std::uint64_t distance =
            next_rip - target;
        constexpr std::uint64_t kMinMagnitude =
            std::uint64_t{1} << 31U;
        if (distance > kMinMagnitude) {
            return false;
        }
        if (distance == kMinMagnitude) {
            relative =
                std::numeric_limits<std::int32_t>::
                    min();
        } else {
            relative =
                -static_cast<std::int32_t>(
                    distance);
        }
    }

    code.push_back(std::byte{0xe8});
    append_u32(
        code,
        static_cast<std::uint32_t>(
            relative));
    return true;
}

[[nodiscard]] astraea::core::Result<
    std::vector<std::byte>,
    ProbeHelloError>
build_probe_code(
    std::uint64_t code_base,
    std::uint64_t data_base,
    std::uint64_t gate_base) {
    try {
        std::vector<std::byte> code;
        code.reserve(64);

        append_mov_rdi_imm64(
            code,
            data_base);
        append_mov_rsi_imm64(
            code,
            kProbeHelloMessage.size());

        if (!append_call_rel32(
                code,
                code_base,
                gate_base)) {
            return astraea::core::Result<
                std::vector<std::byte>,
                ProbeHelloError>::failure(
                    probe_error(
                        ProbeHelloErrorCode::
                            relative_call_out_of_range,
                        true,
                        gate_base));
        }

        // test.write must return the consumed byte count in RAX.
        code.push_back(std::byte{0x48});
        code.push_back(std::byte{0x83});
        code.push_back(std::byte{0xf8});
        code.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    kProbeHelloMessage.size())));

        const std::size_t branch_offset =
            code.size();
        code.push_back(std::byte{0x75});
        code.push_back(std::byte{0});

        append_mov_rdi_imm64(
            code,
            42);

        const std::uint64_t exit_gate =
            gate_base +
            kSyntheticGateStride;
        if (exit_gate < gate_base ||
            !append_call_rel32(
                code,
                code_base,
                exit_gate)) {
            return astraea::core::Result<
                std::vector<std::byte>,
                ProbeHelloError>::failure(
                    probe_error(
                        ProbeHelloErrorCode::
                            relative_call_out_of_range,
                        true,
                        gate_base));
        }

        const std::size_t failure_offset =
            code.size();
        const std::size_t branch_next =
            branch_offset + 2U;
        const std::size_t branch_distance =
            failure_offset - branch_next;
        if (branch_distance >
            static_cast<std::size_t>(
                std::numeric_limits<std::int8_t>::
                    max())) {
            return astraea::core::Result<
                std::vector<std::byte>,
                ProbeHelloError>::failure(
                    probe_error(
                        ProbeHelloErrorCode::
                            relative_call_out_of_range));
        }

        code[branch_offset + 1U] =
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    branch_distance));

        // Reaching this means RAX was wrong or test.exit resumed.
        code.push_back(std::byte{0x0f});
        code.push_back(std::byte{0x0b});

        return astraea::core::Result<
            std::vector<std::byte>,
            ProbeHelloError>::success(
                std::move(code));
    } catch (const std::bad_alloc&) {
        return astraea::core::Result<
            std::vector<std::byte>,
            ProbeHelloError>::failure(
                probe_error(
                    ProbeHelloErrorCode::
                        host_allocation_failure));
    } catch (const std::length_error&) {
        return astraea::core::Result<
            std::vector<std::byte>,
            ProbeHelloError>::failure(
                probe_error(
                    ProbeHelloErrorCode::
                        host_size_unrepresentable));
    }
}

}  // namespace

ProbeHelloResult build_probe_hello_fixture(
    astraea::memory::GuestAddress guest_base,
    std::uint64_t page_size) {
    if (page_size < 4096 ||
        (page_size & (page_size - 1U)) != 0) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    invalid_page_size));
    }

    if ((guest_base.value() % page_size) != 0) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    unaligned_guest_base,
                true,
                guest_base.value()));
    }

    auto two_pages =
        astraea::memory::GuestSize::checked_add(
            astraea::memory::GuestSize{
                page_size},
            astraea::memory::GuestSize{
                page_size});
    if (!two_pages.has_value()) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    guest_address_overflow,
                true,
                guest_base.value()));
    }
    auto three_pages =
        astraea::memory::GuestSize::checked_add(
            two_pages.value(),
            astraea::memory::GuestSize{
                page_size});
    if (!three_pages.has_value()) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    guest_address_overflow,
                true,
                guest_base.value()));
    }

    auto data_base =
        astraea::memory::GuestAddress::checked_add(
            guest_base,
            astraea::memory::GuestSize{
                page_size});
    auto stack_base =
        astraea::memory::GuestAddress::checked_add(
            guest_base,
            two_pages.value());
    auto gate_base =
        astraea::memory::GuestAddress::checked_add(
            guest_base,
            three_pages.value());
    if (!data_base.has_value() ||
        !stack_base.has_value() ||
        !gate_base.has_value()) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    guest_address_overflow,
                true,
                guest_base.value()));
    }

    auto stack_storage =
        astraea::memory::GuestRange::create(
            stack_base.value(),
            astraea::memory::GuestSize{
                page_size});
    if (!stack_storage.has_value()) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    guest_address_overflow,
                true,
                stack_base->value()));
    }

    auto code =
        build_probe_code(
            guest_base.value(),
            data_base->value(),
            gate_base->value());
    if (!code.has_value()) {
        return ProbeHelloResult::failure(
            code.error());
    }

    if (page_size >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::
                max())) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    host_size_unrepresentable));
    }

    const std::uint64_t data_offset =
        two_pages->value();
    const std::uint64_t message_size =
        static_cast<std::uint64_t>(
            kProbeHelloMessage.size());
    if (data_offset >
        std::numeric_limits<std::uint64_t>::max() -
            message_size) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    host_size_unrepresentable));
    }
    const std::uint64_t raw_file_size =
        data_offset + message_size;
    if (raw_file_size >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::
                max())) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    host_size_unrepresentable));
    }

    try {
        std::vector<std::byte> elf_bytes(
            static_cast<std::size_t>(
                raw_file_size),
            std::byte{0});

        elf_bytes[0] = std::byte{0x7f};
        elf_bytes[1] = std::byte{'E'};
        elf_bytes[2] = std::byte{'L'};
        elf_bytes[3] = std::byte{'F'};
        elf_bytes[4] = std::byte{2};
        elf_bytes[5] = std::byte{1};
        elf_bytes[6] = std::byte{1};

        write_u16(elf_bytes, 16, 2);
        write_u16(elf_bytes, 18, 62);
        write_u32(elf_bytes, 20, 1);
        write_u64(
            elf_bytes,
            24,
            guest_base.value());
        write_u64(
            elf_bytes,
            32,
            kProgramHeaderOffset);
        write_u16(
            elf_bytes,
            52,
            kElfHeaderSize);
        write_u16(
            elf_bytes,
            54,
            kProgramHeaderSize);
        write_u16(elf_bytes, 56, 2);

        write_program_header(
            elf_bytes,
            0,
            0x5,
            page_size,
            guest_base.value(),
            static_cast<std::uint64_t>(
                code->size()),
            page_size);
        write_program_header(
            elf_bytes,
            1,
            0x4,
            data_offset,
            data_base->value(),
            message_size,
            page_size);

        const auto code_offset =
            static_cast<std::size_t>(
                page_size);
        std::copy(
            code->begin(),
            code->end(),
            elf_bytes.begin() +
                static_cast<std::ptrdiff_t>(
                    code_offset));

        const auto message_offset =
            static_cast<std::size_t>(
                data_offset);
        for (std::size_t i = 0;
             i < kProbeHelloMessage.size();
             ++i) {
            elf_bytes[message_offset + i] =
                static_cast<std::byte>(
                    static_cast<unsigned char>(
                        kProbeHelloMessage[i]));
        }

        astraea::loader::InitialStackRequest
            stack_request{
                .storage =
                    stack_storage.value(),
                .arguments =
                    std::vector<std::string>{
                        "probe_hello.elf",
                    },
                .environment =
                    std::vector<std::string>{
                        "ASTRAEA_PROBE=1",
                    },
                .auxiliary_vector =
                    std::vector<
                        astraea::loader::
                            AuxiliaryVectorEntry>{
                        astraea::loader::
                            AuxiliaryVectorEntry{
                                .type = 6,
                                .value = page_size,
                            },
                    },
            };

        return ProbeHelloResult::success(
            ProbeHelloFixture{
                .elf_bytes =
                    std::move(elf_bytes),
                .initial_stack =
                    std::move(stack_request),
                .code_base = guest_base,
                .data_base =
                    data_base.value(),
                .gate_base =
                    gate_base.value(),
            });
    } catch (const std::bad_alloc&) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return ProbeHelloResult::failure(
            probe_error(
                ProbeHelloErrorCode::
                    host_size_unrepresentable));
    }
}

}  // namespace astraea::probe
