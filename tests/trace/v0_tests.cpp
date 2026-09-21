#include <astraea/trace/v0.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::trace::GuestLocationV0;
using astraea::trace::TraceDocumentV0;
using astraea::trace::TraceErrorCode;
using astraea::trace::TraceEventV0;
using astraea::trace::TraceFieldV0;
using astraea::trace::TraceProducerV0;
using astraea::trace::TraceProvenanceV0;
using astraea::trace::TraceRunMetadataV0;
using astraea::trace::TraceValueV0;

TraceFieldV0 u64_field(
    std::string name,
    std::uint64_t value) {
    return TraceFieldV0{
        .name = std::move(name),
        .value = TraceValueV0{value},
    };
}

TraceFieldV0 bool_field(
    std::string name,
    bool value) {
    return TraceFieldV0{
        .name = std::move(name),
        .value = TraceValueV0{value},
    };
}

TraceFieldV0 text_field(
    std::string name,
    std::string value) {
    return TraceFieldV0{
        .name = std::move(name),
        .value =
            TraceValueV0{
                std::move(value)},
    };
}

TraceDocumentV0 make_reference_trace() {
    return TraceDocumentV0{
        .run =
            TraceRunMetadataV0{
                .target_kind = "astraea",
                .platform_family = "astraea",
                .architecture = "x86_64",
                .probe_id = std::nullopt,
                .probe_version = std::nullopt,
                .case_sha256 = std::nullopt,
            },
        .provenance =
            TraceProvenanceV0{
                .source_kind =
                    "astraea_owned_synthetic",
                .producer =
                    TraceProducerV0{
                        .name = "astraea",
                        .version = "0",
                        .commit = std::nullopt,
                    },
                .artifact_digests = {},
            },
        .events =
            std::vector<TraceEventV0>{
                TraceEventV0{
                    .id = 0,
                    .subsystem = "execution",
                    .type = "guest_entry",
                    .guest =
                        GuestLocationV0{
                            .object =
                                "probe_hello.elf",
                            .offset = 0,
                        },
                    .stable = {},
                    .diagnostics = {},
                },
                TraceEventV0{
                    .id = 1,
                    .subsystem = "execution",
                    .type = "gate_stop",
                    .guest = std::nullopt,
                    .stable =
                        std::vector<TraceFieldV0>{
                            text_field(
                                "function",
                                "astraea.test.write"),
                            u64_field(
                                "gate_slot",
                                0),
                        },
                    .diagnostics =
                        std::vector<TraceFieldV0>{
                            u64_field(
                                "host_code",
                                0),
                        },
                },
                TraceEventV0{
                    .id = 2,
                    .subsystem = "hle",
                    .type = "resume",
                    .guest = std::nullopt,
                    .stable =
                        std::vector<TraceFieldV0>{
                            u64_field(
                                "result",
                                16),
                        },
                    .diagnostics = {},
                },
                TraceEventV0{
                    .id = 3,
                    .subsystem = "hle",
                    .type = "exit",
                    .guest = std::nullopt,
                    .stable =
                        std::vector<TraceFieldV0>{
                            u64_field(
                                "exit_code",
                                42),
                        },
                    .diagnostics = {},
                },
            },
        .diagnostics = {},
    };
}

std::string digest(char fill) {
    return std::string{"sha256:"} +
           std::string(64, fill);
}

}  // namespace

TEST_CASE(
    "Trace v0 canonical serialization is exact",
    "[trace][v0]") {
    auto first =
        astraea::trace::serialize_trace_v0(
            make_reference_trace());
    auto second =
        astraea::trace::serialize_trace_v0(
            make_reference_trace());

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value() == second.value());

    const std::string expected =
        R"({"diagnostics":{},"events":[{"diagnostics":{},"guest":{"kind":"object_offset","object":"probe_hello.elf","offset":"0x0000000000000000"},"id":"0x0000000000000000","stable":{},"subsystem":"execution","type":"guest_entry"},{"diagnostics":{"host_code":{"type":"u64","value":"0x0000000000000000"}},"id":"0x0000000000000001","stable":{"function":{"type":"utf8","value":"astraea.test.write"},"gate_slot":{"type":"u64","value":"0x0000000000000000"}},"subsystem":"execution","type":"gate_stop"},{"diagnostics":{},"id":"0x0000000000000002","stable":{"result":{"type":"u64","value":"0x0000000000000010"}},"subsystem":"hle","type":"resume"},{"diagnostics":{},"id":"0x0000000000000003","stable":{"exit_code":{"type":"u64","value":"0x000000000000002a"}},"subsystem":"hle","type":"exit"}],"provenance":{"artifact_digests":[],"producer":{"name":"astraea","version":"0"},"source_kind":"astraea_owned_synthetic"},"run":{"architecture":"x86_64","platform_family":"astraea","target_kind":"astraea"},"schema":"astraea.trace/v0"})";

    REQUIRE(first.value() == expected);
}

TEST_CASE(
    "Trace v0 normalization sorts sets and field maps",
    "[trace][v0]") {
    auto trace = make_reference_trace();
    trace.provenance.artifact_digests = {
        digest('b'),
        digest('a'),
    };
    trace.events[0].stable = {
        u64_field("z_value", 2),
        bool_field("a_flag", true),
    };
    trace.diagnostics = {
        u64_field("z_diag", 2),
        u64_field("a_diag", 1),
    };

    auto normalized =
        astraea::trace::normalize_trace_v0(
            std::move(trace));

    REQUIRE(normalized.has_value());
    REQUIRE(
        normalized->provenance.
            artifact_digests[0] ==
        digest('a'));
    REQUIRE(
        normalized->provenance.
            artifact_digests[1] ==
        digest('b'));
    REQUIRE(
        normalized->events[0].
            stable[0].name ==
        "a_flag");
    REQUIRE(
        normalized->events[0].
            stable[1].name ==
        "z_value");
    REQUIRE(
        normalized->diagnostics[0].name ==
        "a_diag");
    REQUIRE(
        normalized->events[0].id == 0);
    REQUIRE(
        normalized->events[1].id == 1);
}

TEST_CASE(
    "Trace v0 rejects non-monotonic event identity",
    "[trace][v0]") {
    auto trace = make_reference_trace();
    trace.events[2].id = 1;

    auto normalized =
        astraea::trace::normalize_trace_v0(
            std::move(trace));

    REQUIRE_FALSE(normalized.has_value());
    REQUIRE(
        normalized.error().code ==
        TraceErrorCode::
            non_monotonic_event_id);
    REQUIRE(
        normalized.error().has_event_id);
    REQUIRE(
        normalized.error().event_id == 1);
}

TEST_CASE(
    "Trace v0 rejects duplicate map and set members",
    "[trace][v0]") {
    SECTION("stable field") {
        auto trace = make_reference_trace();
        trace.events[0].stable = {
            u64_field("same", 1),
            u64_field("same", 2),
        };

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                duplicate_stable_field);
    }

    SECTION("diagnostic field") {
        auto trace = make_reference_trace();
        trace.events[0].diagnostics = {
            u64_field("same", 1),
            u64_field("same", 2),
        };

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                duplicate_diagnostic_field);
    }

    SECTION("artifact digest") {
        auto trace = make_reference_trace();
        trace.provenance.artifact_digests = {
            digest('a'),
            digest('a'),
        };

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                duplicate_artifact_digest);
    }
}

TEST_CASE(
    "Trace v0 validates identifiers digests and probe linkage",
    "[trace][v0]") {
    SECTION("identifier") {
        auto trace = make_reference_trace();
        trace.events[0].subsystem =
            "Execution";

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                invalid_identifier);
    }

    SECTION("digest") {
        auto trace = make_reference_trace();
        trace.provenance.artifact_digests = {
            "sha256:not-a-digest",
        };

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                malformed_sha256);
    }

    SECTION("incomplete probe") {
        auto trace = make_reference_trace();
        trace.run.probe_id =
            "astraea.reference.echo";

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                incomplete_probe_identity);
    }

    SECTION("case without probe") {
        auto trace = make_reference_trace();
        trace.run.case_sha256 =
            digest('c');

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                incomplete_probe_identity);
    }

    SECTION("probe version range") {
        auto trace = make_reference_trace();
        trace.run.probe_id =
            "astraea.reference.echo";
        trace.run.probe_version =
            9007199254740992ULL;

        auto normalized =
            astraea::trace::normalize_trace_v0(
                std::move(trace));

        REQUIRE_FALSE(
            normalized.has_value());
        REQUIRE(
            normalized.error().code ==
            TraceErrorCode::
                probe_version_out_of_range);
    }
}

TEST_CASE(
    "Trace v0 rejects invalid UTF-8 stable text",
    "[trace][v0]") {
    auto trace = make_reference_trace();

    std::string invalid;
    invalid.push_back(
        static_cast<char>(0xc0));
    invalid.push_back(
        static_cast<char>(0x80));

    trace.events[0].stable.push_back(
        text_field(
            "text",
            std::move(invalid)));

    auto normalized =
        astraea::trace::normalize_trace_v0(
            std::move(trace));

    REQUIRE_FALSE(normalized.has_value());
    REQUIRE(
        normalized.error().code ==
        TraceErrorCode::invalid_utf8);
}

TEST_CASE(
    "Trace v0 escapes JSON text while preserving valid UTF-8",
    "[trace][v0]") {
    auto trace = make_reference_trace();

    std::string text{"quote:"};
    text.push_back('"');
    text.append(" slash:");
    text.push_back('\\');
    text.append(" line:");
    text.push_back('\n');
    text.append(" snowman:");
    text.append(
        "\xe2\x98\x83",
        3);

    trace.events[0].stable.push_back(
        text_field(
            "text",
            std::move(text)));

    auto serialized =
        astraea::trace::serialize_trace_v0(
            std::move(trace));

    REQUIRE(serialized.has_value());

    std::string expected{
        "quote:\\\" slash:\\\\ line:\\u000a snowman:"};
    expected.append(
        "\xe2\x98\x83",
        3);

    REQUIRE(
        serialized->find(expected) !=
        std::string::npos);
}

TEST_CASE(
    "Trace v0 serializes byte fields as lowercase hex",
    "[trace][v0]") {
    auto trace = make_reference_trace();
    trace.events[0].stable.push_back(
        TraceFieldV0{
            .name = "bytes",
            .value =
                TraceValueV0{
                    std::vector<std::byte>{
                        std::byte{0x00},
                        std::byte{0xaf},
                        std::byte{0xff},
                    }},
        });

    auto serialized =
        astraea::trace::serialize_trace_v0(
            std::move(trace));

    REQUIRE(serialized.has_value());
    REQUIRE(
        serialized->find(
            R"("bytes":{"encoding":"hex","type":"bytes","value":"00afff"})") !=
        std::string::npos);
}
