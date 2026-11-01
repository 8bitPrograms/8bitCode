#!/usr/bin/env python3
"""Legacy Qt 6.5.3 packaging helpers; current releases use scripts/ci. Standard library only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
VERSION = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


def checked_version(value: str) -> str:
    value = value.removeprefix("v")
    if not VERSION.fullmatch(value):
        raise ValueError("Use a three-part version such as 1.2.3 (release tag v1.2.3).")
    if any(int(part) > limit for part, limit in zip(value.split("."), (255, 255, 65535))):
        raise ValueError("Version exceeds Windows Installer limits: 255.255.65535.")
    return value


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def checked_tool(manifest: Path, name: str) -> dict:
    entry = json.loads(manifest.read_text(encoding="utf-8"))["tools"][name]
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", entry["filename"]):
        raise ValueError("Unsafe tool filename")
    github_release = entry["url"].startswith("https://github.com/") and "/releases/download/" in entry["url"]
    qt_source = entry["url"].startswith("https://download.qt.io/archive/qt/6.5/6.5.3/submodules/") and entry["url"].endswith(".tar.xz")
    if not (github_release or qt_source):
        raise ValueError("Use pinned official HTTPS release/archive URLs")
    if "/continuous/" in entry["url"] or "/latest/" in entry["url"]:
        raise ValueError("Mutable tool release URLs are not accepted")
    if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
        raise ValueError("Invalid pinned SHA256")
    return entry


def fetch_tool(manifest: Path, name: str, directory: Path, verify_only: bool = False) -> Path:
    entry = checked_tool(manifest, name)
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / entry["filename"]
    if target.exists():
        if sha256(target) != entry["sha256"]:
            raise ValueError(f"Checksum mismatch: {target}; refusing to execute or overwrite it")
        return target
    if verify_only:
        raise FileNotFoundError(f"Missing {target}; run the tools command without --verify-only first")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=directory, delete=False) as output:
            temporary = Path(output.name)
            request = urllib.request.Request(entry["url"], headers={"User-Agent": "8bitCode-build"})
            with urllib.request.urlopen(request, timeout=60) as response:
                if not response.url.startswith("https://"):
                    raise ValueError("Tool download redirected away from HTTPS")
                shutil.copyfileobj(response, output)
        if sha256(temporary) != entry["sha256"]:
            raise ValueError(f"Downloaded {name} failed SHA256 verification")
        temporary.replace(target)
        return target
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def checksums(directory: Path, version: str) -> Path:
    version = checked_version(version)
    required = [f"8bitCode-{version}-windows-x64.msi", f"8bitCode-{version}-linux-x86_64.tar.gz", f"8bitCode-{version}-source.tar.gz", f"8bitCode-{version}-qt-6.5.3-source.tar.gz"]
    for name in required:
        if not (directory / name).is_file():
            raise ValueError(f"Missing release asset: {name}")
    files = sorted(p for p in directory.iterdir() if p.is_file() and p.name != "SHA256SUMS.txt")
    for path in files:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", path.name):
            raise ValueError(f"Unsafe release filename: {path.name}")
    result = directory / "SHA256SUMS.txt"
    result.write_text("".join(f"{sha256(path)}  {path.name}\n" for path in files), encoding="ascii", newline="\n")
    return result


def metadata(output: Path | None) -> str:
    project = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(8bitPad\s+VERSION\s+([0-9.]+)", project)
    if not match:
        raise ValueError("Cannot find the 8bitPad project version")
    version = checked_version(match[1])
    ref = os.environ.get("GITHUB_REF", "")
    if ref.startswith("refs/tags/") and ref != "refs/tags/v" + version:
        raise ValueError("Release tag must match CMakeLists.txt: v" + version)
    if os.environ.get("REQUEST_DRAFT") == "true" and ref != "refs/tags/v" + version:
        raise ValueError("To create a draft, run this workflow on an existing matching vX.Y.Z tag")
    if output:
        with output.open("a", encoding="utf-8", newline="\n") as stream:
            stream.write(f"version={version}\n")
    return version


def source_archive(directory: Path, version: str) -> Path:
    version = checked_version(version)
    # Required sources must be committed, not downloaded behind the release's back.
    for name in ("ads", "singleapplication", "editorconfig-core-qt"):
        relative = f"dependency-sources/{name}/CMakeLists.txt"
        subprocess.run(["git", "ls-files", "--error-unmatch", relative], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / f"8bitCode-{version}-source.tar.gz"
    if target.exists():
        raise ValueError(f"Refusing to overwrite {target}")
    subprocess.run(["git", "archive", "--format=tar.gz", f"--prefix=8bitCode-{version}/", f"--output={target.resolve()}", "HEAD"], cwd=ROOT, check=True)
    for source, suffix in (("LICENSE", "LICENSE.txt"), ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md")):
        shutil.copyfile(ROOT / source, directory / f"8bitCode-{version}-{suffix}")
    return target


def qt_sources(directory: Path, version: str, cache: Path, licenses: Path) -> Path:
    """Preserve official source archives; extract notices only into a fresh tree."""
    version = checked_version(version)
    if licenses.exists():
        raise ValueError("Qt license output must be a fresh directory")
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / f"8bitCode-{version}-qt-6.5.3-source.tar.gz"
    if target.exists():
        raise ValueError("Qt source output already exists")
    manifest = Path(__file__).with_name("release-tools.json")
    entries = []
    archives = []
    for module in ("qtbase", "qt5compat", "qtsvg"):
        entry = checked_tool(manifest, module + "-source")
        archive = fetch_tool(manifest, module + "-source", cache)
        archives.append(archive)
        entries.append({"module": module, "version": "6.5.3", "archive": entry["filename"], "source": entry["url"], "sha256": entry["sha256"]})
        with tarfile.open(archive, "r:xz") as source:
            for member in source:
                parts = Path(member.name).parts
                if not member.isfile() or len(parts) < 2 or any(part in ("..", ".") for part in parts) or Path(member.name).is_absolute():
                    continue
                name = parts[-1].lower()
                if not (name.startswith(("license", "copying", "copyright")) or name == "qt_attribution.json" or "LICENSES" in parts):
                    continue
                destination = licenses / module / Path(*parts[1:])
                destination.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as contents, destination.open("wb") as output:
                    shutil.copyfileobj(contents, output)
    licenses.mkdir(parents=True, exist_ok=True)
    (licenses / "SOURCES.json").write_text(json.dumps({"qtVersion": "6.5.3", "sourceArchives": entries, "note": "Notices extracted from these exact source archives; no Qt 6.11 runtime SBOM is claimed for this CI build."}, indent=2) + "\n", encoding="utf-8")
    with tarfile.open(target, "w:gz") as output:
        for archive in archives:
            output.add(archive, arcname="qt-6.5.3/" + archive.name)
        output.add(licenses, arcname="qt-6.5.3/notices")
    return target


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    tools = commands.add_parser("tools")
    tools.add_argument("--tool", action="append", required=True)
    tools.add_argument("--directory", type=Path, required=True)
    tools.add_argument("--verify-only", action="store_true")
    tools.add_argument("--manifest", type=Path, default=Path(__file__).with_name("release-tools.json"))
    meta = commands.add_parser("metadata")
    meta.add_argument("--github-output", type=Path)
    for name in ("source", "checksums"):
        command = commands.add_parser(name)
        command.add_argument("--directory", type=Path, required=True)
        command.add_argument("--version", required=True)
    qt = commands.add_parser("qt-sources")
    qt.add_argument("--directory", type=Path, required=True)
    qt.add_argument("--version", required=True)
    qt.add_argument("--cache", type=Path, required=True)
    qt.add_argument("--licenses", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "tools":
        for name in args.tool:
            print(fetch_tool(args.manifest, name, args.directory, args.verify_only))
    elif args.command == "metadata":
        print(metadata(args.github_output))
    elif args.command == "source":
        print(source_archive(args.directory, args.version))
    elif args.command == "checksums":
        print(checksums(args.directory, args.version))
    elif args.command == "qt-sources":
        print(qt_sources(args.directory, args.version, args.cache, args.licenses))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
