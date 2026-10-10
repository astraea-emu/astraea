#pragma once

#include <string_view>

#include <astraea/loader/elf64.hpp>

namespace astraea::app {

// Read-only static dependency inventory. Never maps executable guest code
// into the host for execution or resolves a Sony system library.
[[nodiscard]] int
run_retail_dependency_manifest(
    std::string_view artifact_path,
    bool decode_public_sce_pack_v1 = false,
    astraea::loader::ElfParseProfile profile =
        astraea::loader::ElfParseProfile::ps5_sce);

}  // namespace astraea::app
