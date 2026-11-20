"""Real loopback HTTPS against the production curl adapter; no provider traffic."""
import gzip
import http.server
import os
import ssl
import subprocess
import sys
import tempfile
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *_):
        pass

    def do_GET(self):
        body = b"hello"
        status = 200
        if self.path == "/large":
            body = b"x" * 1000
        elif self.path == "/gzip":
            body = gzip.compress(b"x" * 1000)
        elif self.path == "/error":
            status = 503
        elif self.path == "/redirect":
            status = 302
        self.send_response(status)
        if self.path == "/redirect":
            self.send_header("Location", "https://example.invalid/should-never-be-followed")
        if self.path == "/gzip":
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.path == "/slow":
            time.sleep(2)
        try:
            self.wfile.write(body)
        except (OSError, ssl.SSLError):
            pass

    def do_POST(self):
        assert self.headers.get("X-Test") == "contract"
        assert self.headers.get("Content-Type") == "text/plain"
        body = self.rfile.read(int(self.headers["Content-Length"]))
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


with tempfile.TemporaryDirectory(prefix="orbita-https-") as tmp:
    cert, key = (os.path.join(tmp, name) for name in ("cert.pem", "key.pem"))
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-keyout", key, "-out", cert, "-subj", "/CN=localhost",
        "-addext", "subjectAltName=DNS:localhost",
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    host = "localhost:%d" % server.server_port
    env = dict(os.environ, ORBITA_TEST_CA=cert)
    cases = [
        ("get", host, "/small", 200, env),
        ("persistent", host, "/small", 200, env),
        ("post", host, "/post", 200, env),
        ("get", host, "/error", 503, env),
        ("get", host, "/redirect", 302, env),
        ("large", host, "/large", 0, env),
        ("large", host, "/gzip", 0, env),
        ("cert", "127.0.0.1:%d" % server.server_port, "/small", 0, env),
        ("cert", host, "/small", 0, {k: v for k, v in env.items() if k != "ORBITA_TEST_CA"}),
        ("cancel", host, "/small", 0, env),
        ("cancel-running", host, "/slow", 0, env),
        ("invalid", "localhost@evil.invalid", "/", 0, env),
    ]
    try:
        for mode, target, path, status, variables in cases:
            subprocess.run([sys.argv[1], mode, target, path, str(status)], env=variables, check=True, timeout=10)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    print("HTTPS: %d loopback scenarios passed; no external requests" % len(cases))
