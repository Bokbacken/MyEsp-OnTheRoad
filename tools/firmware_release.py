#!/usr/bin/env python3
"""Fail-closed ES3C35P release packaging and explicitly dispatched publication."""
import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BOARD = "ES3C35P"
TARGET = "esp32s3"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def version(root=ROOT):
    values = re.findall(r'^\s*set\(PROJECT_VER\s+"([^"]+)"\)\s*$',
                        (root / "CMakeLists.txt").read_text(), re.MULTILINE)
    require(len(values) == 1 and re.fullmatch(r'(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)',
                                            values[0], re.ASCII),
            "Expected exactly one PROJECT_VER in canonical MAJOR.MINOR.PATCH format")
    return values[0]


def dispatch(root=ROOT):
    require(os.environ.get("GITHUB_EVENT_NAME") == "workflow_dispatch"
            and os.environ.get("GITHUB_REF") == "refs/heads/main",
            "Release is only allowed via workflow_dispatch on main")
    sha = os.environ.get("EXPECTED_SHA", "")
    require(re.fullmatch(r'[0-9a-f]{40}', sha) is not None
            and sha == os.environ.get("GITHUB_SHA"), "Expected SHA must equal dispatch main SHA")
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    require(head == sha, "Checkout does not match the selected main SHA")
    return sha


def prefix(ver):
    return f"MyEsp-OnTheRoad-{TARGET}-{BOARD}-v{ver}"


def nonempty(path):
    require(path.is_file() and not path.is_symlink() and path.stat().st_size > 0,
            f"Missing or empty required file: {path}")
    return path.read_bytes()


def flash_file(build, name):
    path = (build / name).resolve()
    require(path.is_relative_to(build.resolve()), "Flash file escapes build directory")
    nonempty(path)
    return path


def ota_slots(text):
    slots = {}
    for row in csv.reader(io.StringIO(text)):
        if not row or row[0].lstrip().startswith("#"):
            continue
        row = [value.strip() for value in row]
        if len(row) >= 5 and row[1] == "app" and row[2] in ("ota_0", "ota_1"):
            size = row[4]
            scale = {"K": 1024, "M": 1024 * 1024}
            slots[row[2]] = int(size[:-1], 0) * scale[size[-1]] if size[-1] in scale else int(size, 0)
    require(set(slots) == {"ota_0", "ota_1"} and min(slots.values()) > 0,
            "Generated partition table must contain both OTA slots")
    return slots


def package(build, output, root=ROOT):
    ver = version(root)
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    desc = json.loads(nonempty(build / "project_description.json"))
    require(desc["target"] == TARGET and desc["project_version"] == ver,
            "Build target/version does not match source")
    config = json.loads(nonempty(build / "config/sdkconfig.json"))
    require(config.get("IDF_TARGET") == TARGET and config.get("OTR_HW_PROFILE_ES3C35P") is True
            and not config.get("OTR_HW_PROFILE_P4_43"), "Only the real ES3C35P profile may be released")
    require(not config.get("SECURE_BOOT") and not config.get("SECURE_FLASH_ENC_ENABLED"),
            "Secure boot/encrypted flash needs a separate release procedure")
    app = nonempty(build / "MyEsp_OnTheRoad.bin")
    require(len(app) >= 80 and app[0] == 0xE9 and struct.unpack_from("<H", app, 12)[0] == 9
            and struct.unpack_from("<I", app, 32)[0] == 0xABCD5432
            and app[48:80].split(b"\0", 1)[0].decode("ascii") == ver,
            "App image target/version header is not the expected ESP32-S3 firmware")
    partitions = nonempty(build / "release-partitions.csv")
    slots = ota_slots(partitions.decode())
    require(len(app) <= min(slots.values()), "App exceeds the smallest generated OTA partition")
    flasher = json.loads(nonempty(build / "flasher_args.json"))
    files = flasher["flash_files"]
    require(isinstance(files, dict) and len(files) >= 3, "Incomplete flash metadata")
    require({"MyEsp_OnTheRoad.bin", "bootloader/bootloader.bin",
             "partition_table/partition-table.bin"}.issubset(files.values()),
            "Flash metadata lacks app, bootloader or partition table")
    merged = nonempty(build / "merged.bin")
    for offset, name in files.items():
        data = nonempty(flash_file(build, name))
        start = int(offset, 0)
        require(start >= 0 and merged[start:start + len(data)] == data,
                f"Merged image does not contain {name} at {offset}")
    require(not output.exists(), "Output directory already exists; use a clean build directory")
    output.mkdir(parents=True)
    stem = prefix(ver)
    (output / f"{stem}-ota.bin").write_bytes(app)
    (output / f"{stem}-initial-flash-0x0.bin").write_bytes(merged)
    with zipfile.ZipFile(output / f"{stem}-flash-metadata.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(build / "flasher_args.json", "flasher_args.json")
        archive.writestr("partitions.csv", partitions)
        archive.write(root / "partitions.csv", "source-partitions.csv")
        archive.write(root / "README.md", "FLASH-INSTRUCTIONS.md")
        for name in sorted(set(files.values())):
            archive.write(flash_file(build, name), name)
    sd_files = sorted((root / "sdcard_bundle").rglob("*"))
    require(any(path.is_file() for path in sd_files), "SD bundle is empty")
    with zipfile.ZipFile(output / f"{stem}-sdcard.zip", "w", zipfile.ZIP_DEFLATED) as archive:
        for path in sd_files:
            require(not path.is_symlink(), "SD bundle must not contain symlinks")
            if path.is_file():
                archive.write(path, str(path.relative_to(root / "sdcard_bundle")))
    manifest = {"version": ver, "sha": sha, "board": BOARD, "target": TARGET,
                "ota_bytes": len(app), "ota_partition_bytes": slots,
                "initial_flash_offset": "0x0", "flash_settings": flasher["flash_settings"],
                "flash_files": files}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        for path in sorted(output.iterdir())))


def verify_assets(directory, ver, sha):
    stem = prefix(ver)
    expected = {f"{stem}-ota.bin", f"{stem}-initial-flash-0x0.bin",
                f"{stem}-flash-metadata.zip", f"{stem}-sdcard.zip", "manifest.json"}
    require({path.name for path in directory.iterdir()} == expected | {"SHA256SUMS"},
            "Release assets are missing or unexpected")
    checksums = {}
    for line in nonempty(directory / "SHA256SUMS").decode().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_.-]+)', line)
        require(match is not None and match[2] not in checksums, "Invalid checksum manifest")
        checksums[match[2]] = match[1]
    require(set(checksums) == expected, "Checksum manifest does not cover exactly the release files")
    for name, digest in checksums.items():
        require(hashlib.sha256(nonempty(directory / name)).hexdigest() == digest,
                f"Checksum mismatch: {name}")
    manifest = json.loads(nonempty(directory / "manifest.json"))
    require(all(manifest.get(key) == value for key, value in
                {"version": ver, "sha": sha, "board": BOARD, "target": TARGET}.items()),
            "Release manifest does not match main SHA/version/board")
    return sorted(directory.iterdir())


def gh_api(endpoint, *args):
    return json.loads(subprocess.check_output(["gh", "api", endpoint, *args], text=True))


def publish(directory, root=ROOT):
    sha = dispatch(root)
    ver = version(root)
    assets = verify_assets(directory, ver, sha)
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    require(re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo) is not None,
            "Invalid repository")
    api = f"repos/{repo}"
    tag = f"v{ver}"
    # Listing refs also distinguishes a missing tag from authentication/API failures.
    refs = gh_api(f"{api}/git/matching-refs/tags/{tag}")
    exact = [ref for ref in refs if ref["ref"] == f"refs/tags/{tag}"]
    if exact:
        obj = exact[0]["object"]
        for _ in range(10):
            if obj["type"] != "tag":
                break
            obj = gh_api(f"{api}/git/tags/{obj['sha']}")["object"]
        require(obj["type"] == "commit" and obj["sha"] == sha,
                f"{tag} already points to another SHA; never move it. See README.md#firmware-releases")
    pages = gh_api(f"{api}/releases?per_page=100", "--paginate", "--slurp")
    require(not any(release["tag_name"] == tag for page in pages for release in page),
            f"Release {tag} already exists (draft or published); no overwrite. "
            "See recovery instructions in README.md#firmware-releases")
    notes = gh_api(f"{api}/releases/generate-notes", "--method", "POST",
                   "-f", f"tag_name={tag}", "-f", f"target_commitish={sha}")["body"]
    notes += (f"\n\n## Firmware\nSource: `{sha}`; target: `{TARGET}`; board: `{BOARD}`.\n"
              "Built successfully, but compilation is NOT proof of physical hardware verification. "
              "Maintainer: record actual device tests before publishing this draft.\n\n"
              "Use only `*-ota.bin` for web OTA. `*-initial-flash-0x0.bin` is for USB at 0x0; "
              "back up internal storage first. Verify SHA256SUMS. See the flash-metadata ZIP and "
              f"[release instructions](https://github.com/{repo}/blob/{sha}/README.md#firmware-releases).\n"
              "SD ZIP contents go at the card root; do not overwrite saves. "
              "ELF/map are debug CI artifacts, not release assets. No P4 firmware is included.\n")
    if not exact:
        gh_api(f"{api}/git/refs", "--method", "POST",
               "-f", f"ref=refs/tags/{tag}", "-f", f"sha={sha}")
    with tempfile.TemporaryDirectory(prefix="firmware-release-") as tmp:
        notes_path = Path(tmp) / "notes.txt"
        notes_path.write_text(notes)
        subprocess.run(["gh", "release", "create", tag, "--repo", repo, "--verify-tag",
                        "--draft", "--title", f"{tag} — {BOARD}", "--notes-file", str(notes_path)],
                       check=True)
    subprocess.run(["gh", "release", "upload", tag, "--repo", repo,
                    *map(str, assets)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("prepare")
    pack = commands.add_parser("package")
    pack.add_argument("--build-dir", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    pub = commands.add_parser("publish")
    pub.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            dispatch()
            with open(os.environ["GITHUB_OUTPUT"], "a") as output:
                output.write(f"version={version()}\n")
        elif args.command == "package":
            package(args.build_dir, args.output)
        else:
            publish(args.directory)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release aborted: {error}\n")


if __name__ == "__main__":
    main()
