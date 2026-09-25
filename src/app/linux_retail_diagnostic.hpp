#pragma once

#include <string_view>

namespace astraea::app {

[[nodiscard]] int
run_linux_retail_diagnostic(
    std::string_view artifact_path);

[[nodiscard]] int
run_linux_retail_diagnostic_worker();

}  // namespace astraea::app
