#pragma once

#include <span>
#include <utility>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/backend.hpp>
#include <astraea/execution/memory_plan.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

class WindowsPreparedMemory {
public:
    WindowsPreparedMemory() = default;
    WindowsPreparedMemory(const WindowsPreparedMemory&) = delete;
    WindowsPreparedMemory& operator=(const WindowsPreparedMemory&) = delete;

    WindowsPreparedMemory(WindowsPreparedMemory&& other) noexcept;
    WindowsPreparedMemory& operator=(WindowsPreparedMemory&& other) noexcept;
    ~WindowsPreparedMemory();

    [[nodiscard]] const ExecutionMemoryPlan& plan() const noexcept {
        return plan_;
    }

    [[nodiscard]] std::span<const astraea::memory::GuestRange>
    reservation_ranges() const noexcept {
        return reservation_ranges_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return reservation_ranges_.empty();
    }

private:
    friend astraea::core::Result<
        WindowsPreparedMemory,
        NativeBackendError>
    prepare_windows_guest_memory(
        const astraea::loader::GuestImage& image);

    void reset() noexcept;

    ExecutionMemoryPlan plan_{
        .host_page_size = 0,
        .regions = {},
    };
    std::vector<astraea::memory::GuestRange> reservation_ranges_;
};

using WindowsPreparedMemoryResult =
    astraea::core::Result<
        WindowsPreparedMemory,
        NativeBackendError>;

[[nodiscard]] bool windows_native_memory_backend_available() noexcept;

[[nodiscard]] WindowsPreparedMemoryResult prepare_windows_guest_memory(
    const astraea::loader::GuestImage& image);

}  // namespace astraea::execution
