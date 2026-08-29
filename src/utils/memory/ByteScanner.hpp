#pragma once

// Pure signature scan over caller-supplied address ranges.
//
// Deliberately free of any Android dependency: it takes a span of [start, end)
// pairs and only ever reads inside them. That keeps it host-testable -- a test
// can hand it the address range of an ordinary stack buffer, where every
// candidate position is readable. ResolveBySignature (Android) is just one
// caller, the one that supplies /proc-derived executable ranges.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "utils/memory/ProcMaps.hpp"

namespace arc_helper::mem {

// Finds the unique occurrence of `sig` inside `ranges`, probing every `align`
// bytes. `out_hits`, when non-null, always receives the number of matches seen
// -- including the second one that aborted the scan.
//
// Two or more matches make a signature useless as an address source, so the
// scan stops at the second hit rather than counting them all: the caller only
// ever needs to know "exactly one" or "not exactly one".
//
// Returns the match address, or 0 when the count is not exactly 1.
inline uintptr_t FindUniqueBytesInRanges(std::span<const MemRange> ranges,
                                         const uint8_t *sig,
                                         size_t sig_len,
                                         size_t align,
                                         int *out_hits) {
    if (out_hits) *out_hits = 0;
    if (ranges.empty() || !sig || sig_len == 0) return 0;
    if (align == 0) align = 1;

    uintptr_t found = 0;
    int hits = 0;

    for (const MemRange &range : ranges) {
        // A range shorter than the signature cannot hold a match; an inverted
        // one only shows up when a mapping is torn down mid-scan.
        if (range.end <= range.start || (range.end - range.start) < sig_len) continue;

        const uintptr_t last = range.end - sig_len;
        for (uintptr_t p = range.start;;) {
            if (memcmp(reinterpret_cast<const void *>(p), sig, sig_len) == 0) {
                if (++hits > 1) {
                    if (out_hits) *out_hits = hits;
                    return 0;
                }
                found = p;
            }
            if (last - p < align) break;
            p += align;
        }
    }

    if (out_hits) *out_hits = hits;
    return (hits == 1) ? found : 0;
}

} // namespace arc_helper::mem
