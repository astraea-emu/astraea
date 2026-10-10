#pragma once

// Test-only complete host-linked x86-64 ELF. The guest process uses a
// synthetic, explicitly named entry/gate policy, never Sony process state.
#include "owned_two_elf_worker_fixture.hpp"

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
#include "owned_linked_elf_bytes.hpp"
#endif

namespace astraea::test {

namespace detail {
#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

constexpr std::uint64_t kLinkedPage = 4096U;
constexpr std::array<std::byte, 8> kLinkedBundleMagic{
    std::byte{'A'}, std::byte{'S'}, std::byte{'T'}, std::byte{'L'},
    std::byte{'I'}, std::byte{'N'}, std::byte{'K'}, std::byte{'1'},
};

[[nodiscard]] inline std::optional<std::vector<std::byte>>
make_runtime_linked_elf(std::uint64_t base) {
    if (base == 0U || base % kLinkedPage != 0U ||
        base > std::numeric_limits<std::uint64_t>::max() - 5U * kLinkedPage ||
        kOwnedLinkedElf.size() < kLinkedPage + 22U)
        return std::nullopt;

    std::vector<std::byte> image{
        kOwnedLinkedElf.begin(), kOwnedLinkedElf.end()};
    // The linker produced the COMPLETE ET_EXEC ELF; only these explicitly
    // isolated values are adapted to the test worker's free guest VA.
    // No section/program-header table is reconstructed by Astraea.
    write_le<std::uint64_t>(image, 24U, base);        // ELF e_entry
    write_le<std::uint64_t>(image, 64U + 16U, base);  // PT_LOAD p_vaddr
    write_le<std::uint64_t>(image, 64U + 24U, base);  // PT_LOAD p_paddr
    write_le<std::uint64_t>(
        image, static_cast<std::size_t>(kLinkedPage) + 12U,
        base + 2U * kLinkedPage);                     // owned gate
    return image;
}
#endif
} // namespace detail

[[nodiscard]] inline std::optional<std::vector<std::byte>>
make_owned_linked_elf_sealed_bundle() {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    return std::nullopt;
#else
    if (::sysconf(_SC_PAGESIZE) != static_cast<long>(detail::kLinkedPage))
        return std::nullopt;
    auto base = detail::free_guest_block(detail::kLinkedPage);
    if (!base.has_value()) return std::nullopt;
    auto image = detail::make_runtime_linked_elf(base.value());
    if (!image.has_value()) return std::nullopt;
    std::vector<std::byte> result;
    result.reserve(16U + image->size());
    result.insert(result.end(), detail::kLinkedBundleMagic.begin(),
        detail::kLinkedBundleMagic.end());
    for (unsigned i = 0U; i < 8U; ++i)
        result.push_back(static_cast<std::byte>(
            (base.value() >> (i * 8U)) & 0xffU));
    result.insert(result.end(), image->begin(), image->end());
    return result;
#endif
}

[[nodiscard]] inline bool owned_linked_elf_bundle_matches_source(
    std::span<const std::byte> bytes) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    (void)bytes;
    return false;
#else
    if (::sysconf(_SC_PAGESIZE) != static_cast<long>(detail::kLinkedPage) ||
        bytes.size() != 16U + detail::kOwnedLinkedElf.size() ||
        !std::equal(detail::kLinkedBundleMagic.begin(),
            detail::kLinkedBundleMagic.end(), bytes.begin()))
        return false;
    const auto base = detail::read_le64(bytes, 8U);
    auto expected = detail::make_runtime_linked_elf(base);
    return expected.has_value() && std::equal(
        expected->begin(), expected->end(), bytes.begin() + 16);
#endif
}

[[nodiscard]] inline std::optional<astraea::execution::GuestWorkerStop>
run_owned_linked_elf_worker(
    astraea::execution::GuestWorkerId worker_id,
    astraea::execution::GuestThreadId thread_id,
    std::span<const std::byte> sealed_bundle) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    (void)worker_id;
    (void)thread_id;
    (void)sealed_bundle;
    return std::nullopt;
#else
    using namespace astraea;
    if (!owned_linked_elf_bundle_matches_source(sealed_bundle))
        return std::nullopt;
    const auto base = detail::read_le64(sealed_bundle, 8U);
    auto range = memory::GuestRange::create(
        memory::GuestAddress{base + detail::kLinkedPage},
        memory::GuestSize{detail::kLinkedPage});
    if (!range.has_value()) return std::nullopt;
    loader::InitialStackRequest stack{
        .storage = range.value(),
        .arguments = {"astraea-owned-linked-entry"},
        .environment = {},
        .auxiliary_vector = {},
    };
    auto source = sealed_bundle.subspan(16U);
    auto image = loader::build_guest_image({
        .image_bytes = std::vector<std::byte>{source.begin(), source.end()},
        .initial_stack = std::move(stack),
        .elf_profile = loader::ElfParseProfile::generic,
    });
    if (!image.has_value() || image->elf.header.type != 2U ||
        image->elf.header.entry != base || image->mappings.size() != 1U ||
        image->elf.program_headers.size() != 1U ||
        image->mappings.front().range.base() !=
            memory::GuestAddress{base})
        return std::nullopt;

    auto registry = execution::HleRegistry::create({
        execution::HleFunctionDescriptor{
            .id = execution::kSyntheticTestExitId,
            .canonical_name = "astraea.owned.exit",
            .argument_count = 1U,
        },
    });
    if (!registry.has_value()) return std::nullopt;
    auto gates = execution::build_synthetic_gate_region(
        registry.value(),
        memory::GuestAddress{base + 2U * detail::kLinkedPage}, 1U,
        {execution::GateBinding{
            .slot = 0U,
            .function_id = execution::kSyntheticTestExitId,
        }},
        image.value());
    if (!gates.has_value()) return std::nullopt;

    auto prepared = execution::prepare_linux_guest_memory(image.value());
    if (!prepared.has_value()) return std::nullopt;
    auto session = execution::run_linux_synthetic_session(
        image.value(), prepared.value(), registry.value(), gates.value(),
        execution::make_synthetic_initial_context(image.value()));
    if (!session.has_value() || session->exit_code != 42U ||
        session->gate_stop_count != 1U || !session->output.empty() ||
        session->events.size() != 3U ||
        session->events[0].kind !=
            execution::SyntheticSessionEventKind::guest_entry ||
        session->events[1].kind !=
            execution::SyntheticSessionEventKind::gate_stop ||
        session->events[2].kind !=
            execution::SyntheticSessionEventKind::hle_exit ||
        session->events[2].value != 42U)
        return std::nullopt;

    const auto stop = execution::GuestWorkerStop{
        .worker_id = worker_id,
        .thread_id = thread_id,
        .reason = execution::GuestWorkerStopReason::normal_guest_return,
        .guest_rip = memory::GuestAddress{session->events[1].rip},
    };
    prepared.value() = execution::LinuxPreparedMemory{};
    return stop;
#endif
}

} // namespace astraea::test
