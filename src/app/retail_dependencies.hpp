#pragma once

#include <string_view>

namespace astraea::app {

// Read-only static dependency inventory. Never maps executable guest code
// into the host for execution or resolves a Sony system library.
[[nodiscard]] int
run_retail_dependency_manifest(std::string_view artifact_path);

}  // namespace astraea::app
