#include "retail_closure_profile.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/retail_closure_profile.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::app {
namespace {

constexpr std::size_t kMaxProfileArtifactBytes =
    1024ULL * 1024ULL * 1024ULL;

enum class ArtifactReadErrorCode {
    open_failed,
    size_query_failed,
    empty_artifact,
    artifact_too_large,
    read_failed,
    artifact_changed_during_read,
    host_allocation_failure,
};

using ArtifactReadResult =
    astraea::core::Result<
        std::vector<std::byte>,
        ArtifactReadErrorCode>;

[[nodiscard]] std::string_view artifact_error_name(
    ArtifactReadErrorCode code) noexcept {
    switch (code) {
    case ArtifactReadErrorCode::open_failed:
        return "open_failed";
    case ArtifactReadErrorCode::size_query_failed:
        return "size_query_failed";
    case ArtifactReadErrorCode::empty_artifact:
        return "empty_artifact";
    case ArtifactReadErrorCode::artifact_too_large:
        return "artifact_too_large";
    case ArtifactReadErrorCode::read_failed:
        return "read_failed";
    case ArtifactReadErrorCode::artifact_changed_during_read:
        return "artifact_changed_during_read";
    case ArtifactReadErrorCode::host_allocation_failure:
        return "host_allocation_failure";
    }
    return "unknown";
}

[[nodiscard]] std::string_view guest_image_error_name(
    astraea::loader::GuestImageErrorCode code) noexcept {
    using astraea::loader::GuestImageErrorCode;
    switch (code) {
    case GuestImageErrorCode::elf_parse_failure:
        return "elf_parse_failure";
    case GuestImageErrorCode::mapping_failure:
        return "mapping_failure";
    case GuestImageErrorCode::initialized_image_failure:
        return "initialized_image_failure";
    case GuestImageErrorCode::duplicate_dynamic_segment:
        return "duplicate_dynamic_segment";
    case GuestImageErrorCode::mapping_overlap_conflict:
        return "mapping_overlap_conflict";
    case GuestImageErrorCode::initial_stack_overlaps_load_mapping:
        return "initial_stack_overlaps_load_mapping";
    case GuestImageErrorCode::dynamic_parse_failure:
        return "dynamic_parse_failure";
    case GuestImageErrorCode::dynamic_metadata_failure:
        return "dynamic_metadata_failure";
    case GuestImageErrorCode::dynamic_symbol_failure:
        return "dynamic_symbol_failure";
    case GuestImageErrorCode::dynamic_relocation_failure:
        return "dynamic_relocation_failure";
    case GuestImageErrorCode::tls_failure:
        return "tls_failure";
    case GuestImageErrorCode::initial_stack_failure:
        return "initial_stack_failure";
    case GuestImageErrorCode::host_allocation_failure:
        return "host_allocation_failure";
    case GuestImageErrorCode::host_size_unrepresentable:
        return "host_size_unrepresentable";
    }
    return "unknown";
}

[[nodiscard]] std::string_view profile_error_name(
    astraea::execution::
        RetailArtifactClosureProfileErrorCode code) noexcept {
    using astraea::execution::
        RetailArtifactClosureProfileErrorCode;
    switch (code) {
    case RetailArtifactClosureProfileErrorCode::
        analysis_stack_unavailable:
        return "analysis_stack_unavailable";
    case RetailArtifactClosureProfileErrorCode::
        guest_image_failure:
        return "guest_image_failure";
    case RetailArtifactClosureProfileErrorCode::
        static_profile_failure:
        return "static_profile_failure";
    case RetailArtifactClosureProfileErrorCode::
        host_allocation_failure:
        return "host_allocation_failure";
    }
    return "unknown";
}

[[nodiscard]] ArtifactReadResult read_artifact(
    std::string_view artifact_path) {
    try {
        std::ifstream input(
            std::string{artifact_path},
            std::ios::binary | std::ios::ate);
        if (!input) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::open_failed);
        }

        const auto end = input.tellg();
        if (end < std::streampos{0}) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::size_query_failed);
        }
        if (end == std::streampos{0}) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::empty_artifact);
        }

        const auto byte_count =
            static_cast<std::uintmax_t>(
                static_cast<std::streamoff>(end));
        if (byte_count >
                static_cast<std::uintmax_t>(
                    kMaxProfileArtifactBytes) ||
            byte_count >
                static_cast<std::uintmax_t>(
                    std::numeric_limits<
                        std::size_t>::max()) ||
            byte_count >
                static_cast<std::uintmax_t>(
                    std::numeric_limits<
                        std::streamsize>::max())) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::artifact_too_large);
        }

        input.seekg(0, std::ios::beg);
        if (!input) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::read_failed);
        }

        const auto host_size =
            static_cast<std::size_t>(byte_count);
        std::vector<std::byte> bytes(host_size);
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(host_size));
        if (!input ||
            static_cast<std::size_t>(
                input.gcount()) != host_size) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::read_failed);
        }

        char trailing = '\0';
        input.read(&trailing, 1);
        if (input.gcount() != 0) {
            return ArtifactReadResult::failure(
                ArtifactReadErrorCode::
                    artifact_changed_during_read);
        }

        return ArtifactReadResult::success(
            std::move(bytes));
    } catch (const std::bad_alloc&) {
        return ArtifactReadResult::failure(
            ArtifactReadErrorCode::
                host_allocation_failure);
    } catch (const std::length_error&) {
        return ArtifactReadResult::failure(
            ArtifactReadErrorCode::
                host_allocation_failure);
    }
}

}  // namespace

int run_retail_closure_profile(
    std::string_view artifact_path) {
    auto bytes = read_artifact(artifact_path);
    if (!bytes.has_value()) {
        std::cerr
            << "Astraea retail closure profile error\n"
            << "artifact_error="
            << artifact_error_name(bytes.error())
            << "\n";
        return 3;
    }

    const auto result =
        astraea::execution::profile_retail_artifact(
            std::move(bytes.value()));
    if (!result.has_value()) {
        std::cerr
            << "Astraea retail closure profile rejected\n"
            << "profile_error="
            << profile_error_name(
                   result.error().code)
            << "\n";
        if (result.error().guest_image_error.has_value()) {
            std::cerr
                << "guest_image_error="
                << guest_image_error_name(
                       result.error().
                           guest_image_error->code)
                << "\n";
        }
        return 4;
    }

    const auto& profile = result.value();
    std::cout
        << "Astraea retail closure profile\n"
        << "program_headers="
        << profile.program_header_count << "\n"
        << "load_segments="
        << profile.load_segment_count << "\n"
        << "load_memory_bytes="
        << profile.load_memory_bytes << "\n"
        << "executable_load_segments="
        << profile.executable_load_segment_count << "\n"
        << "executable_load_memory_bytes="
        << profile.executable_load_memory_bytes << "\n"
        << "generic_needed="
        << profile.generic_needed_count << "\n"
        << "sce_needed_modules="
        << profile.sce_needed_module_count << "\n"
        << "sce_import_libraries="
        << profile.sce_import_library_count << "\n"
        << "sce_unknown_dynamic_records="
        << profile.sce_unknown_dynamic_record_count << "\n"
        << "dynamic_symbols=";
    if (profile.dynamic_symbol_count.has_value()) {
        std::cout << profile.dynamic_symbol_count.value();
    } else {
        std::cout << "unknown";
    }
    std::cout
        << "\n"
        << "rel_relocations="
        << profile.rel_relocation_count << "\n"
        << "rela_relocations="
        << profile.rela_relocation_count << "\n"
        << "plt_relocations="
        << profile.plt_relocation_count << "\n"
        << "total_relocations="
        << profile.total_relocation_count << "\n"
        << "tls_present="
        << (profile.tls_present ? 1 : 0) << "\n"
        << "tls_initialized_bytes="
        << profile.tls_initialized_bytes << "\n"
        << "tls_total_bytes="
        << profile.tls_total_bytes << "\n"
        << "tls_alignment="
        << profile.tls_alignment << "\n";

    return 0;
}

}  // namespace astraea::app
