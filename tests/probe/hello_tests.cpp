#include <astraea/probe/hello.hpp>

#include <astraea/execution/context.hpp>
#include <astraea/execution/hle.hpp>
#include <astraea/execution/hle_runtime.hpp>
#include <astraea/execution/linux_memory.hpp>
#include <astraea/execution/linux_session.hpp>
#include <astraea/loader/guest_image.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__linux__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

astraea::loader::GuestImage build_image(
    const astraea::probe::ProbeHelloFixture& fixture) {
    auto image =
        astraea::loader::build_guest_image(
            astraea::loader::GuestImageRequest{
                .image_bytes = fixture.elf_bytes,
                .initial_stack =
                    fixture.initial_stack,
            });
    REQUIRE(image.has_value());
    return std::move(image).value();
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

std::vector<std::byte> message_bytes() {
    std::vector<std::byte> bytes;
    bytes.reserve(
        astraea::probe::kProbeHelloMessage.size());
    for (const char value :
         astraea::probe::kProbeHelloMessage) {
        bytes.push_back(
            static_cast<std::byte>(
                static_cast<unsigned char>(
                    value)));
    }
    return bytes;
}

std::uint64_t page_size() {
    const long value = ::sysconf(_SC_PAGESIZE);
    REQUIRE(value > 0);
    return static_cast<std::uint64_t>(value);
}

std::uint64_t find_free_block(std::size_t size) {
    void* const mapped =
        ::mmap(
            nullptr,
            size,
            PROT_NONE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0);
    REQUIRE(mapped != MAP_FAILED);

    const auto address =
        static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(
                mapped));
    REQUIRE(::munmap(mapped, size) == 0);
    return address;
}

astraea::execution::HleRegistry make_registry() {
    auto registry =
        astraea::execution::HleRegistry::create(
            std::vector<
                astraea::execution::
                    HleFunctionDescriptor>{
                astraea::execution::
                    HleFunctionDescriptor{
                        .id =
                            astraea::execution::
                                kSyntheticTestWriteId,
                        .canonical_name =
                            "astraea.test.write",
                        .argument_count = 2,
                    },
                astraea::execution::
                    HleFunctionDescriptor{
                        .id =
                            astraea::execution::
                                kSyntheticTestExitId,
                        .canonical_name =
                            "astraea.test.exit",
                        .argument_count = 1,
                    },
            });
    REQUIRE(registry.has_value());
    return std::move(registry).value();
}

astraea::execution::SyntheticGateRegion make_gates(
    const astraea::loader::GuestImage& image,
    const astraea::execution::HleRegistry& registry,
    astraea::memory::GuestAddress gate_base) {
    auto gates =
        astraea::execution::
            build_synthetic_gate_region(
                registry,
                gate_base,
                2,
                std::vector<
                    astraea::execution::GateBinding>{
                    astraea::execution::GateBinding{
                        .slot = 0,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestWriteId,
                    },
                    astraea::execution::GateBinding{
                        .slot = 1,
                        .function_id =
                            astraea::execution::
                                kSyntheticTestExitId,
                    },
                },
                image);
    REQUIRE(gates.has_value());
    return std::move(gates).value();
}

#endif

}  // namespace

TEST_CASE(
    "probe_hello ELF fixture is deterministic and accepted by strict loader",
    "[probe][hello][elf]") {
    constexpr std::uint64_t kBase =
        0x40000000ULL;
    constexpr std::uint64_t kPage =
        0x1000ULL;

    auto first =
        astraea::probe::
            build_probe_hello_fixture(
                astraea::memory::GuestAddress{
                    kBase},
                kPage);
    auto second =
        astraea::probe::
            build_probe_hello_fixture(
                astraea::memory::GuestAddress{
                    kBase},
                kPage);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(
        first->elf_bytes ==
        second->elf_bytes);
    REQUIRE(
        first->code_base ==
        second->code_base);
    REQUIRE(
        first->data_base ==
        second->data_base);
    REQUIRE(
        first->gate_base ==
        second->gate_base);

    auto first_image = build_image(first.value());
    auto second_image = build_image(second.value());

    REQUIRE(
        first_image.elf.header.entry ==
        first->code_base.value());
    REQUIRE(
        first_image.elf.program_headers.size() ==
        2);
    REQUIRE(first_image.mappings.size() == 2);
    REQUIRE_FALSE(
        first_image.dynamic_table.has_value());
    REQUIRE_FALSE(first_image.tls.has_value());
    REQUIRE(
        first_image.mappings ==
        second_image.mappings);
    REQUIRE(
        first_image.initial_stack.bytes ==
        second_image.initial_stack.bytes);
    REQUIRE(
        first_image.initial_stack.rsp ==
        second_image.initial_stack.rsp);
}

TEST_CASE(
    "probe_hello fixture rejects invalid layout",
    "[probe][hello][elf]") {
    SECTION("page size") {
        auto result =
            astraea::probe::
                build_probe_hello_fixture(
                    astraea::memory::GuestAddress{
                        0x40000000},
                    3000);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::
                ProbeHelloErrorCode::
                    invalid_page_size);
    }

    SECTION("unaligned base") {
        auto result =
            astraea::probe::
                build_probe_hello_fixture(
                    astraea::memory::GuestAddress{
                        0x40000001},
                    0x1000);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::
                ProbeHelloErrorCode::
                    unaligned_guest_base);
    }

    SECTION("address overflow") {
        auto result =
            astraea::probe::
                build_probe_hello_fixture(
                    astraea::memory::GuestAddress{
                        0xfffffffffffff000ULL},
                    0x1000);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(
            result.error().code ==
            astraea::probe::
                ProbeHelloErrorCode::
                    guest_address_overflow);
    }
}

#if defined(__linux__) && defined(__x86_64__) && defined(MAP_FIXED_NOREPLACE)

TEST_CASE(
    "probe_hello ELF executes write resume and exit end to end",
    "[probe][hello][m2-exit]") {
    const auto page = page_size();
    REQUIRE(
        page <=
        std::numeric_limits<
            std::uint64_t>::max() /
            4U);
    const auto total =
        page * 4U;
    REQUIRE(
        total <=
        static_cast<std::uint64_t>(
            std::numeric_limits<
                std::size_t>::max()));

    const auto base =
        find_free_block(
            static_cast<std::size_t>(
                total));

    auto fixture =
        astraea::probe::
            build_probe_hello_fixture(
                astraea::memory::GuestAddress{
                    base},
                page);
    REQUIRE(fixture.has_value());

    auto image = build_image(fixture.value());

    auto prepared =
        astraea::execution::
            prepare_linux_guest_memory(image);
    REQUIRE(prepared.has_value());

    auto registry = make_registry();
    auto gates =
        make_gates(
            image,
            registry,
            fixture->gate_base);

    const auto initial =
        astraea::execution::
            make_synthetic_initial_context(
                image);

    auto first =
        astraea::execution::
            run_linux_synthetic_session(
                image,
                prepared.value(),
                registry,
                gates,
                initial);
    auto second =
        astraea::execution::
            run_linux_synthetic_session(
                image,
                prepared.value(),
                registry,
                gates,
                initial);

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value() == second.value());

    const auto expected_output =
        message_bytes();
    REQUIRE(
        first->output ==
        expected_output);
    REQUIRE(first->exit_code == 42);
    REQUIRE(first->gate_stop_count == 2);
    REQUIRE(
        first->final_context.rax ==
        expected_output.size());
    REQUIRE(first->final_context.rdi == 42);
    REQUIRE(
        first->final_context.rip ==
        fixture->gate_base.value() +
            astraea::execution::
                kSyntheticGateStride);

    REQUIRE(first->events.size() == 6);

    REQUIRE(
        first->events[0].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                guest_entry);
    REQUIRE(
        first->events[0].rip ==
        fixture->code_base.value());

    REQUIRE(
        first->events[1].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                gate_stop);
    REQUIRE(first->events[1].gate_slot == 0);
    REQUIRE(first->events[1].has_function_id);
    REQUIRE(
        first->events[1].function_id ==
        astraea::execution::
            kSyntheticTestWriteId);

    REQUIRE(
        first->events[2].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                hle_resume);
    REQUIRE(
        first->events[2].value ==
        expected_output.size());

    REQUIRE(
        first->events[3].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                guest_entry);

    REQUIRE(
        first->events[4].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                gate_stop);
    REQUIRE(first->events[4].gate_slot == 1);
    REQUIRE(
        first->events[4].function_id ==
        astraea::execution::
            kSyntheticTestExitId);

    REQUIRE(
        first->events[5].kind ==
        astraea::execution::
            SyntheticSessionEventKind::
                hle_exit);
    REQUIRE(first->events[5].value == 42);
}

#endif
