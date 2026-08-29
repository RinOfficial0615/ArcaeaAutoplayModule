from __future__ import annotations

import pathlib
import shutil
import json
import subprocess
import zipfile
import sys

# Import the sibling helper by path so this runs both as
# `python tests/run_host_tests.py` and `python -m tests.run_host_tests`.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import toolchain_probe

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
BUILD = ROOT / "build" / "host-tests"
MAGIC_ENUM_INCLUDE = ROOT / "third_party" / "magic_enum" / "include"
BUILD.mkdir(parents=True, exist_ok=True)


CXX = toolchain_probe.find_ndk_clang()
print(f"using NDK clang++: {CXX}")


# ---------------------------------------------------------------------------
# Host toolchain: NDK clang++ driving the MSVC STL. See toolchain_probe.py for
# how the pieces are located and why the MSVC STL rather than the NDK's own
# MinGW libstdc++.
#
# The Android build keeps using the vendored third_party/libcxx, so src/ has
# to stay inside the intersection of the two. check_production_conformance()
# enforces that by compiling every first-party source against the vendored
# libcxx for arm64 -- a feature only MSVC has then fails the host run instead
# of the release build. See docs/cpp/toolchain.md.
# ---------------------------------------------------------------------------


def _remove_source_package(path: pathlib.Path) -> None:
    """Drop a fixture from the scanned charts directory."""
    path.unlink()


# MSVC's STL hard-rejects clang older than 20; NDK 27 (clang 18) fails outright.
HOST_COMPILE_ARGS, HOST_LINK_ARGS = toolchain_probe.host_toolchain_args()
print(f"using MSVC STL: {toolchain_probe.find_visual_studio()}")

RYML_SOURCES = [
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "base64.cpp",
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "error.cpp",
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "format.cpp",
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "language.cpp",
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "memory_util.cpp",
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "utf.cpp",
    ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src" / "c4" / "version.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "common.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "node_type.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "parse.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "reference_resolver.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "scalar_style.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "tag.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "tree.cpp",
    ROOT / "third_party" / "rapidyaml" / "src" / "c4" / "yml" / "version.cpp",
]
RYML_INCLUDES = [
    "-isystem",
    str(ROOT / "third_party" / "rapidyaml" / "src"),
    "-isystem",
    str(ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src"),
]


def compile_ryml_objects() -> list[pathlib.Path]:
    objects: list[pathlib.Path] = []
    for source in RYML_SOURCES:
        obj = BUILD / f"ryml-{source.stem}.o"
        subprocess.run(
            [
                CXX,
                # Compile-only: no linker args, so no .lib or -L noise on -c.
                *HOST_COMPILE_ARGS,
                "-std=c++23",
                "-O2",
                "-c",
                str(source),
                "-o",
                str(obj),
                "-DC4_NO_DEBUG_BREAK",
                "-DC4_USE_ASSERT=0",
                *RYML_INCLUDES,
            ],
            check=True,
            cwd=ROOT,
        )
        objects.append(obj)
    return objects


ryml_objects = compile_ryml_objects()

exe = BUILD / "host_tests.exe"
compile_cmd = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DARC_HELPER_HOST_TEST",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    "-I",
    str(MAGIC_ENUM_INCLUDE),
    "-I",
    str(ROOT / "third_party" / "json" / "include"),
    str(ROOT / "tests" / "host_tests.cpp"),
    str(ROOT / "src" / "manager" / "network" / "NetworkHandlerSnapshot.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartGameplaySession.cpp"),
    str(ROOT / "src" / "utils" / "Sha256.cpp"),
    str(ROOT / "src" / "utils" / "ZipArchive.cpp"),
    str(ROOT / "src" / "utils" / "Log.cpp"),
    "-o",
    str(exe),
]
subprocess.run(compile_cmd, check=True, cwd=ROOT)

network_block_exe = BUILD / "network_block_host_test.exe"
network_block_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    "-I",
    str(MAGIC_ENUM_INCLUDE),
    str(ROOT / "tests" / "network_block_host_test.cpp"),
    "-o",
    str(network_block_exe),
]
subprocess.run(network_block_compile, check=True, cwd=ROOT)
subprocess.run([str(network_block_exe)], check=True, cwd=ROOT)
print("validated ordinary block matching and custom-chart isolation policy")

config_manager_exe = BUILD / "config_manager_host_test.exe"
config_manager_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DARC_HELPER_HOST_TEST",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    "-I",
    str(MAGIC_ENUM_INCLUDE),
    "-I",
    str(ROOT),
    "-I",
    str(ROOT / "third_party" / "json" / "include"),
    str(ROOT / "tests" / "config_manager_host_test.cpp"),
    str(ROOT / "src" / "manager" / "ConfigManager.cpp"),
    str(ROOT / "src" / "utils" / "Log.cpp"),
    "-o",
    str(config_manager_exe),
]
subprocess.run(config_manager_compile, check=True, cwd=ROOT)
config_root = BUILD / "config-manager-root"
subprocess.run([str(config_manager_exe), str(config_root)], check=True, cwd=ROOT)
print("validated inferred feature config, fallback normalization, and atomic save")

logger_exe = BUILD / "logger_host_test.exe"
logger_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DARC_HELPER_HOST_TEST",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    "-I",
    str(MAGIC_ENUM_INCLUDE),
    "-I",
    str(ROOT / "third_party" / "json" / "include"),
    str(ROOT / "tests" / "logger_host_test.cpp"),
    str(ROOT / "src" / "features" / "Logging.cpp"),
    str(ROOT / "src" / "manager" / "ConfigManager.cpp"),
    str(ROOT / "src" / "utils" / "Log.cpp"),
    "-o",
    str(logger_exe),
]
subprocess.run(logger_compile, check=True, cwd=ROOT)
logger_root = BUILD / "logger-root"
subprocess.run([str(logger_exe), str(logger_root)], check=True, cwd=ROOT)
print("validated logger source format, UTF-8 truncation, full file output, and rotation")

hook_manager_exe = BUILD / "hook_manager_host_test.exe"
hook_manager_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    "-I",
    str(MAGIC_ENUM_INCLUDE),
    str(ROOT / "tests" / "hook_manager_host_test.cpp"),
    str(ROOT / "src" / "manager" / "HookManager.cpp"),
    str(ROOT / "src" / "utils" / "Log.cpp"),
    # No -ldl: dlopen is a POSIX-only concern and the Windows host runtimes
    # have no such import library; the hook manager tests use the stubs.
    "-o",
    str(hook_manager_exe),
]
subprocess.run(hook_manager_compile, check=True, cwd=ROOT)
subprocess.run([str(hook_manager_exe)], check=True, cwd=ROOT)
print("validated inline-hook register/commit rollback and destructor recovery")

memory_patch_exe = BUILD / "memory_patch_host_test.exe"
memory_patch_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    str(ROOT / "tests" / "memory_patch_host_test.cpp"),
    str(ROOT / "src" / "utils" / "memory" / "RuntimeMemory.cpp"),
    str(ROOT / "src" / "utils" / "memory" / "PatchTransaction.cpp"),
    str(ROOT / "src" / "utils" / "memory" / "ProcMaps.cpp"),
    "-o",
    str(memory_patch_exe),
]
subprocess.run(memory_patch_compile, check=True, cwd=ROOT)
subprocess.run([str(memory_patch_exe)], check=True, cwd=ROOT)
print("validated runtime memory range checks and patch transaction rollback")

proc_maps_exe = BUILD / "proc_maps_host_test.exe"
proc_maps_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    str(ROOT / "tests" / "proc_maps_host_test.cpp"),
    str(ROOT / "src" / "utils" / "memory" / "ProcMaps.cpp"),
    "-o",
    str(proc_maps_exe),
]
subprocess.run(proc_maps_compile, check=True, cwd=ROOT)
subprocess.run([str(proc_maps_exe)], check=True, cwd=ROOT)
print("validated library load-bias selection against remapped GNU_RELRO maps")

aff_normalizer_exe = BUILD / "aff_normalizer_host_test.exe"
subprocess.run(
    [
        CXX,
        *HOST_COMPILE_ARGS,
        *HOST_LINK_ARGS,
        "-std=c++23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        "-I",
        str(MAGIC_ENUM_INCLUDE),
        str(ROOT / "tests" / "aff_normalizer_host_test.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "AffNormalizer.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "AffOfficialParser.cpp"),
        "-o",
        str(aff_normalizer_exe),
    ],
    check=True,
    cwd=ROOT,
)
subprocess.run([str(aff_normalizer_exe)], check=True, cwd=ROOT)
print("validated AFF normalize: official scenecontrol, timinggroup, timing tokens")

official_parser_exe = BUILD / "aff_official_parser_host_test.exe"
subprocess.run(
    [
        CXX,
        *HOST_COMPILE_ARGS,
        *HOST_LINK_ARGS,
        "-std=c++23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        "-I",
        str(MAGIC_ENUM_INCLUDE),
        str(ROOT / "tests" / "aff_official_parser_host_test.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "AffNormalizer.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "AffOfficialParser.cpp"),
        "-o",
        str(official_parser_exe),
    ],
    check=True,
    cwd=ROOT,
)
subprocess.run([str(official_parser_exe)], check=True, cwd=ROOT)
print("validated official 6.16.2c AFF token grammar")

songlist_snapshot_exe = BUILD / "songlist_snapshot_host_test.exe"
subprocess.run(
    [
        CXX,
        *HOST_COMPILE_ARGS,
        *HOST_LINK_ARGS,
        "-std=c++23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        "-I",
        str(MAGIC_ENUM_INCLUDE),
        "-I",
        str(ROOT / "third_party" / "json" / "include"),
        str(ROOT / "tests" / "songlist_snapshot_host_test.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartSnapshot.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartAssetIndex.cpp"),
        "-o",
        str(songlist_snapshot_exe),
    ],
    check=True,
    cwd=ROOT,
)
subprocess.run([str(songlist_snapshot_exe)], check=True, cwd=ROOT)
print("validated import snapshot songs JSON and official list merge alias echo")

image_raster_exe = BUILD / "image_raster_host_test.exe"
subprocess.run(
    [
        CXX,
        *HOST_COMPILE_ARGS,
        *HOST_LINK_ARGS,
        "-std=c++23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        "-I",
        str(ROOT / "third_party"),
        str(ROOT / "tests" / "image_raster_host_test.cpp"),
        str(ROOT / "src" / "utils" / "ImageRaster.cpp"),
        "-o",
        str(image_raster_exe),
    ],
    check=True,
    cwd=ROOT,
)
subprocess.run([str(image_raster_exe)], check=True, cwd=ROOT)
print("validated background resample to 1920x1440 JPEG")

yaml_exe = BUILD / "arc_package_format_host_test.exe"
subprocess.run(
    [
        CXX,
        *HOST_COMPILE_ARGS,
        *HOST_LINK_ARGS,
        "-std=c++23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DC4_NO_DEBUG_BREAK",
        "-DC4_USE_ASSERT=0",
        "-I",
        str(ROOT / "src"),
        "-I",
        str(MAGIC_ENUM_INCLUDE),
        *RYML_INCLUDES,
        str(ROOT / "tests" / "arc_package_format_host_test.cpp"),
        str(ROOT / "src" / "manager" / "custom_chart" / "ArcPackageFormat.cpp"),
        *map(str, ryml_objects),
        "-o",
        str(yaml_exe),
    ],
    check=True,
    cwd=ROOT,
)
subprocess.run([str(yaml_exe)], check=True, cwd=ROOT)
print("validated ArcCreate YAML index/project parsing")

valid = sorted((WORKSPACE / "ArcCreate" / "arcpkg-samples").glob("*.arcpkg"))
valid += sorted((WORKSPACE / "ArcCreate" / "raw-zip-samples").glob("*.zip"))
if len(valid) < 7:
    raise SystemExit(f"expected at least 7 package fixtures, found {len(valid)}")

# The Strawberry MinGW CRT exposes narrow argv; stage fixtures under ASCII names
# so the native host test remains deterministic on Windows.
staged: list[pathlib.Path] = []
for index, source in enumerate(valid):
    target = BUILD / f"fixture-{index}{source.suffix}"
    shutil.copyfile(source, target)
    staged.append(target)

unsafe = BUILD / "zip-slip.zip"
with zipfile.ZipFile(unsafe, "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("../escape.aff", "AudioOffset:0\n-\ntiming(0,120,4);\n")
short_archive = BUILD / "short.zip"
short_archive.write_bytes(b"PK\x03\x04")

subprocess.run(
    [str(exe), *map(str, staged), "--expect-fail", str(unsafe),
     "--expect-fail", str(short_archive)],
    check=True,
    cwd=ROOT,
)
print(f"validated {len(valid)} real fixtures and two rejected invalid fixtures")

import_root = BUILD / "import-root"
if import_root.exists():
    shutil.rmtree(import_root)
charts_dir = import_root / "charts"
charts_dir.mkdir(parents=True)
(import_root / "config.json").write_text(
    '{"autoplay":false,"customCharts":false broken}', encoding="utf-8"
)
for index, source in enumerate(valid):
    shutil.copyfile(source, charts_dir / f"package-{index}{source.suffix}")

# Minimal raw ZIP with broken metadata: importer must recover from audio + AFF.
with zipfile.ZipFile(charts_dir / "minimal-broken-json.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("songlist.json", "{broken")
    archive.writestr("base.ogg", b"OggS-host-test")
    archive.writestr("2.aff", "AudioOffset:0\n-\ntiming(0,180,4);\n")

# Broken metadata with two independent folders must still import both songs.
with zipfile.ZipFile(charts_dir / "multi-broken-json.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("songlist.json", "{broken")
    for song_id, bpm in (("alpha", 150), ("beta", 200)):
        archive.writestr(f"{song_id}/base.ogg", b"OggS-host-test")
        archive.writestr(
            f"{song_id}/2.aff",
            f"AudioOffset:0\n-\ntiming(0,{bpm},4);\n",
        )

# Valid JSON with fractional and out-of-range integral fields must not reach
# narrowing conversions. The importer should retain the song with defaults.
with zipfile.ZipFile(charts_dir / "bounded-numbers.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr(
        "songlist.json",
        json.dumps({
            "songs": [{
                "id": "bounded_numeric",
                "title_localized": {"en": "Bounded Numeric"},
                "bpm_base": -5,
                "side": 1.5,
                "audioPreview": -1,
                "audioPreviewEnd": 9_223_372_036_854_775_807,
                "bg": "missing_custom_background",
                "difficulties": [{
                    "ratingClass": 2,
                    "rating": 9_223_372_036_854_775_807,
                    "ratingPlus": "true",
                }],
            }],
        }),
    )
    archive.writestr("base.ogg", b"OggS-host-test")
    archive.writestr("2.aff", "AudioOffset:0\n-\ntiming(0,120,4);\n")

with zipfile.ZipFile(charts_dir / "bounded-arc.arcpkg", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr(
        "index.yml",
        "- directory: bounded\n  identifier: bounded_arc\n"
        "  settingsFile: project.arcproj\n  type: level\n",
    )
    archive.writestr(
        "bounded/project.arcproj",
        "charts:\n"
        "- chartPath: 2.aff\n"
        "  audioPath: base.ogg\n"
        "  title: Bounded Arc\n"
        "  difficulty: Future 11+\n"
        "  charter: >-\n"
        "    Folded\n"
        "    Charter\n"
        "  baseBpm: 120junk\n"
        "  chartConstant: 9e999\n"
        "  previewStart: -5\n"
        "  previewEnd: 999999999999999999999\n"
        "  skin:\n"
        "    side: light\n",
    )
    archive.writestr("bounded/base.ogg", b"OggS-host-test")
    archive.writestr("bounded/2.aff", "AudioOffset:0\n-\ntiming(0,120,4);\n")

# A raw package may ship its metadata as `songlist` (the spelling the game uses
# for its own asset) or as `songlist.json`. On its own either one must work.
with zipfile.ZipFile(charts_dir / "songlist-plain.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr(
        "songlist",
        json.dumps({
            "songs": [{
                "id": "plain_only",
                "title_localized": {"en": "Plain Songlist"},
                "artist": "Plain Artist",
                "bpm_base": 132,
            }],
        }),
    )
    archive.writestr("base.ogg", b"OggS-host-test")
    archive.writestr("2.aff", "AudioOffset:0\n-\ntiming(0,132,4);\n")

# When both spellings are present the importer must warn and keep `songlist`,
# so the same archive can never import differently depending on entry order.
with zipfile.ZipFile(charts_dir / "songlist-conflict.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr(
        "songlist",
        json.dumps({
            "songs": [{
                "id": "conflict_winner",
                "title_localized": {"en": "Conflict Winner"},
            }],
        }),
    )
    archive.writestr(
        "songlist.json",
        json.dumps({
            "songs": [{
                "id": "conflict_loser",
                "title_localized": {"en": "Conflict Loser"},
            }],
        }),
    )
    archive.writestr("base.ogg", b"OggS-host-test")
    archive.writestr("2.aff", "AudioOffset:0\n-\ntiming(0,140,4);\n")

# Content-addressed IDs must not make a byte-identical package imported under a
# second filename invalidate the whole snapshot.
shutil.copyfile(charts_dir / "bounded-arc.arcpkg", charts_dir / "duplicate-content.arcpkg")

# The importer must reject oversized metadata before inflating it into memory.
with zipfile.ZipFile(charts_dir / "oversized-text.zip", "w", zipfile.ZIP_STORED) as archive:
    archive.writestr("songlist.json", " " * (8 * 1024 * 1024 + 1))

importer_exe = BUILD / "importer_host_test.exe"
import_compile = [
    CXX,
    *HOST_COMPILE_ARGS,
    *HOST_LINK_ARGS,
    "-std=c++23",
    "-O2",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-DARC_HELPER_HOST_TEST",
    "-I",
    str(ROOT / "tests" / "stubs"),
    "-I",
    str(ROOT / "src"),
    "-I",
    str(MAGIC_ENUM_INCLUDE),
    "-I",
    str(ROOT),
    "-I",
    str(ROOT / "third_party" / "json" / "include"),
    "-I",
    str(ROOT / "third_party"),
    *RYML_INCLUDES,
    "-DC4_NO_DEBUG_BREAK",
    "-DC4_USE_ASSERT=0",
    str(ROOT / "tests" / "importer_host_test.cpp"),
    str(ROOT / "src" / "manager" / "CustomChartManager.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartImporter.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "ArcPackageFormat.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "AffNormalizer.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartAssetIndex.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartSnapshot.cpp"),
    str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartReportWriter.cpp"),
    str(ROOT / "src" / "utils" / "Log.cpp"),
    str(ROOT / "src" / "utils" / "Sha256.cpp"),
    str(ROOT / "src" / "utils" / "ZipArchive.cpp"),
    str(ROOT / "src" / "utils" / "ImageRaster.cpp"),
    *map(str, ryml_objects),
    "-o",
    str(importer_exe),
]
subprocess.run(import_compile, check=True, cwd=ROOT)
subprocess.run([str(importer_exe), str(import_root)], check=True, cwd=ROOT)

report = json.loads((import_root / "import-report.json").read_text(encoding="utf-8"))
manifest_before_delete = json.loads((import_root / "manifest.json").read_text(encoding="utf-8"))
song_count_before_delete = manifest_before_delete["songs"]
cache_count_before_delete = len([
    path for path in (import_root / "cache").iterdir() if path.is_dir()
])
statuses = [entry["status"] for entry in report["entries"]]
assert statuses.count("LOADED") == song_count_before_delete, statuses
assert "DEFAULTED_FIELD" in statuses
assert any(entry.get("detail") == "background" for entry in report["entries"]), report
assert any(
    entry.get("detail") == "duplicate package content"
    for entry in report["entries"]
), report
assert any(
    entry.get("detail") == "text entry size limit; fallback discovery"
    for entry in report["entries"]
), report
skipped_charts = {
    entry["item"]
    for entry in report["entries"]
    if entry["status"] == "SKIPPED_CHART"
}
assert "4.aff" not in skipped_charts, skipped_charts
assert {"5.aff", "6.aff"} <= skipped_charts, skipped_charts
print("validated multi-package import, broken JSON fallback, defaults, cache, and reports")

# Removing a source package must remove its song and orphaned content-addressed cache.
_remove_source_package(charts_dir / "package-6.zip")
subprocess.run([str(importer_exe), str(import_root)], check=True, cwd=ROOT)
manifest_after_delete = json.loads((import_root / "manifest.json").read_text(encoding="utf-8"))
assert manifest_after_delete["songs"] < song_count_before_delete
cache_dirs = [path for path in (import_root / "cache").iterdir() if path.is_dir()]
assert len(cache_dirs) < cache_count_before_delete, cache_dirs
print("validated source deletion and orphan cache cleanup")


# ---------------------------------------------------------------------------
# Production conformance.
#
# Host tests build against the MSVC STL, which is a *wider* library than the
# vendored third_party/libcxx the Android build actually ships. Without this
# gate, a feature MSVC has and libcxx does not (views::enumerate, chunk,
# slide, join_with, fold_right, cartesian_product) would pass here and then
# break the release build -- the exact failure this is meant to catch.
#
# Compiling every first-party source for arm64 against the vendored libcxx is
# the cheapest check that still uses the real production flags.
# ---------------------------------------------------------------------------

# Only the hooking engines need headers outside src/ and third_party/*.
CONFORMANCE_INCLUDES = [
    "-I", str(ROOT / "third_party" / "lsplt" / "lsplt" / "src" / "main" / "jni" / "include"),
    "-I", str(ROOT / "third_party" / "shadowhook" / "shadowhook" / "src" / "main" / "cpp" / "include"),
]
VENDORED_LIBCXX_INCLUDE = ROOT / "third_party" / "libcxx" / "include"


def check_production_conformance() -> None:
    if not (VENDORED_LIBCXX_INCLUDE / "__config").is_file():
        raise SystemExit(
            f"Missing {VENDORED_LIBCXX_INCLUDE}; run 'git submodule update --init --recursive'."
        )
    sources = sorted((ROOT / "src").rglob("*.cpp"))
    if not sources:
        raise SystemExit("No first-party sources under src/ to check.")

    failures: list[str] = []
    for source in sources:
        result = subprocess.run(
            [
                CXX,
                "--target=aarch64-linux-android21",
                # Mirrors APP_CPPFLAGS in Application.mk.
                "-std=c++23",
                "-fno-exceptions",
                "-fno-rtti",
                "-fvisibility=hidden",
                "-fvisibility-inlines-hidden",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-O2",
                "-c",
                "-nostdinc++",
                "-isystem",
                str(VENDORED_LIBCXX_INCLUDE),
                "-D_LIBCPP_NO_EXCEPTIONS",
                "-D_LIBCPP_NO_RTTI",
                "-DJSON_NOEXCEPTION",
                # Same as Android.mk: rapidyaml's std_fwd.hpp sniffs the standard
                # library from these, and they are not implied by -nostdinc++.
                "-D_LIBCPP_ABI_NAMESPACE=_LIBCPP_ABI_NAMESPACE",
                "-include",
                "vector",
                "-DC4_NO_DEBUG_BREAK",
                "-DC4_USE_ASSERT=0",
                "-I",
                str(ROOT / "src"),
                "-I",
                str(MAGIC_ENUM_INCLUDE),
                "-I",
                str(ROOT / "third_party" / "json" / "include"),
                "-I",
                str(ROOT / "third_party"),
                "-isystem",
                str(ROOT / "third_party" / "rapidyaml" / "src"),
                "-isystem",
                str(ROOT / "third_party" / "rapidyaml" / "ext" / "c4core.src"),
                *CONFORMANCE_INCLUDES,
                str(source),
                "-o",
                str(BUILD / "conformance.o"),
            ],
            capture_output=True,
            text=True,
            check=False,
            cwd=ROOT,
        )
        if result.returncode != 0:
            failures.append(f"{source.relative_to(ROOT)}:\n{result.stderr.strip()[:1500]}")

    if failures:
        raise SystemExit(
            "Production conformance failed -- these sources use a feature the "
            "Android build's vendored libcxx does not have "
            "(see docs/cpp/feature-matrix.md):\n\n" + "\n\n".join(failures)
        )

    # Compiling is not linking, and std::format is the case that proves it:
    # __visit_format_arg switches on the *runtime* type of basic_format_arg, so
    # formatter<double>/formatter<float> get instantiated for every format call
    # even when no floating-point argument is ever passed. Every source above
    # therefore compiles cleanly while the release link needs libcxx's Ryu
    # implementation. This only bites if the vendored Android.mk stops building
    # it, which a per-file compile can never notice.
    if any("std::format" in s.read_text(encoding="utf-8", errors="ignore") for s in sources):
        android_mk = ROOT / "Android.mk"
        mk_text = android_mk.read_text(encoding="utf-8")
        missing = [
            name
            for name in ("libcxx_ryu", "third_party/libcxx/src/ryu/d2s.cpp")
            if name not in mk_text
        ]
        if missing:
            raise SystemExit(
                f"{android_mk.name} no longer builds libc++'s Ryu sources "
                f"(missing: {', '.join(missing)}), but src/ uses std::format. "
                "The release link will fail with undefined "
                "__d2s/__d2exp/__d2fixed/__f2s symbols. "
                "See docs/cpp/build-pitfalls.md."
            )

    print(f"validated production conformance: {len(sources)} sources, vendored libcxx")


check_production_conformance()
