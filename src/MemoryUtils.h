#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace arc_autoplay::mem {

template <typename T>
inline T Read(uintptr_t addr) {
    return *reinterpret_cast<T *>(addr);
}

template <typename T>
inline void Write(uintptr_t addr, T value) {
    *reinterpret_cast<T *>(addr) = value;
}

struct MemRange {
    uintptr_t start;
    uintptr_t end;
};

class ProcMaps {
public:
    static uintptr_t FindLibraryBase(std::string_view soname);
    static bool GetLibraryExecRanges(std::string_view soname,
                                     std::array<MemRange, 64> &out_ranges,
                                     size_t &out_count);
    static bool GetPermissions(uintptr_t addr, int &out_perms);
    static bool IsReadable(uintptr_t addr, size_t len);
    static bool IsExecutable(uintptr_t addr);
};

class AddressResolver {
public:
    explicit AddressResolver(uintptr_t lib_base = 0) : lib_base_(lib_base) {}

    void SetLibBase(uintptr_t lib_base) { lib_base_ = lib_base; }
    uintptr_t GetLibBase() const { return lib_base_; }

    uintptr_t ResolveBySignature(uintptr_t hint_offset,
                                 const uint8_t *sig,
                                 size_t sig_len,
                                 std::string_view soname) const;

private:
    uintptr_t lib_base_ = 0;
};

class Patcher {
public:
    static bool PatchU32WithPerms(uintptr_t addr, uint32_t value);
    static bool PatchA64ClearImm12(uintptr_t addr, uint32_t expected_insn);
    static bool PatchA64AbsoluteJump(uintptr_t target, uintptr_t dst);
};

class InlineHook {
public:
    static bool InstallA64(uintptr_t target, void *hook_fn, void **orig_fn_out);
};

bool IsAddrInLibraryExec(uintptr_t addr, std::string_view soname);

} // namespace arc_autoplay::mem
