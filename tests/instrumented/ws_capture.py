"""The probe's logic capture into the test's WireSkein record (pytest-embedded-wireskein's ws_run): a capture on the
session the test already holds (oep_client.acquire.capture), handed over as wireskein.sources.oep.capture_of gives
it. This module only takes the capture; what the lines must look like is the test's `expect=` and WireSkein judges
it (no waveform is counted here).

Channels are named by the DUT's pads (PB6), the names the test's expectations use. acquire names a line by the
probe's channel name (fn 0's channel_name) or by a role label (S.N); a plain `<port>@<pad>` line has neither form,
so the line's channel number is looked up in the probe's channel names."""


def channel_names(host):
    """{channel number: the probe's channel name} (fn 0's describe, core §7.5)."""
    import struct
    from oep_client import core, registry
    tag = registry.CORE.tlv["describe"]["channel_name"]
    return {struct.unpack_from("<H", v)[0]: v[2:].decode("utf-8", "replace")
            for t, v in core.describe(host, 0) if t == tag and len(v) >= 2}


def capture(ws_run, oep_host, lines, *, rate, samples, trigger=None, pretrigger=None):
    """Capture `lines` (bench_lines.port_lines entries) once and add it to the open section of ws_run.
    trigger: (pad, kind) as oep_client.acquire.Request takes it (kind one of acquire.TRIGGERS)."""
    from oep_client import acquire
    from wireskein.sources.oep import capture_of
    host = getattr(oep_host, "host", oep_host)
    names = channel_names(host)
    req = acquire.Request([(line["pad"], names[line["channel"]]) for line in lines], rate, samples,
                          trigger=trigger, pretrigger=pretrigger)
    t = ws_run.armed()
    res = acquire.capture(host, req)
    return ws_run.add(t, capture_of(res))
