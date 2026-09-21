#include <astraea/execution/context.hpp>

namespace astraea::execution {

GuestCpuContext make_synthetic_initial_context(
    std::uint64_t entry_point,
    astraea::memory::GuestAddress stack_pointer) noexcept {
    GuestCpuContext context;
    context.rsp = stack_pointer.value();
    context.rip = entry_point;
    context.rflags = kSyntheticInitialRflags;
    return context;
}

GuestCpuContext make_synthetic_initial_context(
    const astraea::loader::GuestImage& image) noexcept {
    return make_synthetic_initial_context(
        image.elf.header.entry,
        image.initial_stack.rsp);
}

}  // namespace astraea::execution
