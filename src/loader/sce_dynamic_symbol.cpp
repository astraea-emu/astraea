#include <astraea/loader/sce_dynamic_symbol.hpp>

#include <utility>

namespace astraea::loader {
namespace {

[[nodiscard]] SceDynamicSymbolError symbol_failure(
    std::uint64_t symbol_index,
    DynamicSymbolError error) {
    return SceDynamicSymbolError{
        .code =
            SceDynamicSymbolErrorCode::
                dynamic_symbol_failure,
        .symbol_index = symbol_index,
        .symbol_error = std::move(error),
        .string_error = std::nullopt,
        .identity_error = std::nullopt,
    };
}

[[nodiscard]] SceDynamicSymbolError string_failure(
    std::uint64_t symbol_index,
    DynamicStringError error) {
    return SceDynamicSymbolError{
        .code =
            SceDynamicSymbolErrorCode::
                dynamic_string_failure,
        .symbol_index = symbol_index,
        .symbol_error = std::nullopt,
        .string_error = std::move(error),
        .identity_error = std::nullopt,
    };
}

[[nodiscard]] SceDynamicSymbolError identity_failure(
    std::uint64_t symbol_index,
    SceSymbolIdentityError error) {
    return SceDynamicSymbolError{
        .code =
            SceDynamicSymbolErrorCode::
                sce_identity_failure,
        .symbol_index = symbol_index,
        .symbol_error = std::nullopt,
        .string_error = std::nullopt,
        .identity_error = std::move(error),
    };
}

}  // namespace

SceDynamicSymbolResult
materialize_sce_dynamic_symbol(
    const DynamicSymbolTableDescriptor& symbols,
    const DynamicStringTableDescriptor& strings,
    std::uint64_t symbol_index,
    const astraea::memory::InitializedImageView& image_view) {
    auto symbol =
        parse_dynamic_symbol(
            symbols,
            symbol_index,
            image_view);
    if (!symbol.has_value()) {
        return SceDynamicSymbolResult::failure(
            symbol_failure(
                symbol_index,
                symbol.error()));
    }

    const DynamicStringRef name_reference{
        .offset = symbol->name_offset,
        .source_entry_index =
            symbols.symtab_source_entry_index,
    };

    auto raw_name =
        resolve_dynamic_string(
            strings,
            name_reference,
            image_view);
    if (!raw_name.has_value()) {
        return SceDynamicSymbolResult::failure(
            string_failure(
                symbol_index,
                raw_name.error()));
    }

    auto name =
        parse_sce_dynamic_symbol_name(
            raw_name.value());
    if (!name.has_value()) {
        return SceDynamicSymbolResult::failure(
            identity_failure(
                symbol_index,
                name.error()));
    }

    return SceDynamicSymbolResult::success(
        SceDynamicSymbolRecord{
            .symbol = std::move(symbol.value()),
            .name = std::move(name.value()),
        });
}

}  // namespace astraea::loader
