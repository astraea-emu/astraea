#pragma once

#include <cstddef>
#include <optional>
#include <variant>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/loader/dynamic.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/dynamic_symbols.hpp>
#include <astraea/loader/elf64.hpp>
#include <astraea/loader/initial_stack.hpp>
#include <astraea/loader/tls_template.hpp>
#include <astraea/memory/initialized_image_view.hpp>
#include <astraea/memory/mapping.hpp>

namespace astraea::loader {

enum class GuestImageErrorCode {
    elf_parse_failure,
    mapping_failure,
    initialized_image_failure,
    duplicate_dynamic_segment,
    mapping_overlap_conflict,
    initial_stack_overlaps_load_mapping,
    dynamic_parse_failure,
    dynamic_metadata_failure,
    dynamic_symbol_failure,
    dynamic_relocation_failure,
    tls_failure,
    initial_stack_failure,
    host_allocation_failure,
    host_size_unrepresentable,
};

using GuestImageErrorCause = std::variant<
    std::monostate,
    ElfError,
    astraea::memory::MappingError,
    astraea::memory::InitializedImageError,
    DynamicError,
    DynamicMetadataError,
    DynamicSymbolError,
    DynamicRelocationError,
    TlsTemplateError,
    InitialStackError>;

struct GuestImageError {
    GuestImageErrorCode code;
    GuestImageErrorCause cause;
    std::optional<std::size_t> source_program_header_index;
    std::optional<std::size_t> conflicting_program_header_index;
};

struct GuestImageRequest {
    // Ownership is transferred into GuestImage on success.
    std::vector<std::byte> image_bytes;
    InitialStackRequest initial_stack;
    ElfParseProfile elf_profile = ElfParseProfile::generic;
};

class GuestImage {
public:
    std::vector<std::byte> image_bytes;
    ElfImage elf;
    std::vector<astraea::memory::MappingIntent> mappings;
    std::optional<DynamicTable> dynamic_table;
    std::optional<DynamicStringMetadata> dynamic_strings;
    std::optional<DynamicSymbolTableDescriptor> dynamic_symbols;
    GeneralDynamicRelocationMetadata general_relocations;
    std::optional<DynamicRelocationTableDescriptor> plt_relocations;
    std::optional<TlsTemplateDescriptor> tls;
    InitialStackImage initial_stack;

    [[nodiscard]] astraea::memory::InitializedImageView::CreateResult
    initialized_image_view() const;

    [[nodiscard]] astraea::core::Result<
        std::optional<std::vector<std::byte>>,
        TlsTemplateError>
    materialize_tls() const;
};

using GuestImageResult =
    astraea::core::Result<GuestImage, GuestImageError>;

[[nodiscard]] GuestImageResult build_guest_image(GuestImageRequest request);

}  // namespace astraea::loader
