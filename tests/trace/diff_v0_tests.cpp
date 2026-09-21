#include <astraea/trace/diff_v0.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::trace::GuestLocationV0;
using astraea::trace::TraceDiffErrorCode;
using astraea::trace::TraceDiffPolicyV0;
using astraea::trace::TraceDivergenceKindV0;
using astraea::trace::TraceDocumentV0;
using astraea::trace::TraceEventV0;
using astraea::trace::TraceFieldV0;
using astraea::trace::TraceProducerV0;
using astraea::trace::TraceProvenanceV0;
using astraea::trace::TraceRunMetadataV0;
using astraea::trace::TraceStableFieldIgnoreRuleV0;
using astraea::trace::TraceValueV0;

TraceFieldV0 u64_field(
    std::string name,
    std::uint64_t value) {
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

TraceEventV0 event(
    std::uint64_t id,
    std::string subsystem,
    std::string type,
    std::uint64_t offset,
    std::vector<TraceFieldV0> stable) {
    return TraceEventV0{
        .id = id,
        .subsystem = std::move(subsystem),
        .type = std::move(type),
        .guest =
            GuestLocationV0{
                .object = "probe_hello.elf",
                .offset = offset,
            },
        .stable = std::move(stable),
        .diagnostics =
            std::vector<TraceFieldV0>{
                text_field(
                    "host_note",
                    "baseline"),
            },
    };
}

TraceDocumentV0 make_trace() {
    return TraceDocumentV0{
        .run =
            TraceRunMetadataV0{
                .target_kind =
                    "synthetic-probe",
                .platform_family = "linux",
                .architecture = "x86-64",
                .probe_id = "probe.hello",
                .probe_version = 1,
                .case_sha256 =
                    "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            },
        .provenance =
            TraceProvenanceV0{
                .source_kind = "astraea",
                .producer =
                    TraceProducerV0{
                        .name = "astraea",
                        .version = "v0",
                        .commit = "abc123",
                    },
                .artifact_digests =
                    std::vector<std::string>{
                        "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                    },
            },
        .events =
            std::vector<TraceEventV0>{
                event(
                    10,
                    "execution",
                    "guest_entry",
                    0,
                    {}),
                event(
                    20,
                    "hle",
                    "resume",
                    16,
                    std::vector<TraceFieldV0>{
                        u64_field("result", 16),
                        text_field(
                            "service",
                            "astraea.test.write"),
                    }),
                event(
                    30,
                    "hle",
                    "exit",
                    32,
                    std::vector<TraceFieldV0>{
                        u64_field("code", 42),
                    }),
            },
        .diagnostics =
            std::vector<TraceFieldV0>{
                text_field(
                    "runner",
                    "baseline"),
            },
    };
}

}  // namespace

TEST_CASE(
    "Trace diff ignores diagnostics and provenance by default",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();

    right.provenance.producer.version =
        "v1";
    right.provenance.producer.commit =
        "def456";
    right.events[0].diagnostics[0].value =
        TraceValueV0{
            std::string{"different"}};
    right.diagnostics[0].value =
        TraceValueV0{
            std::string{"different"}};

    auto result =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right));

    REQUIRE(result.has_value());
    REQUIRE(result->equivalent);
    REQUIRE(result->matched_event_count == 3);
    REQUIRE_FALSE(
        result->first_divergence.has_value());
}

TEST_CASE(
    "Trace diff reports stable run metadata mismatch",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();
    right.run.platform_family = "windows";

    auto result =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->equivalent);
    REQUIRE(
        result->first_divergence->kind ==
        TraceDivergenceKindV0::
            run_field_mismatch);
    REQUIRE(
        result->first_divergence->path ==
        "run.platform_family");
    REQUIRE(
        result->first_divergence->left_value ==
        std::optional<TraceValueV0>{
            TraceValueV0{
                std::string{"linux"}}});
    REQUIRE(
        result->first_divergence->right_value ==
        std::optional<TraceValueV0>{
            TraceValueV0{
                std::string{"windows"}}});
}

TEST_CASE(
    "Trace diff reports first stable field value mismatch",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();
    right.events[1].stable[0].value =
        TraceValueV0{static_cast<std::uint64_t>(17)};

    auto result =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->equivalent);
    REQUIRE(result->matched_event_count == 1);
    REQUIRE(
        result->first_divergence->kind ==
        TraceDivergenceKindV0::
            stable_field_value_mismatch);
    REQUIRE(
        result->first_divergence->path ==
        "events[1].stable.result");
    REQUIRE(
        result->first_divergence->field_name ==
        std::optional<std::string>{"result"});
    REQUIRE(
        result->first_divergence->left_event_id ==
        std::optional<std::uint64_t>{20});
    REQUIRE(
        result->first_divergence->right_event_id ==
        std::optional<std::uint64_t>{20});
    REQUIRE(
        result->first_divergence->left_value ==
        std::optional<TraceValueV0>{
            TraceValueV0{static_cast<std::uint64_t>(16)}});
    REQUIRE(
        result->first_divergence->right_value ==
        std::optional<TraceValueV0>{
            TraceValueV0{static_cast<std::uint64_t>(17)}});

    auto report =
        astraea::trace::format_trace_diff_v0(
            result.value());
    REQUIRE(report.has_value());
    REQUIRE(
        report->find(
            "events[1].stable.result") !=
        std::string::npos);
    REQUIRE(
        report->find(
            "0x0000000000000010") !=
        std::string::npos);
    REQUIRE(
        report->find(
            "0x0000000000000011") !=
        std::string::npos);
}

TEST_CASE(
    "Trace diff reports insertion before next aligned event",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();

    right.events.insert(
        right.events.begin() + 1,
        event(
            15,
            "execution",
            "gate_stop",
            8,
            std::vector<TraceFieldV0>{
                text_field(
                    "service",
                    "astraea.test.write"),
            }));

    auto result =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->equivalent);
    REQUIRE(result->matched_event_count == 1);
    REQUIRE(
        result->first_divergence->kind ==
        TraceDivergenceKindV0::
            event_insertion);
    REQUIRE(
        result->first_divergence->event_count ==
        1);
    REQUIRE(
        result->first_divergence->
            right_event_index ==
        std::optional<std::size_t>{1});
    REQUIRE(
        result->first_divergence->
            right_identity->type ==
        "gate_stop");

    auto report =
        astraea::trace::format_trace_diff_v0(
            result.value());
    REQUIRE(report.has_value());
    REQUIRE(
        report->find(
            "1 event inserted on right") !=
        std::string::npos);
}

TEST_CASE(
    "Trace diff reports deletion before next aligned event",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();

    right.events.erase(
        right.events.begin() + 1);

    auto result =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->equivalent);
    REQUIRE(result->matched_event_count == 1);
    REQUIRE(
        result->first_divergence->kind ==
        TraceDivergenceKindV0::
            event_deletion);
    REQUIRE(
        result->first_divergence->event_count ==
        1);
    REQUIRE(
        result->first_divergence->
            left_event_index ==
        std::optional<std::size_t>{1});
    REQUIRE(
        result->first_divergence->
            left_identity->type ==
        "resume");

    auto report =
        astraea::trace::format_trace_diff_v0(
            result.value());
    REQUIRE(report.has_value());
    REQUIRE(
        report->find(
            "1 event deleted from right") !=
        std::string::npos);
}

TEST_CASE(
    "Trace diff reports ambiguous semantic identity replacement",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();
    right.events[1].type = "different";

    auto result =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right));

    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->equivalent);
    REQUIRE(
        result->first_divergence->kind ==
        TraceDivergenceKindV0::
            event_identity_mismatch);
    REQUIRE(
        result->first_divergence->
            left_identity->type ==
        "resume");
    REQUIRE(
        result->first_divergence->
            right_identity->type ==
        "different");
}

TEST_CASE(
    "Trace diff can ignore event ids after semantic alignment",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();

    right.events[0].id = 100;
    right.events[1].id = 200;
    right.events[2].id = 300;

    auto exact =
        astraea::trace::diff_trace_v0(
            left,
            right);
    REQUIRE(exact.has_value());
    REQUIRE_FALSE(exact->equivalent);
    REQUIRE(
        exact->first_divergence->kind ==
        TraceDivergenceKindV0::
            event_id_mismatch);

    TraceDiffPolicyV0 policy{};
    policy.compare_event_ids = false;

    auto semantic =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right),
            std::move(policy));
    REQUIRE(semantic.has_value());
    REQUIRE(semantic->equivalent);
    REQUIRE(
        semantic->matched_event_count == 3);
}

TEST_CASE(
    "Trace diff ignore rules are scoped to semantic event identity",
    "[trace][diff-v0]") {
    auto left = make_trace();
    auto right = make_trace();
    right.events[1].stable[0].value =
        TraceValueV0{static_cast<std::uint64_t>(999)};

    TraceDiffPolicyV0 policy{
        .compare_event_ids = true,
        .ignored_stable_fields =
            std::vector<
                TraceStableFieldIgnoreRuleV0>{
                TraceStableFieldIgnoreRuleV0{
                    .subsystem = "hle",
                    .type = "resume",
                    .field = "result",
                },
            },
    };

    auto ignored =
        astraea::trace::diff_trace_v0(
            left,
            right,
            policy);
    REQUIRE(ignored.has_value());
    REQUIRE(ignored->equivalent);

    policy.ignored_stable_fields[0].type =
        "exit";

    auto not_ignored =
        astraea::trace::diff_trace_v0(
            std::move(left),
            std::move(right),
            std::move(policy));
    REQUIRE(not_ignored.has_value());
    REQUIRE_FALSE(not_ignored->equivalent);
    REQUIRE(
        not_ignored->first_divergence->kind ==
        TraceDivergenceKindV0::
            stable_field_value_mismatch);
}

TEST_CASE(
    "Trace diff reports stable field presence mismatch",
    "[trace][diff-v0]") {
    SECTION("missing on right") {
        auto left = make_trace();
        auto right = make_trace();
        left.events[1].stable.push_back(
            u64_field("extra", 7));

        auto result =
            astraea::trace::diff_trace_v0(
                std::move(left),
                std::move(right));

        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->equivalent);
        REQUIRE(
            result->first_divergence->kind ==
            TraceDivergenceKindV0::
                stable_field_missing_right);
        REQUIRE(
            result->first_divergence->
                field_name ==
            std::optional<std::string>{"extra"});
    }

    SECTION("missing on left") {
        auto left = make_trace();
        auto right = make_trace();
        right.events[1].stable.push_back(
            u64_field("extra", 7));

        auto result =
            astraea::trace::diff_trace_v0(
                std::move(left),
                std::move(right));

        REQUIRE(result.has_value());
        REQUIRE_FALSE(result->equivalent);
        REQUIRE(
            result->first_divergence->kind ==
            TraceDivergenceKindV0::
                stable_field_missing_left);
    }
}

TEST_CASE(
    "Trace diff rejects malformed traces instead of comparing them",
    "[trace][diff-v0]") {
    SECTION("left") {
        auto left = make_trace();
        auto right = make_trace();
        left.events[1].stable.push_back(
            u64_field("result", 99));

        auto result =
            astraea::trace::diff_trace_v0(
                std::move(left),
                std::move(right));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TraceDiffErrorCode::
                invalid_left_trace);
        REQUIRE(
            result.error().has_trace_error);
        REQUIRE(
            result.error().trace_error.code ==
            astraea::trace::TraceErrorCode::
                duplicate_stable_field);
    }

    SECTION("right") {
        auto left = make_trace();
        auto right = make_trace();
        right.run.target_kind = "INVALID";

        auto result =
            astraea::trace::diff_trace_v0(
                std::move(left),
                std::move(right));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TraceDiffErrorCode::
                invalid_right_trace);
    }
}

TEST_CASE(
    "Trace diff validates ignore policy deterministically",
    "[trace][diff-v0]") {
    SECTION("invalid rule") {
        TraceDiffPolicyV0 policy{
            .compare_event_ids = true,
            .ignored_stable_fields =
                std::vector<
                    TraceStableFieldIgnoreRuleV0>{
                    TraceStableFieldIgnoreRuleV0{
                        .subsystem = "HLE",
                        .type = "resume",
                        .field = "result",
                    },
                },
        };

        auto result =
            astraea::trace::diff_trace_v0(
                make_trace(),
                make_trace(),
                std::move(policy));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TraceDiffErrorCode::
                invalid_ignore_rule);
    }

    SECTION("duplicate rule") {
        TraceStableFieldIgnoreRuleV0 rule{
            .subsystem = "hle",
            .type = "resume",
            .field = "result",
        };
        TraceDiffPolicyV0 policy{
            .compare_event_ids = true,
            .ignored_stable_fields =
                std::vector<
                    TraceStableFieldIgnoreRuleV0>{
                    rule,
                    rule,
                },
        };

        auto result =
            astraea::trace::diff_trace_v0(
                make_trace(),
                make_trace(),
                std::move(policy));

        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            TraceDiffErrorCode::
                duplicate_ignore_rule);
    }
}
