"""Prepare portable GNU ddrescue from official Cygwin packages. No disk access."""
import hashlib
import io
import json
import lzma
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO / "dist" / "recovery-tools"
BASE = "https://cygwin.com/pub/cygwin/"
PACKAGES = ("ddrescue", "cygwin", "libgcc1", "libstdc++6", "libiconv2", "libintl8", "libzstd1", "zlib0")


def download(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    archive_dir = ROOT / "packages"
    bin_dir = ROOT / "bin"
    archive_dir.mkdir(exist_ok=True)
    bin_dir.mkdir(exist_ok=True)
    metadata = lzma.decompress(download(BASE + "x86_64/setup.xz")).decode("utf-8")
    blocks = {b.splitlines()[0]: b.split("\n[", 1)[0] for b in re.split(r"^@ ", metadata, flags=re.M)[1:]}
    sevenzip = shutil.which("7z") or "C:/Program Files/7-Zip/7z.exe"
    manifest = []
    for name in PACKAGES:
        block = blocks[name]
        version = re.search(r"^version: (.+)$", block, re.M).group(1)
        entry = re.search(r"^install: (\S+) (\d+) ([a-f0-9]{128})$", block, re.M)
        if not entry:
            raise RuntimeError("Missing verified package metadata: " + name)
        relative, size, expected_hash = entry.groups()
        if not relative.startswith("x86_64/release/") or ".." in relative.split("/"):
            raise RuntimeError("Unexpected package path")
        archive = archive_dir / Path(relative).name
        data = archive.read_bytes() if archive.exists() else download(BASE + relative)
        if len(data) != int(size) or hashlib.sha512(data).hexdigest() != expected_hash:
            raise RuntimeError("Package integrity check failed: " + name)
        archive.write_bytes(data)
        if relative.endswith(".zst"):
            result = subprocess.run([sevenzip, "x", "-so", str(archive)], check=True, capture_output=True)
            contents = tarfile.open(fileobj=io.BytesIO(result.stdout), mode="r:")
        else:
            contents = tarfile.open(fileobj=io.BytesIO(data), mode="r:*")
        extracted = []
        with contents:
            for member in contents:
                # Extract individual regular files; never materialize archive paths.
                parts = member.name.removeprefix("./").split("/")
                if not member.isfile() or len(parts) != 3 or parts[:2] != ["usr", "bin"]:
                    continue
                filename = parts[-1]
                if not re.fullmatch(r"[A-Za-z0-9+_.-]+", filename):
                    continue
                if not filename.endswith(".dll") and filename not in (
                    "ddrescue.exe", "ddrescuelog.exe", "cygwin-console-helper.exe"
                ):
                    continue
                payload = contents.extractfile(member).read()
                (bin_dir / filename).write_bytes(payload)
                extracted.append({"file": filename, "sha256": hashlib.sha256(payload).hexdigest()})
        manifest.append({"package": name, "version": version, "url": BASE + relative,
                         "sha512": expected_hash, "files": extracted})
        print(name, version, "verified", len(extracted), "files")
    (ROOT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if not (bin_dir / "ddrescue.exe").is_file():
        raise RuntimeError("ddrescue was not extracted")
    print("Prepared:", bin_dir)


if __name__ == "__main__":
    main()
