#!/usr/bin/env -S uv run --no-project --script
# /// script
# requires-python = ">=3.10"
# ///
"""Repack probe-rs's Windows zip so arduino-cli will install it.

arduino-cli refuses a tool archive whose files sit at the root:

    Cannot install tool ch32-riscv-ug:probe-rs@0.32.0: searching package root
    dir: files in archive must be placed in a subdirectory

probe-rs's Linux and macOS tarballs do have a single root directory, so those
install straight from the upstream URL. The Windows zip is flat - seven files,
no directory - so it is the one archive we have to re-host (ADR-0002 prefers
direct upstream links; see the note there). probe-rs is MIT/Apache-2.0 and both
license files are inside the archive, so they travel with it.

The output is byte-for-byte reproducible: entries sorted, one fixed timestamp,
fixed compression. That matters because the package index pins its SHA-256, so
anyone must be able to rebuild the exact asset the index describes and check it.

  uv run tools/index/repack_probe_rs.py --out dist
  uv run tools/index/repack_probe_rs.py --out dist --update   # rewrite the index fragment

Verifies the upstream download against the checksum already recorded in
tools_probe_rs.json before touching anything.
"""
import argparse
import hashlib
import json
import pathlib
import sys
import urllib.request
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
FRAGMENT = HERE / "tools_probe_rs.json"

# The hosts whose upstream archive is flat and therefore has to be repacked.
# Everything else keeps its direct upstream link.
REPACK_HOSTS = ("x86_64-mingw32", "i686-mingw32")

# One fixed timestamp for every entry. Any value works as long as it never
# changes; this is probe-rs 0.32.0's own release date.
FIXED_DATE = (2026, 7, 22, 0, 0, 0)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(url: str, cache: pathlib.Path) -> bytes:
    if cache.exists():
        return cache.read_bytes()
    print(f"downloading {url}")
    with urllib.request.urlopen(url) as r:      # noqa: S310 - fixed https URL
        data = r.read()
    cache.parent.mkdir(parents=True, exist_ok=True)
    cache.write_bytes(data)
    return data


def repack(source: bytes, root: str) -> bytes:
    """Rewrite a flat zip with every entry under `root/`."""
    import io

    out = io.BytesIO()
    with zipfile.ZipFile(io.BytesIO(source)) as src:
        names = sorted(n for n in src.namelist() if not n.endswith("/"))
        if any("/" in n for n in names):
            raise SystemExit("source archive is not flat; check whether upstream "
                             "changed layout and this script is still needed")
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as dst:
            for name in names:
                info = zipfile.ZipInfo(f"{root}/{name}", date_time=FIXED_DATE)
                # Regular file, rw-r--r--; the .exe bit is meaningless on
                # Windows and arduino-cli does not read it there.
                info.external_attr = (0o100644 << 16)
                info.compress_type = zipfile.ZIP_DEFLATED
                dst.writestr(info, src.read(name))
    return out.getvalue()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=pathlib.Path, required=True,
                    help="directory to write the repacked archive into")
    ap.add_argument("--cache", type=pathlib.Path,
                    help="where to keep the upstream download (default: <out>/upstream)")
    ap.add_argument("--update", action="store_true",
                    help="rewrite tools_probe_rs.json with the new url/checksum/size")
    ap.add_argument("--base-url",
                    help="where the repacked archive will be served from; "
                         "required with --update")
    args = ap.parse_args()

    if args.update and not args.base_url:
        raise SystemExit("--update needs --base-url")

    fragment = json.loads(FRAGMENT.read_text(encoding="utf-8"))
    args.out.mkdir(parents=True, exist_ok=True)
    cache_dir = args.cache or (args.out / "upstream")

    # All the repack hosts share one upstream archive; do it once.
    done = {}
    for system in fragment["systems"]:
        if system["host"] not in REPACK_HOSTS:
            continue
        upstream_name = system.get("upstreamArchiveFileName", system["archiveFileName"])
        upstream_url = system.get("upstreamUrl", system["url"])
        if upstream_name in done:
            new = done[upstream_name]
        else:
            data = fetch(upstream_url, cache_dir / upstream_name)
            want = system.get("upstreamChecksum", system["checksum"])
            got = "SHA-256:" + sha256(data)
            if got != want:
                raise SystemExit(f"{upstream_name}: checksum {got} != recorded {want}")
            root = upstream_name.rsplit(".", 1)[0]          # drop ".zip"
            packed = repack(data, root)
            name = f"{root}-arduino.zip"
            (args.out / name).write_bytes(packed)
            new = {"archiveFileName": name, "checksum": "SHA-256:" + sha256(packed),
                   "size": str(len(packed)), "root": root,
                   "upstreamUrl": upstream_url, "upstreamName": upstream_name,
                   "upstreamChecksum": want}
            done[upstream_name] = new
            print(f"wrote: {args.out / name}")
            print(f"  root      {root}/")
            print(f"  checksum  {new['checksum']}")
            print(f"  size      {new['size']}")

        if args.update:
            system["upstreamUrl"] = new["upstreamUrl"]
            system["upstreamArchiveFileName"] = new["upstreamName"]
            system["upstreamChecksum"] = new["upstreamChecksum"]
            system["url"] = f"{args.base_url.rstrip('/')}/{new['archiveFileName']}"
            system["archiveFileName"] = new["archiveFileName"]
            system["checksum"] = new["checksum"]
            system["size"] = new["size"]

    if not done:
        raise SystemExit(f"no system matched {REPACK_HOSTS}")
    if args.update:
        FRAGMENT.write_text(json.dumps(fragment, indent=2) + "\n", encoding="utf-8")
        print(f"updated: {FRAGMENT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
