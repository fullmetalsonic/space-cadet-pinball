"""Check every packaged ELF and uncompressed native library ZIP offset; no device access."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

apk = Path(sys.argv[1])
report = {"apk": str(apk), "sha256": hashlib.sha256(apk.read_bytes()).hexdigest(), "libraries": [], "errors": []}
with zipfile.ZipFile(apk) as archive, apk.open("rb") as raw:
    for info in archive.infolist():
        if not info.filename.endswith(".so"):
            continue
        data = archive.read(info)
        entry = {"path": info.filename, "sha256": hashlib.sha256(data).hexdigest()}
        if data[:5] != b"\x7fELF\x02" or data[5] != 1:
            report["errors"].append(f"{info.filename}: expected little-endian ELF64")
            continue
        offset = struct.unpack_from("<Q", data, 32)[0]
        size, count = struct.unpack_from("<HH", data, 54)
        aligns = []
        segments = []
        for i in range(count):
            ph = offset + i * size
            if struct.unpack_from("<I", data, ph)[0] == 1:
                aligns.append(struct.unpack_from("<Q", data, ph + 48)[0])
                file_offset, virtual_address = struct.unpack_from("<QQ", data, ph + 8)
                segments.append({"file_offset": file_offset, "virtual_address": virtual_address})
        entry["load_alignments"] = aligns
        entry["load_segments"] = segments
        if not aligns or any(a < 16384 for a in aligns):
            report["errors"].append(f"{info.filename}: PT_LOAD alignment below 16 KB")
        if any((s["virtual_address"] - s["file_offset"]) % 16384 for s in segments):
            report["errors"].append(f"{info.filename}: PT_LOAD offset/address not congruent at 16 KB")
        raw.seek(info.header_offset + 26)
        name_length, extra_length = struct.unpack("<HH", raw.read(4))
        entry["zip_data_offset"] = info.header_offset + 30 + name_length + extra_length
        entry["compression"] = info.compress_type
        if info.compress_type != zipfile.ZIP_STORED or entry["zip_data_offset"] % 16384:
            report["errors"].append(f"{info.filename}: native ZIP data not stored/aligned to 16 KB")
        report["libraries"].append(entry)
    report["game_assets"] = sorted(i.filename for i in archive.infolist() if i.filename.startswith("assets/game/"))
    report["font_assets"] = sorted(i.filename for i in archive.infolist() if i.filename.startswith("assets/fonts/"))
    report["audio_assets"] = sorted(i.filename for i in archive.infolist() if i.filename.startswith("assets/audio/"))
    manifest_name = "assets/pinball-assets.sha256"
    if manifest_name not in archive.namelist():
        report["errors"].append("asset integrity manifest missing")
    else:
        for line in archive.read(manifest_name).decode("utf-8").splitlines():
            digest, name = line.split("\t")
            if hashlib.sha256(archive.read("assets/" + name)).hexdigest() != digest:
                report["errors"].append("asset manifest hash mismatch: " + name)
    if not report["game_assets"]:
        report["errors"].append("game assets missing")
    if "assets/fonts/NotoSansKR.ttf" not in report["font_assets"]:
        report["errors"].append("Korean font asset missing")
    if "assets/audio/TimGM6mb.sf2" not in report["audio_assets"]:
        report["errors"].append("required MIDI soundfont missing")
if not report["libraries"]:
    report["errors"].append("no native libraries packaged")
abi = {row["path"].split("/")[1] for row in report["libraries"]}
if "arm64-v8a" not in abi:
    report["errors"].append("arm64-v8a ABI missing")
report["status"] = "FAIL" if report["errors"] else "PASS"
output = Path(__file__).with_name("apk-package-report.json")
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"{report['status']}: {len(report['libraries'])} native libraries, {len(report['game_assets'])} game assets, sha256={report['sha256']}")
for error in report["errors"]:
    print(error)
sys.exit(bool(report["errors"]))
