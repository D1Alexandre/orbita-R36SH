#!/usr/bin/env python3
"""Package an installed ARM64 build with executable modes and SHA-256 hashes."""
import argparse
from hashlib import sha256
from pathlib import Path
import re
import shutil
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path, help="Installed package directory containing Orbita.sh and orbita/")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    package = args.package.resolve()
    binary = (package / "orbita/bin/orbita").read_bytes()
    if binary[:6] != b"\x7fELF\x02\x01" or int.from_bytes(binary[18:20], "little") != 183:
        raise SystemExit("Expected an AArch64 ELF executable")
    info = (package / "orbita/BUILDINFO.txt").read_text()
    match = re.search(r"^orbita=(\d+\.\d+\.\d+)$", info, re.MULTILINE)
    if not match or "arch=aarch64\n" not in info:
        raise SystemExit("Missing ARM64 version metadata")
    version = match[1]
    archive = repo / ("dist/orbita-r36sh-%s.zip" % version)
    for name in ("LICENSE", "THIRD_PARTY.md"):
        shutil.copyfile(repo / name, package / "orbita" / name)
    shutil.copyfile(repo / "app/r36s/README.md", package / "orbita/README.md")
    validation = repo / "docs/port-r36sh/SEARCH-2026-11-20.md"
    if validation.is_file():
        shutil.copyfile(validation, package / "orbita/SEARCH-VALIDATION.md")
    if b"\r" in (package / "Orbita.sh").read_bytes():
        raise SystemExit("Launcher must use Unix line endings")
    manifest = package / "orbita/SHA256SUMS"
    files = sorted(p for p in package.rglob("*") if p.is_file() and p != manifest)
    if any(p.name.upper() in ("TOKEN.TXT", "NAMES.TXT") or "logs" in p.relative_to(package).parts for p in files):
        raise SystemExit("Package must not contain account data or device logs")
    expected = {p.relative_to(package).as_posix(): sha256(p.read_bytes()).hexdigest() for p in files}
    manifest.write_bytes("".join("%s  %s\n" % (digest, name) for name, digest in expected.items()).encode("ascii"))
    archive.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
        for path in sorted(files + [manifest]):
            name = path.relative_to(package).as_posix()
            entry = zipfile.ZipInfo.from_file(path, name)
            entry.create_system = 3
            mode = 0o100755 if name == "Orbita.sh" or name.startswith("orbita/bin/") else 0o100644
            entry.external_attr = mode << 16
            output.writestr(entry, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED)
    with zipfile.ZipFile(archive) as output:
        assert output.testzip() is None
        for name, digest in expected.items():
            assert sha256(output.read(name)).hexdigest() == digest
    digest = sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".zip.sha256").write_bytes(("%s  %s\n" % (digest, archive.name)).encode("ascii"))
    print("%s: %d bytes, %d files, AArch64 verified" % (archive, archive.stat().st_size, len(expected) + 1))
    print("sha256=" + digest)


if __name__ == "__main__":
    main()
