"""The probe's logic capture into the test's WireSkein record (pytest-embedded-wireskein's ws_run), on the session the
test already holds (oep_client.acquire.capture), handed over as wireskein.sources.oep.capture_of gives it. This
module only takes the capture; what the lines must look like is the test's `expect=` and WireSkein judges it (no
waveform is counted here).

Channels are named by the DUT's pads (PB6), the names the test's expectations use, and found by their label
(`<port>@<pad>`, which acquire resolves from the probe's labels)."""


def _host(oep_host):
    return getattr(oep_host, "host", oep_host)


def _channels(lines):
    return [(line["pad"], line["label"]) for line in lines]


def capture(ws_run, oep_host, lines, *, rate, samples, trigger=None, pretrigger=None):
    """Capture `lines` (bench_lines.port_lines entries) once and add it to the open section of ws_run.
    trigger: (pad, kind) as oep_client.acquire.Request takes it (kind one of acquire.TRIGGERS)."""
    from oep_client import acquire
    from wireskein.sources.oep import capture_of
    req = acquire.Request(_channels(lines), rate, samples, trigger=trigger, pretrigger=pretrigger)
    t = ws_run.armed()
    res = acquire.capture(_host(oep_host), req)
    return ws_run.add(t, capture_of(res))


def stream(ws_run, oep_host, lines, *, rate, steps):
    """One capture of `lines` that runs while steps(wait) does: steps opens and closes ws_run's sections (put on the
    probe's clock here, so WireSkein cuts each one out of this capture), sends the DUT its commands and calls
    wait(seconds) to let the stream be read while it waits (oep_client.acquire's during). The capture is added to
    ws_run when steps returns; a loss stops it there and fails the run (on_loss "stop")."""
    from oep_client import acquire
    from wireskein.sources.oep import capture_of
    host = _host(oep_host)
    probe = getattr(getattr(host, "address", None), "place", None)

    def during(wait):
        ws_run.use_clock(lambda: host.clock_best(2), probe=probe)
        try:
            steps(wait)
        finally:
            ws_run.use_clock(None)
    req = acquire.Request(_channels(lines), rate, 0, mode="stream", during=during)
    t = ws_run.armed()
    res = acquire.capture(host, req)
    return ws_run.add(t, capture_of(res))
