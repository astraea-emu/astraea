#include "retail_dependencies.hpp"

#include "artifact_file.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <astraea/execution/retail_closure_profile.hpp>
#include <astraea/loader/dynamic_metadata.hpp>
#include <astraea/loader/dynamic_string.hpp>
#include <astraea/loader/dynamic_symbols.hpp>
#include <astraea/loader/dynamic_relocations.hpp>
#include <astraea/loader/sce_symbol_identity.hpp>
#include <astraea/loader/guest_image.hpp>

namespace astraea::app {
namespace {

constexpr std::size_t kMaxManifestRecords = 4096U;
constexpr std::uint64_t kMaxDependencyNameBytes = 256U;

// Hex is lossless and cannot emit terminal escapes, invalid UTF-8 or
// attacker-controlled line breaks into command output.
[[nodiscard]] std::string encode_hex(std::string_view bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(bytes.size() * 2U);
    for (char raw_byte : bytes) {
        const auto byte = static_cast<unsigned char>(raw_byte);
        encoded.push_back(digits[byte >> 4U]);
        encoded.push_back(digits[byte & 0x0fU]);
    }
    return encoded;
}

// Reuse the production validated string reader, but cap each individual
// reference BEFORE reading or allocating the resolved string.
[[nodiscard]] std::string read_bounded_name(
    const astraea::loader::DynamicStringTableDescriptor& original,
    const astraea::loader::DynamicStringRef& reference,
    const astraea::memory::InitializedImageView& view) {
    const auto table_size = original.range.size().value();
    if (reference.offset >= table_size) {
        throw std::runtime_error("name_offset_out_of_bounds");
    }
    const auto remaining = table_size - reference.offset;
    const auto size = reference.offset +
        std::min(remaining, kMaxDependencyNameBytes + 1U);
    const auto range = astraea::memory::GuestRange::create(
        original.range.base(),
        astraea::memory::GuestSize{size});
    if (!range.has_value()) {
        throw std::runtime_error("name_range_invalid");
    }
    const astraea::loader::DynamicStringTableDescriptor limited{
        .range = range.value(),
    };
    const auto name = astraea::loader::resolve_dynamic_string(
        limited, reference, view);
    if (!name.has_value()) {
        throw std::runtime_error("name_unreadable_or_unterminated");
    }
    return name.value();
}

[[nodiscard]] std::string manifest_for(
    const astraea::loader::GuestImage& image) {
    std::ostringstream out;
    out << "Astraea dependency manifest v0\n"
        << "execution=none\n"
        << "identity_policy=opaque\n";
    const auto view = image.initialized_image_view();
    if (!view.has_value()) {
        throw std::runtime_error("initialized_image_unavailable");
    }

    std::set<std::string> needed;
    const auto& strings = image.dynamic_strings;
    const std::size_t count =
        strings.has_value() ? strings->needed.size() : 0U;
    if (count > kMaxManifestRecords) {
        throw std::runtime_error("too_many_generic_dependencies");
    }
    out << "generic_needed_records=" << count << '\n';
    for (std::size_t i = 0; i < count; ++i) {
        if (!strings->string_table.has_value()) {
            throw std::runtime_error("missing_dynamic_string_table");
        }
        const auto& ref = strings->needed[i];
        const auto name = read_bounded_name(
            strings->string_table.value(), ref, view.value());
        if (name.empty()) {
            throw std::runtime_error("empty_dependency_name");
        }
        const auto [iter, fresh] = needed.insert(name);
        (void)iter;
        out << "generic_needed[" << i << "].source_index="
            << ref.source_entry_index << '\n'
            << "generic_needed[" << i << "].offset="
            << ref.offset << '\n'
            << "generic_needed[" << i << "].name_hex="
            << encode_hex(name) << '\n'
            << "generic_needed[" << i << "].duplicate="
            << (fresh ? 0 : 1) << '\n';
    }
    out << "generic_needed_unique_names=" << needed.size() << '\n';

    std::set<std::uint64_t> needed_raw;
    std::set<std::uint64_t> libraries_raw;
    std::size_t needed_records = 0U;
    std::size_t library_records = 0U;

    if (image.dynamic_table.has_value()) {
        if (image.dynamic_table->entries.size() > kMaxManifestRecords) {
            throw std::runtime_error("too_many_dynamic_records");
        }
        const auto sce = astraea::loader::build_sce_dynamic_metadata(
            image.dynamic_table.value());
        if (!sce.has_value()) {
            throw std::runtime_error("sce_dynamic_metadata_invalid");
        }
        for (const auto& record : sce->records) {
            const bool is_needed =
                record.kind ==
                astraea::loader::SceDynamicTagKind::needed_module;
            const bool is_library =
                record.kind ==
                astraea::loader::SceDynamicTagKind::import_library;
            if (!is_needed && !is_library) {
                continue;
            }
            const std::size_t i =
                is_needed ? needed_records++ : library_records++;
            auto& seen = is_needed ? needed_raw : libraries_raw;
            const auto [iter, fresh] = seen.insert(record.raw_value);
            (void)iter;
            const char* key =
                is_needed ? "sce_needed_module" : "sce_import_library";
            out << key << "[" << i << "].source_index="
                << record.source_entry_index << '\n'
                << key << "[" << i << "].raw_tag=0x"
                << std::hex << static_cast<std::uint64_t>(record.raw_tag)
                << std::dec << '\n'
                << key << "[" << i << "].raw_value=0x"
                << std::hex << record.raw_value
                << std::dec << '\n'
                << key << "[" << i << "].duplicate_raw="
                << (fresh ? 0 : 1) << '\n';
        }
    }
    out << "sce_needed_module_records=" << needed_records << '\n'
        << "sce_needed_module_unique_raw=" << needed_raw.size() << '\n'
        << "sce_import_library_records=" << library_records << '\n'
        << "sce_import_library_unique_raw=" << libraries_raw.size() << '\n';

    // Stage B: inventory symbols using existing checked ELF symbol and SCE
    // long-form parsers. Do not resolve an import, infer a symbol's SDK name,
    // or convert a symbol table row into a called function.
    const auto symbol_count = image.dynamic_symbols.has_value()
        ? image.dynamic_symbols->symbol_count : 0U;
    if (symbol_count > kMaxManifestRecords) {
        throw std::runtime_error("too_many_dynamic_symbols");
    }
    std::set<std::string> symbol_names;
    out << "dynamic_symbol_records=" << symbol_count << '\n';
    for (std::uint64_t i = 0; i < symbol_count; ++i) {
        auto symbol = astraea::loader::parse_dynamic_symbol(
            image.dynamic_symbols.value(), i, view.value());
        if (!symbol.has_value()) {
            throw std::runtime_error("dynamic_symbol_unreadable");
        }
        std::string raw_name;
        if (symbol->name_offset != 0U) {
            if (!strings.has_value() ||
                !strings->string_table.has_value()) {
                throw std::runtime_error("symbol_string_table_unavailable");
            }
            const astraea::loader::DynamicStringRef name_ref{
                .offset = symbol->name_offset,
                .source_entry_index =
                    image.dynamic_symbols->symtab_source_entry_index,
            };
            raw_name = read_bounded_name(
                strings->string_table.value(), name_ref, view.value());
        }
        const auto name = astraea::loader::parse_sce_dynamic_symbol_name(
            raw_name);
        if (!name.has_value()) {
            throw std::runtime_error("malformed_sce_symbol_identity");
        }
        bool duplicate = false;
        if (!raw_name.empty()) {
            const auto [iter, fresh] = symbol_names.insert(raw_name);
            (void)iter;
            duplicate = !fresh;
        }
        out << "symbol[" << i << "].name_hex="
            << encode_hex(raw_name) << '\n'
            << "symbol[" << i << "].undefined="
            << (symbol->section_index_raw == 0U ? 1 : 0) << '\n'
            << "symbol[" << i << "].binding="
            << static_cast<unsigned int>(symbol->binding()) << '\n'
            << "symbol[" << i << "].type="
            << static_cast<unsigned int>(symbol->type()) << '\n'
            << "symbol[" << i << "].name_duplicate="
            << (duplicate ? 1 : 0) << '\n'
            << "symbol[" << i << "].sce_longform="
            << (name->identity.has_value() ? 1 : 0) << '\n';
        if (name->identity.has_value()) {
            out << "symbol[" << i << "].nid_hex="
                << encode_hex(name->identity->nid) << '\n'
                << "symbol[" << i << "].library_id_hex="
                << encode_hex(name->identity->library_id) << '\n'
                << "symbol[" << i << "].module_id_hex="
                << encode_hex(name->identity->module_id) << '\n';
        }
    }
    out << "dynamic_symbol_unique_nonempty_names="
        << symbol_names.size() << '\n';

    // Stage C: use the existing validated relocation reader for a static
    // census. A relocation symbol index is a reference, not a resolved HLE.
    // The loader's x86-64 relocation semantics are NOT applied by this path.
    std::size_t total_relocations = 0U;
    std::set<std::uint32_t> relocation_symbols;
    const auto read_table = [&](
        const std::optional<
            astraea::loader::DynamicRelocationTableDescriptor>& descriptor,
        std::string_view kind) {
        if (!descriptor.has_value()) {
            return;
        }
        if (descriptor->count > kMaxManifestRecords ||
            total_relocations >
                kMaxManifestRecords -
                    static_cast<std::size_t>(descriptor->count)) {
            throw std::runtime_error("too_many_relocations");
        }
        if (!image.dynamic_symbols.has_value()) {
            throw std::runtime_error("relocations_missing_symbol_table");
        }
        for (std::uint64_t i = 0; i < descriptor->count; ++i) {
            const auto relocation = astraea::loader::parse_dynamic_relocation(
                descriptor.value(),
                i,
                image.dynamic_symbols.value(),
                view.value());
            if (!relocation.has_value()) {
                throw std::runtime_error("relocation_unreadable");
            }
            relocation_symbols.insert(relocation->symbol_index);
            const auto row = total_relocations++;
            out << "relocation[" << row << "].table="
                << kind << '\n'
                << "relocation[" << row << "].table_index="
                << relocation->table_index << '\n'
                << "relocation[" << row << "].target=0x"
                << std::hex << relocation->target.value() << std::dec << '\n'
                << "relocation[" << row << "].type="
                << relocation->relocation_type << '\n'
                << "relocation[" << row << "].symbol_index="
                << relocation->symbol_index << '\n'
                << "relocation[" << row << "].raw_info=0x"
                << std::hex << relocation->raw_info << std::dec << '\n'
                << "relocation[" << row << "].has_addend="
                << (relocation->addend.has_value() ? 1 : 0) << '\n';
            if (relocation->addend.has_value()) {
                out << "relocation[" << row << "].addend="
                    << relocation->addend.value() << '\n';
            }
        }
    };
    read_table(image.general_relocations.rel, "rel");
    read_table(image.general_relocations.rela, "rela");
    read_table(image.plt_relocations,
        image.plt_relocations.has_value() &&
        image.plt_relocations->kind ==
            astraea::loader::RelocationTableKind::plt_rel
        ? "plt_rel" : "plt_rela");
    out << "relocation_records=" << total_relocations << '\n'
        << "relocation_unique_symbol_indices="
        << relocation_symbols.size() << '\n'
        << "relocation_application=not_attempted\n"
        << "resolution=not_attempted\n"
        << "guest_instructions=0\n";
    return out.str();
}

}  // namespace

int run_retail_dependency_manifest(std::string_view artifact_path) {
    auto artifact = read_artifact_file(artifact_path);
    if (!artifact.has_value()) {
        std::cerr << "Astraea dependency manifest v0\n"
                  << "manifest_error=artifact_read_failure\n"
                  << "artifact_error="
                  << artifact_read_error_name(artifact.error()) << '\n';
        return 3;
    }

    try {
        const auto stack = astraea::execution::choose_retail_analysis_stack(
            artifact.value());
        if (!stack.has_value()) {
            throw std::runtime_error("planning_stack_unavailable");
        }
        auto image = astraea::loader::build_guest_image(
            astraea::loader::GuestImageRequest{
                .image_bytes = std::move(artifact.value()),
                .initial_stack = astraea::loader::InitialStackRequest{
                    .storage = stack.value(),
                    .arguments = {"astraea-manifest"},
                    .environment = {},
                    .auxiliary_vector = {},
                },
                .elf_profile =
                    astraea::loader::ElfParseProfile::ps5_sce,
            });
        if (!image.has_value()) {
            throw std::runtime_error("guest_image_failure");
        }
        // Construct the whole report before emitting any of it, so a
        // malformed later record cannot leave a misleading partial manifest.
        const auto report = manifest_for(image.value());
        std::cout << report;
        return 0;
    } catch (const std::bad_alloc&) {
        std::cerr << "Astraea dependency manifest v0\n"
                  << "manifest_error=host_allocation_failure\n";
        return 4;
    } catch (const std::length_error&) {
        std::cerr << "Astraea dependency manifest v0\n"
                  << "manifest_error=host_allocation_failure\n";
        return 4;
    } catch (const std::runtime_error& error) {
        std::cerr << "Astraea dependency manifest v0\n"
                  << "manifest_error=" << error.what() << '\n';
        return 4;
    }
}

}  // namespace astraea::app
