#include <astraea/execution/linux_retail_diagnostic.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

#include <astraea/execution/linux_execution.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/loader/elf64.hpp>

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

[[nodiscard]] std::size_t generic_dynamic_dependency_count(
    const astraea::loader::GuestImage& image) noexcept {
    if (!image.dynamic_strings.has_value()) {
        return 0U;
    }
    return image.dynamic_strings->needed.size();
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

LinuxRetailDiagnosticStackResult
choose_linux_retail_diagnostic_stack(
    std::span<const std::byte> artifact_bytes) {
    constexpr std::uint64_t kPtLoad = 1U;
    constexpr std::uint64_t kCanonicalUserLimitExclusive =
        0x0000800000000000ULL;

    const auto parsed =
        astraea::loader::parse_elf64(
            artifact_bytes,
            astraea::loader::ElfParseProfile::ps5_sce);
    if (!parsed.has_value()) {
        return LinuxRetailDiagnosticStackResult::failure(
            LinuxRetailDiagnosticStackError{
                .code =
                    LinuxRetailDiagnosticStackErrorCode::
                        elf_parse_failure,
                .elf_error = parsed.error(),
                .detail = 0U,
            });
    }

    std::uint64_t highest_end = 0U;
    for (const auto& header :
         parsed->program_headers) {
        if (header.type != kPtLoad ||
            header.memory_size == 0U) {
            continue;
        }
        if (header.virtual_address >
            std::numeric_limits<std::uint64_t>::max() -
                header.memory_size) {
            return LinuxRetailDiagnosticStackResult::failure(
                LinuxRetailDiagnosticStackError{
                    .code =
                        LinuxRetailDiagnosticStackErrorCode::
                            load_range_overflow,
                    .elf_error = std::nullopt,
                    .detail = header.virtual_address,
                });
        }
        highest_end =
            std::max(
                highest_end,
                header.virtual_address +
                    header.memory_size);
    }

    if (highest_end >
        std::numeric_limits<std::uint64_t>::max() -
            kLinuxRetailDiagnosticStackGuard) {
        return LinuxRetailDiagnosticStackResult::failure(
            LinuxRetailDiagnosticStackError{
                .code =
                    LinuxRetailDiagnosticStackErrorCode::
                        address_space_exhausted,
                .elf_error = std::nullopt,
                .detail = highest_end,
            });
    }

    const auto guarded =
        highest_end +
        kLinuxRetailDiagnosticStackGuard;
    const auto mask =
        kLinuxRetailDiagnosticStackAlignment - 1U;
    if (guarded >
        std::numeric_limits<std::uint64_t>::max() -
            mask) {
        return LinuxRetailDiagnosticStackResult::failure(
            LinuxRetailDiagnosticStackError{
                .code =
                    LinuxRetailDiagnosticStackErrorCode::
                        address_space_exhausted,
                .elf_error = std::nullopt,
                .detail = guarded,
            });
    }

    const auto base =
        (guarded + mask) & ~mask;
    if (base >= kCanonicalUserLimitExclusive ||
        kLinuxRetailDiagnosticStackSize >
            kCanonicalUserLimitExclusive - base) {
        return LinuxRetailDiagnosticStackResult::failure(
            LinuxRetailDiagnosticStackError{
                .code =
                    LinuxRetailDiagnosticStackErrorCode::
                        address_space_exhausted,
                .elf_error = std::nullopt,
                .detail = base,
            });
    }

    const auto range =
        astraea::memory::GuestRange::create(
            astraea::memory::GuestAddress{base},
            astraea::memory::GuestSize{
                kLinuxRetailDiagnosticStackSize});
    if (!range.has_value()) {
        return LinuxRetailDiagnosticStackResult::failure(
            LinuxRetailDiagnosticStackError{
                .code =
                    LinuxRetailDiagnosticStackErrorCode::
                        address_space_exhausted,
                .elf_error = std::nullopt,
                .detail = base,
            });
    }
    return LinuxRetailDiagnosticStackResult::success(
        range.value());
}

LinuxRetailDiagnosticRuntimeBoundary
run_linux_retail_diagnostic_native_once(
    const astraea::loader::GuestImage& image) {
    const auto prepared =
        prepare_linux_guest_memory(image);
    if (!prepared.has_value()) {
        return LinuxRetailDiagnosticRuntimeBoundary{
            .kind =
                LinuxRetailDiagnosticRuntimeBoundaryKind::
                    native_backend_error,
            .guest_rip =
                astraea::memory::GuestAddress{
                    image.elf.header.entry},
            .syscall_number = 0,
            .audit_arch = 0U,
            .guest_fault = std::nullopt,
            .backend_error = prepared.error(),
        };
    }

    const auto entered =
        enter_linux_guest_with_seccomp_syscall_trap(
            image,
            prepared.value(),
            make_synthetic_initial_context(image));
    if (!entered.has_value()) {
        return LinuxRetailDiagnosticRuntimeBoundary{
            .kind =
                LinuxRetailDiagnosticRuntimeBoundaryKind::
                    native_backend_error,
            .guest_rip =
                astraea::memory::GuestAddress{
                    image.elf.header.entry},
            .syscall_number = 0,
            .audit_arch = 0U,
            .guest_fault = std::nullopt,
            .backend_error = entered.error(),
        };
    }

    if (const auto* syscall =
            std::get_if<LinuxSeccompSyscallTrap>(
                &entered.value());
        syscall != nullptr) {
        return LinuxRetailDiagnosticRuntimeBoundary{
            .kind =
                LinuxRetailDiagnosticRuntimeBoundaryKind::
                    unsupported_syscall,
            .guest_rip = syscall->guest_rip,
            .syscall_number = syscall->syscall_number,
            .audit_arch = syscall->audit_arch,
            .guest_fault = std::nullopt,
            .backend_error = std::nullopt,
        };
    }

    const auto& stop =
        std::get<ExecutionStop>(
            entered.value());
    if (stop.reason ==
            ExecutionStopReason::guest_fault &&
        stop.has_fault) {
        return LinuxRetailDiagnosticRuntimeBoundary{
            .kind =
                LinuxRetailDiagnosticRuntimeBoundaryKind::
                    guest_fault,
            .guest_rip =
                astraea::memory::GuestAddress{
                    stop.context.rip},
            .syscall_number = 0,
            .audit_arch = 0U,
            .guest_fault = stop.fault,
            .backend_error = std::nullopt,
        };
    }

    return LinuxRetailDiagnosticRuntimeBoundary{
        .kind =
            LinuxRetailDiagnosticRuntimeBoundaryKind::
                native_backend_error,
        .guest_rip =
            astraea::memory::GuestAddress{
                stop.context.rip},
        .syscall_number = 0,
        .audit_arch = 0U,
        .guest_fault = std::nullopt,
        .backend_error =
            NativeBackendError{
                .code =
                    NativeBackendErrorCode::
                        internal_transition_failure,
                .has_guest_address = true,
                .guest_address = stop.context.rip,
                .has_host_code = false,
                .host_code = 0U,
            },
    };
}

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
            .sce_dynamic_metadata_error = std::nullopt,
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
            .sce_dynamic_metadata_error = std::nullopt,
            .dynamic_dependency_count = 0,
            .relocation_count = 0,
            .image =
                std::optional<astraea::loader::GuestImage>{
                    std::move(image)},
        };
    }

    std::size_t dependencies =
        generic_dynamic_dependency_count(image);

    if (image.dynamic_table.has_value()) {
        const auto sce =
            astraea::loader::build_sce_dynamic_metadata(
                *image.dynamic_table);
        if (!sce.has_value()) {
            return LinuxRetailDiagnosticPreflight{
                .boundary =
                    LinuxRetailDiagnosticPreflightBoundaryKind::
                        sce_dynamic_metadata_rejected,
                .loader_error = std::nullopt,
                .sce_dynamic_metadata_error =
                    sce.error(),
                .dynamic_dependency_count = 0,
                .relocation_count = 0,
                .image =
                    std::optional<astraea::loader::GuestImage>{
                        std::move(image)},
            };
        }

        for (const auto& record : sce->records) {
            if (record.kind ==
                    astraea::loader::
                        SceDynamicTagKind::needed_module ||
                record.kind ==
                    astraea::loader::
                        SceDynamicTagKind::import_library) {
                if (dependencies !=
                    std::numeric_limits<std::size_t>::max()) {
                    ++dependencies;
                }
            }
        }
    }

    if (dependencies != 0U) {
        return LinuxRetailDiagnosticPreflight{
            .boundary =
                LinuxRetailDiagnosticPreflightBoundaryKind::
                    unsupported_dynamic_dependencies,
            .loader_error = std::nullopt,
            .sce_dynamic_metadata_error = std::nullopt,
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
            .sce_dynamic_metadata_error = std::nullopt,
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
            .sce_dynamic_metadata_error = std::nullopt,
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
        .sce_dynamic_metadata_error = std::nullopt,
        .dynamic_dependency_count = 0,
        .relocation_count = 0,
        .image =
            std::optional<astraea::loader::GuestImage>{
                std::move(image)},
    };
}

}  // namespace astraea::execution
