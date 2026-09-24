#include <astraea/execution/linux_retail_diagnostic.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/memory/mapping.hpp>

namespace astraea::execution {
namespace {

[[nodiscard]] bool entry_is_executable(
    const astraea::loader::GuestImage& image) noexcept {
    const astraea::memory::GuestAddress entry{
        image.elf.header.entry};

    for (const auto& mapping : image.mappings) {
        if (mapping.range.contains(entry) &&
            mapping.permissions.has(
                astraea::memory::GuestPermission::execute)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t dynamic_dependency_count(
    const astraea::loader::GuestImage& image) {
    std::size_t count = 0;

    if (image.dynamic_strings.has_value()) {
        count = image.dynamic_strings->needed.size();
    }

    if (!image.dynamic_table.has_value()) {
        return count;
    }

    const auto sce =
        astraea::loader::build_sce_dynamic_metadata(
            *image.dynamic_table);
    if (!sce.has_value()) {
        // GuestImage has already structurally validated the dynamic table.
        // If the evidence-only SCE metadata view cannot be materialized, fail
        // closed by reporting unresolved dynamic work instead of admitting
        // native entry.
        return std::max<std::size_t>(count, 1U);
    }

    for (const auto& record : sce->records) {
        if (record.kind ==
                astraea::loader::
                    SceDynamicTagKind::needed_module ||
            record.kind ==
                astraea::loader::
                    SceDynamicTagKind::import_library) {
            if (count !=
                std::numeric_limits<std::size_t>::max()) {
                ++count;
            }
        }
    }

    return count;
}

[[nodiscard]] std::uint64_t relocation_count(
    const astraea::loader::GuestImage& image) noexcept {
    std::uint64_t count = 0;

    const auto add =
        [&count](const std::optional<
                     astraea::loader::
                         DynamicRelocationTableDescriptor>&
                     descriptor) {
            if (!descriptor.has_value()) {
                return;
            }
            if (descriptor->count >
                std::numeric_limits<std::uint64_t>::max() -
                    count) {
                count =
                    std::numeric_limits<std::uint64_t>::max();
                return;
            }
            count += descriptor->count;
        };

    add(image.general_relocations.rel);
    add(image.general_relocations.rela);
    add(image.plt_relocations);
    return count;
}

}  // namespace

LinuxRetailDiagnosticPreflight
preflight_linux_retail_diagnostic(
    LinuxRetailDiagnosticPreflightRequest request) {
    auto built =
        astraea::loader::build_guest_image(
            astraea::loader::GuestImageRequest{
                .image_bytes =
                    std::move(request.artifact_bytes),
                .initial_stack =
                    astraea::loader::InitialStackRequest{
                        .storage =
                            request.stack_storage,
                        .arguments =
                            std::move(request.arguments),
                        .environment =
                            std::move(request.environment),
                        .auxiliary_vector =
                            std::move(
                                request.auxiliary_vector),
                    },
                .elf_profile =
                    astraea::loader::
                        ElfParseProfile::ps5_sce,
            });

    if (!built.has_value()) {
        return LinuxRetailDiagnosticPreflight{
            .boundary =
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    loader_rejected,
            .loader_error =
                std::move(built.error()),
            .dynamic_dependency_count = 0,
            .relocation_count = 0,
            .image = std::nullopt,
        };
    }

    auto image = std::move(built.value());

    if (!entry_is_executable(image)) {
        return LinuxRetailDiagnosticPreflight{
            .boundary =
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    entry_not_executable,
            .loader_error = std::nullopt,
            .dynamic_dependency_count = 0,
            .relocation_count = 0,
            .image =
                std::optional<astraea::loader::GuestImage>{
                    std::move(image)},
        };
    }

    const auto dependencies =
        dynamic_dependency_count(image);
    if (dependencies != 0U) {
        return LinuxRetailDiagnosticPreflight{
            .boundary =
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    unsupported_dynamic_dependencies,
            .loader_error = std::nullopt,
            .dynamic_dependency_count =
                dependencies,
            .relocation_count = 0,
            .image =
                std::optional<astraea::loader::GuestImage>{
                    std::move(image)},
        };
    }

    const auto relocations =
        relocation_count(image);
    if (relocations != 0U) {
        return LinuxRetailDiagnosticPreflight{
            .boundary =
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    unsupported_relocations,
            .loader_error = std::nullopt,
            .dynamic_dependency_count = 0,
            .relocation_count =
                relocations,
            .image =
                std::optional<astraea::loader::GuestImage>{
                    std::move(image)},
        };
    }

    if (image.tls.has_value()) {
        return LinuxRetailDiagnosticPreflight{
            .boundary =
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    unsupported_tls,
            .loader_error = std::nullopt,
            .dynamic_dependency_count = 0,
            .relocation_count = 0,
            .image =
                std::optional<astraea::loader::GuestImage>{
                    std::move(image)},
        };
    }

    return LinuxRetailDiagnosticPreflight{
        .boundary =
            LinuxRetailDiagnosticPreflightBoundaryKind::
                ready_for_native_entry,
        .loader_error = std::nullopt,
        .dynamic_dependency_count = 0,
        .relocation_count = 0,
        .image =
            std::optional<astraea::loader::GuestImage>{
                std::move(image)},
    };
}

}  // namespace astraea::execution
