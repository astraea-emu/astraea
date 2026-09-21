#pragma once

#include <cstdint>
#include <optional>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic_string.hpp>
#include <astraea/loader/dynamic_symbols.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>
#include <astraea/memory/initialized_image_view.hpp>

namespace astraea::loader {

enum class SceDynamicSymbolErrorCode {
    dynamic_symbol_failure,
    dynamic_string_failure,
    sce_identity_failure,
};

struct SceDynamicSymbolError {
    SceDynamicSymbolErrorCode code =
        SceDynamicSymbolErrorCode::dynamic_symbol_failure;
    std::uint64_t symbol_index = 0;
    std::optional<DynamicSymbolError> symbol_error;
    std::optional<DynamicStringError> string_error;
    std::optional<SceSymbolIdentityError> identity_error;
};

struct SceDynamicSymbolRecord {
    DynamicSymbol symbol;
    SceDynamicSymbolName name;

    auto operator<=>(const SceDynamicSymbolRecord&) const = default;
};

using SceDynamicSymbolResult =
    astraea::core::Result<
        SceDynamicSymbolRecord,
        SceDynamicSymbolError>;

[[nodiscard]] SceDynamicSymbolResult
materialize_sce_dynamic_symbol(
    const DynamicSymbolTableDescriptor& symbols,
    const DynamicStringTableDescriptor& strings,
    std::uint64_t symbol_index,
    const astraea::memory::InitializedImageView& image_view);

}  // namespace astraea::loader
