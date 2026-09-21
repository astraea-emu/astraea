#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <astraea/core/result.hpp>

namespace astraea::trace {

inline constexpr std::string_view kTraceSchemaV0 =
    "astraea.trace/v0";

enum class TraceErrorCode {
    invalid_identifier,
    invalid_utf8,
    malformed_sha256,
    incomplete_probe_identity,
    probe_version_out_of_range,
    duplicate_artifact_digest,
    non_monotonic_event_id,
    duplicate_stable_field,
    duplicate_diagnostic_field,
    host_size_unrepresentable,
    host_allocation_failure,
};

struct TraceError {
    TraceErrorCode code =
        TraceErrorCode::host_allocation_failure;
    bool has_event_id = false;
    std::uint64_t event_id = 0;

    auto operator<=>(const TraceError&) const = default;
};

struct GuestLocationV0 {
    std::string object;
    std::uint64_t offset = 0;

    auto operator<=>(const GuestLocationV0&) const = default;
};

using TraceValueV0 =
    std::variant<
        std::uint64_t,
        bool,
        std::string,
        std::vector<std::byte>>;

struct TraceFieldV0 {
    std::string name;
    TraceValueV0 value;

    auto operator<=>(const TraceFieldV0&) const = default;
};

struct TraceEventV0 {
    std::uint64_t id = 0;
    std::string subsystem;
    std::string type;
    std::optional<GuestLocationV0> guest;
    std::vector<TraceFieldV0> stable;
    std::vector<TraceFieldV0> diagnostics;

    auto operator<=>(const TraceEventV0&) const = default;
};

struct TraceRunMetadataV0 {
    std::string target_kind;
    std::string platform_family;
    std::string architecture;
    std::optional<std::string> probe_id;
    std::optional<std::uint64_t> probe_version;
    std::optional<std::string> case_sha256;

    auto operator<=>(const TraceRunMetadataV0&) const = default;
};

struct TraceProducerV0 {
    std::string name;
    std::string version;
    std::optional<std::string> commit;

    auto operator<=>(const TraceProducerV0&) const = default;
};

struct TraceProvenanceV0 {
    std::string source_kind;
    TraceProducerV0 producer;
    std::vector<std::string> artifact_digests;

    auto operator<=>(const TraceProvenanceV0&) const = default;
};

struct TraceDocumentV0 {
    TraceRunMetadataV0 run;
    TraceProvenanceV0 provenance;
    std::vector<TraceEventV0> events;
    std::vector<TraceFieldV0> diagnostics;

    auto operator<=>(const TraceDocumentV0&) const = default;
};

using TraceNormalizeResult =
    astraea::core::Result<
        TraceDocumentV0,
        TraceError>;

using TraceSerializeResult =
    astraea::core::Result<
        std::string,
        TraceError>;

[[nodiscard]] TraceNormalizeResult normalize_trace_v0(
    TraceDocumentV0 trace);

[[nodiscard]] TraceSerializeResult serialize_trace_v0(
    TraceDocumentV0 trace);

}  // namespace astraea::trace
