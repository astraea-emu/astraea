#pragma once

#include <cstdint>
#include <vector>

#include <astraea/core/result.hpp>
#include <astraea/execution/backend.hpp>
#include <astraea/execution/memory_plan.hpp>
#include <astraea/loader/guest_image.hpp>
#include <astraea/memory/guest_address.hpp>

namespace astraea::execution {

class LinuxPreparedMemory {
public:
    LinuxPreparedMemory() = default;
    ~LinuxPreparedMemory();

    LinuxPreparedMemory(const LinuxPreparedMemory&) = delete;
    LinuxPreparedMemory& operator=(const LinuxPreparedMemory&) = delete;

    LinuxPreparedMemory(LinuxPreparedMemory&& other) noexcept;
    LinuxPreparedMemory& operator=(LinuxPreparedMemory&& other) noexcept;

    [[nodiscard]] const ExecutionMemoryPlan& plan() const noexcept {
        return plan_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return mapped_regions_.empty();
    }

private:
    friend astraea::core::Result<LinuxPreparedMemory, NativeBackendError>
    prepare_linux_guest_memory(const astraea::loader::GuestImage& image);

    void reset() noexcept;

    ExecutionMemoryPlan plan_{.host_page_size = 0, .regions = {}};
    std::vector<astraea::memory::GuestRange> mapped_regions_;
};

using LinuxPreparedMemoryResult =
    astraea::core::Result<LinuxPreparedMemory, NativeBackendError>;

[[nodiscard]] bool linux_native_memory_backend_available() noexcept;

[[nodiscard]] LinuxPreparedMemoryResult prepare_linux_guest_memory(
    const astraea::loader::GuestImage& image);

}  // namespace astraea::execution
