#include <astraea/trace/v0.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace astraea::trace {
namespace {

constexpr std::uint64_t kMaxExactJsonInteger =
    9007199254740991ULL;

[[nodiscard]] TraceError trace_error(
    TraceErrorCode code,
    std::optional<std::uint64_t> event_id =
        std::nullopt) noexcept {
    return TraceError{
        .code = code,
        .has_event_id = event_id.has_value(),
        .event_id =
            event_id.has_value()
                ? event_id.value()
                : 0,
    };
}

[[nodiscard]] bool is_identifier_char(
    char value) noexcept {
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') ||
           value == '.' ||
           value == '_' ||
           value == ':' ||
           value == '-';
}

[[nodiscard]] bool valid_identifier(
    std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }

    const char first = value.front();
    if (!((first >= 'a' && first <= 'z') ||
          (first >= '0' && first <= '9'))) {
        return false;
    }

    return std::all_of(
        value.begin(),
        value.end(),
        [](char character) {
            return is_identifier_char(character);
        });
}

[[nodiscard]] bool valid_sha256(
    std::string_view value) noexcept {
    constexpr std::string_view kPrefix =
        "sha256:";

    if (value.size() !=
            kPrefix.size() + 64U ||
        !value.starts_with(kPrefix)) {
        return false;
    }

    for (std::size_t index = kPrefix.size();
         index < value.size();
         ++index) {
        const char character = value[index];
        const bool decimal =
            character >= '0' &&
            character <= '9';
        const bool lower_hex =
            character >= 'a' &&
            character <= 'f';
        if (!decimal && !lower_hex) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool continuation(
    unsigned char value) noexcept {
    return value >= 0x80U &&
           value <= 0xbfU;
}

[[nodiscard]] bool valid_utf8(
    std::string_view value) noexcept {
    std::size_t index = 0;

    while (index < value.size()) {
        const auto first =
            static_cast<unsigned char>(
                value[index]);

        if (first <= 0x7fU) {
            ++index;
            continue;
        }

        if (first >= 0xc2U &&
            first <= 0xdfU) {
            if (index + 1U >= value.size() ||
                !continuation(
                    static_cast<unsigned char>(
                        value[index + 1U]))) {
                return false;
            }
            index += 2U;
            continue;
        }

        if (first >= 0xe0U &&
            first <= 0xefU) {
            if (index + 2U >= value.size()) {
                return false;
            }

            const auto second =
                static_cast<unsigned char>(
                    value[index + 1U]);
            const auto third =
                static_cast<unsigned char>(
                    value[index + 2U]);

            if (!continuation(third)) {
                return false;
            }

            if (first == 0xe0U) {
                if (second < 0xa0U ||
                    second > 0xbfU) {
                    return false;
                }
            } else if (first == 0xedU) {
                if (second < 0x80U ||
                    second > 0x9fU) {
                    return false;
                }
            } else if (!continuation(second)) {
                return false;
            }

            index += 3U;
            continue;
        }

        if (first >= 0xf0U &&
            first <= 0xf4U) {
            if (index + 3U >= value.size()) {
                return false;
            }

            const auto second =
                static_cast<unsigned char>(
                    value[index + 1U]);
            const auto third =
                static_cast<unsigned char>(
                    value[index + 2U]);
            const auto fourth =
                static_cast<unsigned char>(
                    value[index + 3U]);

            if (!continuation(third) ||
                !continuation(fourth)) {
                return false;
            }

            if (first == 0xf0U) {
                if (second < 0x90U ||
                    second > 0xbfU) {
                    return false;
                }
            } else if (first == 0xf4U) {
                if (second < 0x80U ||
                    second > 0x8fU) {
                    return false;
                }
            } else if (!continuation(second)) {
                return false;
            }

            index += 4U;
            continue;
        }

        return false;
    }

    return true;
}

[[nodiscard]] bool valid_value(
    const TraceValueV0& value) noexcept {
    if (const auto* text =
            std::get_if<std::string>(&value);
        text != nullptr) {
        return valid_utf8(*text);
    }
    return true;
}

[[nodiscard]] std::optional<TraceError>
normalize_fields(
    std::vector<TraceFieldV0>& fields,
    TraceErrorCode duplicate_code,
    std::optional<std::uint64_t> event_id) {
    for (const auto& field : fields) {
        if (!valid_identifier(field.name)) {
            return trace_error(
                TraceErrorCode::
                    invalid_identifier,
                event_id);
        }

        if (!valid_value(field.value)) {
            return trace_error(
                TraceErrorCode::invalid_utf8,
                event_id);
        }
    }

    std::sort(
        fields.begin(),
        fields.end(),
        [](const TraceFieldV0& lhs,
           const TraceFieldV0& rhs) {
            return lhs.name < rhs.name;
        });

    for (std::size_t index = 1;
         index < fields.size();
         ++index) {
        if (fields[index - 1U].name ==
            fields[index].name) {
            return trace_error(
                duplicate_code,
                event_id);
        }
    }

    return std::nullopt;
}

void append_json_string(
    std::string& output,
    std::string_view value) {
    constexpr std::array<char, 16> kHex{
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'a', 'b',
        'c', 'd', 'e', 'f',
    };

    output.push_back('"');

    for (const char raw : value) {
        const auto byte =
            static_cast<unsigned char>(raw);
        if (raw == '"') {
            output.append("\\\"");
        } else if (raw == '\\') {
            output.append("\\\\");
        } else if (byte < 0x20U) {
            output.append("\\u00");
            output.push_back(
                kHex[(byte >> 4U) & 0x0fU]);
            output.push_back(
                kHex[byte & 0x0fU]);
        } else {
            output.push_back(raw);
        }
    }

    output.push_back('"');
}

void append_u64_hex(
    std::string& output,
    std::uint64_t value) {
    constexpr std::array<char, 16> kHex{
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'a', 'b',
        'c', 'd', 'e', 'f',
    };

    output.append("\"0x");
    for (int shift = 60;
         shift >= 0;
         shift -= 4) {
        output.push_back(
            kHex[
                static_cast<std::size_t>(
                    (value >>
                     static_cast<unsigned int>(
                         shift)) &
                    0x0fU)]);
    }
    output.push_back('"');
}

void append_decimal(
    std::string& output,
    std::uint64_t value) {
    std::array<char, 32> buffer{};
    const auto result =
        std::to_chars(
            buffer.data(),
            buffer.data() + buffer.size(),
            value);
    if (result.ec != std::errc{}) {
        throw std::length_error(
            "trace integer serialization");
    }

    output.append(
        buffer.data(),
        static_cast<std::size_t>(
            result.ptr - buffer.data()));
}

void append_bytes_hex(
    std::string& output,
    const std::vector<std::byte>& bytes) {
    constexpr std::array<char, 16> kHex{
        '0', '1', '2', '3',
        '4', '5', '6', '7',
        '8', '9', 'a', 'b',
        'c', 'd', 'e', 'f',
    };

    output.push_back('"');
    for (const std::byte value : bytes) {
        const auto byte =
            std::to_integer<unsigned int>(
                value);
        output.push_back(
            kHex[(byte >> 4U) & 0x0fU]);
        output.push_back(
            kHex[byte & 0x0fU]);
    }
    output.push_back('"');
}

void append_value(
    std::string& output,
    const TraceValueV0& value) {
    if (const auto* integer =
            std::get_if<std::uint64_t>(&value);
        integer != nullptr) {
        output.append(
            "{\"type\":\"u64\",\"value\":");
        append_u64_hex(output, *integer);
        output.push_back('}');
        return;
    }

    if (const auto* boolean =
            std::get_if<bool>(&value);
        boolean != nullptr) {
        output.append(
            "{\"type\":\"bool\",\"value\":");
        output.append(
            *boolean ? "true" : "false");
        output.push_back('}');
        return;
    }

    if (const auto* text =
            std::get_if<std::string>(&value);
        text != nullptr) {
        output.append(
            "{\"type\":\"utf8\",\"value\":");
        append_json_string(output, *text);
        output.push_back('}');
        return;
    }

    const auto& bytes =
        std::get<std::vector<std::byte>>(
            value);
    output.append(
        "{\"encoding\":\"hex\",\"type\":\"bytes\",\"value\":");
    append_bytes_hex(output, bytes);
    output.push_back('}');
}

void append_fields(
    std::string& output,
    const std::vector<TraceFieldV0>& fields) {
    output.push_back('{');
    for (std::size_t index = 0;
         index < fields.size();
         ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_json_string(
            output,
            fields[index].name);
        output.push_back(':');
        append_value(
            output,
            fields[index].value);
    }
    output.push_back('}');
}

void append_event(
    std::string& output,
    const TraceEventV0& event) {
    output.append("{\"diagnostics\":");
    append_fields(
        output,
        event.diagnostics);

    if (event.guest.has_value()) {
        output.append(
            ",\"guest\":{\"kind\":\"object_offset\",\"object\":");
        append_json_string(
            output,
            event.guest->object);
        output.append(",\"offset\":");
        append_u64_hex(
            output,
            event.guest->offset);
        output.push_back('}');
    }

    output.append(",\"id\":");
    append_u64_hex(output, event.id);
    output.append(",\"stable\":");
    append_fields(
        output,
        event.stable);
    output.append(",\"subsystem\":");
    append_json_string(
        output,
        event.subsystem);
    output.append(",\"type\":");
    append_json_string(
        output,
        event.type);
    output.push_back('}');
}

void append_provenance(
    std::string& output,
    const TraceProvenanceV0& provenance) {
    output.append(
        "{\"artifact_digests\":[");
    for (std::size_t index = 0;
         index < provenance.artifact_digests.size();
         ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        append_json_string(
            output,
            provenance.artifact_digests[index]);
    }

    output.append("],\"producer\":{");
    bool needs_comma = false;
    if (provenance.producer.commit.has_value()) {
        output.append("\"commit\":");
        append_json_string(
            output,
            provenance.producer.commit.value());
        needs_comma = true;
    }

    if (needs_comma) {
        output.push_back(',');
    }
    output.append("\"name\":");
    append_json_string(
        output,
        provenance.producer.name);
    output.append(",\"version\":");
    append_json_string(
        output,
        provenance.producer.version);
    output.append("},\"source_kind\":");
    append_json_string(
        output,
        provenance.source_kind);
    output.push_back('}');
}

void append_run(
    std::string& output,
    const TraceRunMetadataV0& run) {
    output.append("{\"architecture\":");
    append_json_string(
        output,
        run.architecture);

    if (run.case_sha256.has_value()) {
        output.append(",\"case_sha256\":");
        append_json_string(
            output,
            run.case_sha256.value());
    }

    output.append(",\"platform_family\":");
    append_json_string(
        output,
        run.platform_family);

    if (run.probe_id.has_value()) {
        output.append(",\"probe\":{\"id\":");
        append_json_string(
            output,
            run.probe_id.value());
        output.append(",\"version\":");
        append_decimal(
            output,
            run.probe_version.value());
        output.push_back('}');
    }

    output.append(",\"target_kind\":");
    append_json_string(
        output,
        run.target_kind);
    output.push_back('}');
}

}  // namespace

TraceNormalizeResult normalize_trace_v0(
    TraceDocumentV0 trace) {
    try {
        if (!valid_identifier(
                trace.run.target_kind) ||
            !valid_identifier(
                trace.run.platform_family) ||
            !valid_identifier(
                trace.run.architecture) ||
            !valid_identifier(
                trace.provenance.source_kind) ||
            !valid_identifier(
                trace.provenance.producer.name) ||
            !valid_identifier(
                trace.provenance.producer.version) ||
            (trace.provenance.producer.commit.has_value() &&
             !valid_identifier(
                 trace.provenance.producer.commit.value()))) {
            return TraceNormalizeResult::failure(
                trace_error(
                    TraceErrorCode::
                        invalid_identifier));
        }

        if (trace.run.probe_id.has_value() !=
            trace.run.probe_version.has_value()) {
            return TraceNormalizeResult::failure(
                trace_error(
                    TraceErrorCode::
                        incomplete_probe_identity));
        }

        if (trace.run.probe_id.has_value()) {
            if (!valid_identifier(
                    trace.run.probe_id.value())) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            invalid_identifier));
            }

            if (trace.run.probe_version.value() >
                kMaxExactJsonInteger) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            probe_version_out_of_range));
            }
        }

        if (trace.run.case_sha256.has_value()) {
            if (!trace.run.probe_id.has_value()) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            incomplete_probe_identity));
            }

            if (!valid_sha256(
                    trace.run.case_sha256.value())) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            malformed_sha256));
            }
        }

        for (const auto& digest :
             trace.provenance.artifact_digests) {
            if (!valid_sha256(digest)) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            malformed_sha256));
            }
        }

        std::sort(
            trace.provenance.artifact_digests.begin(),
            trace.provenance.artifact_digests.end());

        for (std::size_t index = 1;
             index <
                 trace.provenance.artifact_digests.size();
             ++index) {
            if (trace.provenance.
                    artifact_digests[index - 1U] ==
                trace.provenance.
                    artifact_digests[index]) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            duplicate_artifact_digest));
            }
        }

        if (auto error =
                normalize_fields(
                    trace.diagnostics,
                    TraceErrorCode::
                        duplicate_diagnostic_field,
                    std::nullopt);
            error.has_value()) {
            return TraceNormalizeResult::failure(
                error.value());
        }

        bool have_previous = false;
        std::uint64_t previous_id = 0;

        for (auto& event : trace.events) {
            if (have_previous &&
                event.id <= previous_id) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            non_monotonic_event_id,
                        event.id));
            }
            previous_id = event.id;
            have_previous = true;

            if (!valid_identifier(
                    event.subsystem) ||
                !valid_identifier(
                    event.type)) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            invalid_identifier,
                        event.id));
            }

            if (event.guest.has_value() &&
                !valid_identifier(
                    event.guest->object)) {
                return TraceNormalizeResult::failure(
                    trace_error(
                        TraceErrorCode::
                            invalid_identifier,
                        event.id));
            }

            if (auto error =
                    normalize_fields(
                        event.stable,
                        TraceErrorCode::
                            duplicate_stable_field,
                        event.id);
                error.has_value()) {
                return TraceNormalizeResult::failure(
                    error.value());
            }

            if (auto error =
                    normalize_fields(
                        event.diagnostics,
                        TraceErrorCode::
                            duplicate_diagnostic_field,
                        event.id);
                error.has_value()) {
                return TraceNormalizeResult::failure(
                    error.value());
            }
        }

        return TraceNormalizeResult::success(
            std::move(trace));
    } catch (const std::bad_alloc&) {
        return TraceNormalizeResult::failure(
            trace_error(
                TraceErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return TraceNormalizeResult::failure(
            trace_error(
                TraceErrorCode::
                    host_size_unrepresentable));
    }
}

TraceSerializeResult serialize_trace_v0(
    TraceDocumentV0 trace) {
    auto normalized =
        normalize_trace_v0(
            std::move(trace));
    if (!normalized.has_value()) {
        return TraceSerializeResult::failure(
            normalized.error());
    }

    try {
        const auto& value =
            normalized.value();

        std::string output;
        output.append("{\"diagnostics\":");
        append_fields(
            output,
            value.diagnostics);
        output.append(",\"events\":[");

        for (std::size_t index = 0;
             index < value.events.size();
             ++index) {
            if (index != 0) {
                output.push_back(',');
            }
            append_event(
                output,
                value.events[index]);
        }

        output.append("],\"provenance\":");
        append_provenance(
            output,
            value.provenance);
        output.append(",\"run\":");
        append_run(
            output,
            value.run);
        output.append(",\"schema\":\"");
        output.append(kTraceSchemaV0);
        output.append("\"}");

        return TraceSerializeResult::success(
            std::move(output));
    } catch (const std::bad_alloc&) {
        return TraceSerializeResult::failure(
            trace_error(
                TraceErrorCode::
                    host_allocation_failure));
    } catch (const std::length_error&) {
        return TraceSerializeResult::failure(
            trace_error(
                TraceErrorCode::
                    host_size_unrepresentable));
    }
}

}  // namespace astraea::trace
