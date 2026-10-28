#!/usr/bin/env python3
"""Release regression tests: synthetic IDF output, mocked GitHub, no network/writes."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("firmware_release", ROOT / "tools/firmware_release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
SHA = "a" * 40


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="firmware-release-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.build = self.root / "build"
        self.output = self.root / "release"
        self.write("CMakeLists.txt", 'set(PROJECT_VER "1.2.3")\n')
        self.write("README.md", "Flash instructions\n")
        self.write("partitions.csv", "# Source layout\n")
        self.write("sdcard_bundle/otr/adventure/demo.adv", "Demo\n")
        self.write("build/project_description.json", json.dumps({
            "target": "esp32s3", "project_version": "1.2.3"}))
        self.config = {"IDF_TARGET": "esp32s3", "OTR_HW_PROFILE_ES3C35P": True}
        self.save_config()
        self.write("build/release-partitions.csv",
                   "# Generated\napp0,app,ota_0,0x10000,0x500000,\n"
                   "app1,app,ota_1,0x510000,5M,\n")
        app = bytearray(256)
        app[0] = 0xE9
        struct.pack_into("<H", app, 12, 9)
        struct.pack_into("<I", app, 32, 0xABCD5432)
        app[48:53] = b"1.2.3"
        self.write("build/MyEsp_OnTheRoad.bin", app)
        self.write("build/bootloader/bootloader.bin", b"bootloader")
        self.write("build/partition_table/partition-table.bin", b"partitions")
        self.write("build/ota_data_initial.bin", b"otadata")
        self.files = {"0x0": "bootloader/bootloader.bin",
                      "0x8000": "partition_table/partition-table.bin",
                      "0xe000": "ota_data_initial.bin", "0x10000": "MyEsp_OnTheRoad.bin"}
        self.write("build/flasher_args.json", json.dumps({
            "flash_files": self.files,
            "flash_settings": {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"}}))
        merged = bytearray(b"\xff" * (0x10000 + len(app)))
        for offset, name in self.files.items():
            data = (self.build / name).read_bytes()
            start = int(offset, 0)
            merged[start:start + len(data)] = data
        self.write("build/merged.bin", merged)
        self.env = {"GITHUB_EVENT_NAME": "workflow_dispatch", "GITHUB_REF": "refs/heads/main",
                    "EXPECTED_SHA": SHA, "GITHUB_SHA": SHA, "GITHUB_REPOSITORY": "owner/repo"}

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data.encode() if isinstance(data, str) else data)

    def save_config(self):
        self.write("build/config/sdkconfig.json", json.dumps(self.config))

    def package(self):
        with patch.object(release.subprocess, "check_output", return_value=SHA + "\n"):
            release.package(self.build, self.output, self.root)

    def publish(self, refs=None, releases=None, annotated_sha=SHA):
        def api(endpoint, *args):
            if "/matching-refs/" in endpoint:
                return refs or []
            if "/git/tags/" in endpoint:
                return {"object": {"type": "commit", "sha": annotated_sha}}
            if endpoint.endswith("/releases?per_page=100"):
                return [releases or []]
            if endpoint.endswith("/generate-notes"):
                return {"body": "Actual changes from GitHub"}
            if endpoint.endswith("/git/refs"):
                return {}
            self.fail(f"Unexpected API endpoint: {endpoint}")

        with patch.dict(os.environ, self.env, clear=True), \
                patch.object(release.subprocess, "check_output", return_value=SHA + "\n"), \
                patch.object(release, "gh_api", side_effect=api) as api_mock, \
                patch.object(release.subprocess, "run") as run_mock:
            self.api_mock = api_mock
            self.run_mock = run_mock
            release.publish(self.output, self.root)
        return api_mock, run_mock

    def test_version_rejects_noncanonical_or_ambiguous_values(self):
        for value in ("01.2.3", "1.2", "1.2.3-rc1", "1.2.3; echo bad", ""):
            with self.subTest(value=value):
                self.write("CMakeLists.txt", f'set(PROJECT_VER "{value}")\n')
                with self.assertRaises(ValueError):
                    release.version(self.root)
        self.write("CMakeLists.txt", 'set(PROJECT_VER "1.2.3")\nset(PROJECT_VER "1.2.4")\n')
        with self.assertRaises(ValueError):
            release.version(self.root)

    def test_dispatch_requires_main_manual_and_exact_sha(self):
        with patch.dict(os.environ, self.env, clear=True), \
                patch.object(release.subprocess, "check_output", return_value=SHA + "\n"):
            self.assertEqual(release.dispatch(self.root), SHA)
            for key, value in (("GITHUB_EVENT_NAME", "pull_request"), ("GITHUB_EVENT_NAME", "push"),
                               ("GITHUB_REF", "refs/heads/topic"), ("EXPECTED_SHA", "b" * 40),
                               ("EXPECTED_SHA", "$(touch /tmp/not-executed)")):
                with self.subTest(key=key, value=value), patch.dict(os.environ, {key: value}):
                    with self.assertRaises(ValueError):
                        release.dispatch(self.root)
            with patch.object(release.subprocess, "check_output", return_value="b" * 40):
                with self.assertRaisesRegex(ValueError, "Checkout"):
                    release.dispatch(self.root)

    def test_package_and_checksums(self):
        self.package()
        assets = release.verify_assets(self.output, "1.2.3", SHA)
        self.assertEqual(len(assets), 6)
        stem = release.prefix("1.2.3")
        with zipfile.ZipFile(self.output / f"{stem}-sdcard.zip") as archive:
            self.assertEqual(archive.namelist(), ["otr/adventure/demo.adv"])
        with zipfile.ZipFile(self.output / f"{stem}-flash-metadata.zip") as archive:
            self.assertTrue(set(self.files.values()).issubset(archive.namelist()))
            self.assertIn("flasher_args.json", archive.namelist())
        self.assertEqual((self.output / f"{stem}-ota.bin").read_bytes(),
                         (self.build / "MyEsp_OnTheRoad.bin").read_bytes())

    def test_missing_or_empty_required_files(self):
        for name in ("MyEsp_OnTheRoad.bin", "merged.bin", "bootloader/bootloader.bin",
                     "partition_table/partition-table.bin", "ota_data_initial.bin"):
            path = self.build / name
            data = path.read_bytes()
            with self.subTest(name=name):
                path.write_bytes(b"")
                with self.assertRaises(ValueError):
                    self.package()
                path.write_bytes(data)

    def test_partition_size_uses_smallest_generated_slot(self):
        self.write("build/release-partitions.csv",
                   "app0,app,ota_0,0x10000,5M,\napp1,app,ota_1,0x510000,128,\n")
        with self.assertRaisesRegex(ValueError, "smallest"):
            self.package()

    def test_flash_metadata_requires_initial_ota_data(self):
        data = json.loads((self.build / "flasher_args.json").read_text())
        del data["flash_files"]["0xe000"]
        self.write("build/flasher_args.json", json.dumps(data))
        with self.assertRaisesRegex(ValueError, "initial OTA data"):
            self.package()

    def test_missing_ota_slot(self):
        with self.assertRaisesRegex(ValueError, "both OTA"):
            release.ota_slots("factory,app,factory,0x10000,5M\n")

    def test_wrong_target_profile_version_and_security_config(self):
        for key, value in (("IDF_TARGET", "esp32p4"), ("OTR_HW_PROFILE_P4_43", True),
                           ("OTR_HW_PROFILE_ES3C35P", False), ("SECURE_BOOT", True),
                           ("SECURE_FLASH_ENC_ENABLED", True)):
            with self.subTest(key=key):
                original = self.config.copy()
                self.config[key] = value
                self.save_config()
                with self.assertRaises(ValueError):
                    self.package()
                self.config = original
        self.save_config()
        self.write("build/project_description.json",
                   '{"target":"esp32s3","project_version":"9.9.9"}')
        with self.assertRaisesRegex(ValueError, "target/version"):
            self.package()

    def test_actual_app_header_version(self):
        path = self.build / "MyEsp_OnTheRoad.bin"
        app = bytearray(path.read_bytes())
        app[48] = ord("9")
        path.write_bytes(app)
        with self.assertRaisesRegex(ValueError, "App image"):
            self.package()

    def test_merged_image_mismatch(self):
        self.write("build/merged.bin", b"not a merged image")
        with self.assertRaisesRegex(ValueError, "Merged image"):
            self.package()

    def test_flash_path_escape(self):
        with self.assertRaisesRegex(ValueError, "escapes"):
            release.flash_file(self.build, "../README.md")

    def test_corrupt_missing_and_wrong_sha_assets(self):
        self.package()
        with self.assertRaisesRegex(ValueError, "main SHA"):
            release.verify_assets(self.output, "1.2.3", "b" * 40)
        app = self.output / f"{release.prefix('1.2.3')}-ota.bin"
        app.write_bytes(b"corrupted")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.verify_assets(self.output, "1.2.3", SHA)
        app.unlink()
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            release.verify_assets(self.output, "1.2.3", SHA)

    def test_checksum_manifest_cannot_escape_directory(self):
        self.package()
        (self.output / "SHA256SUMS").write_text(f"{'a' * 64}  ../secret\n")
        with self.assertRaisesRegex(ValueError, "Invalid checksum"):
            release.verify_assets(self.output, "1.2.3", SHA)

    def test_publication_creates_exact_tag_and_draft_without_clobber(self):
        self.package()
        api, run = self.publish()
        tag_call = [call for call in api.call_args_list if call.args[0].endswith("/git/refs")]
        self.assertEqual(len(tag_call), 1)
        self.assertIn(f"sha={SHA}", tag_call[0].args)
        create, upload = [call.args[0] for call in run.call_args_list]
        self.assertIn("--draft", create)
        self.assertIn("--verify-tag", create)
        self.assertEqual(create[create.index("--target") + 1], SHA)
        self.assertNotIn("--clobber", upload)

    def test_existing_same_sha_tag_is_reused_including_annotated(self):
        self.package()
        for kind in ("commit", "tag"):
            with self.subTest(kind=kind):
                api, _ = self.publish(refs=[{"ref": "refs/tags/v1.2.3",
                                            "object": {"type": kind, "sha": SHA}}])
                self.assertFalse(any(call.args[0].endswith("/git/refs")
                                     for call in api.call_args_list))

    def test_conflicting_tag_stops_before_writes(self):
        self.package()
        for kind in ("commit", "tag"):
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "another SHA"):
                self.publish(refs=[{"ref": "refs/tags/v1.2.3",
                                    "object": {"type": kind, "sha": "b" * 40}}],
                             annotated_sha="b" * 40)
            self.run_mock.assert_not_called()
            self.assertFalse(any(call.args[0].endswith("/git/refs")
                                 for call in self.api_mock.call_args_list))

    def test_existing_draft_or_published_release_never_overwritten(self):
        self.package()
        for draft in (True, False):
            with self.subTest(draft=draft), self.assertRaisesRegex(ValueError, "no overwrite"):
                self.publish(releases=[{"tag_name": "v1.2.3", "draft": draft}])
            self.run_mock.assert_not_called()
            self.assertFalse(any(call.args[0].endswith("/git/refs")
                                 for call in self.api_mock.call_args_list))

    def test_checksum_failure_precedes_any_github_request(self):
        self.package()
        (self.output / "manifest.json").write_text("{}")
        with patch.object(release, "gh_api") as api, patch.dict(os.environ, self.env, clear=True), \
                patch.object(release.subprocess, "check_output", return_value=SHA):
            with self.assertRaises(ValueError):
                release.publish(self.output, self.root)
            api.assert_not_called()


if __name__ == "__main__":
    unittest.main()
