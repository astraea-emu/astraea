#include <retail_profile.hpp>

#include <cstdint>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

TEST_CASE(
    "retail closure profile text format is stable and complete",
    "[app][analysis][retail][profile]") {
    const astraea::execution::RetailStaticClosureProfile profile{
        .program_header_count = 7U,
        .load_segment_count = 4U,
        .load_memory_bytes = 123456U,
        .executable_load_segment_count = 1U,
        .executable_load_memory_bytes = 4096U,
        .generic_needed_count = 2U,
        .sce_needed_module_count = 3U,
        .sce_import_library_count = 5U,
        .sce_unknown_dynamic_record_count = 1U,
        .dynamic_symbol_count = 77U,
        .rel_relocation_count = 11U,
        .rela_relocation_count = 13U,
        .plt_relocation_count = 17U,
        .total_relocation_count = 41U,
        .tls_present = true,
        .tls_initialized_bytes = 32U,
        .tls_total_bytes = 96U,
        .tls_alignment = 16U,
    };

    std::ostringstream output;
    astraea::app::write_retail_closure_profile(
        output,
        profile);

    REQUIRE(
        output.str() ==
        "Astraea retail closure profile\n"
        "program_headers=7\n"
        "load_segments=4\n"
        "load_memory_bytes=123456\n"
        "executable_load_segments=1\n"
        "executable_load_memory_bytes=4096\n"
        "generic_needed=2\n"
        "sce_needed_modules=3\n"
        "sce_import_libraries=5\n"
        "sce_unknown_dynamic_records=1\n"
        "dynamic_symbols=77\n"
        "rel_relocations=11\n"
        "rela_relocations=13\n"
        "plt_relocations=17\n"
        "total_relocations=41\n"
        "tls_present=1\n"
        "tls_initialized_bytes=32\n"
        "tls_total_bytes=96\n"
        "tls_alignment=16\n");
}

TEST_CASE(
    "retail closure profile text preserves unavailable optional symbol count",
    "[app][analysis][retail][profile][optional]") {
    const astraea::execution::RetailStaticClosureProfile profile{};

    std::ostringstream output;
    astraea::app::write_retail_closure_profile(
        output,
        profile);

    REQUIRE(
        output.str().find(
            "dynamic_symbols=unavailable\n") !=
        std::string::npos);
    REQUIRE(
        output.str().find(
            "tls_present=0\n") !=
        std::string::npos);
}
