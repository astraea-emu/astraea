#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#ifndef ASTRAEA_SOURCE_DIR
#error "ASTRAEA_SOURCE_DIR must be defined for source invariant tests"
#endif

TEST_CASE(
    "pre-CRT observer snapshot region remains MOV-only",
    "[probe][c1][process-entry][observer][source]") {
    const std::string path =
        std::string{ASTRAEA_SOURCE_DIR} +
        "/tools/reference/ps5_process_entry_observer/entry.S";

    std::ifstream input{path};
    REQUIRE(input.good());

    bool in_snapshot = false;
    bool saw_instruction = false;
    bool saw_completion = false;

    std::string line;
    while (std::getline(input, line)) {
        if (line.find(".Lastraea_snapshot_begin:") !=
            std::string::npos) {
            in_snapshot = true;
            continue;
        }
        if (line.find(".Lastraea_snapshot_complete:") !=
            std::string::npos) {
            saw_completion = true;
            break;
        }
        if (!in_snapshot) {
            continue;
        }

        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        const auto view = line.substr(first);
        if (view.starts_with("/*") ||
            view.starts_with("*") ||
            view.starts_with("*/")) {
            continue;
        }

        const auto token_end =
            view.find_first_of(" \t");
        const auto mnemonic =
            view.substr(0, token_end);

        INFO("unexpected pre-completion instruction: " << view);
        REQUIRE(mnemonic.starts_with("mov"));
        saw_instruction = true;
    }

    REQUIRE(saw_instruction);
    REQUIRE(saw_completion);
}
