#!/usr/bin/env python3
"""Packages the built editor for a release (context/infrastructure.md, "Engine CI and releases").

    scripts/release/package.py --build 142 --commit abc1234 [--platform macos|windows|linux]
                               [--channel preview] [--bin bin] [--output dist]

macOS: a dmg of bin/PandaEditor.app with an Applications link (sign the bundle before, sign and
notarize the dmg after: sign_macos.sh, notarize_macos.sh). Windows: a zip of the bin/ layout under
PandaEditor/, with the SDK copied by cmake/sync_panda_sdk.cmake exactly as the macOS bundle embeds
it (without the SDK's own tools/: the editor brings Tools/). Linux: the same layout as a tar.gz. The archive name carries the version, ENGINE_VERSION
of Panda/include/Panda/Base/Version.hpp, and on a rolling channel the build:
PandaEditor-0.1.0-preview.142-macos-arm64.dmg.

Next to the archive the script writes <archive>.json: the os, arch and kind of the manifest entry
with the version, build and commit. publish.py assembles the channel manifest from these files and
recomputes size and sha256 from the archives it uploads (signing changes the dmg after packaging).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path
from typing import NoReturn

ROOT = Path(__file__).resolve().parents[2]
VERSION_HEADER = ROOT / "Panda" / "include" / "Panda" / "Base" / "Version.hpp"
VOLUME_NAME = "Panda Editor"
KINDS = {"macos": "dmg", "windows": "zip", "linux": "tar.gz"}
# Entries of bin/ that are not the editor: the other desktop applications of the tree and the
# documentation site; the dot files are the stamps of the build scripts, the suffixes are MSVC
# by-products of the link.
EXCLUDED_ENTRIES = {"PandaUITest", "PandaUITest.exe", "PandaSmoke", "PandaSmoke.exe", "docs"}
EXCLUDED_SUFFIXES = {".pdb", ".ilk", ".exp", ".lib", ".manifest"}


def fail(message: str) -> NoReturn:
    print(f"package: {message}", file=sys.stderr)
    sys.exit(1)


def engine_version() -> str:
    match = re.search(r'ENGINE_VERSION\s*=\s*"([^"]+)"', VERSION_HEADER.read_text(encoding="utf-8"))
    if not match:
        fail(f"ENGINE_VERSION was not found in {VERSION_HEADER}")
    return match.group(1)


def release_version(version: str, channel: str, build: int) -> str:
    # The stable channel is the plain version; a rolling channel counts its builds.
    return version if channel == "stable" else f"{version}-{channel}.{build}"


def host_platform() -> str:
    platforms = {"darwin": "macos", "win32": "windows", "linux": "linux"}
    if sys.platform not in platforms:
        fail(f"unsupported platform {sys.platform}")
    return platforms[sys.platform]


def host_arch() -> str:
    machine = platform.machine().lower()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    if machine in ("x86_64", "amd64"):
        return "x86_64"
    fail(f"unsupported architecture {machine}")


def git_short_commit() -> str:
    try:
        result = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short=7", "HEAD"],
                                check=True, capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError):
        return ""
    return result.stdout.strip()


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def embed_sdk(source: Path, destination: Path) -> None:
    """The SDK next to the editor. cmake/sync_panda_sdk.cmake owns what of the package goes there; it
    also embeds the SDK into the macOS bundle, so the three platforms carry the same SDK."""
    cmake = shutil.which("cmake")
    if not cmake:
        fail("cmake was not found in PATH")
    subprocess.run([cmake, f"-DPANDA_SDK_SOURCE={source.as_posix()}",
                    f"-DPANDA_SDK_DESTINATION={destination.as_posix()}",
                    "-P", (ROOT / "cmake" / "sync_panda_sdk.cmake").as_posix()], check=True)


def stage_layout(bin_dir: Path, staging: Path, executable: str) -> None:
    """Copies the editor layout of bin/ (the executable, its resources, Tools/) and the SDK into staging."""
    if not (bin_dir / executable).is_file():
        fail(f"{executable} was not found in {bin_dir}; build the PandaEditor target first")
    if not (bin_dir / "PandaSDK" / "lib").is_dir():
        fail(f"PandaSDK is not packaged in {bin_dir}; run scripts/build_panda_sdk_release first")
    if not (bin_dir / "Tools").is_dir():
        fail(f"Tools/ (the bundled cmake and slangc) is missing in {bin_dir}; fetch the shader tools and build PandaEditor")
    for entry in sorted(bin_dir.iterdir()):
        if entry.name.startswith(".") or entry.name in EXCLUDED_ENTRIES or entry.name == "PandaSDK":
            continue
        if entry.is_dir():
            shutil.copytree(entry, staging / entry.name, symlinks=True)
        elif entry.suffix.lower() not in EXCLUDED_SUFFIXES:
            shutil.copy2(entry, staging / entry.name)
    embed_sdk(bin_dir / "PandaSDK", staging / "PandaSDK")


def make_zip(staging: Path, archive: Path) -> None:
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zip_file:
        for path in sorted(staging.rglob("*")):
            name = (Path("PandaEditor") / path.relative_to(staging)).as_posix()
            zip_file.write(path, name + "/" if path.is_dir() else name)


def make_tar_gz(staging: Path, archive: Path) -> None:
    # tarfile keeps the permissions, so the executable stays executable.
    with tarfile.open(archive, "w:gz", compresslevel=6) as tar:
        tar.add(staging, arcname="PandaEditor")


def make_dmg(app: Path, archive: Path) -> None:
    if not (app / "Contents" / "MacOS" / "PandaEditor").is_file():
        fail(f"{app} is not the editor bundle; build the PandaEditor target first")
    if not (app / "Contents" / "Resources" / "PandaSDK" / "lib").is_dir():
        fail("PandaSDK is not embedded in the bundle; run scripts/build_panda_sdk_release.sh first")
    if not (app / "Contents" / "Resources" / "Tools").is_dir():
        fail("Tools/ (the bundled cmake and slangc) is missing in the bundle; fetch the shader tools and build PandaEditor")
    with tempfile.TemporaryDirectory() as temp:
        staging = Path(temp) / "dmg"
        staging.mkdir()
        # ditto keeps the bundle exactly as signed (permissions, symlinks, extended attributes).
        subprocess.run(["ditto", str(app), str(staging / app.name)], check=True)
        os.symlink("/Applications", staging / "Applications")
        command = ["hdiutil", "create", "-volname", VOLUME_NAME, "-srcfolder", str(staging), "-ov",
                   "-format", "UDZO", "-fs", "HFS+", str(archive)]
        # hdiutil occasionally fails with "Resource busy"; a retry is the usual cure.
        for attempt in range(3):
            if attempt > 0:
                time.sleep(5)
            if subprocess.run(command).returncode == 0:
                return
        fail("hdiutil create failed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--platform", choices=sorted(KINDS), default=host_platform(),
                        help="the platform of the archive; the host by default")
    parser.add_argument("--channel", default="preview", help="the release channel (default: preview)")
    parser.add_argument("--build", type=int, required=True,
                        help="the build number: the run number of the release workflow")
    parser.add_argument("--commit", default=None, help="the short commit of the build; HEAD by default")
    parser.add_argument("--bin", type=Path, default=ROOT / "bin", help="the output directory of the build")
    parser.add_argument("--output", type=Path, default=ROOT / "dist", help="where the archive goes")
    args = parser.parse_args()

    if args.platform != host_platform():
        fail(f"the {args.platform} archive is packaged on {args.platform}")
    version = release_version(engine_version(), args.channel, args.build)
    commit = args.commit if args.commit is not None else git_short_commit()
    arch = host_arch()
    kind = KINDS[args.platform]
    name = f"PandaEditor-{version}-{args.platform}-{arch}.{kind}"
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / name
    if archive.exists():
        archive.unlink()

    if args.platform == "macos":
        make_dmg(args.bin / "PandaEditor.app", archive)
    else:
        with tempfile.TemporaryDirectory() as temp:
            staging = Path(temp) / "PandaEditor"
            staging.mkdir()
            stage_layout(args.bin, staging, "PandaEditor.exe" if args.platform == "windows" else "PandaEditor")
            if args.platform == "windows":
                make_zip(staging, archive)
            else:
                make_tar_gz(staging, archive)

    entry = {
        "os": args.platform,
        "arch": arch,
        "kind": kind,
        "fileName": name,
        "version": version,
        "build": args.build,
        "commit": commit,
        "size": archive.stat().st_size,
        "sha256": sha256_of(archive),
    }
    (args.output / f"{name}.json").write_text(json.dumps(entry, indent=2) + "\n", encoding="utf-8")
    print(f"{archive} ({entry['size']} bytes, sha256 {entry['sha256']})")


if __name__ == "__main__":
    main()
