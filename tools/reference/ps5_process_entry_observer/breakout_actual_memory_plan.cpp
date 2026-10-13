// C1 exact-source-pinned Breakout pre-entry research only.
// A scratch W^X proof is extended to *actual temporary guest-VA mappings*
// with one pre-staged RELATIVE. No guest entry, GLOB_DAT binding, or HLE.
// Deliberate page permission narrowing is NOT faithful writable-code support.
#include "owned_pair_sha256.hpp"
#include "breakout_offline_entry_interpreter.hpp"
#include "source_x86_64_decoder.hpp"
#include "breakout_owned_callee_execution.hpp"

#include <Zydis/Mnemonic.h>

#include <astraea/execution/memory_plan.hpp>
#include <astraea/execution/linux_memory.hpp>
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

// This is the FIRST selected-ELF proof that uses the actual Linux guest
// memory backend, not just the scratch allocator. It is strictly pre-entry.
// The returned count is evidence that temporary guest VA mappings were
// materialized and checked; all mapped regions are RAII-released on return.
// The source image and initial stack, as well as native production policies,
// are unchanged. The transient mixed page is *read-only* even where guest
// source PH1 specified RW, so it cannot be promoted as general compatibility.
struct TemporaryGuestMemoryEvidence {
    std::size_t source_mapping_fragments = 0U;
    std::size_t transient_mapping_fragments = 0U;
    std::size_t restricted_collision_fragments = 0U;
    std::size_t restricted_source_write_bytes = 0U;
    std::size_t mapped_shared_page_compared_bytes = 0U;
    std::size_t original_imports_unchanged = 0U;
    std::size_t staged_unsupported_imports = 0U;
    std::size_t synthetic_trap_page_compared_bytes = 0U;
    bool prepared_guest_page_write_denied = false;
};

[[nodiscard]] std::optional<TemporaryGuestMemoryEvidence>
prepare_temporary_breakout_guest_memory(
    const l::GuestImage& original, bool stage_unsupported_import_stops) {
    auto fail = [](std::string_view code)
        -> std::optional<TemporaryGuestMemoryEvidence> {
        std::cerr << "transient_guest_memory_refused=" << code << '\n';
        return std::nullopt;
    };
    const auto view = original.initialized_image_view();
    if (!view.has_value() || !original.dynamic_symbols.has_value() ||
        !original.general_relocations.rela.has_value() ||
        original.general_relocations.rela->count != 20U ||
        ::sysconf(_SC_PAGESIZE) != static_cast<long>(kPageSize) ||
        !e::linux_native_memory_backend_available())
        return fail("image_native_requirements");

    constexpr std::uint64_t kReserveBytes = 32U * 1024U * 1024U;
    constexpr std::uint64_t kMaxGuestOffset = 16U * 1024U * 1024U;
    constexpr std::uint64_t kStopPageOffset = 16U * 1024U * 1024U;
    constexpr std::size_t kStopStride = 16U;
    constexpr std::size_t kImportCount = 19U;
    for (const auto& mapping : original.mappings) {
        const auto first = mapping.range.base().value();
        if (first >= kMaxGuestOffset ||
            mapping.range.size().value() > kMaxGuestOffset - first)
            return fail("unbounded_original_guest_mapping");
    }

    // Non-destructively discover a free, aligned 32-MiB window. The actual
    // backend uses MAP_FIXED_NOREPLACE, so address races fail closed.
    void* reserved = ::mmap(nullptr, kReserveBytes, PROT_NONE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reserved == MAP_FAILED) return fail("reserve_guest_window");
    const auto base = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(reserved));
    if (::munmap(reserved, kReserveBytes) != 0 ||
        base == 0U || base % kPageSize != 0U ||
        base > std::numeric_limits<std::uint64_t>::max() - kReserveBytes)
        return fail("invalid_guest_window");

    auto transient = original; // Own and alter a source-pinned *copy* only.
    TemporaryGuestMemoryEvidence result{};
    result.source_mapping_fragments = original.mappings.size();
    transient.mappings.clear();
    transient.mappings.reserve(original.mappings.size() * 3U);
    const auto page_rights = m::GuestPermissions::checked_from_bits(
        static_cast<std::uint8_t>(m::GuestPermission::read) |
        static_cast<std::uint8_t>(m::GuestPermission::execute));
    if (!page_rights.has_value()) return fail("unsupported_page_rights");

    for (const auto& source : original.mappings) {
        const auto start = source.range.base().value();
        const auto end = start + source.range.size().value();
        // Split at the actual 4-KiB collision-page boundaries, preserving
        // exact source-index, file backing, and byte offsets for each slice.
        std::array<std::uint64_t, 4U> boundaries{
            start, end, kScratchGuestPage, kScratchGuestLimit,
        };
        std::sort(boundaries.begin(), boundaries.end());
        for (std::size_t i = 0U; i + 1U < boundaries.size(); ++i) {
            const auto lo = std::max(start, boundaries[i]);
            const auto hi = std::min(end, boundaries[i + 1U]);
            if (lo >= hi) continue;
            const bool source_executable =
                source.permissions.has(m::GuestPermission::execute);
            const bool source_writable =
                source.permissions.has(m::GuestPermission::write);
            const bool collision = lo >= kScratchGuestPage &&
                hi <= kScratchGuestLimit;
            if (source_executable && !source_writable)
                return fail("unexpected_original_executable_rights");
            // The exact Breakout RWX PH0 becomes RX and the RW portion of
            // PH1 within host page 0x6000 becomes RX too. Outside that one
            // page the original nonexecutable RW mapping stays RW.
            const bool narrowed = source_executable ||
                (collision && source_writable);
            auto mapped = source;
            mapped.permissions = narrowed ? page_rights.value()
                                         : source.permissions;
            const auto shifted = m::GuestRange::create(
                m::GuestAddress{base + lo}, m::GuestSize{hi - lo});
            if (!shifted.has_value()) return fail("rebased_extent_invalid");
            mapped.range = shifted.value();
            mapped.backing.byte_count = m::GuestSize{hi - lo};
            if (mapped.backing.kind == m::MappingBackingKind::file) {
                if (mapped.backing.file_offset >
                    std::numeric_limits<std::uint64_t>::max() - (lo - start))
                    return fail("file_offset_overflow");
                mapped.backing.file_offset += lo - start;
            }
            if (narrowed && source_writable) {
                ++result.restricted_collision_fragments;
                result.restricted_source_write_bytes +=
                    static_cast<std::size_t>(hi - lo);
            }
            transient.mappings.push_back(std::move(mapped));
        }
    }
    std::sort(transient.mappings.begin(), transient.mappings.end(),
              m::mapping_intent_less);
    result.transient_mapping_fragments = transient.mappings.size();
    if (result.restricted_collision_fragments < 2U ||
        result.restricted_source_write_bytes == 0U)
        return fail("missing_source_permission_narrowing");

    // The selected RELATIVE patch is built with Astraea's own checked
    // relocation semantics and this explicit research load bias.
    std::optional<e::OwnedRelativePatch> relative;
    std::size_t imports = 0U;
    for (std::uint64_t i = 0U; i < 20U; ++i) {
        auto relocation = l::parse_dynamic_relocation(
            original.general_relocations.rela.value(), i,
            original.dynamic_symbols.value(), view.value());
        if (!relocation.has_value()) return fail("bad_source_relocation");
        if (relocation->relocation_type == 8U &&
            relocation->symbol_index == 0U) {
            if (relative.has_value() || relocation->target.value() != 0x6900U)
                return fail("relative_census_mismatch");
            auto patch = e::build_owned_x86_64_relative_patch(
                relocation.value(), m::GuestAddress{base});
            if (!patch.has_value()) return fail("relative_patch_rejected");
            relative = patch.value();
        } else if (relocation->relocation_type == 6U &&
                   relocation->symbol_index != 0U) {
            ++imports;
        } else {
            return fail("unexpected_relocation_kind");
        }
    }
    if (!relative.has_value() || imports != 19U)
        return fail("incomplete_relocation_census");
    const std::uint64_t target = relative->target.value();
    std::size_t source_owners = 0U;
    std::size_t file_offset = 0U;
    for (const auto& mapping : original.mappings) {
        if (mapping.range.base().value() <= target &&
            mapping.range.size().value() >=
                target - mapping.range.base().value() + 8U &&
            mapping.permissions.has(m::GuestPermission::write) &&
            !mapping.permissions.has(m::GuestPermission::execute) &&
            mapping.backing.kind == m::MappingBackingKind::file) {
            ++source_owners;
            file_offset = static_cast<std::size_t>(
                mapping.backing.file_offset +
                target - mapping.range.base().value());
        }
    }
    if (source_owners != 1U ||
        file_offset > transient.image_bytes.size() ||
        transient.image_bytes.size() - file_offset < relative->bytes.size())
        return fail("relative_target_backing_not_unique");
    std::copy(relative->bytes.begin(), relative->bytes.end(),
              transient.image_bytes.begin() +
                  static_cast<std::ptrdiff_t>(file_offset));

    // Optional, entirely test-owned stop staging. These are NOT Sony HLE
    // bindings, not provider identities, and not an execution admission.
    // Each raw NID receives a distinct UD2 destination so a *future*
    // authenticated runtime event can distinguish unsupported imports.
    // Stop-page filler is INT3 rather than executable host helper code.
    std::array<std::array<std::byte, 8U>, 20U> import_words{};
    std::array<std::uint64_t, 20U> import_targets{};
    std::array<bool, 20U> seen_imports{};
    if (stage_unsupported_import_stops) {
        if (!original.dynamic_strings.has_value() ||
            !original.dynamic_strings->string_table.has_value() ||
            transient.image_bytes.size() >
                std::numeric_limits<std::size_t>::max() -
                    static_cast<std::size_t>(kPageSize))
            return fail("unsupported_stop_source_preflight");

        const auto stop_file_offset = transient.image_bytes.size();
        transient.image_bytes.insert(
            transient.image_bytes.end(),
            static_cast<std::size_t>(kPageSize), std::byte{0xcc});
        std::size_t import_count = 0U;
        for (std::uint64_t i = 0U; i < 20U; ++i) {
            const auto relocation = l::parse_dynamic_relocation(
                original.general_relocations.rela.value(), i,
                original.dynamic_symbols.value(), view.value());
            if (!relocation.has_value()) return fail("stop_relocation_parse");
            if (relocation->relocation_type == 8U &&
                relocation->symbol_index == 0U) continue;
            const auto symbol_index = relocation->symbol_index;
            if (relocation->relocation_type != 6U ||
                symbol_index == 0U || symbol_index > kImportCount ||
                seen_imports[symbol_index])
                return fail("stop_import_identity_or_duplicate");
            const auto symbol = l::materialize_sce_dynamic_symbol(
                original.dynamic_symbols.value(),
                original.dynamic_strings->string_table.value(),
                symbol_index, view.value());
            if (!symbol.has_value() || symbol->symbol.index != symbol_index ||
                symbol->symbol.section_index_raw != 0U ||
                symbol->symbol.type() != 2U ||
                symbol->name.raw.size() != 11U ||
                symbol->name.identity.has_value())
                return fail("stop_missing_raw_nid_authority");

            const auto import_target = relocation->target.value();
            if (import_target < kScratchGuestPage ||
                import_target > kScratchGuestLimit - 8U ||
                (import_target <= 0x6907U && import_target + 7U >= 0x6900U))
                return fail("stop_target_outside_writable_import_extent");
            std::size_t owners = 0U;
            std::size_t target_file_offset = 0U;
            for (const auto& mapping : original.mappings) {
                if (mapping.range.base().value() > import_target ||
                    mapping.range.size().value() <
                        import_target - mapping.range.base().value() + 8U ||
                    !mapping.permissions.has(m::GuestPermission::write) ||
                    mapping.permissions.has(m::GuestPermission::execute) ||
                    mapping.backing.kind != m::MappingBackingKind::file)
                    continue;
                ++owners;
                target_file_offset = static_cast<std::size_t>(
                    mapping.backing.file_offset +
                    import_target - mapping.range.base().value());
            }
            if (owners != 1U ||
                target_file_offset > stop_file_offset ||
                stop_file_offset - target_file_offset < 8U)
                return fail("stop_target_file_backing_invalid");

            const auto stop_va = base + kStopPageOffset +
                static_cast<std::uint64_t>(symbol_index - 1U) * kStopStride;
            for (std::size_t byte = 0U; byte < 8U; ++byte) {
                import_words[symbol_index][byte] = static_cast<std::byte>(
                    (stop_va >> (8U * byte)) & 0xffU);
            }
            std::copy(
                import_words[symbol_index].begin(),
                import_words[symbol_index].end(),
                transient.image_bytes.begin() +
                    static_cast<std::ptrdiff_t>(target_file_offset));
            import_targets[symbol_index] = import_target;
            seen_imports[symbol_index] = true;
            ++import_count;

            const auto slot_offset = static_cast<std::size_t>(
                symbol_index - 1U) * kStopStride;
            transient.image_bytes[stop_file_offset + slot_offset] =
                std::byte{0x0f};
            transient.image_bytes[stop_file_offset + slot_offset + 1U] =
                std::byte{0x0b}; // UD2: unsupported, never a return stub
        }
        if (import_count != kImportCount ||
            std::any_of(seen_imports.begin() + 1U, seen_imports.end(),
                        [](bool present) { return !present; }))
            return fail("stop_incomplete_nid_census");

        const auto stop_range = m::GuestRange::create(
            m::GuestAddress{base + kStopPageOffset},
            m::GuestSize{kPageSize});
        if (!stop_range.has_value()) return fail("stop_range_invalid");
        transient.mappings.push_back(m::MappingIntent{
            .range = stop_range.value(),
            .permissions = page_rights.value(),
            .backing = m::MappingBacking{
                .kind = m::MappingBackingKind::file,
                .file_offset = static_cast<std::uint64_t>(stop_file_offset),
                .byte_count = m::GuestSize{kPageSize},
            },
            .source_index = original.elf.program_headers.size(),
        });
        std::sort(transient.mappings.begin(), transient.mappings.end(),
                  m::mapping_intent_less);
    }

    // This is never used to simulate the original PS5 process entry state.
    // Runtime-only VA rebasing preserves source bytes and file offsets.
    if (original.elf.header.entry >= kMaxGuestOffset)
        return fail("unbounded_entry");
    transient.elf.header.entry = base + original.elf.header.entry;

    // The real production planner and mapper are now exercised against the
    // transient actual GuestImage. No executable guest instruction is run.
    const auto planned = e::build_execution_memory_plan(
        e::ExecutionMemoryPlanRequest{
            .mappings = transient.mappings,
            .stack_storage = transient.initial_stack.storage,
            .entry_point = m::GuestAddress{transient.elf.header.entry},
            .stack_pointer = transient.initial_stack.rsp,
            .host_page_size = kPageSize,
        });
    if (!planned.has_value()) return fail("transient_plan_refused");
    for (const auto& region : planned->regions) {
        if (region.permissions.has(m::GuestPermission::execute) &&
            region.permissions.has(m::GuestPermission::write))
            return fail("transient_plan_rwx");
    }
    const auto prepared = e::prepare_linux_guest_memory(transient);
    if (!prepared.has_value()) {
        std::cerr << "transient_backend_error="
                  << static_cast<unsigned>(prepared.error().code) << '\n';
        return fail("actual_native_guest_prepare");
    }
    const auto guest_shared_page = reinterpret_cast<const std::byte*>(
        static_cast<std::uintptr_t>(base + kScratchGuestPage));
    for (std::uint64_t offset = 0U; offset < kPageSize; ++offset) {
        const std::uint64_t original_address = kScratchGuestPage + offset;
        auto source = view->read_byte(m::GuestAddress{original_address});
        std::byte expected = std::byte{0};
        if (source.has_value()) {
            expected = source.value();
        } else if (source.error().code !=
                   m::InitializedImageErrorCode::unmapped_guest_address) {
            return fail("original_page_read_ambiguous");
        }
        if (original_address >= target &&
            original_address < target + relative->bytes.size())
            expected = relative->bytes[static_cast<std::size_t>(
                original_address - target)];
        if (stage_unsupported_import_stops) {
            for (std::size_t index = 1U; index <= kImportCount; ++index) {
                if (original_address >= import_targets[index] &&
                    original_address < import_targets[index] + 8U)
                    expected = import_words[index][static_cast<std::size_t>(
                        original_address - import_targets[index])];
            }
        }
        if (guest_shared_page[offset] != expected)
            return fail("prepared_guest_page_readback_mismatch");
        ++result.mapped_shared_page_compared_bytes;
    }
    // Every imported GOT word is inspected via the real prepared memory
    // and must equal the unchanged source word: no fake HLE/gate binding.
    for (std::uint64_t i = 0U; i < 20U; ++i) {
        auto relocation = l::parse_dynamic_relocation(
            original.general_relocations.rela.value(), i,
            original.dynamic_symbols.value(), view.value());
        if (!relocation.has_value()) return fail("relocation_readback_parse");
        if (relocation->relocation_type != 6U) continue;
        for (std::size_t j = 0U; j < 8U; ++j) {
            const auto address = relocation->target.value() + j;
            auto source = view->read_byte(m::GuestAddress{address});
            if (!source.has_value() ||
                relocation->symbol_index == 0U ||
                relocation->symbol_index > kImportCount)
                return fail("unsupported_stop_import_readback_source");
            const auto expected = stage_unsupported_import_stops
                ? import_words[relocation->symbol_index][j]
                : source.value();
            if (guest_shared_page[address - kScratchGuestPage] != expected)
                return fail("unresolved_import_readback_mismatch");
        }
        if (stage_unsupported_import_stops)
            ++result.staged_unsupported_imports;
        else
            ++result.original_imports_unchanged;
    }
    if (result.mapped_shared_page_compared_bytes != kPageSize ||
        (stage_unsupported_import_stops
            ? result.staged_unsupported_imports != kImportCount
            : result.original_imports_unchanged != kImportCount))
        return fail("incomplete_native_image_readback");

    if (stage_unsupported_import_stops) {
        const auto* trap = reinterpret_cast<const std::byte*>(
            static_cast<std::uintptr_t>(base + kStopPageOffset));
        for (std::size_t offset = 0U; offset < kPageSize; ++offset) {
            std::byte expected{0xcc};
            for (std::size_t index = 0U; index < kImportCount; ++index) {
                const auto slot = index * kStopStride;
                if (offset == slot) expected = std::byte{0x0f};
                if (offset == slot + 1U) expected = std::byte{0x0b};
            }
            if (trap[offset] != expected)
                return fail("unsupported_stop_executable_bytes_mismatch");
            ++result.synthetic_trap_page_compared_bytes;
        }
    }

    // Unlike #424's unrelated scratch page, this negative attempts an
    // actual write through the real prepared guest VA. It must SIGSEGV or
    // SIGBUS; the disposable child never enters guest instructions.
    const pid_t child = ::fork();
    if (child < 0) return fail("guest_permission_negative_fork");
    if (child == 0) {
        const rlimit no_core{0U, 0U};
        (void)::setrlimit(RLIMIT_CORE, &no_core);
        auto* const target_byte = reinterpret_cast<volatile unsigned char*>(
            static_cast<std::uintptr_t>(base + kScratchGuestPage + 0x900U));
        *target_byte = static_cast<unsigned char>(0x5a);
        ::_exit(91);
    }
    int status = 0;
    pid_t waited = -1;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited == -1 && errno == EINTR);
    if (waited != child || !WIFSIGNALED(status) ||
        (WTERMSIG(status) != SIGSEGV && WTERMSIG(status) != SIGBUS))
        return fail("prepared_guest_code_data_page_was_writable");
    result.prepared_guest_page_write_denied = true;

    // RAII LinuxPreparedMemory destruction tears down guest VMAs here.
    // No execution callback, synthetic HLE gate, IPC or provider lookup.
    return result;
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

    // The first actual independent Breakout instructions are evaluated
    // non-natively from the immutable, source-backed GuestImage view.
    // An owned simulated stack receives exactly one 8-byte return word.
    // Native guest control is NEVER transferred, and no Sony ABI is assumed.
    const auto interpreted =
        astraea::research::interpret_pinned_breakout_entry(image.value());
    if (!interpreted.has_value()) {
        std::cerr << "breakout_interpreter_refused="
                  << static_cast<unsigned>(interpreted.error()) << '\n';
        return 22;
    }
    if (interpreted->interpreted_instructions != 2U ||
        interpreted->context.rip != interpreted->branch_target ||
        interpreted->context.rsp != interpreted->guest_stack_write_address ||
        interpreted->context.rsp % 16U != 8U ||
        interpreted->return_rip != interpreted->entry_rip + 9U)
        return 23;

    // Independently decode the two verified entry instructions and a
    // bounded prefix of their *actual* branch target with a maintained
    // MIT-licensed x86 decoder. Decoding is NEVER execution; the existing
    // model remains limited to the two entry instructions above.
    const auto entry_decode =
        astraea::research::decode_source_x86_64(
            image.value(), interpreted->entry_rip, 2U);
    if (!entry_decode.has_value() || entry_decode->count != 2U ||
        entry_decode->instructions[0].length != 4U ||
        entry_decode->instructions[1].length != 5U ||
        entry_decode->instructions[1].rip != interpreted->entry_rip + 4U ||
        entry_decode->instructions[0].mnemonic != ZYDIS_MNEMONIC_AND ||
        entry_decode->instructions[0].reads_rax ||
        entry_decode->instructions[0].control_flow_boundary ||
        entry_decode->instructions[1].mnemonic != ZYDIS_MNEMONIC_CALL ||
        !entry_decode->instructions[1].control_flow_boundary)
        return 29;
    // Independent SOURCE-OWNED decoder fixtures use a disposable copy of
    // this mapped guest image. They verify the generic RAX dependency and
    // exceptional/control-flow classification without executing any byte,
    // changing the exact pinned baseline or assuming Sony ABI semantics.
    // No fixture may be admitted by the original Breakout source SHA gate.
    const auto fixture_map = std::find_if(
        image->mappings.begin(), image->mappings.end(),
        [&](const m::MappingIntent& mapping) {
            return mapping.range.contains(entry) &&
                mapping.backing.kind == m::MappingBackingKind::file;
        });
    if (fixture_map == image->mappings.end()) return 66;
    const auto fixture_offset = fixture_map->backing.file_offset +
        entry.value() - fixture_map->range.base().value();
    if (fixture_offset > image->image_bytes.size() ||
        image->image_bytes.size() - fixture_offset < 6U) return 67;
    struct OwnedDecodeCase {
        std::array<std::uint8_t, 6U> bytes;
        std::size_t length;
        bool reads_rax;
        bool overwrites_full_rax;
        bool control_flow_boundary;
    };
    constexpr std::array<OwnedDecodeCase, 15U> kOwnedDecoderCases{{
        {{0xb8U, 0x2aU, 0U, 0U, 0U, 0U}, 5U, false, true, false}, // mov eax,42
        {{0xb0U, 0x7fU, 0U, 0U, 0U, 0U}, 2U, false, false, false}, // mov al,127
        {{0xb4U, 0x7fU, 0U, 0U, 0U, 0U}, 2U, false, false, false}, // mov ah,127
        {{0x48U, 0x85U, 0xc0U, 0U, 0U, 0U}, 3U, true, false, false}, // test rax,rax
        {{0x48U, 0x8bU, 0x38U, 0U, 0U, 0U}, 3U, true, false, false}, // mov rdi,[rax]
        {{0x48U, 0x8dU, 0x40U, 1U, 0U, 0U}, 4U, true, true, false}, // lea rax,[rax+1]
        {{0xe8U, 0U, 0U, 0U, 0U, 0U}, 5U, false, false, true}, // call rel32
        {{0xe9U, 0U, 0U, 0U, 0U, 0U}, 5U, false, false, true}, // jmp rel32
        {{0xc3U, 0U, 0U, 0U, 0U, 0U}, 1U, false, false, true}, // ret
        {{0x0fU, 0x05U, 0U, 0U, 0U, 0U}, 2U, false, false, true}, // syscall
        {{0xccU, 0U, 0U, 0U, 0U, 0U}, 1U, false, false, true}, // int3
        {{0x0fU, 0x0bU, 0U, 0U, 0U, 0U}, 2U, false, false, true}, // ud2
        {{0x48U, 0x89U, 0xc8U, 0U, 0U, 0U}, 3U, false, true, false}, // mov rax,rcx
        {{0xecU, 0U, 0U, 0U, 0U, 0U}, 1U, false, false, true}, // in al,dx
        {{0x48U, 0x89U, 0x18U, 0U, 0U, 0U}, 3U, true, false, false}, // mov [rax],rbx
    }};
    for (const auto& fixture : kOwnedDecoderCases) {
        auto changed = image.value();
        for (std::size_t n = 0U; n < fixture.length; ++n)
            changed.image_bytes[static_cast<std::size_t>(
                fixture_offset) + n] = static_cast<std::byte>(
                    fixture.bytes[n]);
        const auto decoded = astraea::research::decode_source_x86_64(
            changed, interpreted->entry_rip, 1U);
        if (!decoded.has_value() || decoded->count != 1U ||
            decoded->instructions[0].length != fixture.length ||
            decoded->instructions[0].reads_rax != fixture.reads_rax ||
            decoded->instructions[0].overwrites_full_rax !=
                fixture.overwrites_full_rax ||
            decoded->instructions[0].control_flow_boundary !=
                fixture.control_flow_boundary)
            return 68;
    }
    const auto callee_decode =
        astraea::research::decode_source_x86_64(
            image.value(), interpreted->branch_target,
            astraea::research::kC1MaxDecodedInstructions);
    if (!callee_decode.has_value() ||
        callee_decode->count != astraea::research::kC1MaxDecodedInstructions ||
        callee_decode->instructions[0].rip != interpreted->branch_target ||
        callee_decode->next_rip <= interpreted->branch_target ||
        !callee_decode->instructions[11U].control_flow_boundary)
        return 30;
    // Decoder misuse and a source address without an executable mapping
    // must be typed refusals, not partial or synthetic instruction records.
    const auto zero_budget = astraea::research::decode_source_x86_64(
        image.value(), interpreted->branch_target, 0U);
    const auto oversized_budget = astraea::research::decode_source_x86_64(
        image.value(), interpreted->branch_target,
        astraea::research::kC1MaxDecodedInstructions + 1U);
    const auto wrong_address = astraea::research::decode_source_x86_64(
        image.value(), 0xffff000000000000ULL, 1U);
    if (zero_budget.has_value() || oversized_budget.has_value() ||
        wrong_address.has_value() ||
        zero_budget.error() != astraea::research::SourceDecodeError::invalid_budget ||
        oversized_budget.error() != astraea::research::SourceDecodeError::invalid_budget ||
        wrong_address.error() !=
            astraea::research::SourceDecodeError::target_not_executable)
        return 31;

    // The first 11 real callee prologue instructions are now modeled
    // non-natively from Zydis operand semantics, using only an owned
    // simulated guest stack. The 12th is a verified unresolved import
    // stop BEFORE the indirect CALL retires or a GOT pointer is followed.
    const auto callee = astraea::research::run_owned_breakout_callee(
        image.value(), interpreted.value());
    if (!callee.has_value()) {
        std::cerr << "owned_breakout_callee_refused="
                  << static_cast<unsigned>(callee.error()) << '\n';
        return 32;
    }
    if (callee->entry_retired_instructions != 2U ||
        callee->retired_instructions != 11U ||
        callee->guest_stack_writes != 9U ||
        callee->stopped_call_rip != 0x23cU ||
        callee->context.rip != callee->stopped_call_rip ||
        callee->unresolved_got_slot != 0x68c8U ||
        callee->unresolved_symbol_index != 13U ||
        callee->unresolved_bare_nid != "Vo5V8KAwCmk" ||
        callee->context.rbp != callee->initial_callee_rsp - 8U ||
        callee->context.rsp != callee->initial_callee_rsp - 280U)
        return 33;
    {
        // Compare the observed raw word with the original initialized
        // source bytes. This is data provenance, NOT provider resolution.
        const auto original_view = image->initialized_image_view();
        const auto range = m::GuestRange::create(
            m::GuestAddress{callee->unresolved_got_slot},
            m::GuestSize{8U});
        std::array<std::byte, 8U> source_word{};
        if (!original_view.has_value() || !range.has_value() ||
            !original_view->copy_bytes(range.value(),
                                       source_word).has_value())
            return 55;
        std::uint64_t expected_word = 0U;
        for (std::size_t i = 0U; i < source_word.size(); ++i)
            expected_word |= std::to_integer<std::uint64_t>(
                source_word[i]) << (i * 8U);
        if (callee->unresolved_got_raw_word != expected_word)
            return 56;
    }
    // The original Breakout ELF contributes one RELATIVE relocation at
    // guest VA 0x6900 and nineteen unresolved imports. Re-run the exact
    // same source and compare normalized checked CPU/relocation evidence.
    if (callee->applied_relative_patches != 1U ||
        callee->relative_patch_target != 0x6900U ||
        callee->preflighted_unresolved_imports != 19U ||
        callee->untouched_unresolved_imports != 19U)
        return 57;
    const auto repeated = astraea::research::run_owned_breakout_callee(
        image.value(), interpreted.value());
    if (!repeated.has_value() ||
        repeated->context != callee->context ||
        repeated->guest_stack_bytes != callee->guest_stack_bytes ||
        repeated->applied_relative_patches !=
            callee->applied_relative_patches ||
        repeated->relative_patch_target != callee->relative_patch_target ||
        repeated->relative_patch_value != callee->relative_patch_value ||
        repeated->preflighted_unresolved_imports !=
            callee->preflighted_unresolved_imports ||
        repeated->untouched_unresolved_imports !=
            callee->untouched_unresolved_imports ||
        repeated->stopped_call_rip != callee->stopped_call_rip ||
        repeated->unresolved_got_slot != callee->unresolved_got_slot ||
        repeated->unresolved_bare_nid != callee->unresolved_bare_nid)
        return 58;

    // Source-only, no guest continuation: inspect the *compiled* fallthrough
    // of the verified unretired imported CALL. The author's C source
    // discards its return, but the compiled register dependency must be
    // observed independently. Stop at the FIRST RAX read/full overwrite or
    // control-flow boundary; linear decoding across a CALL/JMP is not a
    // path or a simulated HLE. All service side effects remain unknown.
    const auto blocked_import = astraea::research::decode_source_x86_64(
        image.value(), callee->stopped_call_rip, 1U);
    if (!blocked_import.has_value() ||
        blocked_import->count != 1U ||
        blocked_import->instructions[0].mnemonic != ZYDIS_MNEMONIC_CALL ||
        blocked_import->instructions[0].length != 6U ||
        !blocked_import->instructions[0].control_flow_boundary ||
        blocked_import->next_rip != 0x242U)
        return 64;

    std::uint64_t postimport_next_rip = blocked_import->next_rip;
    std::uint64_t postimport_boundary_rip = postimport_next_rip;
    std::size_t postimport_decoded_count = 0U;
    std::string_view postimport_rax_frontier = "budget_without_resolution";
    for (std::size_t i = 0U;
         i < astraea::research::kC1MaxDecodedInstructions; ++i) {
        const auto step = astraea::research::decode_source_x86_64(
            image.value(), postimport_next_rip, 1U);
        if (!step.has_value() || step->count != 1U) {
            postimport_rax_frontier = "source_decode_refused";
            break;
        }
        const auto& instruction = step->instructions[0];
        ++postimport_decoded_count;
        postimport_boundary_rip = instruction.rip;
        if (instruction.reads_rax) {
            postimport_rax_frontier = "read_before_control_boundary";
            break;
        }
        if (instruction.overwrites_full_rax) {
            postimport_rax_frontier = "overwritten_before_read";
            break;
        }
        if (instruction.control_flow_boundary) {
            postimport_rax_frontier = "control_boundary_before_read";
            break;
        }
        postimport_next_rip = step->next_rip;
    }
    if (postimport_decoded_count == 0U) return 65;

    // Ensure the entry CALL's return address is still present in the
    // owned stack buffer following all eleven real callee prologue steps.
    const auto return_index = callee->initial_callee_rsp -
        callee->guest_stack_base;
    if (return_index > callee->guest_stack_bytes.size() - 8U)
        return 34;
    for (std::size_t i = 0U; i < 8U; ++i) {
        if (callee->guest_stack_bytes[
                static_cast<std::size_t>(return_index) + i] !=
            interpreted->guest_stack_return_word[i])
            return 35;
    }
    // A failed stack write must not escape into host native addresses.
    auto bad_stack = interpreted.value();
    bad_stack.context.rsp =
        image->initial_stack.storage.base().value() + 8U;
    bad_stack.guest_stack_write_address = bad_stack.context.rsp;
    const auto stack_refused =
        astraea::research::run_owned_breakout_callee(
            image.value(), bad_stack);
    if (stack_refused.has_value() ||
        stack_refused.error() != astraea::research::
            OwnedCalleeError::stack_out_of_bounds)
        return 36;
    {
        // A valid instruction decoder must not silently interpret a
        // different prologue opcode, even with the expected call nearby.
        auto changed = image.value();
        const auto callee_map = std::find_if(
            changed.mappings.begin(), changed.mappings.end(),
            [&](const m::MappingIntent& mapping) {
                return mapping.range.contains(
                    m::GuestAddress{interpreted->branch_target}) &&
                    mapping.backing.kind == m::MappingBackingKind::file;
            });
        if (callee_map == changed.mappings.end()) return 37;
        const auto source_offset = callee_map->backing.file_offset +
            interpreted->branch_target -
            callee_map->range.base().value();
        if (source_offset > changed.image_bytes.size() ||
            changed.image_bytes.size() - source_offset < 2U)
            return 38;
        changed.image_bytes[static_cast<std::size_t>(source_offset)] =
            std::byte{0x0f};
        changed.image_bytes[static_cast<std::size_t>(source_offset + 1U)] =
            std::byte{0x0b};
        const auto mutation = astraea::research::run_owned_breakout_callee(
            changed, interpreted.value());
        if (mutation.has_value() ||
            mutation.error() != astraea::research::
                OwnedCalleeError::unsupported_instruction)
            return 39;
    }
    {
        // Preserve initialized bytes but strip execute permission from the
        // source mapping containing the callee. Virtual fetch must refuse
        // before retiring an instruction, despite readable code bytes.
        auto changed = image.value();
        const auto nonexec = m::GuestPermissions::checked_from_bits(3U);
        if (!nonexec.has_value()) return 49;
        bool removed = false;
        for (auto& mapping : changed.mappings) {
            if (mapping.range.contains(
                    m::GuestAddress{interpreted->branch_target}) &&
                mapping.permissions.has(m::GuestPermission::execute)) {
                mapping.permissions = nonexec.value();
                removed = true;
            }
        }
        if (!removed) return 50;
        const auto denied = astraea::research::run_owned_breakout_callee(
            changed, interpreted.value());
        if (denied.has_value() ||
            denied.error() != astraea::research::
                OwnedCalleeError::decode_refused)
            return 51;
    }
    {
        // Original source-backed GOT bytes exist, but the CALL operand
        // is inaccessible without guest READ permission. No pointer is
        // followed and no CALL state is committed.
        auto changed = image.value();
        const auto write_only = m::GuestPermissions::checked_from_bits(2U);
        if (!write_only.has_value()) return 52;
        bool changed_owner = false;
        for (auto& mapping : changed.mappings) {
            if (mapping.range.contains(m::GuestAddress{0x68c8U}) &&
                mapping.permissions.has(m::GuestPermission::write)) {
                mapping.permissions = write_only.value();
                changed_owner = true;
            }
        }
        if (!changed_owner) return 53;
        const auto refused = astraea::research::run_owned_breakout_callee(
            changed, interpreted.value());
        if (refused.has_value() ||
            refused.error() != astraea::research::
                OwnedCalleeError::relocation_refused)
            return 54;
    }
    {
        auto changed = image.value();
        changed.dynamic_symbols.reset();
        const auto missing = astraea::research::run_owned_breakout_callee(
            changed, interpreted.value());
        if (missing.has_value() ||
            missing.error() != astraea::research::
                OwnedCalleeError::relocation_refused)
            return 40;
    }
    {
        // Missing RELA record must refuse before any simulated CPU step.
        auto changed = image.value();
        if (!changed.general_relocations.rela.has_value()) return 59;
        changed.general_relocations.rela->count = 19U;
        const auto rejected = astraea::research::run_owned_breakout_callee(
            changed, interpreted.value());
        if (rejected.has_value() ||
            rejected.error() != astraea::research::
                OwnedCalleeError::relocation_refused)
            return 60;
    }
    {
        // Original RELATIVE destination loses virtual write permission.
        auto changed = image.value();
        const auto read_only = m::GuestPermissions::checked_from_bits(1U);
        if (!read_only.has_value()) return 61;
        bool restricted = false;
        for (auto& mapping : changed.mappings) {
            if (mapping.range.contains(m::GuestAddress{0x6900U}) &&
                mapping.permissions.has(m::GuestPermission::write)) {
                mapping.permissions = read_only.value();
                restricted = true;
            }
        }
        if (!restricted) return 62;
        const auto rejected = astraea::research::run_owned_breakout_callee(
            changed, interpreted.value());
        if (rejected.has_value() ||
            rejected.error() != astraea::research::
                OwnedCalleeError::relocation_refused)
            return 63;
    }

    // Adversarial decoder and memory negatives operate on owned copies of
    // the exact pinned image. They must refuse BEFORE any modeled state
    // commits; the baseline image and its source hash remain unchanged.
    const auto source_map = std::find_if(
        image->mappings.begin(), image->mappings.end(),
        [&](const m::MappingIntent& mapping) {
            return mapping.range.contains(entry) &&
                mapping.backing.kind == m::MappingBackingKind::file &&
                mapping.backing.byte_count.value() >=
                    entry.value() - mapping.range.base().value() + 9U;
        });
    if (source_map == image->mappings.end()) return 24;
    const auto entry_file_offset = source_map->backing.file_offset +
        entry.value() - source_map->range.base().value();
    if (entry_file_offset > image->image_bytes.size() ||
        image->image_bytes.size() - entry_file_offset < 9U)
        return 25;
    const auto byte_offset = static_cast<std::size_t>(entry_file_offset);
    {
        auto altered = image.value();
        altered.image_bytes[byte_offset] ^= std::byte{1U};
        const auto rejected =
            astraea::research::interpret_pinned_breakout_entry(altered);
        if (rejected.has_value() ||
            rejected.error() != astraea::research::
                BreakoutEntryStopError::unexpected_entry_encoding)
            return 26;
    }
    {
        auto altered = image.value();
        altered.image_bytes[byte_offset + 5U] = std::byte{0xff};
        altered.image_bytes[byte_offset + 6U] = std::byte{0xff};
        altered.image_bytes[byte_offset + 7U] = std::byte{0xff};
        altered.image_bytes[byte_offset + 8U] = std::byte{0x7f};
        const auto rejected =
            astraea::research::interpret_pinned_breakout_entry(altered);
        if (rejected.has_value() ||
            rejected.error() != astraea::research::
                BreakoutEntryStopError::target_not_executable)
            return 27;
    }
    {
        auto altered = image.value();
        altered.initial_stack.rsp = m::GuestAddress{
            altered.initial_stack.storage.base().value() + 4U};
        const auto rejected =
            astraea::research::interpret_pinned_breakout_entry(altered);
        if (rejected.has_value() ||
            rejected.error() != astraea::research::
                BreakoutEntryStopError::invalid_guest_stack)
            return 28;
    }

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

    auto prepared = prepare_temporary_breakout_guest_memory(
        image.value(), false);
    if (!prepared.has_value() || !prepared->prepared_guest_page_write_denied)
        return 20;

    // Stage all exact bare NIDs as independent, test-owned *unsupported*
    // stop destinations in an ephemeral copy. Never execute third-party
    // instructions or claim a Sony service has been resolved.
    auto unsupported = prepare_temporary_breakout_guest_memory(
        image.value(), true);
    if (!unsupported.has_value() ||
        unsupported->staged_unsupported_imports != 19U ||
        unsupported->synthetic_trap_page_compared_bytes != kPageSize ||
        !unsupported->prepared_guest_page_write_denied)
        return 21;

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
              << "source_x86_decoder=zydis_4_1_1_pinned\n"
              << "source_x86_decoder_dependency=zycore_1_5_2_pinned\n"
              << "source_x86_entry_decode=AND_4_CALL_5_verified\n"
              << "source_x86_callee_first_rip=0x" << std::hex
              << interpreted->branch_target << std::dec << '\n'
              << "source_x86_callee_decode_count="
              << callee_decode->count << '\n'
              << "source_x86_callee_decode_next_rip=0x" << std::hex
              << callee_decode->next_rip << std::dec << '\n'
              << "source_x86_decoder_invalid_requests=3_refused\n"
              << "source_x86_decoder_rax_boundary_owned_cases=15_pass\n"
              << "source_x86_callee_decoded_instructions=12\n"
              << "breakout_owned_callee_fetch=original_guestimage_virtual_mappings\n"
              << "breakout_unified_entry_callee=13_source_backed_steps\n"
              << "breakout_generic_entry_retired_instructions="
              << callee->entry_retired_instructions << '\n'
              << "breakout_owned_callee_retired_instructions="
              << callee->retired_instructions << '\n'
              << "breakout_owned_callee_guest_stack_writes="
              << callee->guest_stack_writes << '\n'
              << "breakout_interpreted_actual_relative_patches="
              << callee->applied_relative_patches << '\n'
              << "breakout_interpreted_relative_guest_target=0x"
              << std::hex << callee->relative_patch_target << std::dec << '\n'
              << "breakout_interpreted_import_write_preflighted="
              << callee->preflighted_unresolved_imports << '\n'
              << "breakout_interpreted_unresolved_imports_unchanged="
              << callee->untouched_unresolved_imports << '\n'
              << "breakout_interpreted_repeat=two_identical_normalized_runs\n"
              << "breakout_interpreted_relative_load_bias=zero_synthetic\n"
              << "breakout_postimport_fallthrough=source_only_no_execution\n"
              << "breakout_postimport_rax_frontier="
              << postimport_rax_frontier << '\n'
              << "breakout_postimport_decoded_instructions="
              << postimport_decoded_count << '\n'
              << "breakout_postimport_first_boundary_rip=0x"
              << std::hex << postimport_boundary_rip << std::dec << '\n'
              << "breakout_postimport_sony_service_effects=unknown\n"
              << "breakout_owned_callee_stop=unresolved_bare_nid_before_call\n"
              << "breakout_owned_callee_stop_rip=0x" << std::hex
              << callee->stopped_call_rip << std::dec << '\n'
              << "breakout_owned_callee_got_slot=0x" << std::hex
              << callee->unresolved_got_slot << std::dec << '\n'
              << "breakout_owned_callee_got_word=observed_read_only_no_transfer\n"
              << "breakout_owned_callee_symbol_index="
              << callee->unresolved_symbol_index << '\n'
              << "breakout_owned_callee_bare_nid="
              << callee->unresolved_bare_nid << '\n'
              << "breakout_owned_callee_stack_retaddr=preserved\n"
              << "breakout_owned_callee_negative_cases=7_refused\n"
              << "breakout_owned_callee_native_guest_execution=0\n"
              << "breakout_owned_callee_sony_provider_calls=0\n";
    constexpr char hex_digits[] = "0123456789abcdef";
    for (std::size_t i = 0U; i < callee_decode->count; ++i) {
        const auto& insn = callee_decode->instructions[i];
        std::cout << "source_x86_callee[" << i << "].rip=0x"
                  << std::hex << insn.rip << std::dec << '\n'
                  << "source_x86_callee[" << i << "].length="
                  << static_cast<unsigned>(insn.length) << '\n'
                  << "source_x86_callee[" << i << "].mnemonic="
                  << astraea::research::source_mnemonic_name(
                         insn.mnemonic) << '\n'
                  << "source_x86_callee[" << i << "].bytes_hex=";
        for (std::size_t j = 0U; j < insn.length; ++j) {
            const auto byte = std::to_integer<unsigned>(insn.bytes[j]);
            std::cout << hex_digits[byte >> 4U]
                      << hex_digits[byte & 0xfU];
        }
        std::cout << '\n';
    }
    std::cout
              << "original_guest_permissions=unchanged\n"
              << "elf_bytes=unchanged\n"
              << "temporary_guest_va_prepared_and_released=1\n"
              << "temporary_guest_source_fragments="
              << prepared->source_mapping_fragments << '\n'
              << "temporary_guest_mapped_fragments="
              << prepared->transient_mapping_fragments << '\n'
              << "temporary_source_writable_bytes_frozen="
              << prepared->restricted_source_write_bytes << '\n'
              << "temporary_guest_page_compared_bytes="
              << prepared->mapped_shared_page_compared_bytes << '\n'
              << "temporary_guest_unresolved_got_words_unchanged="
              << prepared->original_imports_unchanged << '\n'
              << "temporary_guest_real_page_write=denied_by_kernel\n"
              << "temporary_guest_actual_native_relatives=1\n"
              << "temporary_guest_writable_code_semantics=unsupported\n"
              << "native_guest_memory_mapped_at_report=0\n"
              << "scratch_page_rw_to_rx=verified\n"
              << "scratch_relative_patches=" << staged->relative_patches << '\n'
              << "scratch_unresolved_imports=" << staged->unresolved_imports << '\n'
              << "real_sce_planner_bare_nid_refusals="
              << staged->actual_sce_identity_refusals << '\n'
              << "unverified_provider_binding_refusals="
              << staged->unverified_provider_refusals << '\n'
              << "registered_sony_provider_bindings=0\n"
              << "synthetic_import_gate_patches=0\n"
              << "test_only_unsupported_nid_stops_staged="
              << unsupported->staged_unsupported_imports << '\n'
              << "test_only_stop_page_readback_bytes="
              << unsupported->synthetic_trap_page_compared_bytes << '\n'
              << "test_only_stop_guest_code_executed=0\n"
              << "test_only_stop_provider_bindings=0\n"
              << "import_call_observed=0\n"
              << "breakout_entry_interpreter=source_backed_two_instruction_bound\n"
              << "breakout_entry_and_rsp64=interpreted\n"
              << "breakout_entry_call_rel32=interpreted\n"
              << "breakout_entry_call_target=0x" << std::hex
              << interpreted->branch_target << std::dec << '\n'
              << "breakout_guest_stack_return_word=owned_eight_bytes\n"
              << "breakout_call_target_source=executable_guest_mapping\n"
              << "breakout_next_instruction=not_interpreted\n"
              << "breakout_undefined_AF=unclaimed\n"
              << "breakout_interpreter_negative_cases=3_refused\n"
              << "breakout_interpreter_native_guest_entry=none\n"
              << "scratch_postfreeze_write=denied_by_kernel\n"
              << "guest_relocations_applied=0\n"
              << "imports_resolved=0\n"
              << "guest_instructions=2_interpreted_only\n"
              << "native_guest_instructions=0\n"
              << "guest_write_semantics_preserved=not_proven\n"
              << "security_admission=not_granted\n";
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    return inspect(argv[1]);
}
