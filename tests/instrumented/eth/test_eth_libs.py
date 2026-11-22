"""eth.libs (case eth-libs): widely used Arduino network libraries, unchanged, on this core's Ethernet
(tests/sketch_support/eth_libs/eth_libs.ino; their versions pinned in run_hardware.py's CASE_LIBRARIES).

- MQTT: PubSubClient and ArduinoMqttClient connect to a broker this test runs (MQTT 3.1.1, QoS 0 only), subscribe
  <id>/in, publish "hello" to <id>/out; the broker sees the publish and sends a message to <id>/in, which the DUT
  must receive.
- HTTP client: ArduinoHttpClient GETs 200 KiB from a server this test runs (status, length and CRC-32 checked) and
  POSTs a body the server sends back.
- HTTP server: aWOT on the DUT's port 80 answers GET /hello and echoes POST /echo, asked from here.

The board's RJ45 and this host must share a broadcast domain with a DHCP server, as for eth."""
import http.client
import http.server
import json
import pathlib
import re
import secrets
import socket
import socketserver
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
    out = subprocess.run(["ip", "-4", "-o", "route", "get", ip], capture_output=True, text=True, check=True).stdout
    dev = re.search(r"\bdev (\S+)", out).group(1)
    src = re.search(r"\bsrc (\S+)", out).group(1)
    mac = pathlib.Path(f"/sys/class/net/{dev}/address").read_text().strip()
    return dev, src, mac


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

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self.send_response(201)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body) + 5))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(b"got: " + body)

    def log_message(self, *args):
        pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


class Broker:
    """MQTT 3.1.1, QoS 0: CONNECT, SUBSCRIBE, PUBLISH, PINGREQ, DISCONNECT. What the clients publish is kept
    (`published`); `send(topic, payload)` delivers to whoever subscribed to the topic."""

    def __init__(self, address):
        self.sock = socket.create_server((address, 0))
        self.port = self.sock.getsockname()[1]
        self.published = []
        self.subscribers = {}
        self.lock = threading.Lock()
        threading.Thread(target=self._accept, daemon=True).start()

    def close(self):
        self.sock.close()

    def _accept(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    @staticmethod
    def _read(conn, n):
        data = b""
        while len(data) < n:
            chunk = conn.recv(n - len(data))
            if not chunk:
                raise EOFError
            data += chunk
        return data

    def _packet(self, conn):
        head = self._read(conn, 1)[0]
        length, shift = 0, 0
        while True:
            b = self._read(conn, 1)[0]
            length |= (b & 0x7F) << shift
            shift += 7
            if not b & 0x80:
                break
        return head, self._read(conn, length)

    @staticmethod
    def _frame(head, body):
        n, out = len(body), bytearray()
        while True:
            b = n & 0x7F
            n >>= 7
            out.append(b | (0x80 if n else 0))
            if not n:
                break
        return bytes([head]) + bytes(out) + body

    @staticmethod
    def _string(data, at):
        n = struct.unpack_from(">H", data, at)[0]
        return data[at + 2:at + 2 + n].decode(), at + 2 + n

    def send(self, topic, payload):
        with self.lock:
            conns = list(self.subscribers.get(topic, ()))
        t = topic.encode()
        for conn in conns:
            conn.sendall(self._frame(0x30, struct.pack(">H", len(t)) + t + payload))
        return len(conns)

    def _serve(self, conn):
        try:
            while True:
                head, body = self._packet(conn)
                kind = head >> 4
                if kind == 1:                                   # CONNECT
                    conn.sendall(self._frame(0x20, b"\x00\x00"))
                elif kind == 3:                                 # PUBLISH, QoS 0
                    topic, at = self._string(body, 0)
                    with self.lock:
                        self.published.append((topic, body[at:]))
                elif kind == 8:                                 # SUBSCRIBE
                    pid = body[:2]
                    at, granted = 2, b""
                    while at < len(body):
                        topic, at = self._string(body, at)
                        at += 1                                 # requested QoS
                        with self.lock:
                            self.subscribers.setdefault(topic, []).append(conn)
                        granted += b"\x00"
                    conn.sendall(self._frame(0x90, pid + granted))
                elif kind == 12:                                # PINGREQ
                    conn.sendall(self._frame(0xD0, b""))
                elif kind == 14:                                # DISCONNECT
                    break
        except (EOFError, OSError):
            pass
        finally:
            with self.lock:
                for subs in self.subscribers.values():
                    if conn in subs:
                        subs.remove(conn)
            conn.close()

    def wait_subscribed(self, topic, timeout=10):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            with self.lock:
                if self.subscribers.get(topic):
                    return True
            time.sleep(0.02)
        return False


def test_eth_libs(dut, record_property):
    build_id = settings(__file__)["build_id"]
    nonce = secrets.token_hex(4)
    m = bench_ready.ready(dut, rb" (\S+) (\d)", nonce=nonce)
    assert m.group(1).decode() == build_id, "the DUT runs another build"
    assert m.group(2) == b"1", "ETH.begin() failed"
    deadline = time.monotonic() + 20
    while True:
        ip = ask(dut, "ip", rb"IP (\S+)").group(1).decode()
        if ip != "0.0.0.0" or time.monotonic() > deadline:
            break
        time.sleep(0.5)
    assert ip != "0.0.0.0", "no address from DHCP"
    _, host_ip, host_mac = route_to(ip)
    record_property("board_ip", ip)
    assert ask(dut, f"pin {host_ip} {host_mac}", rb"PIN (-?\d+)").group(1) == b"0"

    # MQTT, both libraries.
    broker = Broker(host_ip)
    try:
        for command, tag in (("psc", "PSC"), ("amc", "AMC")):
            client = f"ch32rv-{command}-{secrets.token_hex(2)}"
            message = f"to-{secrets.token_hex(4)}".encode()
            dut.write(f"{command} {host_ip} {broker.port} {client}")
            assert broker.wait_subscribed(f"{client}/in"), f"{tag}: never subscribed"
            time.sleep(0.2)
            assert broker.send(f"{client}/in", message) == 1
            m = dut.expect(tag.encode() + rb" (\d) (\d) (\d) (\S+)\r?\n", timeout=15)
            assert m.group(1, 2, 3) == (b"1", b"1", b"1"), m.group(0)
            assert m.group(4) == message, f"{tag}: received {m.group(4)!r}"
            assert (f"{client}/out", b"hello") in broker.published, f"{tag}: the publish never reached the broker"
    finally:
        broker.close()

    # HTTP client: GET 200 KiB, POST a body.
    server = Server((host_ip, 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        m = ask(dut, f"hget {host_ip} {server.server_port} /body", rb"HGET (-?\d+) (\d+) ([0-9a-f]{8})", timeout=60)
        assert (int(m.group(1)), int(m.group(2)), int(m.group(3), 16)) == (200, len(BODY), zlib.crc32(BODY))
        body = f"post-{secrets.token_hex(6)}"
        m = ask(dut, f"hpost {host_ip} {server.server_port} /in {body}", rb"HPOST (-?\d+) ([^\r\n]*)", timeout=30)
        assert m.group(1, 2) == (b"201", f"got: {body}".encode())
    finally:
        server.shutdown()

    # HTTP server: aWOT on the DUT.
    c = http.client.HTTPConnection(ip, 80, timeout=10)
    c.request("GET", "/hello")
    r = c.getresponse()
    assert (r.status, r.read()) == (200, b"hello from ch32rv")
    c.close()
    payload = secrets.token_hex(64).encode()
    c = http.client.HTTPConnection(ip, 80, timeout=10)
    c.request("POST", "/echo", body=payload, headers={"Content-Type": "text/plain"})
    r = c.getresponse()
    assert (r.status, r.read()) == (200, payload)
    c.close()
