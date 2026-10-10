#pragma once

#include <iosfwd>
#include <string_view>

#include <astraea/execution/retail_closure_profile.hpp>

namespace astraea::app {

void write_retail_closure_profile(
    std::ostream& output,
    const astraea::execution::
        RetailStaticClosureProfile& profile);

[[nodiscard]] int
run_retail_closure_profile(
    std::string_view artifact_path,
    astraea::loader::ElfParseProfile profile =
        astraea::loader::ElfParseProfile::ps5_sce);

}  // namespace astraea::app
