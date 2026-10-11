// C1 exact-source-pinned Breakout pre-entry research only.
// One isolated scratch page (not a guest-VA mapping) stages one RELATIVE,
// then changes RW -> RX. No guest entry, GLOB_DAT binding, or HLE.
#include "owned_pair_sha256.hpp"

#include <astraea/execution/memory_plan.hpp>
#include <astraea/execution/owned_relative_patch.hpp>
#include <astraea/execution/sce_import_resolution.hpp>
#include <astraea/loader/sce_dynamic_symbol.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/mapping.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
namespace e = astraea::execution;
namespace l = astraea::loader;
namespace m = astraea::memory;

constexpr std::size_t kMaxElfBytes = 16U * 1024U * 1024U;
constexpr std::string_view kPinnedSha256 =
    "05c414993d1cd9e7182bbf5647be1ffad72c6ed6b6f5399d01af27281fef48b8";
constexpr std::uint64_t kStackBase = 0x0000700000000000ULL;
constexpr std::uint64_t kPageSize = 4096U;

[[nodiscard]] std::optional<std::vector<std::byte>> read_pinned_elf(
    const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return std::nullopt;
    const auto end = input.tellg();
    if (end <= 0 || end > static_cast<std::streamoff>(kMaxElfBytes))
        return std::nullopt;
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(bytes.size()) ||
        astraea::test::owned_pair_sha256_hex(bytes) != kPinnedSha256)
        return std::nullopt;
    return bytes;
}

[[nodiscard]] e::ExecutionMemoryPlanResult plan(
    const l::GuestImage& image,
    std::span<const m::MappingIntent> mappings) {
    return e::build_execution_memory_plan(e::ExecutionMemoryPlanRequest{
        .mappings = mappings,
        .stack_storage = image.initial_stack.storage,
        .entry_point = m::GuestAddress{image.elf.header.entry},
        .stack_pointer = image.initial_stack.rsp,
        .host_page_size = kPageSize,
    });
}

// No SCE-provider address exists yet. This proof stages the ONE externally
// observed RELATIVE record, then verifies ALL 19 GLOB_DAT targets remain
// original/unknown. Staging the imports would require verified addresses.
constexpr std::uint64_t kScratchGuestPage = 0x6000U;
constexpr std::uint64_t kScratchGuestLimit = kScratchGuestPage + kPageSize;
constexpr std::uint64_t kResearchLoadBias = 0x10000000U;

struct ScratchStagingResult {
    std::size_t initialized_fragments = 0U;
    std::size_t relative_patches = 0U;
    std::size_t unresolved_imports = 0U;
    std::size_t actual_sce_identity_refusals = 0U;
    std::size_t unverified_provider_refusals = 0U;
    bool post_freeze_write_refused = false;
};

[[nodiscard]] std::optional<ScratchStagingResult> stage_scratch_page(
    const l::GuestImage& image) {
    const auto refuse = [](std::string_view code)
        -> std::optional<ScratchStagingResult> {
        std::cerr << "scratch_stage_refused=" << code << '\n';
        return std::nullopt;
    };
    const auto view = image.initialized_image_view();
    if (!view.has_value() || !image.dynamic_symbols.has_value() ||
        !image.dynamic_strings.has_value() ||
        !image.dynamic_strings->string_table.has_value() ||
        !image.general_relocations.rela.has_value() ||
        image.general_relocations.rela->count != 20U ||
        image.dynamic_symbols->symbol_count != 20U)
        return refuse("image_or_relocations_unavailable");

    // Exercise the real production SCE import planner with ZERO registered
    // services. Breakout's bare NIDs do not carry Sony module/library IDs.
    // Do not manufacture a descriptor, provider, gate or return value.
    const auto hle = e::HleRegistry::create({});
    if (!hle.has_value()) return refuse("empty_hle_registry_failure");
    const auto bindings = e::SceImportBindingRegistry::create(
        hle.value(), std::span<const e::SceImportBinding>{});
    if (!bindings.has_value() || !bindings->bindings().empty())
        return refuse("empty_binding_registry_failure");
    std::array<bool, 20U> seen_symbol_indices{};


    // This is deliberately NOT a fixed guest address or a second alias.
    // The only host mapping starts RW/NX and can become RX/NW later.
    void* raw = ::mmap(nullptr, static_cast<std::size_t>(kPageSize),
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) return refuse("scratch_mmap");
    struct OwnedScratch {
        void* address;
        ~OwnedScratch() { (void)::munmap(address, kPageSize); }
    } scratch{raw};
    auto* bytes = static_cast<std::byte*>(raw);
    std::array<bool, static_cast<std::size_t>(kPageSize)> coverage{};
    ScratchStagingResult evidence{};

    // Copy initialized bytes by the REAL GuestImage backing intents, not
    // independent ELF offset arithmetic. The untouched gaps stay zero.
    for (const auto& mapping : image.mappings) {
        const std::uint64_t first = mapping.range.base().value();
        const std::uint64_t last = first + mapping.range.size().value();
        const std::uint64_t from = std::max(first, kScratchGuestPage);
        const std::uint64_t to = std::min(last, kScratchGuestLimit);
        if (from >= to) continue;
        const auto offset = static_cast<std::size_t>(from - kScratchGuestPage);
        const auto length = static_cast<std::size_t>(to - from);
        if (std::any_of(coverage.begin() + offset,
                        coverage.begin() + offset + length,
                        [](bool covered) { return covered; }))
            return refuse("overlapping_mapping_sources");
        const auto guest_extent = m::GuestRange::create(
            m::GuestAddress{from}, m::GuestSize{length});
        if (!guest_extent.has_value() ||
            !view->copy_bytes(guest_extent.value(),
                              std::span<std::byte>{bytes + offset, length})
                 .has_value())
            return refuse("initialized_source_mismatch");
        std::fill(coverage.begin() + offset,
                  coverage.begin() + offset + length, true);
        ++evidence.initialized_fragments;
    }
    if (evidence.initialized_fragments < 2U)
        return refuse("missing_shared_code_and_data");

    std::array<std::byte, static_cast<std::size_t>(kPageSize)> initial{};
    std::memcpy(initial.data(), bytes, initial.size());

    std::array<bool, static_cast<std::size_t>(kPageSize)> relocation_ranges{};
    std::optional<e::OwnedRelativePatch> relative_patch;
    for (std::uint64_t i = 0; i < 20U; ++i) {
        const auto record = l::parse_dynamic_relocation(
            image.general_relocations.rela.value(), i,
            image.dynamic_symbols.value(), view.value());
        if (!record.has_value()) return refuse("relocation_parse");
        const std::uint64_t target = record->target.value();
        if (target < kScratchGuestPage ||
            target > kScratchGuestLimit - 8U)
            return refuse("relocation_outside_scratch");
        const auto off = static_cast<std::size_t>(
            target - kScratchGuestPage);
        for (std::size_t j = off; j < off + 8U; ++j) {
            if (!coverage[j] || relocation_ranges[j])
                return refuse("relocation_unmapped_or_overlap");
            relocation_ranges[j] = true;
        }
        const auto owner = std::count_if(
            image.mappings.begin(), image.mappings.end(),
            [&](const auto& mapping) {
                return mapping.range.contains(record->target) &&
                    mapping.range.base().value() +
                    mapping.range.size().value() >= target + 8U &&
                    mapping.permissions.has(m::GuestPermission::write) &&
                    !mapping.permissions.has(m::GuestPermission::execute);
            });
        if (owner != 1) return refuse("relocation_owner_not_unique_writable");
        if (record->table_kind != l::RelocationTableKind::rela)
            return refuse("unexpected_relocation_table");
        if (record->relocation_type == 8U &&
            record->symbol_index == 0U) {
            if (relative_patch.has_value())
                return refuse("duplicate_relative");
            auto patch = e::build_owned_x86_64_relative_patch(
                record.value(), m::GuestAddress{kResearchLoadBias});
            if (!patch.has_value())
                return refuse("relative_patch_invalid");
            relative_patch = patch.value();
            ++evidence.relative_patches;
        } else if (record->relocation_type == 6U &&
                   record->symbol_index != 0U) {
            if (record->symbol_index >= seen_symbol_indices.size() ||
                seen_symbol_indices[record->symbol_index])
                return refuse("invalid_duplicate_import_symbol_index");
            seen_symbol_indices[record->symbol_index] = true;

            const auto symbol = l::materialize_sce_dynamic_symbol(
                image.dynamic_symbols.value(),
                image.dynamic_strings->string_table.value(),
                record->symbol_index, view.value());
            if (!symbol.has_value() || symbol->symbol.index !=
                    record->symbol_index ||
                symbol->symbol.section_index_raw != 0U ||
                symbol->symbol.type() != 2U ||
                symbol->name.raw.size() != 11U ||
                symbol->name.identity.has_value())
                return refuse("actual_bare_nid_identity_invalid");

            const auto unresolved = e::plan_sce_import_resolution(
                record.value(), symbol.value(), bindings.value());
            if (unresolved.has_value() ||
                unresolved.error().code !=
                    e::SceImportResolutionPlanErrorCode::missing_sce_identity ||
                unresolved.error().relocation_symbol_index !=
                    record->symbol_index)
                return refuse("bare_nid_accepted_as_sce_provider");
            ++evidence.actual_sce_identity_refusals;

            // A caller must not be able to invent a provider and gain a
            // service simply by supplying a syntactically complete ID.
            // This *local negative* is not a discovered Sony association.
            if (evidence.unverified_provider_refusals == 0U) {
                auto invented = symbol.value();
                invented.name.identity = l::SceSymbolIdentity{
                    .nid = invented.name.raw,
                    .library_id = "unverified-library",
                    .module_id = "unverified-module",
                };
                const auto absent_binding = e::plan_sce_import_resolution(
                    record.value(), invented, bindings.value());
                if (absent_binding.has_value() ||
                    absent_binding.error().code !=
                        e::SceImportResolutionPlanErrorCode::unresolved_import)
                    return refuse("unverified_provider_binding_accepted");
                ++evidence.unverified_provider_refusals;
            }
            ++evidence.unresolved_imports;
            // No placeholder gate, guessed Sony export or host callback.
        } else {
            return refuse("unexpected_relocation_kind");
        }
    }
    if (evidence.relative_patches != 1U ||
        evidence.unresolved_imports != 19U ||
        evidence.actual_sce_identity_refusals != 19U ||
        evidence.unverified_provider_refusals != 1U ||
        std::any_of(seen_symbol_indices.begin() + 1U,
                    seen_symbol_indices.end(),
                    [](bool seen) { return !seen; }) ||
        seen_symbol_indices[0] ||
        !relative_patch.has_value() ||
        relative_patch->target.value() != 0x6900U)
        return refuse("pinned_relocation_census_drift");

    const auto relative_offset = static_cast<std::size_t>(
        relative_patch->target.value() - kScratchGuestPage);
    std::memcpy(bytes + relative_offset,
                relative_patch->bytes.data(),
                relative_patch->bytes.size());
    if (std::memcmp(bytes + relative_offset,
                    relative_patch->bytes.data(),
                    relative_patch->bytes.size()) != 0)
        return refuse("relative_readback_mismatch");
    // Byte-for-byte prove no other source byte (including all 19 unresolved
    // GOT locations) has been changed by this research-only staging.
    for (std::size_t i = 0U; i < initial.size(); ++i) {
        if (i >= relative_offset &&
            i < relative_offset + relative_patch->bytes.size()) continue;
        if (bytes[i] != initial[i])
            return refuse("unresolved_import_or_code_changed");
    }

    if (::mprotect(raw, static_cast<std::size_t>(kPageSize),
                   PROT_READ | PROT_EXEC) != 0)
        return refuse("rw_to_rx_freeze");
    if (std::memcmp(bytes + relative_offset,
                    relative_patch->bytes.data(),
                    relative_patch->bytes.size()) != 0)
        return refuse("post_freeze_readback");

    // A disposable fork is used ONLY to test a write fault on the frozen
    // page. The child never executes title bytes and has no controller IPC.
    const pid_t pid = ::fork();
    if (pid < 0) return refuse("write_negative_fork");
    if (pid == 0) {
        const rlimit no_core{0U, 0U};
        (void)::setrlimit(RLIMIT_CORE, &no_core);
        auto* target = reinterpret_cast<volatile unsigned char*>(raw);
        target[relative_offset] = static_cast<unsigned char>(0x5a);
        ::_exit(90); // An unexpectedly allowed write is failure.
    }
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited != pid || !WIFSIGNALED(status) ||
        (WTERMSIG(status) != SIGSEGV && WTERMSIG(status) != SIGBUS))
        return refuse("post_freeze_guest_style_write_not_refused");
    evidence.post_freeze_write_refused = true;
    return evidence;
}

[[nodiscard]] int inspect(const char* path) {
    auto bytes = read_pinned_elf(path);
    if (!bytes.has_value()) return 2;

    const auto stack = m::GuestRange::create(
        m::GuestAddress{kStackBase}, m::GuestSize{kPageSize});
    if (!stack.has_value()) return 3;

    const auto image = l::build_guest_image(l::GuestImageRequest{
        .image_bytes = std::move(bytes.value()),
        .initial_stack = l::InitialStackRequest{
            .storage = stack.value(),
            .arguments = {"astraea-c1-static-breakout-memory-plan"},
            .environment = {},
            .auxiliary_vector = {},
        },
        // The pinned external demo is a PS5-marked ET_DYN, NOT a Sony
        // ET_SCE_DYNEXEC. Reuse the existing structural-only raw profile.
        .elf_profile = l::ElfParseProfile::ps5_marked_raw,
    });
    if (!image.has_value()) {
        std::cerr << "actual_guestimage_build_failure="
                  << static_cast<unsigned>(image.error().code) << '\n';
        return 4;
    }
    // One PT_LOAD may be split into initialized-file and zero-fill intents.
    // Check three actual ELF load headers, not a guessed mapping count.
    const auto loads = std::count_if(
        image->elf.program_headers.begin(), image->elf.program_headers.end(),
        [](const auto& header) { return header.type == 1U; });
    const auto rwx_loads = std::count_if(
        image->elf.program_headers.begin(), image->elf.program_headers.end(),
        [](const auto& header) {
            return header.type == 1U &&
                (header.flags & 0x3U) == 0x3U;
        });
    if (loads != 3 || rwx_loads != 1 ||
        image->mappings.size() < 3U || image->elf.header.entry == 0U)
        return 5;

    std::size_t executable_count = 0U;
    std::size_t source_rwx_count = 0U;
    bool entry_in_source_rwx = false;
    std::optional<std::uint64_t> source_rwx_first_page;
    const auto entry = m::GuestAddress{image->elf.header.entry};
    for (const auto& mapping : image->mappings) {
        const bool write = mapping.permissions.has(m::GuestPermission::write);
        const bool execute = mapping.permissions.has(m::GuestPermission::execute);
        executable_count += execute ? 1U : 0U;
        source_rwx_count += write && execute ? 1U : 0U;
        if (mapping.range.contains(entry) && write && execute)
            entry_in_source_rwx = true;
        if (write && execute) {
            const auto first_page =
                mapping.range.base().value() & ~(kPageSize - 1U);
            source_rwx_first_page = source_rwx_first_page.has_value()
                ? std::min(first_page, source_rwx_first_page.value())
                : first_page;
        }
    }
    if (source_rwx_count == 0U || executable_count != source_rwx_count ||
        !entry_in_source_rwx) return 6;

    const auto source = plan(image.value(), image->mappings);
    if (source.has_value() ||
        source.error().code != e::ExecutionPlanErrorCode::mixed_write_execute_page ||
        !source.error().guest_address.has_value())
        return 7;
    const auto refused_address = source.error().guest_address.value();
    if (!source_rwx_first_page.has_value() ||
        refused_address.value() != source_rwx_first_page.value())
        return 8;

    auto research_candidate = image->mappings;
    std::size_t narrowed_count = 0U;
    for (auto& mapping : research_candidate) {
        if (!mapping.permissions.has(m::GuestPermission::execute))
            continue;
        if (!mapping.permissions.has(m::GuestPermission::write))
            return 9;
        const auto narrowed = m::GuestPermissions::checked_from_bits(
            mapping.permissions.bits() &
            static_cast<std::uint8_t>(
                ~static_cast<std::uint8_t>(m::GuestPermission::write)));
        if (!narrowed.has_value()) return 10;
        mapping.permissions = narrowed.value();
        ++narrowed_count;
    }
    if (narrowed_count != source_rwx_count) return 11;

    const auto candidate = plan(image.value(), research_candidate);
    if (!candidate.has_value() &&
        candidate.error().code != e::ExecutionPlanErrorCode::mixed_write_execute_page) {
        std::cerr << "research_candidate_plan_failure="
                  << static_cast<unsigned>(candidate.error().code) << '\n';
        return 12;
    }
    // Distinguish a still-RWX source load from a host-page conflict
    // between *different* RX and RW mapping intents after narrowing.
    // This check never prepares, maps or modifies actual native memory.
    std::optional<std::uint64_t> candidate_mixed_host_page;
    std::optional<std::size_t> rx_source_index;
    std::optional<std::size_t> rw_source_index;
    if (!candidate.has_value()) {
        if (!candidate.error().guest_address.has_value()) return 16;
        const auto mixed = candidate.error().guest_address->value();
        if (mixed % kPageSize != 0U) return 17;
        candidate_mixed_host_page = mixed;
        for (const auto& mapping : research_candidate) {
            if (mapping.range.empty()) continue;
            const auto first = mapping.range.base().value() / kPageSize;
            const auto last = (mapping.range.base().value() +
                               mapping.range.size().value() - 1U) / kPageSize;
            if (mixed / kPageSize < first || mixed / kPageSize > last)
                continue;
            if (mapping.permissions.has(m::GuestPermission::execute))
                rx_source_index = mapping.source_index;
            if (mapping.permissions.has(m::GuestPermission::write))
                rw_source_index = mapping.source_index;
        }
        if (!rx_source_index.has_value() || !rw_source_index.has_value() ||
            rx_source_index.value() == rw_source_index.value())
            return 18;
    }
    std::size_t executable_regions = 0U;
    if (candidate.has_value()) {
        for (const auto& region : candidate->regions) {
            const bool write = region.permissions.has(m::GuestPermission::write);
            const bool execute = region.permissions.has(m::GuestPermission::execute);
            if (write && execute) return 13;
            executable_regions += execute ? 1U : 0U;
        }
        if (executable_regions == 0U) return 14;
    }
    // The experiment is a copy of mapping descriptors, not a change to
    // ELF bytes, GuestImage permissions, or native backend policy.
    if (research_candidate == image->mappings) return 15;

    auto staged = stage_scratch_page(image.value());
    if (!staged.has_value() || staged->relative_patches != 1U ||
        staged->unresolved_imports != 19U ||
        !staged->post_freeze_write_refused)
        return 19;

    std::cout << "PINNED BREAKOUT ACTUAL MEMORY PLAN PREFLIGHT PASS\n"
              << "source_sha256=" << kPinnedSha256 << '\n'
              << "guestimage_load_segments=" << loads << '\n'
              << "guestimage_mapping_fragments=" << image->mappings.size() << '\n'
              << "source_rwx_executable_loads=" << rwx_loads << '\n'
              << "source_rwx_mapping_fragments=" << source_rwx_count << '\n'
              << "source_plan=mixed_write_execute_page\n"
              << "research_narrowed_mapping_count=" << narrowed_count << '\n'
              << "research_narrowed_plan="
              << (candidate.has_value() ? "accepted_no_host_rwx"
                                        : "blocked_mixed_write_execute_page")
              << '\n'
              << "research_narrowed_host_page_conflict="
              << (candidate_mixed_host_page.has_value()
                      ? "rx_rw_distinct_loads"
                      : "none")
              << '\n'
              << "research_narrowed_mixed_page=";
    if (candidate_mixed_host_page.has_value()) {
        std::cout << "0x" << std::hex << candidate_mixed_host_page.value()
                  << std::dec << '\n'
                  << "research_rx_source_ph_index=" << rx_source_index.value()
                  << '\n'
                  << "research_rw_source_ph_index=" << rw_source_index.value()
                  << '\n';
    } else {
        std::cout << "none\n";
    }
    std::cout
              << "original_guest_permissions=unchanged\n"
              << "elf_bytes=unchanged\n"
              << "native_guest_memory_mapped=0\n"
              << "scratch_page_rw_to_rx=verified\n"
              << "scratch_relative_patches=" << staged->relative_patches << '\n'
              << "scratch_unresolved_imports=" << staged->unresolved_imports << '\n'
              << "real_sce_planner_bare_nid_refusals="
              << staged->actual_sce_identity_refusals << '\n'
              << "unverified_provider_binding_refusals="
              << staged->unverified_provider_refusals << '\n'
              << "registered_sony_provider_bindings=0\n"
              << "synthetic_import_gate_patches=0\n"
              << "import_call_observed=0\n"
              << "scratch_postfreeze_write=denied_by_kernel\n"
              << "guest_relocations_applied=0\n"
              << "imports_resolved=0\n"
              << "guest_instructions=0\n"
              << "guest_write_semantics_preserved=not_proven\n"
              << "security_admission=not_granted\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    return inspect(argv[1]);
}
