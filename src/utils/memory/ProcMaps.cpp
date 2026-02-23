#include "utils/memory/ProcMaps.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstring>

#include <sys/mman.h>

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

} // namespace arc_autoplay::mem
