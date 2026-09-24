#include <astraea/core/version.hpp>

#include <iostream>
#include <string_view>

#include "linux_retail_diagnostic.hpp"

namespace {

void print_usage() {
    std::cout
        << "Astraea "
        << astraea::core::version()
        << "\n"
        << "Usage:\n"
        << "  astraea diagnose <ps5-elf-or-eboot>\n"
        << "\n"
        << "The retail diagnostic is currently Linux x86-64 only. "
           "It reports the first verified unsupported boundary; "
           "it is not a game-compatibility or playability claim.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 &&
        std::string_view{argv[1]} ==
            "--linux-retail-diagnostic-worker") {
        return astraea::app::
            run_linux_retail_diagnostic_worker();
    }

    if (argc == 3 &&
        std::string_view{argv[1]} ==
            "diagnose") {
        return astraea::app::
            run_linux_retail_diagnostic(
                argv[2]);
    }

    print_usage();
    return argc == 1 ? 0 : 2;
}
