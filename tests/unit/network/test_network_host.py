"""The Network library (libraries/Network) built for the host and run against
Linux's own TCP/IP over a tap device: addresses (static, DHCP client and
server), ICMP, UDP, SNTP, mDNS, TLS (NetworkClientSecure),
TCP both ways and in parallel, an outgoing connection, DNS - on a clean link
and on one that loses and reorders frames.

The sketch is app.cpp on the library's public API. It is built with the
address and undefined-behaviour sanitizers, and a watchdog in host/main
prints the stack of a loop() that hangs. The peer side runs in a user and
network namespace (`unshare -rn`), so no root is needed; where the kernel
does not allow that, or dnsmasq is missing, the tests skip.
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parents[3]
HERE = pathlib.Path(__file__).resolve().parent
LIB = REPO / "libraries/Network/src"
TLS = REPO / "libraries/NetworkClientSecure/src"
API = ["String", "IPAddress", "Print", "Stream", "Common"]


def build(out: pathlib.Path) -> pathlib.Path:
    cc, cxx = shutil.which("gcc"), shutil.which("g++")
    if not cc or not cxx:
        pytest.skip("no host gcc / g++")
    flags = ["-g", "-O1", "-fsanitize=address,undefined", "-DNETWORK_HOST_TEST",
             f"-I{HERE / 'host'}", f"-I{REPO / 'cores/arduino'}", f"-I{LIB}", f"-I{TLS}"]
    objects = []
    flags.append(f"-I{REPO / 'libraries/CH32RVUsbDevice/src'}")   # CH32RVUsbCallback.h for the USB stand-in
    c_sources = sorted((LIB / "lwip-src").rglob("*.c")) + sorted((TLS / "mbedtls-src").glob("*.c")) + [
        HERE / "host/link.c", REPO / "cores/arduino/itoa.c", REPO / "cores/arduino/dtostrf.c",
        REPO / "cores/arduino/ch32rv_service.c", REPO / "cores/arduino/ch32rv_format.c"]
    cxx_sources = (sorted(LIB.glob("*.cpp")) + sorted(TLS.glob("*.cpp")) + sorted((HERE / "host").glob("*.cpp")) + [HERE / "app.cpp"]
                   + [REPO / f"cores/arduino/api/{name}.cpp" for name in API]
                   + [REPO / "cores/arduino/ch32rv_printf.cpp"])   # Print::printFloat's text
    # Mbed TLS's configuration checks: compile-time only, built here so a bad
    # combination in our two configuration headers fails the test.
    checks = sorted((TLS.parent / "extras/mbedtls-config-check").glob("*.c"))
    for index, source in enumerate(c_sources + checks + cxx_sources):
        obj = out / f"{index:03d}-{source.stem}.o"
        if source in checks:
            command = [cc, *flags, f"-I{TLS / 'mbedtls-src'}", "-w", "-c", str(source), "-o", str(obj)]
        elif source.suffix == ".c":
            command = [cc, *flags, "-w", "-c", str(source), "-o", str(obj)]
        else:
            command = [cxx, "-std=gnu++17", *flags, "-include", str(HERE / "host/prelude.h"),
                       "-Wall", "-Wextra", "-Werror", "-c", str(source), "-o", str(obj)]
        subprocess.run(command, check=True)
        objects.append(str(obj))
    binary = out / "network-host"
    subprocess.run([cxx, "-rdynamic", "-fsanitize=address,undefined", "-Wl,--wrap=settimeofday", *objects,
                    "-o", str(binary)], check=True)
    return binary


@pytest.fixture(scope="module")
def sketch(tmp_path_factory):
    if subprocess.run(["unshare", "-rn", "true"], capture_output=True).returncode != 0:
        pytest.skip("unprivileged user namespaces are not available")
    if not shutil.which("dnsmasq"):
        pytest.skip("dnsmasq is needed for DHCP and DNS")
    return build(tmp_path_factory.mktemp("network-host"))


def drive(binary, mode, **link):
    env = dict(os.environ, **{f"LINK_{k.upper()}": str(v) for k, v in link.items()})
    p = subprocess.run(["unshare", "-rn", sys.executable, "-I", str(HERE / "net_driver.py"), str(binary), mode],
                       capture_output=True, text=True, env=env, timeout=1500)
    assert p.returncode == 0, p.stderr
    results = json.loads(p.stdout.strip().splitlines()[-1])
    print(json.dumps(results, indent=1))
    return results


def check(results, names):
    failed = {n: results.get(n) for n in names if not results.get(n, {}).get("ok")}
    assert not failed, f"{failed}\nsketch output: {results['sketch_output']}"
    assert results["alive"], results["sketch_output"]
    reports = [l for l in results["sketch_output"] if "Sanitizer" in l or "runtime error" in l or "WATCHDOG" in l]
    assert not reports, results["sketch_output"]


ALL = ["address", "ping", "udp_echo", "tcp_echo", "tcp_host_to_sketch", "tcp_sketch_to_host", "tcp_timed_read", "tcp_parallel",
       "tcp_client", "udp_loopback", "udp_own_address", "dns", "sntp", "mdns_host", "mdns_service"]
TLS_CASES = ["tls_ecdsa_ca", "tls_ecdsa_bundle", "tls_rsa_bundle", "tls_rsa_tls12_server", "tls_insecure",
             "tls_client_certificate", "tls_tls13_only_server", "tls_tls12_only_server", "tls_asked_for_tls12",
             "tls_tls13_refused_by_tls12_server", "tls_tls12_refused_by_tls13_server", "tls_refuses_wrong_root",
             "tls_refuses_wrong_name", "tls_bundle_refuses_unknown_root", "tls_bundle_refuses_impostor_root",
             "tls_ca_refuses_impostor_root"]


def test_static_address(sketch):
    check(drive(sketch, "static"), ALL + TLS_CASES)


def test_dhcp_client(sketch):
    results = drive(sketch, "dhcpc")
    check(results, ALL)
    assert results["address"]["line"].startswith("IP 10.9.0.5"), results["address"]


def test_dhcp_server(sketch):
    check(drive(sketch, "dhcps"), ["dhcp_server"] + ALL)


def test_usb_ncm_espusbdevice_calls(sketch):
    """CH32RVUsbDeviceNet's ipConfig() / dhcpServer() / dhcpDns() / beginNetwork() on
    NetworkUsbNcm, over host/CH32RVUsbDevice.h's stand-in for the USB function."""
    check(drive(sketch, "ncm"), ["dhcp_server"] + ALL)


@pytest.mark.parametrize("seed", [1, 4])
def test_lossy_link(sketch, seed):
    """10 Mbit/s, 1 ms each way, 1 % of frames lost and 1 % reordered, in both directions."""
    check(drive(sketch, "static", kbps=10000, delay_us=1000, loss_ppm=10000, reorder_ppm=10000, seed=seed), ALL)
