"""The peer side of test_network_host.py, run inside `unshare -rn` (a user and
network namespace, so no root is needed): makes tap0 at 10.9.0.1, starts
dnsmasq for DHCP and DNS, starts the test sketch, exercises it and prints one
JSON line of results.

  python3 -I net_driver.py <sketch binary> <static|dhcpc>

LINK_* in the environment shapes the emulated link (host/link.c). With
LINK_LOSS_PPM set, datagram tests allow for the loss and commands are
retried; TCP must still deliver every byte.
"""
import json
import os
import queue
import random
import socket
import subprocess
import sys
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
dnsmasq = ["dnsmasq", "-d", "-k", "-i", "tap0", "--bind-interfaces", "--no-resolv", "--no-hosts",
           "--address=/test.lan/10.9.0.99", "--user=root", "--group=root", "--pid-file=", "--leasefile-ro",
           "--log-facility=-"]
if MODE == "dhcpc":
    dnsmasq += ["--dhcp-range=10.9.0.50,10.9.0.60,1h", "--dhcp-option=3,10.9.0.1", "--dhcp-option=6,10.9.0.1"]
dns = subprocess.Popen(dnsmasq, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.3)

lines = queue.Queue()
log = []
started = time.time()
sketch = subprocess.Popen([BINARY], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=dict(os.environ, STACK_MODE=MODE))


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


def dns_lookup():
    _, reply = command("D test.lan", 12, 3 if LOSSY else 1)
    return {"ok": reply == "DNS 10.9.0.99", "reply": reply}


if ip_line:
    run("ping", ping, 30)
    run("udp_echo", udp_echo, 300)
    run("tcp_echo", tcp_echo, 120)
    run("tcp_host_to_sketch", tcp_host_to_sketch, 150)
    run("tcp_sketch_to_host", tcp_sketch_to_host, 150)
    run("tcp_parallel", tcp_parallel, 150)
    run("tcp_client", tcp_client, 120)
    run("dns", dns_lookup, 60)

results["alive"] = sketch.poll() is None
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
