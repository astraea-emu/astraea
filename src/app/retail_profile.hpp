#pragma once

#include <string_view>

namespace astraea::app {

[[nodiscard]] int
run_retail_closure_profile(
    std::string_view artifact_path);

}  // namespace astraea::app
