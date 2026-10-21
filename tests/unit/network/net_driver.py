"""The peer side of test_network_host.py, run inside `unshare -rn` (a user and
network namespace, so no root is needed): makes tap0 at 10.9.0.1, starts
dnsmasq for DHCP and DNS, starts the test sketch, exercises it and prints one
JSON line of results.

  python3 -I net_driver.py <sketch binary> <static|dhcpc|dhcps|ncm>

LINK_* in the environment shapes the emulated link (host/link.c). With
LINK_LOSS_PPM set, datagram tests allow for the loss and commands are
retried; TCP must still deliver every byte.
"""
import json
import os
import queue
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

BINARY, MODE = sys.argv[1], sys.argv[2]
HOST = "10.9.0.1"
SOURCE_BYTES = 1 << 20
LOSSY = bool(os.environ.get("LINK_LOSS_PPM"))


def pattern(n):
    return bytes(((i * 7) + (i >> 9)) & 0xFF for i in range(n))


def sh(command):
    subprocess.run(command, shell=True, check=True)


sh("ip link set lo up; ip tuntap add dev tap0 mode tap; ip link set tap0 up; ip addr add 10.9.0.1/24 dev tap0")


# ---- a test PKI for NetworkClientSecure, made before the sketch starts (it reads TLS_* then)
PKI = tempfile.mkdtemp(prefix="net-driver-pki-")


def ossl(*args):
    subprocess.run(["openssl", *args], check=True, capture_output=True, cwd=PKI)


def make_key(name, kind):
    if kind.startswith("rsa"):
        ossl("genrsa", "-out", f"{name}.key", kind[3:])
    else:
        ossl("ecparam", "-name", {"p256": "prime256v1", "p384": "secp384r1"}[kind], "-genkey", "-noout", "-out", f"{name}.key")


def make_root(name, kind):
    make_key(name, kind)
    ossl("req", "-x509", "-new", "-key", f"{name}.key", "-subj", f"/CN={name}", "-days", "30", "-sha256",
         "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign",
         "-out", f"{name}.pem")


def make_signed(name, kind, issuer, ca, extra):
    make_key(name, kind)
    ossl("req", "-new", "-key", f"{name}.key", "-subj", f"/CN={name}", "-out", f"{name}.csr")
    with open(os.path.join(PKI, f"{name}.ext"), "w") as f:
        f.write(extra)
    ossl("x509", "-req", "-in", f"{name}.csr", "-CA", f"{issuer}.pem", "-CAkey", f"{issuer}.key", "-CAcreateserial",
         "-days", "30", "-sha256", "-extfile", f"{name}.ext", "-out", f"{name}.pem")


CA_EXT = "basicConstraints=critical,CA:TRUE\nkeyUsage=critical,keyCertSign,cRLSign\n"
SERVER_EXT = "subjectAltName=DNS:tls.lan\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n"
CLIENT_EXT = "keyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\n"
make_root("ec-root", "p384")
make_signed("ec-int", "p384", "ec-root", True, CA_EXT)
make_signed("ec-leaf", "p256", "ec-int", False, SERVER_EXT)
make_root("rsa-root", "rsa4096")
make_signed("rsa-int", "rsa2048", "rsa-root", True, CA_EXT)
make_signed("rsa-leaf", "rsa2048", "rsa-int", False, SERVER_EXT)
make_root("wrong-root", "p256")
make_signed("wrong-leaf", "p256", "wrong-root", False, SERVER_EXT)
# An impostor: a root with the same name as ec-root ("CN=ec-root") and a key of its own. The bundle
# finds ec-root by that name; only the signature check can tell the chain is not its.
os.makedirs(os.path.join(PKI, "impostor"))
make_key("impostor/ec-root", "p384")
ossl("req", "-x509", "-new", "-key", "impostor/ec-root.key", "-subj", "/CN=ec-root", "-days", "30", "-sha256",
     "-addext", "basicConstraints=critical,CA:TRUE", "-addext", "keyUsage=critical,keyCertSign,cRLSign",
     "-out", "impostor/ec-root.pem")
make_signed("impostor-leaf", "p256", "impostor/ec-root", False, SERVER_EXT)
make_root("client-root", "p256")
make_signed("client", "p256", "client-root", False, CLIENT_EXT)


def der_items(data, at, end):
    """The (tag, start of the whole element, start of its content, end) of each DER element in data[at:end]."""
    out = []
    while at < end:
        tag, n = data[at], data[at + 1]
        head = 2
        if n & 0x80:
            k = n & 0x7F
            n = int.from_bytes(data[at + 2:at + 2 + k], "big")
            head = 2 + k
        out.append((tag, at, at + head, at + head + n))
        at += head + n
    return out


def name_and_key(pem_path):
    """The subject Name and the SubjectPublicKeyInfo of a certificate, DER."""
    der = subprocess.run(["openssl", "x509", "-in", pem_path, "-outform", "DER"], check=True, capture_output=True).stdout
    cert = der_items(der, 0, len(der))[0]
    tbs = der_items(der, cert[2], cert[3])[0]
    fields = der_items(der, tbs[2], tbs[3])
    if fields[0][0] == 0xA0:            # [0] version
        fields = fields[1:]
    subject, spki = fields[4], fields[5]  # serial, signature, issuer, validity, subject, spki
    return der[subject[1]:subject[3]], der[spki[1]:spki[3]]


def write_bundle(path, roots):
    """ESP-IDF's x509_crt_bundle: u32 offsets, then (u16 name len, u16 key len, name, key), sorted by name."""
    entries = sorted(name_and_key(os.path.join(PKI, r)) for r in roots)
    blobs = [struct.pack("<HH", len(n), len(k)) + n + k for n, k in entries]
    offset = 4 * len(blobs)
    table = b""
    for b in blobs:
        table += struct.pack("<I", offset)
        offset += len(b)
    with open(path, "wb") as f:
        f.write(table + b"".join(blobs))


write_bundle(os.path.join(PKI, "bundle.bin"), ["ec-root.pem", "rsa-root.pem", "client-root.pem"])
TLS_ENV = {
    "TLS_CA": os.path.join(PKI, "ec-root.pem"),
    "TLS_CA_WRONG": os.path.join(PKI, "wrong-root.pem"),
    "TLS_BUNDLE": os.path.join(PKI, "bundle.bin"),
    "TLS_CLIENT_CERT": os.path.join(PKI, "client.pem"),
    "TLS_CLIENT_KEY": os.path.join(PKI, "client.key"),
}
dnsmasq = ["dnsmasq", "-d", "-k", "-i", "tap0", "--bind-interfaces", "--no-resolv", "--no-hosts",
           "--address=/test.lan/10.9.0.99", "--address=/tls.lan/10.9.0.1", "--address=/wrong.lan/10.9.0.1", "--user=root", "--group=root", "--pid-file=", "--leasefile-ro",
           "--log-facility=-"]
if MODE == "dhcpc":
    dnsmasq += ["--dhcp-range=10.9.0.50,10.9.0.60,1h", "--dhcp-option=3,10.9.0.1", "--dhcp-option=6,10.9.0.1"]
dns = subprocess.Popen(dnsmasq, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.3)

lines = queue.Queue()
log = []
started = time.time()
sketch = subprocess.Popen([BINARY], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=dict(os.environ, STACK_MODE=MODE, **TLS_ENV))


def reader():
    for raw in sketch.stdout:
        line = raw.decode(errors="replace").rstrip()
        log.append(line)
        lines.put(line)


threading.Thread(target=reader, daemon=True).start()


def wait_line(prefix, timeout):
    end = time.time() + timeout
    while time.time() < end:
        try:
            line = lines.get(timeout=max(0.01, end - time.time()))
        except queue.Empty:
            break
        if line.startswith(prefix):
            return line
    return None


results = {"mode": MODE, "lossy": LOSSY}


def run(name, fn, timeout=120):
    box = {}

    def go():
        try:
            box["v"] = fn()
        except Exception as e:  # recorded, not raised: the other checks still run
            box["v"] = {"ok": False, "error": f"{type(e).__name__}: {e}"}

    t = threading.Thread(target=go, daemon=True)
    t.start()
    t.join(timeout)
    results[name] = box.get("v", {"ok": False, "error": "timeout"})


ip_line = wait_line("IP ", 30)
results["address"] = {"ok": ip_line is not None, "line": ip_line, "seconds": round(time.time() - started, 2)}
DUT = ip_line.split()[1] if ip_line else "10.9.0.2"


def ping():
    p = subprocess.run(["ping", "-c", "50", "-i", "0.01", "-W", "1", "-s", "1000", DUT], capture_output=True, text=True)
    received = 0
    for line in p.stdout.splitlines():
        if "received" in line:
            received = int(line.split(",")[1].split()[0])
    return {"ok": received >= (45 if LOSSY else 50), "received": received, "of": 50}


def udp_echo():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.5)
    rnd = random.Random(1)
    good = 0
    for _ in range(300):
        data = rnd.randbytes(rnd.randint(1, 1472))
        s.sendto(data, (DUT, 7))
        try:
            good += s.recvfrom(2048)[0] == data
        except socket.timeout:
            pass
    return {"ok": good >= (270 if LOSSY else 300), "echoed": good, "of": 300}


def tcp_echo(nbytes=256 * 1024, seed=2):
    data = random.Random(seed).randbytes(nbytes)
    s = socket.create_connection((DUT, 7), timeout=10)
    s.settimeout(60)
    threading.Thread(target=s.sendall, args=(data,), daemon=True).start()
    got = bytearray()
    t = time.time()
    while len(got) < nbytes:
        chunk = s.recv(65536)
        if not chunk:
            break
        got += chunk
    s.close()
    dt = time.time() - t
    return {"ok": bytes(got) == data, "bytes": len(got), "kBps": round(len(got) / dt / 1024, 1)}


def tcp_host_to_sketch():
    s = socket.create_connection((DUT, 9), timeout=10)
    s.settimeout(60)
    t = time.time()
    s.sendall(pattern(SOURCE_BYTES))
    s.shutdown(socket.SHUT_WR)
    line = wait_line("DISCARD", 120)
    dt = time.time() - t
    s.close()
    return {"ok": line == f"DISCARD {SOURCE_BYTES}", "line": line, "kBps": round(SOURCE_BYTES / dt / 1024, 1)}


def tcp_sketch_to_host():
    s = socket.create_connection((DUT, 19), timeout=10)
    s.settimeout(60)
    t = time.time()
    got = bytearray()
    while True:
        chunk = s.recv(65536)
        if not chunk:
            break
        got += chunk
    dt = time.time() - t
    return {"ok": bytes(got) == pattern(SOURCE_BYTES), "bytes": len(got), "kBps": round(len(got) / dt / 1024, 1)}


def tcp_parallel(n=4):
    outcome = [None] * n

    def one(i):
        try:
            outcome[i] = tcp_echo(32 * 1024, 10 + i)["ok"]
        except Exception as e:
            outcome[i] = type(e).__name__

    threads = [threading.Thread(target=one, args=(i,)) for i in range(n)]
    for t in threads:
        t.start()
    for t in threads:
        t.join(100)
    return {"ok": all(x is True for x in outcome), "each": outcome}


def command(text, timeout, tries):
    """A UDP command to the sketch, resent when the link loses it (or its reply)."""
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.settimeout(timeout)
    for _ in range(tries):
        u.sendto(text.encode(), (DUT, 9999))
        try:
            return u, u.recvfrom(200)[0].decode()
        except socket.timeout:
            continue
    return u, None


def tcp_client():
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, 5001))
    srv.listen(1)
    srv.settimeout(5)
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.settimeout(60)
    for _ in range(3 if LOSSY else 1):
        u.sendto(b"C 5001", (DUT, 9999))
        try:
            conn, _ = srv.accept()
            break
        except socket.timeout:
            conn = None
    if conn is None:
        return {"ok": False, "error": "the sketch did not connect"}
    conn.settimeout(60)
    got = bytearray()
    while True:
        chunk = conn.recv(65536)
        if not chunk:
            break
        got += chunk
    try:
        reply = u.recvfrom(200)[0].decode()
    except socket.timeout:
        reply = None
    ok_data = bytes(got) == pattern(256 * 1024)
    # On a lossy link the reply datagram itself may be lost; the data is what counts.
    return {"ok": ok_data and (reply == "CDONE 262144" or (LOSSY and reply is None)), "bytes": len(got), "reply": reply}


def dhcp_server():
    """busybox udhcpc against the sketch's server; the script records what it was given
    without applying it (tap0 keeps 10.9.0.1). Then as another machine (a new MAC),
    and back: the first machine gets its address again."""
    work = tempfile.mkdtemp(prefix="net-driver-")
    script = os.path.join(work, "udhcpc.sh")
    record = os.path.join(work, "leases.txt")
    with open(script, "w") as f:
        f.write("#!/bin/sh\n[ \"$1\" = bound ] && echo \"ip=$ip subnet=$subnet router=$router dns=$dns lease=$lease\" >> " + record + "\n")
    os.chmod(script, 0o755)

    def lease(mac):
        sh(f"ip link set tap0 address {mac}")
        p = subprocess.run(["busybox", "udhcpc", "-i", "tap0", "-n", "-q", "-f", "-t", "5", "-T", "1", "-s", script],
                           capture_output=True, text=True, timeout=30)
        with open(record) as f:
            return f.read().strip().splitlines()[-1] if p.returncode == 0 else None

    try:
        first = lease("02:00:00:00:01:01")
        second = lease("02:00:00:00:01:02")
        again = lease("02:00:00:00:01:01")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    expected = "ip=10.9.0.3 subnet=255.255.255.0 router= dns=10.9.0.53 lease=3600"
    ok = (first == expected and second == expected.replace("10.9.0.3", "10.9.0.4") and again == expected)
    return {"ok": ok, "first": first, "second": second, "again": again}


def tls_servers():
    """openssl s_server, reversing each line: the ECDSA chain on 4431, the RSA chain on 4432, the
    ECDSA chain asking for a client certificate on 4433, a root missing from the bundle on 4434, and an
    impostor of ec-root (same name, another key) on 4435."""
    def serve(port, leaf, chain, extra=()):
        return subprocess.Popen(["openssl", "s_server", "-quiet", "-tls1_2", "-accept", f"10.9.0.1:{port}", "-rev",
                                 "-cert", f"{leaf}.pem", "-key", f"{leaf}.key", "-cert_chain", f"{chain}.pem", *extra],
                                cwd=PKI, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    servers = [serve(4431, "ec-leaf", "ec-int"), serve(4432, "rsa-leaf", "rsa-int"),
               serve(4433, "ec-leaf", "ec-int", ("-Verify", "1", "-CAfile", "client-root.pem")),
               serve(4434, "wrong-leaf", "wrong-root"), serve(4435, "impostor-leaf", "impostor/ec-root")]
    time.sleep(0.5)
    return servers


def tls(host, port, how, expect_ok):
    _, reply = command(f"S {host} {port} {how}", 180, 1)
    ok = reply == "STLS OK olleh" if expect_ok else bool(reply and reply.startswith("STLS FAIL"))
    return {"ok": ok, "reply": reply}


def dns_lookup():
    _, reply = command("D test.lan", 12, 3 if LOSSY else 1)
    return {"ok": reply == "DNS 10.9.0.99", "reply": reply}


NTP_SECONDS = 1893456000   # 2030-01-01 00:00:00 UTC, what the test's NTP server says


def ntp_server(stop):
    """A minimal NTP server on 10.9.0.1:123: answers each request with NTP_SECONDS."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((HOST, 123))
    s.settimeout(0.2)
    while not stop.is_set():
        try:
            request, peer = s.recvfrom(512)
        except socket.timeout:
            continue
        if len(request) < 48:
            continue
        ntp = NTP_SECONDS + 2208988800
        reply = bytearray(48)
        reply[0] = (0 << 6) | (4 << 3) | 4          # no leap warning, version 4, mode 4 (server)
        reply[1] = 1                                 # stratum 1
        reply[24:32] = request[40:48]                # originate = the client's transmit time
        reply[32:40] = struct.pack(">II", ntp, 0)    # receive
        reply[40:48] = struct.pack(">II", ntp, 0)    # transmit
        s.sendto(bytes(reply), peer)


def sntp():
    stop = threading.Event()
    threading.Thread(target=ntp_server, args=(stop,), daemon=True).start()
    try:
        _, reply = command(f"T {HOST}", 5, 3 if LOSSY else 1)
        line = wait_line("SETTIME", 40)
    finally:
        stop.set()
    return {"ok": reply == "TIME STARTED" and line == f"SETTIME {NTP_SECONDS}", "reply": reply, "line": line}


def dns_name(data, at):
    """A (possibly compressed) DNS name at `at`: (name, the offset after it)."""
    labels, end = [], None
    while True:
        n = data[at]
        if n & 0xC0 == 0xC0:
            if end is None:
                end = at + 2
            at = ((n & 0x3F) << 8) | data[at + 1]
            continue
        at += 1
        if n == 0:
            break
        labels.append(data[at:at + n].decode(errors="replace"))
        at += n
    return ".".join(labels), (end if end is not None else at)


def mdns_query(name, qtype):
    """A one-shot (legacy unicast) mDNS query from 10.9.0.1: {type: [(owner, rdata), ...]}."""
    q = struct.pack(">HHHHHH", random.randint(1, 65535), 0, 1, 0, 0, 0)
    q += b"".join(bytes([len(p)]) + p.encode() for p in name.split(".")) + b"\0" + struct.pack(">HH", qtype, 1)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((HOST, 0))
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(HOST))
    s.settimeout(1.0)
    records = {}
    for _ in range(3 if LOSSY else 2):
        s.sendto(q, ("224.0.0.251", 5353))
        try:
            data = s.recvfrom(1500)[0]
        except socket.timeout:
            continue
        counts = struct.unpack(">HHHH", data[4:12])
        at = 12
        for _ in range(counts[0]):
            at = dns_name(data, at)[1] + 4
        for _ in range(counts[1] + counts[2] + counts[3]):
            owner, at = dns_name(data, at)
            rtype, _, _, length = struct.unpack(">HHIH", data[at:at + 10])
            rdata = data[at + 10:at + 10 + length]
            if rtype == 1:
                value = socket.inet_ntoa(rdata)
            elif rtype == 12:
                value = dns_name(data, at + 10)[0]
            elif rtype == 33:
                value = struct.unpack(">H", rdata[4:6])[0]
            elif rtype == 16:
                value = [rdata[i + 1:i + 1 + rdata[i]].decode() for i in _txt_offsets(rdata)]
            else:
                value = rdata.hex()
            records.setdefault(rtype, []).append((owner, value))
            at += 10 + length
        return records
    return records


def _txt_offsets(rdata):
    i = 0
    while i < len(rdata):
        yield i
        i += 1 + rdata[i]


def mdns_host():
    records = mdns_query("ch32test.local", 1)
    return {"ok": ("ch32test.local", DUT) in records.get(1, []), "records": records}


def mdns_service():
    records = mdns_query("_http._tcp.local", 12)
    instance = "ch32test._http._tcp.local"
    ok = ((("_http._tcp.local", instance) in records.get(12, []))
          and (instance, 80) in records.get(33, [])
          and any(owner == instance and "path=/x" in value for owner, value in records.get(16, [])))
    return {"ok": ok, "records": {str(k): v for k, v in records.items()}}


if ip_line and MODE in ("dhcps", "ncm"):
    run("dhcp_server", dhcp_server, 120)
if ip_line:
    run("ping", ping, 30)
    run("udp_echo", udp_echo, 300)
    run("tcp_echo", tcp_echo, 120)
    run("tcp_host_to_sketch", tcp_host_to_sketch, 150)
    run("tcp_sketch_to_host", tcp_sketch_to_host, 150)
    run("tcp_parallel", tcp_parallel, 150)
    run("tcp_client", tcp_client, 120)
    run("dns", dns_lookup, 60)
    run("sntp", sntp, 60)
    servers = tls_servers()
    try:
        run("tls_ecdsa_ca", lambda: tls("tls.lan", 4431, "ca", True), 200)
        run("tls_ecdsa_bundle", lambda: tls("tls.lan", 4431, "bundle", True), 200)
        run("tls_rsa_bundle", lambda: tls("tls.lan", 4432, "bundle", True), 200)
        run("tls_insecure", lambda: tls("tls.lan", 4431, "insecure", True), 200)
        run("tls_client_certificate", lambda: tls("tls.lan", 4433, "client", True), 200)
        run("tls_refuses_wrong_root", lambda: tls("tls.lan", 4431, "wrongca", False), 200)
        run("tls_refuses_wrong_name", lambda: tls("wrong.lan", 4431, "ca", False), 200)
        run("tls_bundle_refuses_unknown_root", lambda: tls("tls.lan", 4434, "bundle", False), 200)
        run("tls_bundle_refuses_impostor_root", lambda: tls("tls.lan", 4435, "bundle", False), 200)
        run("tls_ca_refuses_impostor_root", lambda: tls("tls.lan", 4435, "ca", False), 200)
    finally:
        for p in servers:
            p.terminate()
    run("mdns_host", mdns_host, 30)
    run("mdns_service", mdns_service, 30)

results["alive"] = sketch.poll() is None
shutil.rmtree(PKI, ignore_errors=True)
sketch.terminate()
dns.terminate()
try:
    sketch.wait(5)
except subprocess.TimeoutExpired:
    sketch.kill()
time.sleep(0.2)
# What the sketch printed besides its protocol lines: sanitizer reports, the watchdog.
results["sketch_output"] = [l for l in log if not l.startswith(("IP ", "DISCARD "))][-40:]
print(json.dumps(results))
