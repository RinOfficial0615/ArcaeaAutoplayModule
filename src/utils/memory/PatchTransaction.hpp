#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <vector>

#include "utils/memory/MemoryError.hpp"
#include "utils/memory/RuntimeMemory.hpp"

namespace arc_helper::mem {

enum class PatchState : uint8_t {
    Ready,
    Applied,
    RolledBack,
    Degraded,
};

struct PatchDescriptor {
    uintptr_t address = 0;
    std::span<const std::byte> expected{};
    std::span<const std::byte> replacement{};
};

struct PatchFailure {
    static constexpr size_t kNoDescriptor = std::numeric_limits<size_t>::max();

    PatchState state = PatchState::Ready;
    MemoryError error = MemoryError::InvalidState;
    size_t descriptor_index = kNoDescriptor;
    uintptr_t address = 0;
};

// Applies a group of byte patches as one transaction. Every descriptor must
// have equal-sized expected and replacement bytes. All descriptors are read
// and validated before the first page permission is changed.
class PatchTransaction {
public:
    using Result = std::expected<void, PatchFailure>;

    explicit PatchTransaction(RuntimeMemory memory = RuntimeMemory::Process());
    ~PatchTransaction();

    PatchTransaction(const PatchTransaction &) = delete;
    PatchTransaction &operator=(const PatchTransaction &) = delete;

    Result Apply(std::span<const PatchDescriptor> descriptors);
    Result Rollback();

    [[nodiscard]] PatchState State() const noexcept { return state_; }
    [[nodiscard]] bool IsApplied() const noexcept { return state_ == PatchState::Applied; }
    [[nodiscard]] bool IsDegraded() const noexcept { return state_ == PatchState::Degraded; }

private:
    struct PageRecord {
        uintptr_t address = 0;
        int permissions = 0;
        bool writable_changed = false;
    };

    struct PatchRecord {
        uintptr_t address = 0;
        std::vector<std::byte> original;
        std::vector<std::byte> replacement;
        bool touched = false;
    };

    static constexpr size_t kNoDescriptor = PatchFailure::kNoDescriptor;

    Result InvalidStateFailure() const;
    Result Failure(MemoryError error, size_t descriptor_index, uintptr_t address);

    std::expected<void, MemoryError> CapturePages(uintptr_t address, size_t size);
    std::expected<void, MemoryError> MakePagesWritable();
    std::expected<void, MemoryError> PrepareWritablePages();
    bool RestorePages();
    bool RollbackApplied();
    void ClearRecords();

    RuntimeMemory memory_;
    PatchState state_ = PatchState::Ready;
    std::vector<PageRecord> pages_;
    std::vector<PatchRecord> patches_;
};

} // namespace arc_helper::mem
