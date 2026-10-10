#pragma once

// Linux-only, test-owned exact-source dynamic ELF client/provider pair.
// The inputs are independently linked with GNU ld; the existing Astraea
// parser, ModuleGraph, checked memory and supervised guest runtime own
// validation/execution. This is NOT retail entry or a Sony process ABI.
#include "owned_two_elf_worker_fixture.hpp"

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)
#include "owned_linked_pair_bytes.hpp"
#endif

namespace astraea::test {
namespace detail {

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

constexpr std::array<std::byte, 8> kLinkedPairMagic{
    std::byte{'A'}, std::byte{'S'}, std::byte{'T'}, std::byte{'L'},
    std::byte{'P'}, std::byte{'A'}, std::byte{'I'}, std::byte{'1'},
};

struct LinkedPair {
    std::vector<std::byte> client;
    std::vector<std::byte> provider;
};

[[nodiscard]] inline std::optional<std::uint64_t> free_linked_pair_base(
    std::uint64_t page) {
    if (page != 4096U) return std::nullopt;
    auto* region = ::mmap(nullptr, static_cast<std::size_t>(12U * page),
        PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (region == MAP_FAILED) return std::nullopt;
    const auto base = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(region));
    if (::munmap(region, static_cast<std::size_t>(12U * page)) != 0 ||
        base == 0U || base % page != 0U ||
        base > std::numeric_limits<std::uint64_t>::max() - 12U * page)
        return std::nullopt;
    return base;
}

template <std::size_t N>
[[nodiscard]] inline bool rebase_linked_fields(
    std::vector<std::byte>& data,
    const std::array<std::size_t, N>& offsets,
    std::uint64_t base) {
    for (const auto offset : offsets) {
        if (offset > data.size() || data.size() - offset < sizeof(std::uint64_t))
            return false;
        const auto old_value = read_le64(data, offset);
        if (old_value > std::numeric_limits<std::uint64_t>::max() - base)
            return false;
        write_le<std::uint64_t>(data, offset, old_value + base);
    }
    return true;
}

[[nodiscard]] inline std::optional<LinkedPair> make_runtime_linked_pair(
    std::uint64_t base) {
    constexpr std::uint64_t page = 4096U;
    if (base == 0U || base % page != 0U ||
        base > std::numeric_limits<std::uint64_t>::max() - 12U * page)
        return std::nullopt;
    const auto provider_base = base + 4U * page;
    const auto gate = base + 8U * page;
    std::vector<std::byte> client{kClientElf.begin(), kClientElf.end()};
    std::vector<std::byte> provider{kProviderElf.begin(), kProviderElf.end()};
    if (!rebase_linked_fields(client, kClientRelocationFields, base) ||
        !rebase_linked_fields(provider, kProviderRelocationFields, provider_base) ||
        kProviderGateImmediateOffset > provider.size() ||
        provider.size() - kProviderGateImmediateOffset < 8U)
        return std::nullopt;
    write_le<std::uint64_t>(provider, kProviderGateImmediateOffset, gate);
    return LinkedPair{
        .client = std::move(client),
        .provider = std::move(provider),
    };
}

#endif
} // namespace detail

[[nodiscard]] inline std::optional<std::vector<std::byte>>
make_owned_linked_pair_sealed_bundle() {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    return std::nullopt;
#else
    if (::sysconf(_SC_PAGESIZE) != 4096) return std::nullopt;
    auto base = detail::free_linked_pair_base(4096U);
    if (!base.has_value()) return std::nullopt;
    auto pair = detail::make_runtime_linked_pair(base.value());
    if (!pair.has_value()) return std::nullopt;
    std::vector<std::byte> bundle;
    bundle.reserve(16U + pair->client.size() + pair->provider.size());
    bundle.insert(bundle.end(), detail::kLinkedPairMagic.begin(),
        detail::kLinkedPairMagic.end());
    for (std::size_t i = 0; i < 8U; ++i)
        bundle.push_back(static_cast<std::byte>(
            (base.value() >> (i * 8U)) & 0xffU));
    bundle.insert(bundle.end(), pair->client.begin(), pair->client.end());
    bundle.insert(bundle.end(), pair->provider.begin(), pair->provider.end());
    return bundle;
#endif
}

[[nodiscard]] inline bool owned_linked_pair_bundle_matches_source(
    std::span<const std::byte> bundle) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    (void)bundle;
    return false;
#else
    if (::sysconf(_SC_PAGESIZE) != 4096 ||
        bundle.size() != 16U + detail::kClientElf.size() +
            detail::kProviderElf.size() ||
        !std::equal(detail::kLinkedPairMagic.begin(),
            detail::kLinkedPairMagic.end(), bundle.begin()))
        return false;
    const auto base = detail::read_le64(bundle, 8U);
    auto expected = detail::make_runtime_linked_pair(base);
    return expected.has_value() &&
        std::equal(expected->client.begin(), expected->client.end(),
            bundle.begin() + 16U) &&
        std::equal(expected->provider.begin(), expected->provider.end(),
            bundle.begin() + 16U + detail::kClientElf.size());
#endif
}

[[nodiscard]] inline std::optional<astraea::execution::GuestWorkerStop>
run_owned_linked_pair_worker(
    astraea::execution::GuestWorkerId worker_id,
    astraea::execution::GuestThreadId thread_id,
    std::span<const std::byte> bundle) {
#if !(defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE))
    (void)worker_id;
    (void)thread_id;
    (void)bundle;
    return std::nullopt;
#else
    using namespace astraea;
    constexpr std::uint64_t page = 4096U;
    if (!owned_linked_pair_bundle_matches_source(bundle))
        return std::nullopt;
    const auto base = detail::read_le64(bundle, 8U);
    const auto provider_base = base + 4U * page;
    const auto gate = base + 8U * page;
    const auto provider_entry = provider_base + detail::kProviderEntryOffset;
    const auto got = base + detail::kClientGotOffset;
    auto range = memory::GuestRange::create(
        memory::GuestAddress{base + 10U * page},
        memory::GuestSize{page});
    if (!range.has_value()) return std::nullopt;
    loader::InitialStackRequest stack{
        .storage = range.value(),
        .arguments = {"astraea-owned-linked-pair"},
        .environment = {},
        .auxiliary_vector = {},
    };
    const auto client_blob = bundle.subspan(16U, detail::kClientElf.size());
    const auto provider_blob = bundle.subspan(
        16U + detail::kClientElf.size(), detail::kProviderElf.size());
    auto client = loader::build_guest_image({
        .image_bytes = {client_blob.begin(), client_blob.end()},
        .initial_stack = stack,
        .elf_profile = loader::ElfParseProfile::generic,
    });
    auto provider = loader::build_guest_image({
        .image_bytes = {provider_blob.begin(), provider_blob.end()},
        .initial_stack = stack,
        .elf_profile = loader::ElfParseProfile::generic,
    });
    if (!client.has_value() || !provider.has_value() ||
        !client->dynamic_symbols.has_value() ||
        !client->dynamic_strings.has_value() ||
        !client->dynamic_strings->string_table.has_value() ||
        !client->plt_relocations.has_value() ||
        !provider->dynamic_symbols.has_value() ||
        !provider->dynamic_strings.has_value() ||
        !provider->dynamic_strings->string_table.has_value() ||
        client->elf.header.entry != base + detail::kClientEntryOffset ||
        client->elf.header.type != 3U || provider->elf.header.type != 3U ||
        client->mappings.size() != 3U || provider->mappings.size() != 3U)
        return std::nullopt;
    const auto cv = client->initialized_image_view();
    const auto pv = provider->initialized_image_view();
    if (!cv.has_value() || !pv.has_value()) return std::nullopt;
    auto symbol = loader::materialize_sce_dynamic_symbol(
        *client->dynamic_symbols,
        *client->dynamic_strings->string_table,
        detail::kClientImportSymbolIndex, cv.value());
    auto definition = loader::materialize_sce_dynamic_symbol(
        *provider->dynamic_symbols,
        *provider->dynamic_strings->string_table,
        detail::kProviderExportSymbolIndex, pv.value());
    auto relocation = loader::parse_dynamic_relocation(
        *client->plt_relocations, 0U, *client->dynamic_symbols, cv.value());
    if (!symbol.has_value() || !definition.has_value() ||
        !relocation.has_value() || !symbol->name.identity.has_value() ||
        !definition->name.identity.has_value() ||
        symbol->symbol.section_index_raw != 0U ||
        definition->symbol.section_index_raw == 0U ||
        definition->symbol.value != provider_entry ||
        symbol->name.identity.value() != definition->name.identity.value() ||
        relocation->target != memory::GuestAddress{got} ||
        relocation->relocation_type != 7U)
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
                    .identity = definition->name.identity.value(),
                    .guest_address = memory::GuestAddress{provider_entry},
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

    // Preserve each independently parsed load segment and file offset;
    // combine only for the already-existing Linux GuestImage memory mapper.
    const auto original_size = client->image_bytes.size();
    for (auto mapping : provider->mappings) {
        if (mapping.backing.kind != memory::MappingBackingKind::file ||
            mapping.range.overlaps(client->initial_stack.storage))
            return std::nullopt;
        for (const auto& existing : client->mappings)
            if (mapping.range.overlaps(existing.range))
                return std::nullopt;
        if (mapping.backing.file_offset >
            std::numeric_limits<std::uint64_t>::max() - original_size)
            return std::nullopt;
        mapping.backing.file_offset +=
            static_cast<std::uint64_t>(original_size);
        mapping.source_index += client->elf.program_headers.size();
        client->mappings.push_back(mapping);
    }
    client->image_bytes.insert(client->image_bytes.end(),
        provider->image_bytes.begin(), provider->image_bytes.end());
    if (!client->initialized_image_view().has_value())
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
        registry.value(), memory::GuestAddress{gate}, 1U,
        {execution::GateBinding{
            .slot = 0U,
            .function_id = execution::kSyntheticTestExitId,
        }}, client.value());
    if (!gates.has_value()) return std::nullopt;
    auto prepared = execution::prepare_linux_guest_memory(client.value());
    if (!prepared.has_value()) return std::nullopt;
    execution::GuestMemoryAccess guest_memory{client.value(), prepared.value()};
    if (!guest_memory.is_exact_executable_address(
            memory::GuestAddress{provider_entry}))
        return std::nullopt;
    execution::OwnedProviderRegistry lifetime;
    auto issued = lifetime.register_provider(
        "provider", memory::GuestAddress{provider_entry}, guest_memory);
    if (!issued.has_value()) return std::nullopt;
    const std::array patches{patch.value()};
    const std::array generations{issued.value()};
    if (!execution::apply_generation_bound_owned_jump_slot_batch(
            patches, generations, lifetime, guest_memory).has_value())
        return std::nullopt;
    std::array<std::byte, 8U> got_bytes{};
    if (!guest_memory.read(memory::GuestAddress{got}, got_bytes).has_value())
        return std::nullopt;
    for (std::size_t i = 0; i < got_bytes.size(); ++i)
        if (got_bytes[i] != static_cast<std::byte>(
            (provider_entry >> (8U * i)) & 0xffU))
            return std::nullopt;

    auto run = execution::run_linux_synthetic_session(
        client.value(), prepared.value(), registry.value(), gates.value(),
        execution::make_synthetic_initial_context(client.value()));
    if (!run.has_value() || run->exit_code != 42U ||
        run->gate_stop_count != 1U || !run->output.empty() ||
        run->events.size() != 3U ||
        run->events[0].kind !=
            execution::SyntheticSessionEventKind::guest_entry ||
        run->events[1].kind !=
            execution::SyntheticSessionEventKind::gate_stop ||
        run->events[2].kind !=
            execution::SyntheticSessionEventKind::hle_exit ||
        run->events[2].value != 42U)
        return std::nullopt;

    const auto stop = execution::GuestWorkerStop{
        .worker_id = worker_id,
        .thread_id = thread_id,
        .reason = execution::GuestWorkerStopReason::normal_guest_return,
        .guest_rip = memory::GuestAddress{run->events[1].rip},
    };

    if (!lifetime.retire(issued.value()))
        return std::nullopt;
    const auto stale = execution::apply_generation_bound_owned_jump_slot_batch(
        patches, generations, lifetime, guest_memory);
    if (stale.has_value() ||
        stale.error().code !=
            execution::OwnedBoundJumpSlotErrorCode::
                stale_or_foreign_provider_generation ||
        stale.error().applied_count != 0U)
        return std::nullopt;
    prepared.value() = execution::LinuxPreparedMemory{};
    if (guest_memory.is_exact_executable_address(
        memory::GuestAddress{provider_entry}))
        return std::nullopt;
    return stop;
#endif
}

} // namespace astraea::test
