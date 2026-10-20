#!/usr/bin/env python3
"""Verify EXP-04 artifact checks reject altered ELF/bin/hex; no target execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]
spec = importlib.util.spec_from_file_location("exp04_check", ROOT / "check_flash_regions.py")
checks = importlib.util.module_from_spec(spec); spec.loader.exec_module(checks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(); original = args.evidence.resolve()
    base = json.loads((original / "result.json").read_text())
    assert base["status"] == "passed" and base["results"]
    for name, digest in base["inputs"].items():
        assert checks.sha(REPO / name) == digest, (name, "source changed since base evidence")
    args.out.mkdir(parents=True, exist_ok=False); out = args.out.resolve()
    shutil.copyfile(Path(__file__), out / Path(__file__).name)
    report = dict(status="running",base_result_sha256=checks.sha(original/"result.json"),
                  source_sha256=checks.sha(__file__),positive_checks=0,rejections=[])
    def save(): (out/"result.json").write_text(json.dumps(report,indent=2)+"\n")
    save()
    try:
        for row in base["results"]:
            folder = original / row["part"] / row["mode"] / f"units{row['erase_units']}-lto{int(row['lto'])}"
            dest = out / row["part"] / row["mode"] / folder.name; dest.mkdir(parents=True)
            raw=(folder/"image.elf").read_bytes(); image=(folder/"image.bin").read_bytes(); text=(folder/"image.hex").read_text()
            for name,digest in [("image.elf",row["elf_sha256"]),("image.bin",row["image_sha256"]),("image.hex",row["hex_sha256"])]:
                assert checks.sha(folder/name) == digest
            p=json.loads((folder/"partition.json").read_text()); symbols=checks.elf.symbols((folder/"symbols.txt").read_text())
            checks.inspect(raw,image,symbols,p);checks.inspect_hex(text,image,p["link_origin"]+p["app_offset"])
            report["positive_checks"]+=1
            def reject(label, operation):
                try:operation()
                except AssertionError:
                    report["rejections"].append(dict(part=row["part"],mode=row["mode"],erase_units=row["erase_units"],lto=row["lto"],case=label,rejected=True));save()
                else:raise RuntimeError("fault was accepted: "+label)
            header,_=checks.elf.elf_sections(raw)
            modified=bytearray(raw)
            for index in range(header[10]):
                offset=header[5]+index*header[9];ph=struct.unpack_from("<8I",raw,offset)
                if ph[0]==1 and ph[4]:
                    struct.pack_into("<I",modified,offset+12,p["storage_link_start"]);break
            else:raise AssertionError("missing load header")
            (dest/"load-overlap.elf").write_bytes(modified)
            reject("file-backed-load-at-storage",lambda:checks.inspect(modified,image,symbols,p))
            appended=image+b"\0";(dest/"appended.bin").write_bytes(appended)
            reject("unexpected-binary-tail",lambda:checks.inspect(raw,appended,symbols,p))
            lines=text.splitlines();last=int(lines[0][-2:],16);lines[0]=lines[0][:-2]+f"{last^1:02X}"
            badhex="\n".join(lines)+"\n";(dest/"checksum.hex").write_text(badhex)
            reject("hex-checksum",lambda:checks.inspect_hex(badhex,image,p["link_origin"]+p["app_offset"]))
            if p["reserved_bytes"]:
                modified=bytearray(raw)
                for index in range(header[12]):
                    offset=header[6]+index*header[11];section=struct.unpack_from("<10I",raw,offset)
                    if section[1]==8 and section[3]==p["storage_link_start"] and section[5]==p["reserved_bytes"]:
                        struct.pack_into("<I",modified,offset+4,1);break
                else:raise AssertionError("missing reservation section")
                (dest/"stored-reservation.elf").write_bytes(modified)
                reject("reservation-progbits",lambda:checks.inspect(modified,image,symbols,p))
        report["status"]="passed"
    except Exception as error:report.update(status="failed",error=str(error))
    finally:
        save();(out/"sha256.json").write_text(json.dumps({str(p.relative_to(out)):checks.sha(p) for p in sorted(out.rglob("*")) if p.is_file() and p.name!="sha256.json"},indent=2)+"\n")
    print(json.dumps(dict(status=report["status"],positive_checks=report["positive_checks"],rejections=len(report["rejections"]),error=report.get("error"))))
    return int(report["status"]!="passed")


if __name__=="__main__":sys.exit(main())
