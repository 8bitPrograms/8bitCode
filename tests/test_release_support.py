# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location("release_support", ROOT / "scripts/release_support.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseSupportTest(unittest.TestCase):
    def test_version_rejects_injection_and_msi_overflow(self):
        self.assertEqual(release.checked_version("v1.2.3"), "1.2.3")
        for value in ("../1.2.3", "1.2.3;exit", "1.2", "256.1.0", "1.1.65536", "01.2.3", "1.2.3\n"):
            with self.assertRaises(ValueError):
                release.checked_version(value)

    def test_packaging_tools_have_fixed_urls_and_hashes(self):
        manifest = ROOT / "scripts/release-tools.json"
        for name in ("wix", "linuxdeploy", "linuxdeploy-plugin-qt", "patchelf", "qtbase-source", "qt5compat-source", "qtsvg-source"):
            self.assertEqual(len(release.checked_tool(manifest, name)["sha256"]), 64)

    def test_existing_tool_tampering_fails_without_download(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            payload = directory / "tool.zip"
            payload.write_bytes(b"verified")
            manifest = directory / "manifest.json"
            manifest.write_text(json.dumps({"tools": {"fixture": {"filename": "tool.zip", "url": "https://github.com/owner/tool/releases/download/v1/tool.zip", "sha256": release.sha256(payload)}}}))
            self.assertEqual(release.fetch_tool(manifest, "fixture", directory, True), payload)
            payload.write_bytes(b"modified")
            with self.assertRaises(ValueError):
                release.fetch_tool(manifest, "fixture", directory, True)

    def test_checksums_cover_both_packages_and_source_exactly(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for suffix in ("windows-x64.msi", "linux-x86_64.tar.gz", "source.tar.gz", "qt-6.5.3-source.tar.gz"):
                (directory / ("8bitCode-1.2.3-" + suffix)).write_bytes(suffix.encode())
            result = release.checksums(directory, "1.2.3")
            lines = result.read_text().splitlines()
            self.assertEqual(len(lines), 4)
            for line in lines:
                digest, name = line.split("  ", 1)
                self.assertEqual(digest, release.sha256(directory / name))
            (directory / "8bitCode-1.2.3-source.tar.gz").unlink()
            with self.assertRaises(ValueError):
                release.checksums(directory, "1.2.3")


if __name__ == "__main__":
    unittest.main()
