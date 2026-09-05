from __future__ import annotations

import argparse
import hashlib
import pathlib
import struct
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]

# First 16 bytes at every hooked/patched profile target, plus the exact words
# at runtime patch sites. Addresses are per game build.
PROFILES = {
    "6.16.2c": {
        "so": ROOT.parent / "6.16.2c" / "libcocos2dcpp.so",
        "sha256": "ae723637c967e063a5fa0a906325acf2d030b42143613abefd26a11b9002bdcd",
        "expected": {
            0xBEE260: "e80f19fcfd7b01a9fc6f02a9fa6703a9",
            0x184FBD4: "ff4306d1ea8b00fde923126dfd7b13a9",
            0x745BC4: "ff8302d1fd7b04a9fb2b00f9fa6706a9",
            0x1361DE4: "ff0302d1fd7b04a9f85f05a9f65706a9",
            0x103A07C: "ff8302d1e81b00fdfd7b04a9fc6f05a9",
            0x14A948C: "ffc301d1e81b00fdfd7b04a9f65705a9",
            0xA193CC: "ff0302d1e81b00fdfd7b04a9f85f05a9",
            0xE59C90: "ffc300d1fd7b01a9f44f02a9fd430091",
            0x17E9698: "ff8301d1fd7b02a9f71b00f9f65704a9",
            0x778FD0: "ff0304d1fd7b0fa9fdc30391a20f39a9",
            # Songlist data loader: AAssetManager_open BL at 0x142CFAC,
            # whose return address is the exact caller 0x142CFB0.
            0x142CFAC: "79041494f40300aae00700b4e00314aa",
            # Song-list builder. It filters candidate songs by the exact current
            # difficulty and stores each result as a 16-byte (song, difficulty) pair.
            0x12638FC: "ff8303d1fd7b08a9fc6f09a9fa670aa9",
            # Final difficulty availability predicate used by song cells and selection.
            0xE162C8: "ff8301d1fd7b02a9f85f03a9f65704a9",
            # Song-level unlock mask predicate shared by song cells and play entry.
            0xD988F4: "ffc301d1fd7b04a9f65705a9f44f06a9",
            # Remote-pack predicate. 1 + Beyond (class 3) selects img/download.png.
            0x121E9FC: "fd7bbda9f65701a9f44f02a9fd030091",
            # Play launcher. Immediate Download-song dialog when avail==1 and
            # (Beyond or song+0x1C0). song+0x1C0 also switches preview to dl_*.
            0xC987A8: "fd7bbaa9fc6f01a9fa6702a9f85f03a9",
            # Chart path. Beyond/remote uses writable {id}_{n} pack, not songs/{id}/n.aff.
            0xA74680: "ff8303d1fd7b0ba9f6570ca9f44f0da9",
            # Runtime song registry lookup used to recover the parsed custom-song
            # object after the official unlock filter omits unknown server IDs.
            0xCADFA4: "fd7bbea9f30b00f9fd030091f30300aa",
            # Separate integrity/existence probe; it must not be accepted as loader.
            0xB5E240: "d43f3794600000b4d63f379420008052081740f9a9835ff81f0109eb41010054",
        },
        "patches": {
            0xBEE748: 0x11019148,
            0xBEE800: 0x11019148,
            0xBEE850: 0x11032148,
            # Songlist-only digest failure branches. Runtime patching replaces these
            # with NOP while leaving packlist and unlocks validation intact.
            0x100F814: 0x540013A1,
            0x100F830: 0x350012C0,
        },
    },
    "6.16.8c": {
        "so": ROOT.parent / "6.16.8c" / "libcocos2dcpp.so",
        "sha256": "088053152c407df7dfd9cba20bf30041470f940b317e0cf8b048ea2cddca47a3",
        "expected": {
            0xD7A8D4: "e80f19fcfd7b01a9fc6f02a9fa6703a9",
            0xD4F330: "ff4306d1ea8b00fde923126dfd7b13a9",
            0xDA6D04: "ff8302d1fd7b04a9fb2b00f9fa6706a9",
            0x979CD0: "ff0302d1fd7b04a9f85f05a9f65706a9",
            0x1401F98: "ff8302d1e81b00fdfd7b04a9fc6f05a9",
            0xB79F1C: "ffc301d1e81b00fdfd7b04a9f65705a9",
            0xB4D628: "ff0302d1e81b00fdfd7b04a9f85f05a9",
            0x11F1AC4: "ffc300d1fd7b01a9f44f02a9fd430091",
            0xFA5D40: "ff8301d1fd7b02a9f71b00f9f65704a9",
            0x750734: "ff0304d1fd7b0fa9fdc30391a20f39a9",
            # Songlist data loader: AAssetManager_open BL at 0x1709D94, whose
            # return address is the exact caller 0x1709D98.
            0x1709D94: "eb9d0894f40300aae00700b4e00314aa",
            0xD3AF00: "ff8303d1fd7b08a9fc6f09a9fa670aa9",
            0xE8792C: "ff8301d1fd7b02a9f85f03a9f65704a9",
            0x1068020: "ffc301d1fd7b04a9f65705a9f44f06a9",
            0xF64A90: "fd7bbda9f65701a9f44f02a9fd030091",
            0xA1F1A0: "fd7bbaa9fc6f01a9fa6702a9f85f03a9",
            0xDA71E8: "ff8303d1fd7b0ba9f6570ca9f44f0da9",
            0xC05E74: "fd7bbea9f30b00f9fd030091f30300aa",
        },
        "patches": {
            0xD7ADBC: 0x11019148,
            0xD7AE74: 0x11019148,
            0xD7AEC4: 0x11032148,
            0x77AE2C: 0x540013A1,
            0x77AE48: 0x350012C0,
        },
    },
    "7.0.0c": {
        "so": ROOT.parent / "7.0.0c" / "libcocos2dcpp.so",
        "sha256": "8526a8dad5110e5db3a103e3cc3a8acd33d8faa57d0a53ae6628ba4440948837",
        "expected": {
            0xF21224: "e80f19fcfd7b01a9fc6f02a9fa6703a9",
            0x109F144: "ff4306d1ea8b00fde923126dfd7b13a9",
            # 7.0 reuses the 6.16.2c-style ScoreState prologues.
            0x9D5500: "ff8302d1fd7b04a9fb2b00f9fa6706a9",
            0xE03F70: "ff0302d1fd7b04a9f85f05a9f65706a9",
            0xB804E4: "ff8302d1e81b00fdfd7b04a9fc6f05a9",
            0x7A5484: "ffc301d1e81b00fdfd7b04a9f65705a9",
            0xD0B0F4: "ff0302d1e81b00fdfd7b04a9f85f05a9",
            0x18DBD7C: "ffc300d1fd7b01a9f44f02a9fd430091",
            0x1459B6C: "ff8301d1fd7b02a9f71b00f9f65704a9",
            0xD8B4C4: "ff0304d1fd7b0fa9fdc30391a20f39a9",
            # Songlist data loader: AAssetManager_open BL at 0x105F0F4, whose
            # return address is the exact caller 0x105F0F8.
            0x105F0F4: "87522794f40300aae00700b4e00314aa",
            0xB6E964: "ff8303d1fd7b08a9fc6f09a9fa670aa9",
            0x195AE9C: "ff8301d1fd7b02a9f85f03a9f65704a9",
            0x1399B04: "ffc301d1fd7b04a9f65705a9f44f06a9",
            0x18D8DE0: "fd7bbda9f65701a9f44f02a9fd030091",
            0xA0C430: "fd7bbaa9fc6f01a9fa6702a9f85f03a9",
            0xF4D3C8: "ff8303d1fd7b0ba9f6570ca9f44f0da9",
            0x7AF94C: "fd7bbea9f30b00f9fd030091f30300aa",
            # April-Fools dynamix_conflict flag getter sub_E6F768. Only the
            # module swaps these 8 bytes for MOV W0,#1;RET while custom charts
            # are installed; consumers consult +0x110 through this one function.
            0xE6F768: "00404439c0035fd6",
        },
        "patches": {
            0xF2170C: 0x11019148,
            0xF217C4: 0x11019148,
            0xF21814: 0x11032148,
            0x9289BC: 0x540013A1,
            0x9289D8: 0x350012C0,
        },
    },
    "7.0.1c": {
        "so": ROOT.parent / "7.0.1c" / "libcocos2dcpp.so",
        "sha256": "426ce11edb840514acf0056d9ba48b597cf30f796298461b16c4e4e113e8742d",
        "expected": {
            0xFFC84C: "e80f19fcfd7b01a9fc6f02a9fa6703a9",
            0x15D28B0: "ff4306d1ea8b00fde923126dfd7b13a9",
            0x140CFF4: "ff8302d1fd7b04a9fb2b00f9fa6706a9",
            0x11306E0: "ff0302d1fd7b04a9f85f05a9f65706a9",
            0xAC4794: "ff8302d1e81b00fdfd7b04a9fc6f05a9",
            0x8372E0: "ffc301d1e81b00fdfd7b04a9f65705a9",
            0x19AE168: "ff0302d1e81b00fdfd7b04a9f85f05a9",
            0x886444: "ffc300d1fd7b01a9f44f02a9fd430091",
            0x10FC860: "ff8301d1fd7b02a9f71b00f9f65704a9",
            0x1A09024: "ff0304d1fd7b0fa9fdc30391a20f39a9",
            # Songlist data loader: AAssetManager_open BL at 0xE22204, whose
            # return address is the exact caller 0xE22208.
            0xE22204: "5f483094f40300aae00700b4e00314aa",
            0x18AEFBC: "ff8303d1fd7b08a9fc6f09a9fa670aa9",
            0x1638058: "ff8301d1fd7b02a9f85f03a9f65704a9",
            0x11A0070: "ffc301d1fd7b04a9f65705a9f44f06a9",
            0x19A7194: "fd7bbda9f65701a9f44f02a9fd030091",
            0x18233EC: "fd7bbaa9fc6f01a9fa6702a9f85f03a9",
            0x1937B3C: "ff8303d1fd7b0ba9f6570ca9f44f0da9",
            0xE4B5F8: "fd7bbea9f30b00f9fd030091f30300aa",
            # Scenecontrol getter keeps the 7.0.0c layout at play context +0x110.
            0x11A57A8: "00404439c0035fd6",
        },
        "patches": {
            0xFFCD34: 0x11019148,
            0xFFCDEC: 0x11019148,
            0xFFCE3C: 0x11032148,
            0x17BC004: 0x540013A1,
            0x17BC020: 0x350012C0,
        },
    },
    "7.0.255c": {
        "so": ROOT.parent / "7.0.255c" / "libcocos2dcpp.so",
        "sha256": "72e42cb4925655ecfef98bf2dbf92a5ac05145eb005a531c96c11b927a46e6dc",
        "expected": {
            0x127D830: "e80f19fcfd7b01a9fc6f02a9fa6703a9",
            0x14B7010: "ff4306d1ea8b00fde923126dfd7b13a9",
            0xB4DB58: "ff8302d1fd7b04a9fb2b00f9fa6706a9",
            0xE19784: "ff0302d1fd7b04a9f85f05a9f65706a9",
            # 7.0.255c rewrote ShowJudgementEffectAtNote (0x598 -> 0xAC8 bytes,
            # new prologue); identity is confirmed via the hit_pure/hit_far/
            # hit_lost + EARLY/LATE strings and both NoteEffect callers.
            0xEED33C: "ff4306d1ef3b0f6ded33106deb2b116d",
            0x12E3B28: "ffc301d1e81b00fdfd7b04a9f65705a9",
            0x13511A0: "ff0302d1e81b00fdfd7b04a9f85f05a9",
            0x16DA304: "ffc300d1fd7b01a9f44f02a9fd430091",
            0x13D6630: "ff8301d1fd7b02a9f71b00f9f65704a9",
            0xD2F7D8: "ff0304d1fd7b0fa9fdc30391a20f39a9",
            # Songlist data loader: AAssetManager_open BL at 0xD2787C, whose
            # return address is the exact caller 0xD27880.
            0xD2787C: "31d33494f40300aae00700b4e00314aa",
            0x126F07C: "ff8303d1fd7b08a9fc6f09a9fa670aa9",
            0x150560C: "ff8301d1fd7b02a9f85f03a9f65704a9",
            0x115DE64: "ffc301d1fd7b04a9f65705a9f44f06a9",
            0x13360A4: "fd7bbda9f65701a9f44f02a9fd030091",
            0xD2A368: "fd7bbaa9fc6f01a9fa6702a9f85f03a9",
            0x903168: "ff8303d1fd7b0ba9f6570ca9f44f0da9",
            0x19E8F54: "fd7bbea9f30b00f9fd030091f30300aa",
            # Scenecontrol getter keeps the 7.0.0c layout at play context +0x110.
            0x17B0140: "00404439c0035fd6",
        },
        "patches": {
            0x127DD18: 0x11019148,
            0x127DDD0: 0x11019148,
            0x127DE20: 0x11032148,
            0x13DB3FC: 0x540013A1,
            0x13DB418: 0x350012C0,
        },
    },
}


def elf_dynamic_symbols(elf: bytes) -> dict[str, tuple[int, int, int]]:
    """Return dynsym name -> (value, size, binding/type) for ELF64 LE files."""
    assert elf[:4] == b"\x7fELF" and elf[4] == 2 and elf[5] == 1, "expected ELF64 little-endian"
    section_header_offset = struct.unpack_from("<Q", elf, 0x28)[0]
    section_header_size = struct.unpack_from("<H", elf, 0x3A)[0]
    section_count = struct.unpack_from("<H", elf, 0x3C)[0]
    section_name_index = struct.unpack_from("<H", elf, 0x3E)[0]
    section_headers = [
        struct.unpack_from("<IIQQQQIIQQ", elf, section_header_offset + index * section_header_size)
        for index in range(section_count)
    ]
    name_header = section_headers[section_name_index]
    section_names = elf[name_header[4] : name_header[4] + name_header[5]]

    def section_name(header: tuple[int, ...]) -> bytes:
        start = header[0]
        return section_names[start : section_names.find(b"\0", start)]

    dynsym = next(header for header in section_headers if section_name(header) == b".dynsym")
    dynstr = next(header for header in section_headers if section_name(header) == b".dynstr")
    strings = elf[dynstr[4] : dynstr[4] + dynstr[5]]
    entry_size = dynsym[9] or 24
    symbols = {}
    for offset in range(dynsym[4], dynsym[4] + dynsym[5], entry_size):
        st_name, st_info, _st_other, st_shndx, st_value, st_size = struct.unpack_from(
            "<IBBHQQ", elf, offset
        )
        end = strings.find(b"\0", st_name)
        if st_name and end >= 0:
            symbols[strings[st_name:end].decode("utf-8")] = (st_value, st_size, st_info)
    return symbols


def verify_profile(version: str, spec: dict) -> None:
    so: pathlib.Path = spec["so"]
    data = so.read_bytes()
    actual_sha256 = hashlib.sha256(data).hexdigest()
    assert actual_sha256 == spec["sha256"], (version, actual_sha256, spec["sha256"])
    symbols = elf_dynamic_symbols(data)
    setter_symbol = symbols.get("Java_low_moe_AppActivity_setAppVersion")
    assert setter_symbol is not None, f"{version}: setAppVersion ELF dynamic symbol missing"
    assert setter_symbol[2] & 0x0F == 2 and setter_symbol[2] >> 4 == 1, setter_symbol
    assert setter_symbol[1] > 0, setter_symbol
    cxa_throw_symbol = symbols.get("__cxa_throw")
    assert cxa_throw_symbol is not None, f"{version}: __cxa_throw ELF dynamic symbol missing"
    assert cxa_throw_symbol[2] & 0x0F == 2 and cxa_throw_symbol[2] >> 4 == 1, cxa_throw_symbol
    assert cxa_throw_symbol[1] > 0, cxa_throw_symbol
    cxa_throw_signature = bytes.fromhex("3f2303d5fd7bbca9f70b00f9f65702a9")
    assert data[
        cxa_throw_symbol[0] : cxa_throw_symbol[0] + len(cxa_throw_signature)
    ] == cxa_throw_signature
    for offset, hex_bytes in spec["expected"].items():
        value = bytes.fromhex(hex_bytes)
        assert data[offset : offset + len(value)] == value, (version, hex(offset))
    for offset, instruction in spec["patches"].items():
        assert int.from_bytes(data[offset : offset + 4], "little") == instruction, (
            version,
            hex(offset),
        )

    print(f"[{version}] verified profile against {so.name} sha256={actual_sha256}")
    print(
        f"[{version}] verified setAppVersion dynsym "
        f"value=0x{setter_symbol[0]:x} size={setter_symbol[1]}"
    )
    print(
        f"[{version}] verified __cxa_throw dynsym "
        f"value=0x{cxa_throw_symbol[0]:x} size={cxa_throw_symbol[1]}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description="Verify pinned ArcHelper game profiles")
    parser.add_argument(
        "versions",
        nargs="*",
        choices=tuple(PROFILES),
        help="optional profile versions; omit to verify every available binary",
    )
    args = parser.parse_args()
    selected = args.versions or list(PROFILES)

    for version in selected:
        spec = PROFILES[version]
        if spec["so"].is_file():
            verify_profile(version, spec)
        else:
            print(f"[{version}] skipped, {spec['so'].name} not present")

    if args.versions and "6.16.2c" not in selected:
        return

    apk = ROOT / ".tmp" / "arcaea_6.16.2c_arc_helper_diag.apk"
    if apk.is_file():
        with zipfile.ZipFile(apk) as archive:
            provider = archive.read("lib/arm64-v8a/libfmodProvider.so")
        provider_sha256 = hashlib.sha256(provider).hexdigest()
        assert provider_sha256 == "1f3907b13d3ce3ef6b3d25a92edc971d5763d0b3220a9a99597424481d06a291"
        provider_symbols = elf_dynamic_symbols(provider)
        fmod_symbol = provider_symbols.get("_ZN24AudioProviderFMODAndroid7loadBGMEPKci")
        assert fmod_symbol is not None, "FMOD loadBGM ELF dynamic symbol missing"
        assert fmod_symbol[2] & 0x0F == 2 and fmod_symbol[2] >> 4 == 1, fmod_symbol
        assert fmod_symbol[1] > 0, fmod_symbol
        print(f"verified FMOD provider from {apk.name} sha256={provider_sha256}")
        print(f"verified FMOD loadBGM dynsym value=0x{fmod_symbol[0]:x} size={fmod_symbol[1]}")


if __name__ == "__main__":
    main()
