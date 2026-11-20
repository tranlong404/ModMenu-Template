"""Package LibTool Mod Menu Template as a flashable Zygisk module ZIP."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def module_properties():
    return dict(line.split("=", 1) for line in
                (ROOT / "module/module.prop").read_text(encoding="utf-8").splitlines() if "=" in line)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def verify_elf64(data: bytes, machine: int):
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("Expected little-endian ELF64")
    if struct.unpack_from("<HH", data, 16) != (3, machine):
        raise ValueError(f"Unexpected ELF64 type/machine (expected {machine})")
    offset = struct.unpack_from("<Q", data, 32)[0]
    width, count = struct.unpack_from("<HH", data, 54)
    loads = []
    for i in range(count):
        row = struct.unpack_from("<IIQQQQQQ", data, offset + i * width)
        if row[0] == 1:
            if row[7] < 16384 or row[2] % 16384 != row[3] % 16384:
                raise ValueError("ELF64 LOAD segment lacks 16 KiB alignment")
            loads.append(row[7])
    return {"class": "ELF64", "machine": machine, "load_alignments": loads,
            "sha256": digest(data), "bytes": len(data)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ndk", type=Path, required=True)
    args = parser.parse_args()

    meta = module_properties()
    version = meta["version"].strip()
    readelf = args.ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe"

    inputs = {
        "payload/arm64-v8a.so": (ROOT / "artifacts/arm64-v8a/libModMenu.so", 183),
        "zygisk/arm64-v8a.so": (ROOT / "zygisk-loader/build/libs/arm64-v8a/libLoader.so", 183),
        "zygisk/x86_64.so": (ROOT / "zygisk-loader/build/libs/x86_64/libLoader.so", 62),
    }

    files = {p.relative_to(ROOT / "module").as_posix(): p.read_bytes()
             for p in (ROOT / "module").rglob("*") if p.is_file()}
    inspection = {}
    for arcname, (path, machine) in inputs.items():
        data = path.read_bytes()
        inspection[arcname] = verify_elf64(data, machine)
        syms = subprocess.run([str(readelf), "--dyn-syms", str(path)], check=True,
                              capture_output=True, text=True).stdout
        req = "JNI_OnLoad" if arcname.startswith("payload/") else "zygisk_module_entry"
        if not any(req in ln and " UND " not in ln for ln in syms.splitlines()):
            raise ValueError(f"Missing export {req}: {arcname}")
        files[arcname] = data

    immutable = {k: v for k, v in files.items()
                 if not k.startswith("META-INF/") and k not in {"module.prop", "targets.txt", "delay.txt", "payload.sha256"}}
    files["payload.sha256"] = "".join(f"{digest(v)}  {k}\n" for k, v in sorted(immutable.items())).encode("utf-8")

    out_zip = ROOT / "artifacts" / f"ModMenu-Template-{version}.zip"
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, (2026, 11, 20, 0, 0, 0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if name.endswith(".sh") or name.endswith("update-binary") else 0o100644) << 16
            zf.writestr(info, data)

    with zipfile.ZipFile(out_zip, "r") as zf:
        if zf.testzip() is not None:
            raise ValueError("ZIP CRC failure")

    sha = digest(out_zip.read_bytes())
    (out_zip.parent / (out_zip.name + ".sha256")).write_text(f"{sha}  {out_zip.name}\n", encoding="utf-8")
    print(json.dumps({
        "version": version,
        "module_id": meta["id"],
        "files": inspection,
        "zip": str(out_zip),
        "zip_bytes": out_zip.stat().st_size,
        "zip_sha256": sha,
        "crc": "pass",
    }, indent=2))


if __name__ == "__main__":
    main()
