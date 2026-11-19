#!/usr/bin/env python3
"""A local TLS server that requires a client certificate, for MutualTlsClient.

    python3 mtls_server.py <host IP> [port]      (in the folder with ca.pem, server.pem, server.key)

Each connection: the handshake (the board's certificate checked against ca.pem), the request line, and a short
HTTP/1.0 reply. A board without a valid certificate is refused in the handshake.
"""
import socket
import ssl
import sys

host = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 4433
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain("server.pem", "server.key")
ctx.load_verify_locations("ca.pem")
ctx.verify_mode = ssl.CERT_REQUIRED          # no certificate, or one ca.pem did not sign: refused
with socket.create_server((host, port)) as srv:
    print(f"listening on {host}:{port}", flush=True)
    while True:
        conn, addr = srv.accept()
        try:
            with ctx.wrap_socket(conn, server_side=True) as tls:
                print(addr[0], tls.version(), "client", dict(x[0] for x in tls.getpeercert()["subject"]), flush=True)
                print("  request:", tls.recv(200).split(b"\r\n")[0], flush=True)
                tls.sendall(b"HTTP/1.0 200 ok\r\n\r\nhello, board with a certificate\r\n")
        except (ssl.SSLError, OSError) as e:
            print(addr[0], "refused:", e, flush=True)
