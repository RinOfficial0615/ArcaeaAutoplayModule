#include "MemoryUtils.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

#include <sys/mman.h>
#include <unistd.h>

namespace arc_autoplay::mem {
namespace {

bool EndsWith(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return memcmp(s.data() + (s.size() - suffix.size()), suffix.data(), suffix.size()) == 0;
}

std::string_view TrimSpaces(std::string_view sv) {
    while (!sv.empty()) {
        const char c = sv.front();
        if (c == ' ' || c == '\t') {
            sv.remove_prefix(1);
            continue;
        }
        break;
    }

    while (!sv.empty()) {
        const char c = sv.back();
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            sv.remove_suffix(1);
            continue;
        }
        break;
    }

    return sv;
}

bool ParseProcMapsLine(const char *line,
                      uintptr_t *out_start,
                      uintptr_t *out_end,
                      uintptr_t *out_off,
                      int *out_perms,
                      std::string_view *out_path) {
    if (!line || !out_start || !out_end || !out_off || !out_perms || !out_path) return false;

    uintptr_t start = 0;
    uintptr_t end = 0;
    uintptr_t off = 0;
    unsigned long inode = 0;
    unsigned int dev_major = 0;
    unsigned int dev_minor = 0;
    char perm[5] = {'\0'};
    int path_off = 0;

    const int scanned = sscanf(line,
                               "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %x:%x %lu %n",
                               &start,
                               &end,
                               perm,
                               &off,
                               &dev_major,
                               &dev_minor,
                               &inode,
                               &path_off);
    if (scanned != 7) return false;

    int perms = 0;
    if (perm[0] == 'r') perms |= PROT_READ;
    if (perm[1] == 'w') perms |= PROT_WRITE;
    if (perm[2] == 'x') perms |= PROT_EXEC;

    std::string_view path;
    if (path_off > 0) {
        path = std::string_view(line + path_off);
        path = TrimSpaces(path);
    }

    *out_start = start;
    *out_end = end;
    *out_off = off;
    *out_perms = perms;
    *out_path = path;
    return true;
}

uintptr_t FindUniqueBytesInRanges(const MemRange *ranges,
                                  size_t range_count,
                                  const uint8_t *sig,
                                  size_t sig_len,
                                  size_t align,
                                  int *out_hits) {
    if (out_hits) *out_hits = 0;
    if (!ranges || range_count == 0 || !sig || sig_len == 0) return 0;
    if (align == 0) align = 1;

    uintptr_t found = 0;
    int hits = 0;

    for (size_t r = 0; r < range_count; ++r) {
        const uintptr_t start = ranges[r].start;
        const uintptr_t end = ranges[r].end;
        if (end <= start) continue;
        if ((end - start) < sig_len) continue;

        const uintptr_t last = end - sig_len;
        for (uintptr_t p = start; p <= last; p += align) {
            if (memcmp(reinterpret_cast<const void *>(p), sig, sig_len) != 0) continue;

            hits += 1;
            if (hits == 1) {
                found = p;
            } else {
                if (out_hits) *out_hits = hits;
                return 0;
            }
        }
    }

    if (out_hits) *out_hits = hits;
    return (hits == 1) ? found : 0;
}

} // namespace

uintptr_t ProcMaps::FindLibraryBase(std::string_view soname) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;

    char line[4096];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        uintptr_t off = 0;
        int perms = 0;
        std::string_view path;
        if (!ParseProcMapsLine(line, &start, &end, &off, &perms, &path)) continue;
        (void)end;
        (void)perms;
        if (!EndsWith(path, soname)) continue;

        const uintptr_t candidate = start - off;
        if (base == 0 || candidate < base) base = candidate;
    }

    fclose(fp);
    return base;
}

bool ProcMaps::GetLibraryExecRanges(std::string_view soname,
                                    std::array<MemRange, 64> &out_ranges,
                                    size_t &out_count) {
    out_count = 0;

    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        uintptr_t off = 0;
        int perms = 0;
        std::string_view path;
        if (!ParseProcMapsLine(line, &start, &end, &off, &perms, &path)) continue;
        (void)off;
        if ((perms & PROT_EXEC) == 0) continue;
        if (!EndsWith(path, soname)) continue;
        if (out_count >= out_ranges.size()) break;

        out_ranges[out_count++] = MemRange{start, end};
    }

    fclose(fp);
    return out_count > 0;
}

bool ProcMaps::GetPermissions(uintptr_t addr, int &out_perms) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0;
        uintptr_t end = 0;
        uintptr_t off = 0;
        int perms = 0;
        std::string_view path;
        if (!ParseProcMapsLine(line, &start, &end, &off, &perms, &path)) continue;
        (void)off;
        (void)path;

        if (addr >= start && addr < end) {
            out_perms = perms;
            fclose(fp);
            return true;
        }
    }

    fclose(fp);
    return false;
}

bool ProcMaps::IsReadable(uintptr_t addr, size_t len) {
    if (len == 0) return false;

    int p0 = 0;
    int p1 = 0;
    if (!GetPermissions(addr, p0)) return false;
    if (!GetPermissions(addr + len - 1, p1)) return false;
    return ((p0 & PROT_READ) != 0) && ((p1 & PROT_READ) != 0);
}

bool ProcMaps::IsExecutable(uintptr_t addr) {
    int perms = 0;
    return GetPermissions(addr, perms) && ((perms & PROT_EXEC) != 0);
}

uintptr_t AddressResolver::ResolveBySignature(uintptr_t hint_offset,
                                              const uint8_t *sig,
                                              size_t sig_len,
                                              std::string_view soname) const {
    if (!lib_base_ || !sig || sig_len == 0) return 0;

    const uintptr_t hint = lib_base_ + hint_offset;
    if (ProcMaps::IsReadable(hint, sig_len) &&
        memcmp(reinterpret_cast<void *>(hint), sig, sig_len) == 0) {
        return hint;
    }

    std::array<MemRange, 64> exec_ranges{};
    size_t exec_count = 0;
    if (!ProcMaps::GetLibraryExecRanges(soname, exec_ranges, exec_count)) return 0;

    return FindUniqueBytesInRanges(exec_ranges.data(), exec_count, sig, sig_len, 4, nullptr);
}

bool Patcher::PatchA64AbsoluteJump(uintptr_t target, uintptr_t dst) {
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return false;

    const uintptr_t page_mask = static_cast<uintptr_t>(page_size - 1);
    const uintptr_t page_start = target & ~page_mask;
    const uintptr_t page_end = (target + 16 + page_mask) & ~page_mask;
    const size_t protect_size = page_end - page_start;

    int original_perms = 0;
    if (!ProcMaps::GetPermissions(target, original_perms)) return false;

    if (mprotect(reinterpret_cast<void *>(page_start), protect_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }

    // ldr x17, #8; br x17; .quad dst
    Write<uint32_t>(target + 0, 0x58000051);
    Write<uint32_t>(target + 4, 0xD61F0220);
    Write<uint64_t>(target + 8, static_cast<uint64_t>(dst));

    __builtin___clear_cache(reinterpret_cast<char *>(target), reinterpret_cast<char *>(target + 16));
    mprotect(reinterpret_cast<void *>(page_start), protect_size, original_perms);
    return true;
}

bool Patcher::PatchU32WithPerms(uintptr_t addr, uint32_t value) {
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return false;

    const uintptr_t page_mask = static_cast<uintptr_t>(page_size - 1);
    const uintptr_t page_start = addr & ~page_mask;
    const uintptr_t page_end = (addr + 4 + page_mask) & ~page_mask;
    const size_t protect_size = page_end - page_start;

    int original_perms = 0;
    if (!ProcMaps::GetPermissions(addr, original_perms)) return false;

    if (mprotect(reinterpret_cast<void *>(page_start), protect_size, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        return false;
    }

    Write<uint32_t>(addr, value);
    __builtin___clear_cache(reinterpret_cast<char *>(addr), reinterpret_cast<char *>(addr + 4));
    mprotect(reinterpret_cast<void *>(page_start), protect_size, original_perms);
    return true;
}

bool Patcher::PatchA64ClearImm12(uintptr_t addr, uint32_t expected_insn) {
    if (!ProcMaps::IsReadable(addr, 4)) return false;
    const uint32_t cur = Read<uint32_t>(addr);
    if (cur != expected_insn) return false;

    constexpr uint32_t kImm12Mask = 0x003FFC00u;
    const uint32_t patched = cur & ~kImm12Mask;
    return PatchU32WithPerms(addr, patched);
}

bool InlineHook::InstallA64(uintptr_t target, void *hook_fn, void **orig_fn_out) {
    if (!hook_fn || !orig_fn_out) return false;

    constexpr size_t kTrampSize = 64;
    void *tramp = mmap(nullptr,
                       kTrampSize,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS,
                       -1,
                       0);
    if (tramp == MAP_FAILED) return false;

    memcpy(tramp, reinterpret_cast<void *>(target), 16);

    const uintptr_t tramp_jmp = reinterpret_cast<uintptr_t>(tramp) + 16;
    Write<uint32_t>(tramp_jmp + 0, 0x58000051);
    Write<uint32_t>(tramp_jmp + 4, 0xD61F0220);
    Write<uint64_t>(tramp_jmp + 8, static_cast<uint64_t>(target + 16));
    __builtin___clear_cache(reinterpret_cast<char *>(tramp),
                            reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(tramp) + 32));

    *orig_fn_out = tramp;
    return Patcher::PatchA64AbsoluteJump(target, reinterpret_cast<uintptr_t>(hook_fn));
}

bool IsAddrInLibraryExec(uintptr_t addr, std::string_view soname) {
    if (!addr) return false;

    // Hot path: this is called per-frame for vcall guards.
    // Cache the library's executable ranges once per soname.
    static std::array<MemRange, 64> s_exec_ranges{};
    static size_t s_exec_count = 0;
    static bool s_cached = false;
    static std::string s_soname;

    const bool same_soname = (s_soname.size() == soname.size()) &&
                             (soname.size() == 0 || memcmp(s_soname.data(), soname.data(), soname.size()) == 0);
    if (!s_cached || !same_soname) {
        s_soname.assign(soname.data(), soname.size());
        s_exec_count = 0;
        s_cached = ProcMaps::GetLibraryExecRanges(soname, s_exec_ranges, s_exec_count);
    }

    if (s_cached) {
        for (size_t i = 0; i < s_exec_count; ++i) {
            if (addr >= s_exec_ranges[i].start && addr < s_exec_ranges[i].end) return true;
        }
    }

    return ProcMaps::IsExecutable(addr);
}

} // namespace arc_autoplay::mem
