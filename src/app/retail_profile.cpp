#include "retail_profile.hpp"

#include "artifact_file.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <utility>

#include <astraea/execution/retail_closure_profile.hpp>

namespace astraea::app {
namespace {

[[nodiscard]] std::string_view
artifact_profile_error_name(
    astraea::execution::
        RetailStaticClosureArtifactErrorCode code) noexcept {
    using astraea::execution::
        RetailStaticClosureArtifactErrorCode;

    switch (code) {
    case RetailStaticClosureArtifactErrorCode::
        planning_stack_unavailable:
        return "planning_stack_unavailable";
    case RetailStaticClosureArtifactErrorCode::
        guest_image_failure:
        return "guest_image_failure";
    case RetailStaticClosureArtifactErrorCode::
        profile_failure:
        return "profile_failure";
    }
    return "unknown";
}

}  // namespace

int run_retail_closure_profile(
    std::string_view artifact_path) {
    auto artifact =
        read_artifact_file(artifact_path);
    if (!artifact.has_value()) {
        std::cerr
            << "Astraea retail closure profile\n"
            << "artifact_error="
            << artifact_read_error_name(
                   artifact.error())
            << "\n";
        return 3;
    }

    auto profiled =
        astraea::execution::profile_retail_artifact(
            std::move(artifact.value()));
    if (!profiled.has_value()) {
        const auto& error = profiled.error();
        std::cerr
            << "Astraea retail closure profile\n"
            << "profile_error="
            << artifact_profile_error_name(error.code)
            << "\n";

        if (error.guest_image_error.has_value()) {
            std::cerr
                << "detail0="
                << static_cast<std::uint32_t>(
                       error.guest_image_error->code)
                << "\n";
            if (error.guest_image_error->
                    source_program_header_index.has_value()) {
                std::cerr
                    << "detail1="
                    << (error.guest_image_error->
                            source_program_header_index.value() +
                        1U)
                    << "\n";
            }
        } else if (error.profile_error.has_value() &&
                   error.profile_error->
                       sce_dynamic_metadata_error.has_value()) {
            std::cerr
                << "detail0="
                << static_cast<std::uint32_t>(
                       error.profile_error->
                           sce_dynamic_metadata_error->code)
                << "\n";
        }
        return 4;
    }

    const auto& profile = profiled.value();
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
        std::cout << "unavailable";
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
