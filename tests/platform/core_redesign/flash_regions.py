"""EXP-04 partition arithmetic; emits experiment artifacts, never board/core settings."""
import argparse
import json
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
MANIFEST = REPO / "planning/core-redesign/flash-inputs.json"


def integer(value, name, minimum=0):
    if type(value) is not int or not minimum <= value <= 0xffffffff:
        raise ValueError(f"invalid {name}")
    return value


def geometry(part, mode, manifest=MANIFEST):
    config = json.loads(Path(manifest).read_text())
    try:
        target = next(t for t in config["targets"] if t["part"] == part)
        selected = target["modes"][mode]
    except (KeyError, StopIteration) as error:
        raise ValueError("unknown part or mode") from error
    result = {key: target[key] for key in ["part", "flash_bytes", "physical_origin", "link_origin", "ram_bytes", "march", "abi"]}
    result.update(selected, mode=mode, read_unit=target["read_unit"])
    for key in ["flash_bytes", "ram_bytes", "read_unit", "program_unit", "erase_unit"]:
        integer(result[key], key, 1)
    for key in ["physical_origin", "link_origin"]:
        integer(result[key], key)
        if result[key] + result["flash_bytes"] > 0x100000000:
            raise ValueError("address overflow")
    if result["flash_bytes"] % result["erase_unit"] or result["erase_unit"] % result["program_unit"]:
        raise ValueError("incompatible geometry")
    return result


def plan(part, mode, erase_units, boot_head=0, boot_tail=0, manifest=MANIFEST):
    g = geometry(part, mode, manifest)
    for value, name in [(erase_units, "erase_units"), (boot_head, "boot_head"), (boot_tail, "boot_tail")]:
        integer(value, name)
    if boot_head % g["erase_unit"] or boot_tail % g["erase_unit"]:
        raise ValueError("boot reservation must align to selected erase unit")
    reserved = erase_units * g["erase_unit"]
    app_bytes = g["flash_bytes"] - boot_head - boot_tail - reserved
    if app_bytes <= 0:
        raise ValueError("no application flash remains")
    storage_offset = boot_head + app_bytes
    result = dict(g, erase_units=erase_units, reserved_bytes=reserved,
                  boot_head_bytes=boot_head, boot_tail_bytes=boot_tail,
                  app_offset=boot_head, app_bytes=app_bytes, storage_offset=storage_offset,
                  storage_physical_start=g["physical_origin"] + storage_offset,
                  storage_link_start=g["link_origin"] + storage_offset,
                  storage_physical_end=g["physical_origin"] + storage_offset + reserved,
                  storage_link_end=g["link_origin"] + storage_offset + reserved,
                  program_slots=reserved // g["program_unit"],
                  effective_payload_bytes=None,
                  protection="not selected; reservation alone provides no recovery or upload preservation")
    assert boot_head + app_bytes + reserved + boot_tail == g["flash_bytes"]
    return result


def menu_candidates(part, mode, manifest=MANIFEST):
    # Reviewable data only; this does not alter boards.txt or pick defaults.
    return [plan(part, mode, count, manifest=manifest) for count in [0, 1, 2, 4]]


def upload_overlap(partition, physical_ranges=(), mass_erase=False):
    """Only classifies explicitly supplied erase ranges, not a real uploader."""
    if type(mass_erase) is not bool:
        raise ValueError("mass_erase must be boolean")
    overlaps = []
    lo, hi = partition["storage_physical_start"], partition["storage_physical_end"]
    for start, length in physical_ranges:
        integer(start, "erase start"); integer(length, "erase length")
        if start + length > 0x100000000:
            raise ValueError("erase range overflow")
        if length and lo < hi and start < hi and lo < start + length:
            overlaps.append([max(start, lo), min(start + length, hi)])
    return dict(reservation_overwritten=bool(partition["reserved_bytes"] and (mass_erase or overlaps)),
                overlapping_physical_ranges=overlaps, mass_erase=mass_erase,
                scope="declared erase ranges only; not evidence about any uploader")


def linker(partition):
    """Separate link probe, not a proposed replacement for product startup/linker."""
    p = partition
    return f"""OUTPUT_ARCH(riscv)
ENTRY(probe_main)
MEMORY {{
  APP (rx) : ORIGIN = {p['link_origin'] + p['app_offset']}, LENGTH = {p['app_bytes']}
  STORE (rw) : ORIGIN = {p['storage_link_start']}, LENGTH = {p['reserved_bytes']}
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = {p['ram_bytes']}
}}
SECTIONS {{
  .text : {{ *(.text .text.*) *(.rodata .rodata.*) KEEP(*(.app_fill)) . = ALIGN(4); }} > APP
  .forbidden_persistent : {{ KEEP(*(.persistent .persistent.*)) }} > APP
  .data : {{ *(.sdata .sdata.* .data .data.*) . = ALIGN(4); }} > RAM AT> APP
  .bss (NOLOAD) : {{ *(.sbss .sbss.* .bss .bss.*) *(COMMON) }} > RAM
  __app_load_end = LOADADDR(.data) + SIZEOF(.data);
  .persistent (NOLOAD) : {{
    __persistent_start = .;
    . = ORIGIN(STORE) + LENGTH(STORE);
    __persistent_end = .;
  }} > STORE
  __persistent_physical_start = {p['storage_physical_start']};
  __persistent_physical_end = {p['storage_physical_end']};
  __persistent_reserved_bytes = {p['reserved_bytes']};
  ASSERT(__app_load_end <= ORIGIN(STORE), "application overlaps persistent reservation")
  ASSERT(SIZEOF(.forbidden_persistent) == 0, "persistent reservation accepts no input data")
  ASSERT(SIZEOF(.persistent) == LENGTH(STORE), "persistent reservation size mismatch")
  /DISCARD/ : {{ *(.comment .eh_frame .eh_frame.* .riscv.attributes) }}
}}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--part", required=True); parser.add_argument("--mode", required=True)
    parser.add_argument("--erase-units", type=int, required=True)
    parser.add_argument("--boot-head", type=int, default=0); parser.add_argument("--boot-tail", type=int, default=0)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    p = plan(args.part, args.mode, args.erase_units, args.boot_head, args.boot_tail)
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "partition.json").write_text(json.dumps(p, indent=2) + "\n")
    (args.out / "probe.ld").write_text(linker(p))
    (args.out / "menu-candidates.json").write_text(json.dumps(menu_candidates(args.part, args.mode), indent=2) + "\n")
    print(json.dumps(p))


if __name__ == "__main__":
    main()
