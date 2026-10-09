#pragma once

// Test-only, source-authored Linux x86-64 two-ELF guest execution.
// No retail admission, Sony runtime module or inferred PS5 ABI is involved.
// The worker's existing controller/supervisor protocol owns the process
// lifetime; this helper never opens a guest-controlled host path.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <astraea/execution/guest_memory.hpp>
#include <astraea/execution/guest_worker_protocol.hpp>
#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/linux_session.hpp>
#include <astraea/execution/module_graph.hpp>
#include <astraea/execution/module_graph_import_apply.hpp>
#include <astraea/execution/module_graph_import_patch.hpp>
#include <astraea/execution/module_graph_import_plan.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace astraea::test {
namespace detail {

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

template <typename T>
inline void write_le(
    std::vector<std::byte>& bytes, std::size_t offset, T value) {
    static_assert(std::is_unsigned_v<T>);
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[offset + i] = static_cast<std::byte>(
            (static_cast<std::uint64_t>(value) >> (i * 8U)) & 0xffU);
    }
}

inline void write_phdr(
    std::vector<std::byte>& bytes, std::size_t offset,
    std::uint32_t flags, std::uint64_t file_offset,
    std::uint64_t guest_address, std::uint64_t size,
    std::uint64_t alignment, std::uint32_t type = 1U) {
    write_le<std::uint32_t>(bytes, offset, type);
    write_le<std::uint32_t>(bytes, offset + 4U, flags);
    write_le<std::uint64_t>(bytes, offset + 8U, file_offset);
    write_le<std::uint64_t>(bytes, offset + 16U, guest_address);
    write_le<std::uint64_t>(bytes, offset + 24U, 0U);
    write_le<std::uint64_t>(bytes, offset + 32U, size);
    write_le<std::uint64_t>(bytes, offset + 40U, size);
    write_le<std::uint64_t>(bytes, offset + 48U, alignment);
}

inline std::vector<std::byte> make_elf(
    std::uint64_t entry, std::uint16_t type,
    std::uint16_t phnum, std::uint64_t page,
    std::size_t page_count) {
    std::vector<std::byte> bytes(
        static_cast<std::size_t>(page) * page_count, std::byte{0});
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{2};
    bytes[5] = std::byte{1};
    bytes[6] = std::byte{1};
    write_le<std::uint16_t>(bytes, 16U, type);
    write_le<std::uint16_t>(bytes, 18U, 62U);
    write_le<std::uint32_t>(bytes, 20U, 1U);
    write_le<std::uint64_t>(bytes, 24U, entry);
    write_le<std::uint64_t>(bytes, 32U, 64U);
    write_le<std::uint16_t>(bytes, 52U, 64U);
    write_le<std::uint16_t>(bytes, 54U, 56U);
    write_le<std::uint16_t>(bytes, 56U, phnum);
    return bytes;
}

inline void write_dyn(
    std::vector<std::byte>& bytes, std::size_t offset,
    std::size_t index, std::uint64_t tag, std::uint64_t value) {
    write_le<std::uint64_t>(bytes, offset + index * 16U, tag);
    write_le<std::uint64_t>(bytes, offset + index * 16U + 8U, value);
}

inline std::optional<std::uint64_t> free_guest_block(
    std::uint64_t page) {
    if (page > std::numeric_limits<std::size_t>::max() / 5U)
        return std::nullopt;
    auto* region = ::mmap(
        nullptr, static_cast<std::size_t>(page * 5U),
        PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED)
        return std::nullopt;
    const auto base = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(region));
    if (::munmap(region, static_cast<std::size_t>(page * 5U)) != 0)
        return std::nullopt;
    if (base > std::numeric_limits<std::uint64_t>::max() - page * 5U)
        return std::nullopt;
    return base;
}

inline std::optional<astraea::loader::InitialStackRequest> stack_request(
    std::uint64_t base, std::uint64_t page) {
    auto range = astraea::memory::GuestRange::create(
        astraea::memory::GuestAddress{base + 2U * page},
        astraea::memory::GuestSize{page});
    if (!range.has_value()) return std::nullopt;
    return astraea::loader::InitialStackRequest{
        .storage = range.value(),
        .arguments = {"astraea-owned-two-elf"},
        .environment = {},
        .auxiliary_vector = {},
    };
}

#endif

}  // namespace detail

namespace detail {

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
struct OwnedBinaryPair {
    std::vector<std::byte> client;
    std::vector<std::byte> provider;
};

// This is the single source of independently authored ELF input bytes.
// Both the controller and the worker call this generator, but ONLY the
// controller's bytes are delivered and executed. The worker regenerates
// the expected bytes exclusively to refuse tampering before native entry.
[[nodiscard]] inline std::optional<OwnedBinaryPair>
make_owned_binary_pair(std::uint64_t code, std::uint64_t page) {
    if (page < 4096U ||
        page > std::numeric_limits<std::size_t>::max() / 5U ||
        code > std::numeric_limits<std::uint64_t>::max() - 5U * page)
        return std::nullopt;
    const auto data = code + page;
    const auto gate = code + 3U * page;
    const auto provider_address = code + 4U * page;
    const auto got = data + 0x40U;
    // Client: movabs rdi, 42; call qword [rip + GOT-relative displacement];
    // ud2 (unreachable). One exact imported symbol and one JUMP_SLOT.
    auto client_bytes = detail::make_elf(code, 0xfe10U, 3U, page, 3U);
    detail::write_phdr(
        client_bytes, 64U, 5U, page, code, page, page);
    detail::write_phdr(
        client_bytes, 120U, 6U, 2U * page, data, page, page);
    detail::write_phdr(
        client_bytes, 176U, 6U, 2U * page + 0x400U,
        data + 0x400U, 9U * 16U, 8U, 2U);
    std::size_t c = static_cast<std::size_t>(page);
    client_bytes[c++] = std::byte{0x48};
    client_bytes[c++] = std::byte{0xbf};
    detail::write_le<std::uint64_t>(client_bytes, c, 42U);
    c += 8U;
    client_bytes[c++] = std::byte{0xff};
    client_bytes[c++] = std::byte{0x15};
    const auto next_rip = code + 16U;
    if (got < next_rip ||
        got - next_rip > static_cast<std::uint64_t>(
            std::numeric_limits<std::int32_t>::max()))
        return std::nullopt;
    detail::write_le<std::uint32_t>(
        client_bytes, c, static_cast<std::uint32_t>(got - next_rip));
    c += 4U;
    client_bytes[c++] = std::byte{0x0f};
    client_bytes[c] = std::byte{0x0b};

    constexpr char kImport[] = "ABCDEFGHIJK#owned-lib#owned-provider";
    const auto strings = static_cast<std::size_t>(2U * page + 0x100U);
    client_bytes[strings] = std::byte{0};
    for (std::size_t i = 0; i < sizeof(kImport); ++i)
        client_bytes[strings + 1U + i] =
            static_cast<std::byte>(kImport[i]);
    const auto syms = static_cast<std::size_t>(2U * page + 0x200U);
    detail::write_le<std::uint32_t>(client_bytes, syms + 24U, 1U);
    client_bytes[syms + 28U] = std::byte{0x12};
    const auto rela = static_cast<std::size_t>(2U * page + 0x300U);
    detail::write_le<std::uint64_t>(client_bytes, rela, got);
    detail::write_le<std::uint64_t>(
        client_bytes, rela + 8U, (1ULL << 32U) | 7ULL);
    detail::write_le<std::uint64_t>(client_bytes, rela + 16U, 0U);
    const auto dyn = static_cast<std::size_t>(2U * page + 0x400U);
    detail::write_dyn(client_bytes, dyn, 0U, 5U, data + 0x100U);
    detail::write_dyn(client_bytes, dyn, 1U, 10U, sizeof(kImport) + 1U);
    detail::write_dyn(client_bytes, dyn, 2U, 6U, data + 0x200U);
    detail::write_dyn(client_bytes, dyn, 3U, 11U, 24U);
    detail::write_dyn(client_bytes, dyn, 4U, 39U, 2U * 24U);
    detail::write_dyn(client_bytes, dyn, 5U, 23U, data + 0x300U);
    detail::write_dyn(client_bytes, dyn, 6U, 2U, 24U);
    detail::write_dyn(client_bytes, dyn, 7U, 20U, 7U);
    detail::write_dyn(client_bytes, dyn, 8U, 0U, 0U);

    // An independent PS5-format module ELF: one RX PT_LOAD and an owned
    // function that transfers to the explicit synthetic gate.
    auto provider_bytes = detail::make_elf(
        provider_address, 0xfe18U, 1U, page, 2U);
    detail::write_phdr(
        provider_bytes, 64U, 5U, page,
        provider_address, page, page);
    const auto p = static_cast<std::size_t>(page);
    provider_bytes[p] = std::byte{0x48};
    provider_bytes[p + 1U] = std::byte{0xb8};
    detail::write_le<std::uint64_t>(
        provider_bytes, p + 2U, gate);
    provider_bytes[p + 10U] = std::byte{0xff};
    provider_bytes[p + 11U] = std::byte{0xe0};


    return OwnedBinaryPair{
        .client = std::move(client_bytes),
        .provider = std::move(provider_bytes),
    };
}

constexpr std::array<std::byte, 8U> kOwnedPairMagic{
    std::byte{'A'}, std::byte{'S'}, std::byte{'T'},
    std::byte{'2'}, std::byte{'E'}, std::byte{'L'},
    std::byte{'F'}, std::byte{'1'},
};

inline std::uint64_t read_le64(
    std::span<const std::byte> bytes, std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t i = 0U; i < 8U; ++i)
        value |= static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(bytes[offset + i]))
            << (8U * i);
    return value;
}
#endif

}  // namespace detail

// Produce the exact *same* frozen source-authored ELF input pair once in the
// controller. Callers must transmit the identical returned vector to each
// fresh Linux worker via the existing sealed fd 3 artifact channel.
// A fixed base is frozen into this bundle for both workers; mapping failure
// remains fail-closed instead of choosing a different address on retry.
[[nodiscard]] inline std::optional<std::vector<std::byte>>
make_owned_two_elf_sealed_bundle() {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    return std::nullopt;
#else
    const long raw_page = ::sysconf(_SC_PAGESIZE);
    if (raw_page < 4096)
        return std::nullopt;
    const auto page = static_cast<std::uint64_t>(raw_page);
    if (page > std::numeric_limits<std::size_t>::max() / 5U)
        return std::nullopt;
    auto base = detail::free_guest_block(page);
    if (!base.has_value())
        return std::nullopt;
    auto pair = detail::make_owned_binary_pair(base.value(), page);
    if (!pair.has_value())
        return std::nullopt;

    std::vector<std::byte> result;
    result.reserve(16U + pair->client.size() + pair->provider.size());
    result.insert(
        result.end(), detail::kOwnedPairMagic.begin(),
        detail::kOwnedPairMagic.end());
    for (std::size_t i = 0U; i < 8U; ++i)
        result.push_back(static_cast<std::byte>(
            (base.value() >> (i * 8U)) & 0xffU));
    result.insert(
        result.end(), pair->client.begin(), pair->client.end());
    result.insert(
        result.end(), pair->provider.begin(), pair->provider.end());
    return result;
#endif
}

// Read-only provenance gate shared by the worker's classification and
// execution paths. Even a structurally parseable but changed ELF is refused.
[[nodiscard]] inline bool owned_two_elf_bundle_matches_source(
    std::span<const std::byte> bytes) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    (void)bytes;
    return false;
#else
    const long raw_page = ::sysconf(_SC_PAGESIZE);
    if (raw_page < 4096)
        return false;
    const auto page = static_cast<std::uint64_t>(raw_page);
    if (page > (std::numeric_limits<std::size_t>::max() - 16U) / 5U ||
        bytes.size() != static_cast<std::size_t>(5U * page) + 16U ||
        !std::equal(
            detail::kOwnedPairMagic.begin(),
            detail::kOwnedPairMagic.end(), bytes.begin()))
        return false;
    const auto code = detail::read_le64(bytes, 8U);
    if (code == 0U || code % page != 0U ||
        code > std::numeric_limits<std::uint64_t>::max() - 5U * page)
        return false;

    const auto client_size = static_cast<std::size_t>(3U * page);
    const auto provider_size = static_cast<std::size_t>(2U * page);
    const auto client_blob = bytes.subspan(16U, client_size);
    const auto provider_blob =
        bytes.subspan(16U + client_size, provider_size);
    const auto expected = detail::make_owned_binary_pair(code, page);
    return expected.has_value() &&
        std::equal(
            client_blob.begin(), client_blob.end(),
            expected->client.begin(), expected->client.end()) &&
        std::equal(
            provider_blob.begin(), provider_blob.end(),
            expected->provider.begin(), expected->provider.end());
#endif
}

// Success requires exact byte equality against both independently authored
// source inputs, separate ELF validation, checked native binding, and typed
// worker stop. No Sony ABI or real commercial title is admitted.
[[nodiscard]] inline std::optional<astraea::execution::GuestWorkerStop>
run_owned_two_elf_worker(
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
    const long raw_page = ::sysconf(_SC_PAGESIZE);
    if (raw_page < 4096) return std::nullopt;
    const auto page = static_cast<std::uint64_t>(raw_page);
    if (page > (std::numeric_limits<std::size_t>::max() - 16U) / 5U)
        return std::nullopt;
    if (!owned_two_elf_bundle_matches_source(sealed_bundle))
        return std::nullopt;

    const auto code = detail::read_le64(sealed_bundle, 8U);
    const auto data = code + page;
    const auto gate = code + 3U * page;
    const auto provider_address = code + 4U * page;
    const auto got = data + 0x40U;
    auto stack = detail::stack_request(code, page);
    if (!stack.has_value()) return std::nullopt;

    const auto client_size = static_cast<std::size_t>(page * 3U);
    const auto provider_size = static_cast<std::size_t>(page * 2U);
    const auto client_blob = sealed_bundle.subspan(16U, client_size);
    const auto provider_blob = sealed_bundle.subspan(
        16U + client_size, provider_size);

    std::vector<std::byte> client_bytes(
        client_blob.begin(), client_blob.end());
    std::vector<std::byte> provider_bytes(
        provider_blob.begin(), provider_blob.end());

    auto client = loader::build_guest_image({
        .image_bytes = std::move(client_bytes),
        .initial_stack = stack.value(),
        .elf_profile = loader::ElfParseProfile::ps5_sce,
    });
    auto provider = loader::build_guest_image({
        .image_bytes = std::move(provider_bytes),
        .initial_stack = stack.value(),
        .elf_profile = loader::ElfParseProfile::ps5_sce,
    });
    if (!client.has_value() || !provider.has_value() ||
        !client->dynamic_symbols.has_value() ||
        !client->dynamic_strings.has_value() ||
        !client->dynamic_strings->string_table.has_value() ||
        !client->plt_relocations.has_value() ||
        provider->mappings.size() != 1U ||
        provider->elf.header.type != 0xfe18U)
        return std::nullopt;

    auto view = client->initialized_image_view();
    if (!view.has_value()) return std::nullopt;
    auto symbol = loader::materialize_sce_dynamic_symbol(
        *client->dynamic_symbols,
        *client->dynamic_strings->string_table, 1U, view.value());
    auto relocation = loader::parse_dynamic_relocation(
        *client->plt_relocations, 0U,
        *client->dynamic_symbols, view.value());
    if (!symbol.has_value() || !relocation.has_value() ||
        !symbol->name.identity.has_value() ||
        relocation->target != memory::GuestAddress{got})
        return std::nullopt;

    const std::array modules{
        execution::ModuleGraphDeclaration{
            .module_key = "client",
            .dependency_keys = {"provider"},
            .exports = {},
        },
        execution::ModuleGraphDeclaration{
            .module_key = "provider",
            .dependency_keys = {},
            .exports = {
                execution::ModuleGraphExport{
                    .identity = symbol->name.identity.value(),
                    .guest_address = memory::GuestAddress{provider_address},
                },
            },
        },
    };
    auto graph = execution::ModuleGraph::create(modules);
    if (!graph.has_value()) return std::nullopt;
    auto plan = execution::plan_module_graph_import(
        relocation.value(), symbol.value(),
        graph.value(), "client", "provider");
    if (!plan.has_value()) return std::nullopt;
    auto patch = execution::build_owned_x86_64_module_import_patch(
        plan.value());
    if (!patch.has_value()) return std::nullopt;

    // Stage ONLY the independently parsed provider's disjoint RX mapping.
    // The composite image is test-only, and has no retail loader entrypoint.
    auto mapping = provider->mappings.front();
    if (mapping.range.base() != memory::GuestAddress{provider_address} ||
        mapping.backing.kind != memory::MappingBackingKind::file ||
        mapping.range.overlaps(client->initial_stack.storage))
        return std::nullopt;
    for (const auto& existing : client->mappings)
        if (mapping.range.overlaps(existing.range))
            return std::nullopt;
    const auto offset = client->image_bytes.size();
    if (offset > std::numeric_limits<std::uint64_t>::max() -
            mapping.backing.file_offset)
        return std::nullopt;
    mapping.backing.file_offset += static_cast<std::uint64_t>(offset);
    mapping.source_index += client->elf.program_headers.size();
    client->image_bytes.insert(
        client->image_bytes.end(),
        provider->image_bytes.begin(), provider->image_bytes.end());
    client->mappings.push_back(mapping);
    auto staged = client->initialized_image_view();
    if (!staged.has_value()) return std::nullopt;

    auto registry = execution::HleRegistry::create({
        execution::HleFunctionDescriptor{
            .id = execution::kSyntheticTestExitId,
            .canonical_name = "astraea.owned.exit",
            .argument_count = 1U,
        },
    });
    if (!registry.has_value()) return std::nullopt;
    auto gates = execution::build_synthetic_gate_region(
        registry.value(), memory::GuestAddress{gate}, 1U,
        {execution::GateBinding{
            .slot = 0U,
            .function_id = execution::kSyntheticTestExitId,
        }},
        client.value());
    if (!gates.has_value()) return std::nullopt;

    auto prepared = execution::prepare_linux_guest_memory(client.value());
    if (!prepared.has_value()) return std::nullopt;
    execution::GuestMemoryAccess guest_memory{
        client.value(), prepared.value()};
    if (!guest_memory.is_exact_executable_address(
            memory::GuestAddress{provider_address}))
        return std::nullopt;
    const std::array patches{patch.value()};
    if (!execution::apply_live_owned_jump_slot_batch(
            patches, guest_memory).has_value())
        return std::nullopt;
    std::array<std::byte, 8> got_bytes{};
    if (!guest_memory.read(
            memory::GuestAddress{got}, got_bytes).has_value())
        return std::nullopt;
    for (std::size_t i = 0; i < got_bytes.size(); ++i)
        if (got_bytes[i] != static_cast<std::byte>(
            (provider_address >> (8U * i)) & 0xffU))
            return std::nullopt;

    auto session = execution::run_linux_synthetic_session(
        client.value(), prepared.value(), registry.value(), gates.value(),
        execution::make_synthetic_initial_context(client.value()));
    if (!session.has_value() || session->exit_code != 42U ||
        session->gate_stop_count != 1U ||
        !session->output.empty() ||
        session->events.size() != 3U ||
        session->events[0].kind !=
            execution::SyntheticSessionEventKind::guest_entry ||
        session->events[1].kind !=
            execution::SyntheticSessionEventKind::gate_stop ||
        session->events[2].kind !=
            execution::SyntheticSessionEventKind::hle_exit ||
        session->events[2].value != 42U)
        return std::nullopt;

    // The protocol carries a real (ASLR-dependent) guest RIP, not a
    // fabricated fixed address. Controller-side comparisons normalize
    // away this address and retain exact typed reason and thread identity.
    const auto stop = execution::GuestWorkerStop{
        .worker_id = worker_id,
        .thread_id = thread_id,
        .reason = execution::GuestWorkerStopReason::normal_guest_return,
        .guest_rip = memory::GuestAddress{session->events[1].rip},
    };

    // Explicit teardown in the *supervised worker*: the source module
    // graph remains a declaration, but releasing prepared host mappings
    // must make the provider uncallable and its GOT unwritable. These
    // checks never dereference an unmapped guest pointer.
    prepared.value() = execution::LinuxPreparedMemory{};
    if (guest_memory.is_exact_executable_address(
            memory::GuestAddress{provider_address}) ||
        guest_memory.preflight_write(
            memory::GuestAddress{got}, 8U).has_value())
        return std::nullopt;
    auto after_release =
        execution::apply_live_owned_jump_slot_batch(
            patches, guest_memory);
    if (after_release.has_value() ||
        after_release.error().code !=
            execution::OwnedLiveJumpSlotErrorCode::
                provider_not_live_executable ||
        after_release.error().applied_count != 0U)
        return std::nullopt;

    return stop;
#endif
}

}  // namespace astraea::test
