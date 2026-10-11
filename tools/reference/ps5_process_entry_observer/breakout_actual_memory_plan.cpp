// C1 source-pinned Breakout pre-entry memory-plan assessment only.
// No host mappings, relocations, guest entry, import resolution or HLE.
#include "owned_pair_sha256.hpp"

#include <astraea/execution/memory_plan.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/mapping.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
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
        .elf_profile = l::ElfParseProfile::ps5_sce,
    });
    if (!image.has_value()) {
        std::cerr << "actual_guestimage_build_failure="
                  << static_cast<unsigned>(image.error().code) << '\n';
        return 4;
    }
    // Both plans use the *actual* GuestImage mapping descriptors. No
    // synthetic or independently reconstructed PT_LOAD permissions.
    if (image->mappings.size() != 3U ||
        image->elf.header.entry == 0U)
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
        if (write && execute)
            source_rwx_first_page =
                mapping.range.base().value() & ~(kPageSize - 1U);
    }
    if (executable_count != 1U || source_rwx_count != 1U ||
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
    if (narrowed_count != 1U) return 11;

    const auto candidate = plan(image.value(), research_candidate);
    if (!candidate.has_value() &&
        candidate.error().code != e::ExecutionPlanErrorCode::mixed_write_execute_page) {
        std::cerr << "research_candidate_plan_failure="
                  << static_cast<unsigned>(candidate.error().code) << '\n';
        return 12;
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

    std::cout << "PINNED BREAKOUT ACTUAL MEMORY PLAN PREFLIGHT PASS\n"
              << "source_sha256=" << kPinnedSha256 << '\n'
              << "guestimage_load_mappings=" << image->mappings.size() << '\n'
              << "source_rwx_executable_mappings=" << source_rwx_count << '\n'
              << "source_plan=mixed_write_execute_page\n"
              << "research_narrowed_mapping_count=" << narrowed_count << '\n'
              << "research_narrowed_plan="
              << (candidate.has_value() ? "accepted_no_host_rwx"
                                        : "blocked_mixed_write_execute_page")
              << '\n'
              << "original_guest_permissions=unchanged\n"
              << "elf_bytes=unchanged\n"
              << "native_memory_mapped=0\n"
              << "relocations_applied=0\n"
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
