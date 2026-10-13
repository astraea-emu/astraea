#include <astraea/execution/memory_plan.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

using astraea::execution::ExecutionMemoryPlanRequest;
using astraea::execution::ExecutionMemorySourceKind;
using astraea::execution::ExecutionPlanErrorCode;
using astraea::memory::GuestAddress;
using astraea::memory::GuestPermission;
using astraea::memory::GuestPermissions;
using astraea::memory::GuestRange;
using astraea::memory::GuestSize;
using astraea::memory::MappingBacking;
using astraea::memory::MappingBackingKind;
using astraea::memory::MappingIntent;

GuestRange range(std::uint64_t base, std::uint64_t size) {
    auto result = GuestRange::create(GuestAddress{base}, GuestSize{size});
    REQUIRE(result.has_value());
    return result.value();
}

GuestPermissions permissions(std::uint8_t bits) {
    auto result = GuestPermissions::checked_from_bits(bits);
    REQUIRE(result.has_value());
    return result.value();
}

MappingIntent mapping(
    std::uint64_t base,
    std::uint64_t size,
    std::uint8_t permission_bits,
    std::size_t source_index) {
    return MappingIntent{
        .range = range(base, size),
        .permissions = permissions(permission_bits),
        .backing =
            MappingBacking{
                .kind = MappingBackingKind::zero_fill,
                .file_offset = 0,
                .byte_count = GuestSize{size},
            },
        .source_index = source_index,
    };
}

ExecutionMemoryPlanRequest request(
    std::span<const MappingIntent> mappings,
    GuestRange stack,
    GuestAddress entry,
    GuestAddress rsp,
    std::uint64_t page_size = 0x1000) {
    return ExecutionMemoryPlanRequest{
        .mappings = mappings,
        .stack_storage = stack,
        .entry_point = entry,
        .stack_pointer = rsp,
        .host_page_size = page_size,
    };
}

constexpr std::uint8_t kRead =
    static_cast<std::uint8_t>(GuestPermission::read);
constexpr std::uint8_t kWrite =
    static_cast<std::uint8_t>(GuestPermission::write);
constexpr std::uint8_t kExecute =
    static_cast<std::uint8_t>(GuestPermission::execute);

}  // namespace

TEST_CASE("execution memory plan rounds and protects code and stack", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x401123, 0x100, kRead | kExecute, 4),
    };
    const auto stack = range(0x700000, 0x2000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x401123}, GuestAddress{0x701000}));

    REQUIRE(plan.has_value());
    REQUIRE(plan->host_page_size == 0x1000);
    REQUIRE(plan->regions.size() == 2);

    REQUIRE(plan->regions[0].range == range(0x401000, 0x1000));
    REQUIRE(plan->regions[0].permissions.has(GuestPermission::read));
    REQUIRE_FALSE(plan->regions[0].permissions.has(GuestPermission::write));
    REQUIRE(plan->regions[0].permissions.has(GuestPermission::execute));
    REQUIRE(plan->regions[0].sources.size() == 1);
    REQUIRE(plan->regions[0].sources[0].kind == ExecutionMemorySourceKind::mapping);
    REQUIRE(plan->regions[0].sources[0].source_index == 4);

    REQUIRE(plan->regions[1].range == stack);
    REQUIRE(plan->regions[1].permissions.has(GuestPermission::read));
    REQUIRE(plan->regions[1].permissions.has(GuestPermission::write));
    REQUIRE_FALSE(plan->regions[1].permissions.has(GuestPermission::execute));
    REQUIRE(plan->regions[1].sources.size() == 1);
    REQUIRE(plan->regions[1].sources[0].kind == ExecutionMemorySourceKind::initial_stack);
}

TEST_CASE("execution memory plan rejects invalid host page sizes", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x4000, 0x100, kRead | kExecute, 0),
    };
    const auto stack = range(0x8000, 0x1000);

    for (const auto page_size : {std::uint64_t{0}, std::uint64_t{3}, std::uint64_t{0x1800}}) {
        auto plan = astraea::execution::build_execution_memory_plan(
            request(mappings, stack, GuestAddress{0x4000}, GuestAddress{0x8000}, page_size));
        REQUIRE_FALSE(plan.has_value());
        REQUIRE(plan.error().code == ExecutionPlanErrorCode::invalid_host_page_size);
    }
}

TEST_CASE("page-shared non-executable permissions merge without per-page expansion", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x1000, 0x800, kRead, 9),
        mapping(0x1800, 0x800, kWrite, 2),
        mapping(0x3000, 0x100, kRead | kExecute, 7),
    };
    const auto stack = range(0x5000, 0x1000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x3000}, GuestAddress{0x5000}));

    REQUIRE(plan.has_value());

    const auto shared = std::find_if(
        plan->regions.begin(),
        plan->regions.end(),
        [](const auto& region) {
            return region.range.contains(GuestAddress{0x1000});
        });
    REQUIRE(shared != plan->regions.end());
    REQUIRE(shared->range == range(0x1000, 0x1000));
    REQUIRE(shared->permissions.has(GuestPermission::read));
    REQUIRE(shared->permissions.has(GuestPermission::write));
    REQUIRE_FALSE(shared->permissions.has(GuestPermission::execute));
    REQUIRE(shared->sources.size() == 2);
    REQUIRE(shared->sources[0].source_index == 2);
    REQUIRE(shared->sources[1].source_index == 9);
}

TEST_CASE("host-page rounding that requires write and execute fails closed", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x1000, 0x800, kRead | kExecute, 1),
        mapping(0x1800, 0x800, kWrite, 2),
    };
    const auto stack = range(0x5000, 0x1000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x1000}, GuestAddress{0x5000}));

    REQUIRE_FALSE(plan.has_value());
    REQUIRE(plan.error().code == ExecutionPlanErrorCode::mixed_write_execute_page);
    REQUIRE(plan.error().guest_address == GuestAddress{0x1000});
}

TEST_CASE("entry point must be mapped and executable", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x2000, 0x1000, kRead, 0),
    };
    const auto stack = range(0x8000, 0x1000);

    SECTION("unmapped") {
        auto plan = astraea::execution::build_execution_memory_plan(
            request(mappings, stack, GuestAddress{0x4000}, GuestAddress{0x8000}));
        REQUIRE_FALSE(plan.has_value());
        REQUIRE(plan.error().code == ExecutionPlanErrorCode::entry_point_unmapped);
    }

    SECTION("mapped but not executable") {
        auto plan = astraea::execution::build_execution_memory_plan(
            request(mappings, stack, GuestAddress{0x2000}, GuestAddress{0x8000}));
        REQUIRE_FALSE(plan.has_value());
        REQUIRE(plan.error().code == ExecutionPlanErrorCode::entry_point_not_executable);
    }
}

TEST_CASE("entry point cannot rely on rounded host-page padding", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x2123, 0x100, kRead | kExecute, 0),
    };
    const auto stack = range(0x8000, 0x1000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x2100}, GuestAddress{0x8000}));

    REQUIRE_FALSE(plan.has_value());
    REQUIRE(plan.error().code == ExecutionPlanErrorCode::entry_point_unmapped);
    REQUIRE(plan.error().guest_address == GuestAddress{0x2100});
}

TEST_CASE("entry execute permission comes from the exact guest mapping", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x1000, 0x800, kRead | kExecute, 0),
        mapping(0x1800, 0x800, kRead, 1),
    };
    const auto stack = range(0x8000, 0x1000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x1800}, GuestAddress{0x8000}));

    REQUIRE_FALSE(plan.has_value());
    REQUIRE(plan.error().code == ExecutionPlanErrorCode::entry_point_not_executable);
    REQUIRE(plan.error().guest_address == GuestAddress{0x1800});
}

TEST_CASE("stack pointer must belong to synthetic stack storage", "[execution][memory-plan]") {
    std::array mappings{
        mapping(0x2000, 0x1000, kRead | kExecute, 0),
    };
    const auto stack = range(0x8000, 0x1000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x2000}, GuestAddress{0x9000}));

    REQUIRE_FALSE(plan.has_value());
    REQUIRE(plan.error().code == ExecutionPlanErrorCode::stack_pointer_unmapped);
    REQUIRE(plan.error().guest_address == GuestAddress{0x9000});
}

TEST_CASE("execution memory plan is independent of mapping input order", "[execution][memory-plan]") {
    const auto a = mapping(0x3000, 0x1000, kRead | kExecute, 8);
    const auto b = mapping(0x1000, 0x800, kRead, 9);
    const auto c = mapping(0x1800, 0x800, kWrite, 2);
    std::array forward{a, b, c};
    std::array reverse{c, b, a};
    const auto stack = range(0x6000, 0x1000);

    auto first = astraea::execution::build_execution_memory_plan(
        request(forward, stack, GuestAddress{0x3000}, GuestAddress{0x6000}));
    auto second = astraea::execution::build_execution_memory_plan(
        request(reverse, stack, GuestAddress{0x3000}, GuestAddress{0x6000}));

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first.value() == second.value());
}

TEST_CASE("planner handles a host page ending at UINT64_MAX", "[execution][memory-plan]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    const auto final_page = max - 0xfffU;
    std::array mappings{
        mapping(final_page, 0x1000, kRead | kExecute, 0),
    };
    const auto stack = range(0x8000, 0x1000);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{final_page}, GuestAddress{0x8000}));

    REQUIRE(plan.has_value());
    REQUIRE(plan->regions.back().range == range(final_page, 0x1000));
}

TEST_CASE("planner rejects rounded coverage of the entire 64-bit domain without expansion", "[execution][memory-plan]") {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    std::array<MappingIntent, 0> mappings{};
    const auto enormous_stack = range(1, max);

    auto plan = astraea::execution::build_execution_memory_plan(
        request(
            mappings,
            enormous_stack,
            GuestAddress{1},
            GuestAddress{1}));

    REQUIRE_FALSE(plan.has_value());
    REQUIRE(plan.error().code == ExecutionPlanErrorCode::host_page_arithmetic_overflow);
}


TEST_CASE("write authorization requires exact guest range and mapped host page",
          "[execution][memory-plan][guest-write]") {
    namespace e = astraea::execution;
    std::array mappings{
        mapping(0x4000, 0x200, kRead | kExecute, 0),
        mapping(0x6008, 0x100, kRead | kWrite, 1),
    };
    const auto stack = range(0x8000, 0x1000);
    const auto plan = e::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x4000}, GuestAddress{0x8000}));
    REQUIRE(plan.has_value());
    const auto allowed = [&](std::uint64_t address, std::size_t count) {
        return e::guest_write_owned_by_plan(
            plan.value(), mappings, stack, GuestAddress{address}, count);
    };
    REQUIRE(allowed(0x6008, 1));
    REQUIRE(allowed(0x6100, 8));
    REQUIRE(allowed(0x8000, 8));
    REQUIRE(allowed(0x8ff8, 8));
    REQUIRE_FALSE(allowed(0x6000, 1)); // host-page padding
    REQUIRE_FALSE(allowed(0x6108, 1)); // tail padding
    REQUIRE_FALSE(allowed(0x6100, 9)); // crosses raw mapping
    REQUIRE_FALSE(allowed(0x4000, 1)); // guest code is RX
    REQUIRE_FALSE(allowed(0x5000, 1)); // not in GuestImage
    REQUIRE_FALSE(allowed(0x9000, 1)); // after guest stack
    REQUIRE_FALSE(allowed(0x8000, 0)); // zero write
    REQUIRE_FALSE(allowed(
        std::numeric_limits<std::uint64_t>::max() - 3U, 8));
}

TEST_CASE("write authorization rejects conflicting sources and forged W+X host page",
          "[execution][memory-plan][guest-write]") {
    namespace e = astraea::execution;
    std::array mappings{
        mapping(0x4000, 0x200, kRead | kExecute, 0),
        mapping(0x6008, 0x100, kRead | kWrite, 1),
    };
    const auto stack = range(0x8000, 0x1000);
    auto plan = e::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x4000}, GuestAddress{0x8000}));
    REQUIRE(plan.has_value());
    const auto check = [&](const auto& candidate, std::uint64_t addr,
                           std::size_t size) {
        return e::guest_write_owned_by_plan(
            candidate, mappings, stack, GuestAddress{addr}, size);
    };
    REQUIRE(check(plan.value(), 0x6008, 8));
    for (auto& region : plan->regions) {
        if (!region.range.contains(GuestAddress{0x6008})) continue;
        region.permissions = permissions(kRead | kWrite | kExecute);
    }
    REQUIRE_FALSE(check(plan.value(), 0x6008, 8));

    const auto trusted = e::build_execution_memory_plan(
        request(mappings, stack, GuestAddress{0x4000}, GuestAddress{0x8000}));
    REQUIRE(trusted.has_value());
    // A write-bearing mapping cannot silently authorize bytes that an
    // overlapping independent guest source defines as read-only.
    std::array overlapped{
        mappings[0],
        mappings[1],
        mapping(0x6010, 0x10, kRead, 2),
    };
    REQUIRE_FALSE(e::guest_write_owned_by_plan(
        trusted.value(), overlapped, stack, GuestAddress{0x6010}, 8));
    REQUIRE(e::guest_write_owned_by_plan(
        trusted.value(), overlapped, stack, GuestAddress{0x6030}, 8));
}
