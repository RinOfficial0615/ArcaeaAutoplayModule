#!/usr/bin/env python3
"""Cross-version ARM64 function relocation matcher for libcocos2dcpp.so.

Given a function address in a reference (old) binary, ``locate`` finds the
same function in a new binary. Instruction words are compared after masking
bits that depend on code placement (PC-relative immediates, branch targets,
wide move immediates), so recompilation and section drift survive the match.
Several sliding anchor windows per target vote on candidate base addresses;
a consensus backed by every anchor is reported as UNIQUE.

``diff`` compares pinned old/new pairs word by word and reports how much of
each body survived verbatim, which words differ only in address fields, and
where the real semantic changes (inserted/deleted logic) live.

Typical workflow when adapting a new game version:

    # 1. Copy the previous version's spec and adjust paths/sizes, keeping
    #    only {"name", "old", "size"} per function (drop stale "new" pins):
    #        {"functions": [{"name": "...", "old": "0xD7A8D4", "size": 1760}]}
    #    Old sizes come from IDA's function extents; old addresses from the
    #    previous docs/offsets/<version>-offsets.md table.
    python scripts/port_match.py locate ../6.16.8c/libcocos2dcpp.so \
        ../7.0.0c/libcocos2dcpp.so spec.json -o result.json
    # 2. Pick each target's winning candidate, pin it as "new" in the spec,
    #    rerun locate to verify every pin still ranks first, then confirm
    #    semantics with diff:
    python scripts/port_match.py diff ... ../7.0.0c/libcocos2dcpp.so spec.json
    # 3. Record accepted offsets in src/game/GameProfile.hpp and
    #    docs/offsets/<version>-offsets.md; extend tests/verify_profile.py.
    # scripts/port_match_7000c_spec.json documents the finished mapping of
    # one full migration and doubles as a worked example of the format.

Spec format (JSON): {"functions": [{"name", "old", "size" [, "new"]}]} where
addresses accept ints or "0x..." strings and ``size`` is the function body
size in bytes (function extent, so anchor windows sweep the whole body).

Entries carrying "new" are still located; the pin is verified against the
search result. By default a pin counts as PINNED when its own anchor support
reaches a quarter of the leading candidate's votes — functions whose bodies
changed between builds never win consensus at their true start (insertions
shift mid-body windows), yet their start keeps substantial support; a
mistyped pin gets zero or negligible votes and fails loudly. For bodies that
diverged too much even for that (heavy rewrites), add "expect_bytes": a hex
string of >=4 bytes copied from IDA at the pinned address; the pin is then
confirmed by exact masked comparison against the new binary and consensus is
bypassed entirely. Exit status is non-zero when any unpinned target ends up
NONE/WEAK/AMBIGUOUS or any pin fails — usable as an automated gate before
editing profiles.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import struct
import sys

try:
    import numpy as np
except ImportError:  # pure-Python fallback works, just slower
    np = None

WORD = 4


def load_segments(path):
    data = pathlib.Path(path).read_bytes()
    if data[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF file")
    e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
    e_phnum = struct.unpack_from("<H", data, 0x38)[0]
    segs = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags, p_offset, p_vaddr, _pa, p_filesz, _memsz, _al = struct.unpack_from(
            "<IIQQQQQQ", data, off)
        if p_type == 1 and p_filesz:  # PT_LOAD
            segs.append({"vaddr": p_vaddr, "offset": p_offset, "filesz": p_filesz,
                         "flags": p_flags})
    return data, segs


class Image:
    """Word-granular view over an ELF file mapped through its PT_LOADs."""

    def __init__(self, path):
        self.path = str(path)
        self.data, self.segs = load_segments(path)

    def exec_seg(self):
        for seg in self.segs:
            if seg["flags"] & 1:  # PF_X
                return seg
        raise RuntimeError(f"{self.path}: no executable segment")

    def addr_to_off(self, addr, size):
        for seg in self.segs:
            if seg["vaddr"] <= addr and addr + size <= seg["vaddr"] + seg["filesz"]:
                return seg["offset"] + (addr - seg["vaddr"])
        raise KeyError(f"{self.path}: vaddr {hex(addr)}+{size} outside PT_LOADs")

    def words(self, addr, count):
        assert addr % WORD == 0, hex(addr)
        off = self.addr_to_off(addr, count * WORD)
        return struct.unpack_from("<%dI" % count, self.data, off)


def mask(w):
    """Return the instruction word with address/immediate-dependent bits zeroed."""
    fam26 = w >> 26
    if fam26 == 0b000101 or fam26 == 0b100101:      # B / BL
        return w & 0xFC000000
    if (w >> 24) == 0x54:                            # B.cond
        return w & 0xFF00000F
    if (w & 0x7E000000) == 0x34000000:               # CBZ / CBNZ
        return w & 0xFF00001F
    if (w & 0x7E000000) == 0x36000000:               # TBZ / TBNZ
        return w & 0x81F8001F                        # keep b5/op/b40/Rt, drop imm14
    if (w & 0x1F000000) == 0x10000000:               # ADR / ADRP
        return w & 0x9F00001F
    if (w & 0x3B000000) == 0x18000000:               # literal loads
        return w & 0xFFC0001F
    if (w & 0x1F800000) == 0x52800000:               # MOVN/MOVZ/MOVK wide imm
        return w & 0xFFE0001F
    return w


def build_masked(image):
    """Masked little-endian instruction stream over the executable segment."""
    seg = image.exec_seg()
    raw = struct.unpack_from("<%dI" % (seg["filesz"] // WORD), image.data, seg["offset"])
    if np is not None:
        arr = np.array(raw, dtype=np.uint32)
        fam26 = arr >> np.uint32(26)
        is_b = (fam26 == np.uint32(0b000101)) | (fam26 == np.uint32(0b100101))
        is_bc = (arr >> np.uint32(24)) == np.uint32(0x54)
        f7 = arr & np.uint32(0x7E000000)
        is_cbz = f7 == np.uint32(0x34000000)
        is_tbz = f7 == np.uint32(0x36000000)
        is_adr = (arr & np.uint32(0x1F000000)) == np.uint32(0x10000000)
        is_lit = (arr & np.uint32(0x3B000000)) == np.uint32(0x18000000)
        is_movw = (arr & np.uint32(0x1F800000)) == np.uint32(0x52800000)
        m = np.full(arr.shape, 0xFFFFFFFF, dtype=np.uint32)
        m = np.where(is_b, np.uint32(0xFC000000), m)
        m = np.where(is_bc, np.uint32(0xFF00000F), m)
        m = np.where(is_cbz, np.uint32(0xFF00001F), m)
        m = np.where(is_tbz, np.uint32(0x81F8001F), m)
        m = np.where(is_adr, np.uint32(0x9F00001F), m)
        m = np.where(is_lit, np.uint32(0xFFC0001F), m)
        m = np.where(is_movw, np.uint32(0xFFE0001F), m)
        return (arr & m).astype("<u4").tobytes(), seg["vaddr"]
    masked = b"".join(struct.pack("<I", mask(w)) for w in raw)
    return masked, seg["vaddr"]


def pack_masked(words):
    return b"".join(struct.pack("<I", mask(w)) for w in words)


def find_all(haystack, needle, cap=200):
    positions = []
    start = 0
    while True:
        hit = haystack.find(needle, start)
        if hit < 0:
            break
        positions.append(hit // WORD)
        if len(positions) >= cap:
            break
        start = hit + WORD
    return positions


def gen_windows(nwords, win, step, max_windows):
    """Anchor window offsets (in words) sweeping [0, nwords) end to end."""
    win = min(win, nwords)
    if nwords <= win:
        return [0]
    span = nwords - win
    if (span + step - 1) // step + 1 > max_windows:
        step = -(-span // max_windows)
    return sorted(set(list(range(0, span + 1, step)) + [span]))


def locate(new_packed, img_old, addr, rels, win):
    """Vote on candidate base words using anchors drawn from `addr`."""
    votes = {}
    hits = {}
    for rel in rels:
        pattern = pack_masked(img_old.words(addr + rel * WORD, win))
        positions = find_all(new_packed, pattern)
        hits[rel] = len(positions)
        for pos in positions:
            base = pos - rel
            votes[base] = votes.get(base, 0) + 1
    ranked = sorted(votes.items(), key=lambda kv: (-kv[1], kv[0]))
    return ranked, hits


def classify(ranked, window_count):
    """NONE: zero consensus; WEAK: low coverage; AMBIGUOUS: contested leader."""
    if not ranked or ranked[0][1] == 0:
        return "NONE"
    best = ranked[0][1]
    if best / window_count < 0.70:
        return "WEAK"
    runner = ranked[1][1] if len(ranked) > 1 else 0
    if runner >= 2 and runner >= best - 1:
        return "AMBIGUOUS"
    return "UNIQUE"


def parse_addr(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def load_spec(path):
    spec = json.loads(pathlib.Path(path).read_text())
    funcs = spec.get("functions")
    if not funcs:
        raise ValueError(f"{path}: spec needs a non-empty 'functions' array")
    normalized = []
    for func in funcs:
        if func.get("old") is None or func.get("size") is None:
            raise ValueError("every entry needs 'name'(optional), 'old' and 'size'")
        name = func.get("name") or hex(parse_addr(func["old"]))
        old = parse_addr(func["old"])
        size_field = func["size"]
        size = int(size_field, 0) if isinstance(size_field, str) else int(size_field)
        new = parse_addr(func["new"]) if func.get("new") is not None else None
        if size <= 0 or old % WORD or (new is not None and new % WORD):
            raise ValueError(f"{name}: bad size or unaligned address")
        expect_hex = func.get("expect_bytes")
        expect_bytes = None
        if expect_hex is not None:
            if new is None:
                raise ValueError(f"{name}: 'expect_bytes' needs a pinned 'new'")
            try:
                expect_bytes = bytes.fromhex(str(expect_hex).replace(" ", ""))
            except ValueError:
                raise ValueError(f"{name}: 'expect_bytes' is not valid hex")
            if len(expect_bytes) < WORD or len(expect_bytes) % WORD:
                raise ValueError(f"{name}: 'expect_bytes' must be a whole number "
                                 f"of {WORD}-byte instruction words")
        normalized.append({"name": name, "old": old, "size": size, "new": new,
                           "expect_bytes": expect_bytes})
    return normalized


def summarize(ranked, new_base, limit=2):
    parts = [f"{hex(new_base + base * WORD)}({votes})"
             for base, votes in ranked[:limit]]
    return ", ".join(parts) if parts else "-"


def cmd_locate(args):
    img_old = Image(args.old_so)
    img_new = Image(args.new_so)
    targets = load_spec(args.spec)
    new_packed, new_base = build_masked(img_new)
    print(f"new {img_new.path}: text vaddr={hex(new_base)} "
          f"words={len(new_packed) // WORD} numpy={'yes' if np is not None else 'no'}")

    results = {}
    failed = []
    for item in targets:
        nwords = max(item["size"] // WORD, 1)
        rels = gen_windows(nwords, args.window, args.step, args.max_windows)
        ranked, hits = locate(new_packed, img_old, item["old"], rels, args.window)

        verdict = classify(ranked, len(rels))
        record = {
            "old": hex(item["old"]),
            "size": hex(item["size"]),
            "windows": len(rels),
            "anchor_hits": {("+%s" % hex(rel * WORD)): count
                            for rel, count in sorted(hits.items()) if count},
            "candidates": [{"addr": hex(new_base + base * WORD),
                            "votes": votes,
                            "coverage": round(votes / len(rels), 2)}
                           for base, votes in ranked[:10]],
            "verdict": verdict,
        }

        status = verdict
        if item["new"] is not None:
            exp_word = (item["new"] - new_base) // WORD
            exp_votes = dict(ranked).get(exp_word, 0)
            order = [base for base, _ in ranked]
            rank = order.index(exp_word) if exp_word in order else -1
            record["expected"] = {"addr": hex(item["new"]),
                                  "votes": exp_votes, "rank": rank}
            if item.get("expect_bytes"):
                actual = pack_masked(img_new.words(item["new"],
                                                   len(item["expect_bytes"]) // WORD))
                pin_ok = actual == item["expect_bytes"]
                how = "bytes"
            else:
                leader_votes = ranked[0][1] if ranked else 0
                threshold = max(1, -(-leader_votes // 4))
                pin_ok = exp_votes >= threshold
                how = f"v{exp_votes}@r{rank}"
            if pin_ok:
                status = "PINNED"
                print(f"[{status:>12s}] {item['name']:34s} old={record['old']} -> "
                      f"{summarize(ranked, new_base)} pin ok({how})")
                results[item["name"]] = record
                continue
            status = "PIN-MISMATCH"
            failed.append(item["name"])

        print(f"[{status:>12s}] {item['name']:34s} old={record['old']} -> "
              f"{summarize(ranked, new_base)}")
        if item["new"] is None and verdict != "UNIQUE":
            failed.append(item["name"])
        results[item["name"]] = record

    if args.out:
        pathlib.Path(args.out).write_text(json.dumps(results, indent=1))
        print(f"wrote {args.out}")
    if failed:
        print("NOT CONFIDENT (%d): %s" % (len(failed), " ".join(sorted(set(failed)))))
        return 2
    print("all targets confident")
    return 0


def diff_pair(img_old, img_new, item):
    owords = img_old.words(item["old"], item["size"] // WORD)
    nwords_ = img_new.words(item["new"], item["size"] // WORD)
    exact = rel_only = 0
    semantic = []
    for index, (a, b) in enumerate(zip(owords, nwords_)):
        if a == b:
            exact += 1
        elif mask(a) == mask(b):
            rel_only += 1
        elif semantic and semantic[-1][1] == index - 1:
            semantic[-1][1] = index
        else:
            semantic.append([index, index])
    total = len(owords)
    return {
        "name": item["name"],
        "old": hex(item["old"]),
        "new": hex(item["new"]),
        "size_words": total,
        "exact_pct": round(100 * exact / total, 1) if total else 0.0,
        "exact_words": exact,
        "rel_only_words": rel_only,
        "semantic_ranges": [["+%s" % hex(s * WORD), "+%s" % hex((e + 1) * WORD)]
                            for s, e in semantic],
    }


def cmd_diff(args):
    img_old = Image(args.old_so)
    img_new = Image(args.new_so)
    targets = load_spec(args.spec)
    missing = [item["name"] for item in targets if item["new"] is None]
    if missing:
        raise ValueError("diff mode requires 'new' on every entry; missing on: "
                         + " ".join(missing))
    results = {}
    for item in targets:
        record = diff_pair(img_old, img_new, item)
        ranges = ", ".join("%s..%s" % (s, e)
                           for s, e in record["semantic_ranges"][:14]) or "-"
        print(f"{item['name']:34s} exact={record['exact_pct']:5.1f}% "
              f"rel-only={record['rel_only_words']:4d} sem=[{ranges}]")
        results[item["name"]] = record
    if args.out:
        pathlib.Path(args.out).write_text(json.dumps(results, indent=1))
        print(f"wrote {args.out}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--window", type=int, default=8,
                        help="anchor window size in words (default 8)")
    parser.add_argument("--step", type=int, default=16,
                        help="anchor window stride in words (default 16)")
    parser.add_argument("--max-windows", dest="max_windows", type=int, default=220,
                        help="cap anchors per target; stride grows if exceeded (default 220)")
    sub = parser.add_subparsers(dest="mode", required=True)

    for mode_name, help_text in (("locate", "find old addresses inside a new binary"),
                                 ("diff", "compare pinned old/new pairs")):
        mode_parser = sub.add_parser(mode_name, help=help_text)
        mode_parser.add_argument("old_so")
        mode_parser.add_argument("new_so")
        mode_parser.add_argument("spec", help="JSON spec file (see module docstring)")
        mode_parser.add_argument("-o", "--out", help="write JSON results here")

    args = parser.parse_args(argv)
    if args.mode == "locate":
        return cmd_locate(args)
    return cmd_diff(args)


if __name__ == "__main__":
    sys.exit(main())
