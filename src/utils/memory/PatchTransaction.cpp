#include "utils/memory/PatchTransaction.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

#if defined(__ANDROID__) || defined(__linux__)
#include <sys/mman.h>
#else
constexpr int PROT_READ = 0x1;
constexpr int PROT_WRITE = 0x2;
#endif

#include "utils/memory/CheckedRange.hpp"

namespace arc_helper::mem {
namespace {

using detail::CheckedEnd;

std::expected<std::pair<uintptr_t, uintptr_t>, MemoryError> PageBounds(uintptr_t address,
                                                                        size_t size,
                                                                        size_t page_size) {
    const auto end = CheckedEnd(address, size);
    if (!end) return std::unexpected(end.error());
    if (page_size == 0) return std::unexpected(MemoryError::InvalidState);

    const uintptr_t first = address / page_size * page_size;
    const uintptr_t last = *end;
    const uintptr_t remainder = last % page_size;
    if (remainder == 0) return std::pair{first, last};

    const uintptr_t increment = page_size - remainder;
    if (increment > std::numeric_limits<uintptr_t>::max() - last) {
        return std::unexpected(MemoryError::Overflow);
    }
    return std::pair{first, last + increment};
}

} // namespace

PatchTransaction::PatchTransaction(RuntimeMemory memory) : memory_(memory) {}

PatchTransaction::~PatchTransaction() {
    if (state_ == PatchState::Applied || state_ == PatchState::Degraded) {
        (void)Rollback();
    }
}

PatchTransaction::Result PatchTransaction::InvalidStateFailure() const {
    return std::unexpected(PatchFailure{state_, MemoryError::InvalidState});
}

PatchTransaction::Result PatchTransaction::Failure(MemoryError error,
                                                   size_t descriptor_index,
                                                   uintptr_t address) {
    const bool has_mutation =
        std::ranges::any_of(patches_, [](const auto &patch) { return patch.touched; }) ||
        std::ranges::any_of(pages_, [](const auto &page) { return page.writable_changed; });
    if (!has_mutation) {
        ClearRecords();
        state_ = PatchState::Ready;
        return std::unexpected(PatchFailure{PatchState::Ready, error, descriptor_index, address});
    }

    if (RollbackApplied()) {
        // A failed Apply leaves no active transaction. RolledBack is reserved
        // for an explicit rollback of a previously applied transaction.
        state_ = PatchState::Ready;
        ClearRecords();
        return std::unexpected(PatchFailure{PatchState::Ready, error, descriptor_index, address});
    }

    state_ = PatchState::Degraded;
    return std::unexpected(
        PatchFailure{PatchState::Degraded, MemoryError::RestoreFailed, descriptor_index, address});
}

PatchTransaction::Result PatchTransaction::Apply(std::span<const PatchDescriptor> descriptors) {
    if (state_ == PatchState::Applied || state_ == PatchState::Degraded) {
        return InvalidStateFailure();
    }
    if (descriptors.empty()) {
        return std::unexpected(
            PatchFailure{PatchState::Ready, MemoryError::InvalidRange, kNoDescriptor, 0});
    }

    ClearRecords();
    state_ = PatchState::Ready;
    patches_.reserve(descriptors.size());

    // Preflight all descriptors before changing a permission or writing a byte.
    for (size_t index = 0; index < descriptors.size(); ++index) {
        const auto &descriptor = descriptors[index];
        if (descriptor.address == 0 || descriptor.expected.empty() ||
            descriptor.expected.size() != descriptor.replacement.size() ||
            descriptor.replacement.data() == nullptr) {
            return Failure(MemoryError::InvalidRange, index, descriptor.address);
        }
        if (!descriptor.expected.data()) {
            return Failure(MemoryError::InvalidRange, index, descriptor.address);
        }
        const auto end = CheckedEnd(descriptor.address, descriptor.expected.size());
        if (!end) return Failure(end.error(), index, descriptor.address);
        for (size_t previous_index = 0; previous_index < patches_.size(); ++previous_index) {
            const auto &previous = patches_[previous_index];
            const auto previous_end = CheckedEnd(previous.address, previous.original.size());
            if (!previous_end ||
                (descriptor.address < *previous_end && previous.address < *end)) {
                return Failure(MemoryError::InvalidRange, index, descriptor.address);
            }
        }

        PatchRecord record;
        record.address = descriptor.address;
        record.original.resize(descriptor.expected.size());
        record.replacement.assign(descriptor.replacement.begin(), descriptor.replacement.end());

        const auto read = memory_.ReadBytes(record.address, std::span<std::byte>(record.original));
        if (!read) return Failure(read.error(), index, descriptor.address);
        if (!std::ranges::equal(record.original, descriptor.expected)) {
            return Failure(MemoryError::SignatureMismatch, index, descriptor.address);
        }
        if (const auto pages = CapturePages(record.address, record.original.size()); !pages) {
            return Failure(pages.error(), index, descriptor.address);
        }
        patches_.push_back(std::move(record));
    }

    if (const auto writable = PrepareWritablePages(); !writable) {
        return Failure(writable.error(), kNoDescriptor, 0);
    }

    for (size_t index = 0; index < patches_.size(); ++index) {
        auto &patch = patches_[index];
        // Mark before calling the backend: a backend is allowed to report a
        // failed write after partially touching a destination.
        patch.touched = true;
        const auto write = memory_.WriteBytes(patch.address, std::span<const std::byte>(patch.replacement));
        if (!write) return Failure(write.error(), index, patch.address);
        __builtin___clear_cache(reinterpret_cast<char *>(patch.address),
                                reinterpret_cast<char *>(patch.address + patch.replacement.size()));
    }

    if (!RestorePages()) return Failure(MemoryError::ProtectionChangeFailed, kNoDescriptor, 0);

    state_ = PatchState::Applied;
    return {};
}

PatchTransaction::Result PatchTransaction::Rollback() {
    if (state_ == PatchState::Ready || state_ == PatchState::RolledBack) {
        return state_ == PatchState::RolledBack ? Result{} : InvalidStateFailure();
    }

    if (!RollbackApplied()) {
        state_ = PatchState::Degraded;
        return std::unexpected(PatchFailure{PatchState::Degraded, MemoryError::RestoreFailed, kNoDescriptor, 0});
    }

    state_ = PatchState::RolledBack;
    ClearRecords();
    return {};
}

std::expected<void, MemoryError> PatchTransaction::CapturePages(uintptr_t address, size_t size) {
    const size_t page_size = RuntimeMemory::PageSize();
    const auto bounds = PageBounds(address, size, page_size);
    if (!bounds) return std::unexpected(bounds.error());

    for (uintptr_t page = bounds->first; page < bounds->second;) {
        if (!std::ranges::contains(pages_, page, &PageRecord::address)) {
            const auto permissions = memory_.GetPermissions(page);
            if (!permissions) return std::unexpected(permissions.error());
            pages_.push_back(PageRecord{page, *permissions, false});
        }
        if (bounds->second - page <= page_size) break;
        page += page_size;
    }
    return {};
}

// Pages that are already writable are skipped: their permissions were never
// changed, so RestorePages() must not undo a state we did not set.
std::expected<void, MemoryError> PatchTransaction::MakePagesWritable() {
    const size_t page_size = RuntimeMemory::PageSize();
    for (auto &page : pages_) {
        if ((page.permissions & PROT_WRITE) != 0) continue;
        page.writable_changed = true;
        const int writable_permissions = page.permissions | PROT_READ | PROT_WRITE;
        if (const auto result = memory_.Protect(page.address, page_size, writable_permissions);
            !result) {
            return std::unexpected(result.error());
        }
    }
    return {};
}

std::expected<void, MemoryError> PatchTransaction::PrepareWritablePages() {
    return MakePagesWritable();
}

bool PatchTransaction::RestorePages() {
    bool restored = true;
    const size_t page_size = RuntimeMemory::PageSize();
    for (auto &page : pages_ | std::views::reverse) {
        if (!page.writable_changed) continue;
        if (memory_.Protect(page.address, page_size, page.permissions)) {
            page.writable_changed = false;
        } else {
            restored = false;
        }
    }
    return restored;
}

bool PatchTransaction::RollbackApplied() {
    // Re-open every non-writable page, including pages whose first restore
    // succeeded. This keeps an explicit retry possible after a degraded write.
    bool restored = MakePagesWritable().has_value();

    for (auto &patch : patches_ | std::views::reverse) {
        if (!patch.touched) continue;
        if (!memory_.WriteBytes(patch.address, std::span<const std::byte>(patch.original))) {
            restored = false;
        } else {
            __builtin___clear_cache(reinterpret_cast<char *>(patch.address),
                                    reinterpret_cast<char *>(patch.address + patch.original.size()));
        }
    }

    if (!RestorePages()) restored = false;
    return restored;
}

void PatchTransaction::ClearRecords() {
    patches_.clear();
    pages_.clear();
}

} // namespace arc_helper::mem
