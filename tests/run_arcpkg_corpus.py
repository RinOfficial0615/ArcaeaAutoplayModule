"""Import .tmp/arcpkg (or a given directory) through ArcHelper and check official AFF grammar."""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile

# Import the sibling helper by path so this runs both as
# `python tests/run_arcpkg_corpus.py` and `python -m tests.run_arcpkg_corpus`.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import toolchain_probe

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "host-tests"
MAGIC_ENUM_INCLUDE = ROOT / "third_party" / "magic_enum" / "include"
DEFAULT_PACKAGES = ROOT / ".tmp" / "arcpkg"

# The Infinity APK is not redistributable, so it is never committed and no
# default path could work on another machine. Point ARC_OFFICIAL_APK at a local
# copy; without it the official AFF grammar pass is reported as skipped rather
# than failed.
_official_apk = os.environ.get("ARC_OFFICIAL_APK")
OFFICIAL_APK: pathlib.Path | None = (
    toolchain_probe.from_env_path(_official_apk) if _official_apk else None
)
BUILD.mkdir(parents=True, exist_ok=True)


CXX = toolchain_probe.find_ndk_clang()
# Same host toolchain as run_host_tests.py: NDK clang++ driving the MSVC STL.
# Targetting MinGW instead would need GCC's libgcc_eh/libgcc_s, which no NDK
# ships -- the link fails before this script gets to do anything useful.
HOST_COMPILE_ARGS, HOST_LINK_ARGS = toolchain_probe.host_toolchain_args()
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
        if not obj.is_file() or obj.stat().st_mtime < source.stat().st_mtime:
            subprocess.run(
                [
                    CXX,
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


def compile_exe(name: str, sources: list[str], extra: list[str] | None = None) -> pathlib.Path:
    exe = BUILD / name
    cmd = [
        CXX,
        *HOST_COMPILE_ARGS,
        *HOST_LINK_ARGS,
        "-std=c++23",
        "-O2",
        *(extra or []),
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "src"),
        "-I",
        str(ROOT / "tests" / "stubs"),
        "-I",
        str(MAGIC_ENUM_INCLUDE),
        *sources,
        "-o",
        str(exe),
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    return exe


def compile_official_check() -> pathlib.Path:
    return compile_exe(
        "aff_official_parser_host_test.exe",
        [
            str(ROOT / "tests" / "aff_official_parser_host_test.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "AffNormalizer.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "AffOfficialParser.cpp"),
        ],
    )


def compile_file_checker() -> pathlib.Path:
    src = BUILD / "aff_file_check.cpp"
    src.write_text(
        "#include <fstream>\n"
        "#include <iostream>\n"
        "#include <string>\n"
        "#include \"manager/custom_chart/AffOfficialParser.hpp\"\n"
        "int main(int argc, char **argv) {\n"
        "    if (argc != 2) return 2;\n"
        "    std::ifstream in(argv[1], std::ios::binary);\n"
        "    const std::string text((std::istreambuf_iterator<char>(in)), {});\n"
        "    const auto check = arc_helper::aff::CheckOfficial(text);\n"
        "    if (!check.ok) {\n"
        "        std::cerr << check.line << \" \" << check.error << \"\\n\";\n"
        "        return 1;\n"
        "    }\n"
        "    return 0;\n"
        "}\n",
        encoding="utf-8",
    )
    return compile_exe(
        "aff_file_check.exe",
        [
            str(src),
            str(ROOT / "src" / "manager" / "custom_chart" / "AffOfficialParser.cpp"),
        ],
    )


def compile_corpus(ryml_objects: list[pathlib.Path]) -> pathlib.Path:
    exe = BUILD / "arcpkg_corpus_host.exe"
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
            "-DARC_HELPER_HOST_TEST",
            "-DC4_NO_DEBUG_BREAK",
            "-DC4_USE_ASSERT=0",
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
            str(ROOT / "tests" / "arcpkg_corpus_host.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartImporter.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "ArcPackageFormat.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "AffNormalizer.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "AffOfficialParser.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartAssetIndex.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartSnapshot.cpp"),
            str(ROOT / "src" / "manager" / "custom_chart" / "CustomChartReportWriter.cpp"),
            str(ROOT / "src" / "utils" / "Log.cpp"),
            str(ROOT / "src" / "utils" / "Sha256.cpp"),
            str(ROOT / "src" / "utils" / "ZipArchive.cpp"),
            str(ROOT / "src" / "utils" / "ImageRaster.cpp"),
            *map(str, ryml_objects),
            "-o",
            str(exe),
        ],
        check=True,
        cwd=ROOT,
    )
    return exe


def stage_packages(source: pathlib.Path, dest: pathlib.Path) -> list[dict]:
    dest.mkdir(parents=True, exist_ok=True)
    staged = []
    packages = sorted(source.glob("*.arcpkg")) + sorted(source.glob("*.zip"))
    for index, path in enumerate(packages):
        target = dest / f"pkg-{index:03d}{path.suffix.lower()}"
        shutil.copyfile(path, target)
        staged.append({"index": index, "source": path.name, "staged": target.name})
    return staged


def check_official_files(checker: pathlib.Path) -> dict:
    if OFFICIAL_APK is None or not OFFICIAL_APK.is_file():
        return {"skipped": True, "reason": "set ARC_OFFICIAL_APK to a local Infinity APK"}
    tmp = BUILD / "official-aff"
    if tmp.exists():
        shutil.rmtree(tmp)
    tmp.mkdir(parents=True)
    ok = 0
    fail = []
    with zipfile.ZipFile(OFFICIAL_APK) as archive:
        names = [name for name in archive.namelist() if name.lower().endswith(".aff")]
        for index, name in enumerate(names):
            out = tmp / f"{index}.aff"
            out.write_bytes(archive.read(name))
            result = subprocess.run(
                [str(checker), str(out)],
                cwd=ROOT,
                capture_output=True,
                text=True,
            )
            if result.returncode == 0:
                ok += 1
            else:
                fail.append({
                    "file": name,
                    "error": (result.stderr or result.stdout).strip(),
                })
    return {"aff_count": len(names), "ok": ok, "fail": fail}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "packages",
        nargs="?",
        default=str(DEFAULT_PACKAGES),
        help="directory of .arcpkg / .zip files",
    )
    parser.add_argument("--skip-official", action="store_true")
    args = parser.parse_args()
    source = pathlib.Path(args.packages)
    if not source.is_dir():
        raise SystemExit(f"package directory missing: {source}")

    print(f"using NDK clang++: {CXX}")
    ryml_objects = compile_ryml_objects()
    subprocess.run([str(compile_official_check())], check=True, cwd=ROOT)
    print("validated official AFF parser host tests")

    import_root = BUILD / "arcpkg-corpus"
    if import_root.exists():
        shutil.rmtree(import_root)
    mapping = stage_packages(source, import_root / "charts")
    print(f"staged {len(mapping)} packages from {source}")
    if not mapping:
        raise SystemExit("no .arcpkg/.zip packages found")

    corpus_exe = compile_corpus(ryml_objects)
    subprocess.run([str(corpus_exe), str(import_root)], check=True, cwd=ROOT)
    report = json.loads((import_root / "corpus-report.json").read_text(encoding="utf-8"))
    report["mapping"] = mapping

    if not args.skip_official:
        official = check_official_files(compile_file_checker())
        report["official_infinity"] = official
        if official.get("skipped"):
            print(f"official Infinity skipped: {official['reason']}")
        else:
            print(
                "official Infinity",
                official.get("ok"),
                "/",
                official.get("aff_count"),
                "fail",
                len(official.get("fail") or []),
            )
        if official.get("fail"):
            for item in official["fail"][:12]:
                print("  official fail", item)

    (import_root / "corpus-report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )
    print(f"wrote {import_root / 'corpus-report.json'}")
    if report.get("aff_fail"):
        return 1
    if report.get("official_infinity", {}).get("fail"):
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
