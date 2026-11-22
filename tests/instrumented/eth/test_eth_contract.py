"""eth.dhcp / eth.icmp / eth.udp-echo / eth.tcp-echo / eth.tcp-both-ways / eth.http-get /
eth.https-verified / eth.https-wrong-ca: the CH32H417's ETH (tests/sketch_support/eth/eth.ino)
on the LAN this host is on.

The board's RJ45 and this host must share a broadcast domain with a DHCP server.
Certificates are made here with openssl for each run (a CA, an ECDSA P-256 and an
RSA-2048 server certificate for this host's address, and a second CA the board
must refuse). Throughput is recorded as junit properties; the assertions are
on content and a 5 Mbit/s floor each way, not on speed."""
import datetime
import http.server
import json
import pathlib
import re
import secrets
import socket
import socketserver
import ssl
import struct
import subprocess
import threading
import time
import zlib
import sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))   # bench_ready.py is staged beside the test (run_hardware.py)
import bench_ready  # noqa: E402


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line)
    return dut.expect(reply + rb"\r?\n", timeout=timeout)


def route_to(ip):
    """This host's device, address and MAC towards ip."""
    out = subprocess.run(["ip", "-4", "-o", "route", "get", ip], capture_output=True, text=True, check=True).stdout
    dev = re.search(r"\bdev (\S+)", out).group(1)
    src = re.search(r"\bsrc (\S+)", out).group(1)
    mac = pathlib.Path(f"/sys/class/net/{dev}/address").read_text().strip()
    return dev, src, mac


def pki(directory, address):
    def run(*args):
        subprocess.run(["openssl", *args], cwd=directory, check=True, capture_output=True)

    # Valid from two days back: a board whose clock is set but a little behind (the RTC an earlier sketch set by
    # SNTP, drifting since) saw a certificate made "now" as not yet valid (X509 BADCERT_FUTURE, V307 2026-11-20).
    # -not_before / -not_after are OpenSSL 3.4's; an older one keeps "now" and 30 days.
    now = datetime.datetime.now(datetime.timezone.utc)
    window = ["-not_before", (now - datetime.timedelta(days=2)).strftime("%Y%m%d%H%M%SZ"),
              "-not_after", (now + datetime.timedelta(days=30)).strftime("%Y%m%d%H%M%SZ")]
    dated = "-not_before" in subprocess.run(["openssl", "x509", "-help"], capture_output=True, text=True).stderr
    period = window if dated else ["-days", "30"]
    for ca in ("ca", "other-ca"):
        run("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", f"{ca}.key")
        run("req", "-x509", "-new", "-key", f"{ca}.key", "-sha256", *period, "-subj", f"/CN=eth contract {ca}",
            "-out", f"{ca}.pem")
    (directory / "ext.cnf").write_text(f"subjectAltName=IP:{address}\nextendedKeyUsage=serverAuth\n")
    for kind in ("ec", "rsa"):
        if kind == "ec":
            run("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", f"{kind}.key")
        else:
            run("genrsa", "-out", f"{kind}.key", "2048")
        run("req", "-new", "-key", f"{kind}.key", "-subj", f"/CN={address}", "-out", f"{kind}.csr")
        run("x509", "-req", "-in", f"{kind}.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial", *period,
            "-sha256", "-extfile", "ext.cnf", "-out", f"{kind}.pem")


BODY = secrets.token_bytes(4096) * 50          # 200 KiB


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = BODY if self.path == "/body" else b"hello"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(address, context=None):
    server = Server((address, 0), Handler)
    if context:
        server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def test_eth_contract(dut, tmp_path, record_property):
    build_id = settings(__file__)["build_id"]
    nonce = secrets.token_hex(4)
    m = bench_ready.ready(dut, rb" (\S+) (\d) ([0-9A-F]{12})", nonce=nonce)
    assert m.group(1).decode() == build_id, "the DUT runs another build"
    assert m.group(2) == b"1", "ETH.begin() failed"
    mac = int(m.group(3), 16).to_bytes(6, "big")
    assert mac[0] & 0x03 == 0x02, "a locally administered unicast address"

    # eth.dhcp
    deadline = time.monotonic() + 20
    while True:
        m = ask(dut, "ip", rb"IP (\S+) (\d+) (\d)")
        ip = m.group(1).decode()
        if ip != "0.0.0.0" or time.monotonic() > deadline:
            break
        time.sleep(0.5)
    assert ip != "0.0.0.0", "no address from DHCP"
    record_property("link", f"{m.group(2).decode()}M {'full' if m.group(3) == b'1' else 'half'}")
    assert m.group(2) in (b"10", b"100")
    dev, host_ip, host_mac = route_to(ip)
    record_property("board_ip", ip)
    # A host on two NICs in one subnet answers ARP on both; tie the board to the wired one.
    assert ask(dut, f"pin {host_ip} {host_mac}", rb"PIN (-?\d+)").group(1) == b"0"

    # eth.mdns-query: a name only this test answers (a minimal responder beside the host's own: a legacy unicast
    # query - the board's resolver asks from its own port - is answered to its port), asked by multicast DNS.
    name = "ch32rv-test-" + secrets.token_hex(3)
    seen = []
    stop = threading.Event()

    def responder():
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        s.bind(("", 5353))
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton("224.0.0.251") + socket.inet_aton(host_ip))
        s.settimeout(0.2)
        want = b"".join(bytes([len(p)]) + p.encode() for p in (name, "local")) + b"\0"
        while not stop.is_set():
            try:
                d, src = s.recvfrom(2048)
            except socket.timeout:
                continue
            if len(d) < 12 or d[2] & 0x80 or want not in d[12:].lower():
                continue
            seen.append(src)
            q_end = 12 + d[12:].lower().index(want) + len(want) + 4
            answer = d[:2] + b"\x84\x00\x00\x01\x00\x01\x00\x00\x00\x00" + d[12:q_end] + \
                b"\xc0\x0c\x00\x01\x00\x01\x00\x00\x00\x0a\x00\x04" + socket.inet_aton(host_ip)
            s.sendto(answer, src)
        s.close()

    t = threading.Thread(target=responder, daemon=True)
    t.start()
    try:
        m = ask(dut, f"mdnsq {name}", rb"MDNSQ (\S+) (\d+)", timeout=10)
    finally:
        stop.set()
        t.join(2)
    record_property("mdns_query_ms", int(m.group(2)))
    assert seen, "no mDNS query from the board reached this host"
    assert m.group(1).decode() == host_ip, f"{name}.local answered {m.group(1)!r}, not {host_ip} (asked from {seen})"

    # eth.mdns-service: the same responder offers _ch32test._tcp - an instance on this host, port 4242, TXT
    # path=/x and v=1 - answering the board's browse (a multicast query from 5353) by multicast.
    stop.clear()
    browse = []

    def service_responder():
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        s.bind(("", 5353))
        s.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton("224.0.0.251") + socket.inet_aton(host_ip))
        s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(host_ip))
        s.settimeout(0.2)

        def dn(*labels):
            return b"".join(bytes([len(x)]) + x.encode() for x in labels) + b"\0"

        def rr(owner, rtype, rdata, flush=False):
            return owner + struct.pack(">HHIH", rtype, 0x8001 if flush else 1, 120, len(rdata)) + rdata

        svc = dn("_ch32test", "_tcp", "local")
        inst = dn("bench", "_ch32test", "_tcp", "local")
        host = dn(name, "local")
        txt = b"\x07path=/x\x03v=1"
        while not stop.is_set():
            try:
                d, src = s.recvfrom(2048)
            except socket.timeout:
                continue
            if len(d) < 12 or d[2] & 0x80 or svc not in d[12:].lower():
                continue
            browse.append(src)
            body = rr(svc, 12, inst) + rr(inst, 33, struct.pack(">HHH", 0, 0, 4242) + host, True) + \
                rr(inst, 16, txt, True) + rr(host, 1, socket.inet_aton(host_ip), True)
            s.sendto(struct.pack(">HHHHHH", 0, 0x8400, 0, 4, 0, 0) + body, ("224.0.0.251", 5353))
        s.close()

    t = threading.Thread(target=service_responder, daemon=True)
    t.start()
    try:
        m = ask(dut, "mdnssvc ch32test tcp", rb"MDNSSVC (\d+)(?: (\S+) (\S+) (\d+) (\S*) (\d+))?", timeout=10)
    finally:
        stop.set()
        t.join(2)
    assert browse, "no mDNS browse from the board reached this host"
    assert m.group(1) == b"1", m.group(0)
    assert (m.group(2).decode(), m.group(3).decode(), int(m.group(4))) == (name, host_ip, 4242), m.group(0)
    assert m.group(5) == b"/x" and m.group(6) == b"2", m.group(0)

    # eth.icmp
    out = subprocess.run(["ping", "-c", "20", "-i", "0.2", "-q", ip], capture_output=True, text=True).stdout
    assert " 0% packet loss" in out, out
    record_property("ping", out.strip().splitlines()[-1])

    # eth.udp-echo
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as u:
        u.settimeout(2)
        for size in (1, 64, 512, 1400):
            payload = secrets.token_bytes(size)
            u.sendto(payload, (ip, 9000))
            assert u.recvfrom(2048)[0] == payload

    # eth.tcp-echo
    with socket.create_connection((ip, 7), timeout=5) as s:
        payload = secrets.token_bytes(65536)
        got = bytearray()
        threading.Thread(target=s.sendall, args=(payload,), daemon=True).start()
        while len(got) < len(payload):
            chunk = s.recv(65536)
            assert chunk, "the echo closed early"
            got += chunk
        assert bytes(got) == payload

    # eth.tcp-both-ways: 4 MiB to the board (counted there), 4 MiB from it (a known pattern)
    data = secrets.token_bytes(4 << 20)
    with socket.create_connection((ip, 5001), timeout=10) as s:
        t0 = time.perf_counter()
        s.sendall(data)
        s.shutdown(socket.SHUT_WR)
        s.settimeout(10)
        assert s.recv(1) == b""
        to_board = len(data) * 8 / (time.perf_counter() - t0) / 1e6
    time.sleep(0.5)
    m = ask(dut, "sink", rb"SINK (\d+) (\d+)")
    assert int(m.group(1)) == len(data)
    with socket.create_connection((ip, 5002), timeout=10) as s:
        got = bytearray()
        t0 = time.perf_counter()
        while len(got) < (4 << 20):
            chunk = s.recv(1 << 16)
            assert chunk
            got += chunk
        from_board = len(got) * 8 / (time.perf_counter() - t0) / 1e6
    assert bytes(got[:4 << 20]) == bytes(range(256)) * ((4 << 20) // 256)
    record_property("tcp_to_board_mbit", round(to_board, 2))
    record_property("tcp_from_board_mbit", round(from_board, 2))
    assert to_board > 5 and from_board > 5

    # eth.http-get
    plain = serve(host_ip)
    m = ask(dut, f"http {host_ip} {plain.server_port} /body", rb"HTTP (\d+) (\d+) ([0-9a-f]{8}) (\d+)", timeout=40)
    assert (int(m.group(1)), int(m.group(2)), int(m.group(3), 16)) == (200, len(BODY), zlib.crc32(BODY))
    record_property("http_200k_ms", int(m.group(4)))
    plain.shutdown()

    # eth.https-verified / eth.https-wrong-ca (not on the parts without TLS)
    if ask(dut, f"https {host_ip} 1 /", rb"HTTPS (NOTLS|\d+)[^\r\n]*", timeout=30).group(1) == b"NOTLS":
        record_property("https", "no TLS on this series")
        return
    pki(tmp_path, host_ip)

    def give_ca(name):
        assert ask(dut, "cab", rb"CAB")
        for line in (tmp_path / name).read_text().strip().splitlines():
            ask(dut, "cal " + line, rb"CAL")
        assert int(ask(dut, "cae", rb"CA (\d+)").group(1)) > 0

    give_ca("ca.pem")
    tls_ok = True
    for kind in ("ec", "rsa"):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(tmp_path / f"{kind}.pem", tmp_path / f"{kind}.key")
        tls = serve(host_ip, context)
        m = ask(dut, f"https {host_ip} {tls.server_port} /body c",
                rb"HTTPS (\d+) (\d+) ([0-9a-f]{8}) ([0-9a-f]{4}) (\d+) (-?\d+)", timeout=90)
        if int(m.group(6)) in (-0x7F00, -141):
            # MBEDTLS_ERR_SSL_ALLOC_FAILED, or PSA_ERROR_INSUFFICIENT_MEMORY when the PSA layer runs out
            # first: the handshake's heap (30-40 KB) did not fit - the V307 / V317's 64 KB of RAM
            # (docs/network-stack.ja.md). Recorded, not failed.
            record_property("https", "out of memory (MBEDTLS_ERR_SSL_ALLOC_FAILED)")
            tls.shutdown()
            tls_ok = False
            break
        assert (int(m.group(1)), int(m.group(2)), int(m.group(3), 16)) == (200, len(BODY), zlib.crc32(BODY)), m.group(0)
        assert m.group(4) in (b"0303", b"0304")
        record_property(f"https_{kind}_handshake_ms", int(m.group(5)))
        record_property(f"https_{kind}_tls", m.group(4).decode())
        if kind == "ec":
            m = ask(dut, f"https {host_ip} {tls.server_port} /hello i",
                    rb"HTTPS (\d+) (\d+) ([0-9a-f]{8}) ([0-9a-f]{4}) (\d+) (-?\d+)", timeout=60)
            assert int(m.group(1)) == 200, "setInsecure() should connect"
        tls.shutdown()

    if not tls_ok:
        m = ask(dut, "stats", rb"STATS (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)")
        record_property("stats", m.group(0).decode())
        return
    give_ca("other-ca.pem")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(tmp_path / "ec.pem", tmp_path / "ec.key")
    tls = serve(host_ip, context)
    m = ask(dut, f"https {host_ip} {tls.server_port} /hello c",
            rb"HTTPS (\d+) (\d+) ([0-9a-f]{8}) ([0-9a-f]{4}) (\d+) (-?\d+)", timeout=60)
    assert int(m.group(1)) == 0 and int(m.group(6)) != 0, "a server signed by another CA must be refused"
    tls.shutdown()

    m = ask(dut, "stats", rb"STATS (\d+) (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)")
    record_property("stats", m.group(0).decode())
    assert int(m.group(6)) == 0, "the PHY was reset by a link check during the run"
