"""eth.dhcp / eth.icmp / eth.udp-echo / eth.tcp-echo / eth.tcp-both-ways / eth.http-get /
eth.https-verified / eth.https-wrong-ca: the CH32H417's ETH (tests/sketch_support/eth/eth.ino)
on the LAN this host is on.

The board's RJ45 and this host must share a broadcast domain with a DHCP server.
Certificates are made here with openssl for each run (a CA, an ECDSA P-256 and an
RSA-2048 server certificate for this host's address, and a second CA the board
must refuse). Throughput is recorded as junit properties; the assertions are
on content and a 5 Mbit/s floor each way, not on speed."""
import http.server
import json
import pathlib
import re
import secrets
import socket
import socketserver
import ssl
import subprocess
import threading
import time
import zlib


def settings(test_file):
    return json.loads(pathlib.Path(test_file).with_name("case.json").read_text())


def ask(dut, line, reply, timeout=10):
    dut.write(line + "\n")
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
    for ca in ("ca", "other-ca"):
        run("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", f"{ca}.key")
        run("req", "-x509", "-new", "-key", f"{ca}.key", "-sha256", "-days", "30", "-subj", f"/CN=eth contract {ca}",
            "-out", f"{ca}.pem")
    (directory / "ext.cnf").write_text(f"subjectAltName=IP:{address}\nextendedKeyUsage=serverAuth\n")
    for kind in ("ec", "rsa"):
        if kind == "ec":
            run("ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", f"{kind}.key")
        else:
            run("genrsa", "-out", f"{kind}.key", "2048")
        run("req", "-new", "-key", f"{kind}.key", "-subj", f"/CN={address}", "-out", f"{kind}.csr")
        run("x509", "-req", "-in", f"{kind}.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial", "-days", "30",
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
    m = ask(dut, f"ready {nonce}", rb"READY " + nonce.encode() + rb" (\S+) (\d) ([0-9A-F]{12})", timeout=15)
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

    # eth.https-verified / eth.https-wrong-ca
    pki(tmp_path, host_ip)

    def give_ca(name):
        assert ask(dut, "cab", rb"CAB")
        for line in (tmp_path / name).read_text().strip().splitlines():
            ask(dut, "cal " + line, rb"CAL")
        assert int(ask(dut, "cae", rb"CA (\d+)").group(1)) > 0

    give_ca("ca.pem")
    for kind in ("ec", "rsa"):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(tmp_path / f"{kind}.pem", tmp_path / f"{kind}.key")
        tls = serve(host_ip, context)
        m = ask(dut, f"https {host_ip} {tls.server_port} /body c",
                rb"HTTPS (\d+) (\d+) ([0-9a-f]{8}) ([0-9a-f]{4}) (\d+) (-?\d+)", timeout=90)
        assert (int(m.group(1)), int(m.group(2)), int(m.group(3), 16)) == (200, len(BODY), zlib.crc32(BODY)), m.group(0)
        assert m.group(4) in (b"0303", b"0304")
        record_property(f"https_{kind}_handshake_ms", int(m.group(5)))
        record_property(f"https_{kind}_tls", m.group(4).decode())
        if kind == "ec":
            m = ask(dut, f"https {host_ip} {tls.server_port} /hello i",
                    rb"HTTPS (\d+) (\d+) ([0-9a-f]{8}) ([0-9a-f]{4}) (\d+) (-?\d+)", timeout=60)
            assert int(m.group(1)) == 200, "setInsecure() should connect"
        tls.shutdown()

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
