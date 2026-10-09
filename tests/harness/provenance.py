"""Record reported probe identity and interfaces without interpreting firmware release numbers."""
import dataclasses
import time


def oep_snapshot(port):
    from oep_client import core, link, registry

    deadline = time.monotonic() + 12
    while True:
        try:
            host = link.open_host(port)
            break
        except link.PortBusy:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.2)
    try:
        limits = core.confirm(host)
        declaration = core.describe(host)
        tags = registry.CORE.tlv["describe"]
        result = dict(kind="oep", protocol={k: limits[k] for k in
                      ("revision", "flags", "max_frame", "window", "max_inflight", "boot_id")})
        for field in ("firmware", "model", "unit_id", "chip"):
            values = [value.decode("utf-8", errors="replace") for tag, value in declaration if tag == tags[field]]
            result[field] = values[0] if values else None
        result["core_describe"] = [dict(tag=tag, value_hex=value.hex()) for tag, value in declaration]
        result["interfaces"] = [dict(dataclasses.asdict(entry), describe=[dict(tag=tag, value_hex=value.hex())
                                for tag, value in core.describe(host, entry.fn)])
                                for entry in core.list_entries(host)]
        return result
    finally:
        # No session, plan or target operation is needed for these lock-free declarations.
        host.link.close()
