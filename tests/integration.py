#!/usr/bin/env python3
"""Dependency-free loopback tests. No browser, Wine, or game is required."""
import os
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import time

TOKEN = b"a" * 64

def exact(s, size):
    out = bytearray()
    while len(out) < size:
        chunk = s.recv(size - len(out))
        if not chunk:
            raise EOFError("unexpected connection close")
        out.extend(chunk)
    return bytes(out)

def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]

def connect(port):
    return socket.create_connection(("127.0.0.1", port), timeout=3)

def tag(config):
    value = 14695981039346656037
    for byte in config:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

def profile_tag(profile_id, config):
    value = 14695981039346656037
    for byte in profile_id.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    value = (value * 1099511628211) & ((1 << 64) - 1)  # hash the NUL separator
    for byte in config:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return value

def frame(payload):
    return struct.pack("<I", len(payload)) + payload

def catalog_query(process="demo.exe", arch=2):
    value = process.encode()
    return bytes((1, arch)) + struct.pack("<I", len(value)) + value

def decode_catalog(payload):
    pos = 0
    assert payload[pos] == 1
    pos += 1
    count = struct.unpack_from("<I", payload, pos)[0]
    pos += 4
    result = []
    for _ in range(count):
        fields = []
        for _ in range(2):
            n = struct.unpack_from("<I", payload, pos)[0]
            pos += 4
            fields.append(payload[pos:pos+n].decode())
            pos += n
        identity = struct.unpack_from("<Q", payload, pos)[0]
        pos += 8
        priority = payload[pos]
        pos += 1
        result.append((*fields, identity, priority))
    assert pos == len(payload)
    return result

def request(port, token=TOKEN, origin="https://aurorawright.github.io"):
    s = connect(port)
    req = (b"GET /?token=" + token + b" HTTP/1.1\r\nHost: 127.0.0.1\r\n"
           b"Connection: Upgrade\r\nUpgrade: websocket\r\n"
           b"Sec-WebSocket-Version: 13\r\n"
           b"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n")
    if origin is not None:
        req += b"Origin: " + origin.encode() + b"\r\n"
    req += b"\r\n"
    # Deliberately fragment the HTTP request at the transport level.
    s.sendall(req[:13])
    s.sendall(req[13:])
    response = bytearray()
    while not response.endswith(b"\r\n\r\n"):
        response.extend(exact(s, 1))
    return s, bytes(response)

def ws_read(s):
    a, b = exact(s, 2)
    assert a & 0x80 and not (b & 0x80), "server frames must be final and unmasked"
    length = b & 127
    if length == 126:
        length = struct.unpack("!H", exact(s, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", exact(s, 8))[0]
    return a & 15, exact(s, length)

def masked(opcode, payload=b""):
    assert len(payload) <= 125
    mask = b"1234"
    return bytes([0x80 | opcode, 0x80 | len(payload)]) + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

def event(text, kind=1):
    if isinstance(text, str):
        text = text.encode("utf-8")
    payload = struct.pack("<BIIII", kind, 0, 123, 456, len(text)) + text
    return struct.pack("<I", len(payload)) + payload

def producer(port, config, token=TOKEN, identity=None):
    s = connect(port)
    hello = b"THK2\x02" + token + struct.pack("<Q", tag(config) if identity is None else identity)
    s.sendall(hello[:6])
    s.sendall(hello[6:])
    return s

def selection_checks(agent):
    title = "Game 日本語 🦊"
    request = frame(text_bytes(title))

    def pick():
        s = connect(agent)
        s.sendall(b"THK2\x06" + TOKEN + request)
        return s

    def result(s):
        n = struct.unpack("<I", exact(s, 4))[0]
        payload = exact(s, n)
        assert payload[0] in (0, 1)
        length = struct.unpack_from("<I", payload, 1)[0]
        assert len(payload) == 5 + length
        return payload[0], payload[5:].decode()

    with pick() as no_client:
        assert result(no_client)[0] == 0
    with connect(agent) as worker:
        worker.sendall(b"THK2\x05" + TOKEN)
        assert exact(worker, 4) == b"OKAY"
        with connect(agent) as duplicate:
            duplicate.sendall(b"THK2\x05" + TOKEN)
            assert exact(duplicate, 4) == b"BUSY"
        with pick() as chosen:
            n = struct.unpack("<I", exact(worker, 4))[0]
            assert exact(worker, n) == text_bytes(title)
            with pick() as busy:
                assert result(busy)[0] == 0
            worker.sendall(frame(b"\x01" + text_bytes("Attached 日本語")))
            assert result(chosen) == (1, "Attached 日本語")
        with pick() as missing:
            n = struct.unpack("<I", exact(worker, 4))[0]
            exact(worker, n)
            worker.sendall(frame(b"\x00" + text_bytes("Window not found")))
            assert result(missing) == (0, "Window not found")
        with pick() as interrupted:
            n = struct.unpack("<I", exact(worker, 4))[0]
            exact(worker, n)
            worker.shutdown(socket.SHUT_RDWR)
            assert result(interrupted)[0] == 0
    for payload in (text_bytes(""), text_bytes("a\x00b"), struct.pack("<I", 1) + b"\xff"):
        with connect(agent) as invalid:
            invalid.sendall(b"THK2\x06" + TOKEN + frame(payload))
            assert invalid.recv(1) == b""
    with connect(agent) as invalid:
        invalid.sendall(b"THK2\x05" + b"b" * 64)
        assert invalid.recv(1) == b""
    print("PASS: authenticated window selection, Unicode titles, results, busy clients, disconnects, malformed titles")


def text_bytes(value):
    encoded = value.encode()
    return struct.pack("<I", len(encoded)) + encoded


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())
    with_luau = "--luau" in sys.argv
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        ws, agent = free_port(), free_port()
        while agent == ws:
            agent = free_port()
        args = [binary, "--ws-port", str(ws), "--agent-port", str(agent), "--token", TOKEN.decode(), "--stdin"]
        if with_luau:
            script = root / "test.luau"
            script.write_text('return { id = "integration-x64", engine = "IntegrationEngine", processes = {"demo.exe"},\n'
                              'hooks = {{ name = "dummy", module = "demo.exe", rva = 1, source = "rcx" }},\n'
                              'on_text = function(text, event)\n'
                              'if text == "DROP" then return nil end\n'
                              'return tostring(event.pid) .. ":" .. text\nend }\n', encoding="utf-8")
            args += ["--script", str(script)]
        log = open(root / "host.log", "wb")
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=log)
        sockets = []
        try:
            for _ in range(100):
                if p.poll() is not None:
                    raise RuntimeError("host exited at startup")
                try:
                    with connect(agent):
                        break
                except OSError:
                    time.sleep(.03)
            else:
                raise TimeoutError("host never started")
            selection_checks(agent)
            with connect(agent) as cfg:
                cfg.sendall(b"THK2\x01" + TOKEN)
                size = struct.unpack("<I", exact(cfg, 4))[0]
                config = exact(cfg, size)
                if not with_luau:
                    assert config == struct.pack("<II", 2, 0)
                assert cfg.recv(1) == b""
            with connect(agent) as cat:
                cat.sendall(b"THK2\x03" + TOKEN + frame(catalog_query()))
                size = struct.unpack("<I", exact(cat, 4))[0]
                items = decode_catalog(exact(cat, size))
                assert len(items) == (1 if with_luau else 0)
                if with_luau:
                    profile_id, engine, identity, priority = items[0]
                    assert (profile_id, engine, priority) == ("integration-x64", "IntegrationEngine", 2)
                    assert identity == profile_tag(profile_id, config)
                    with connect(agent) as getcfg:
                        getcfg.sendall(b"THK2\x04" + TOKEN + frame(b"\x01" + struct.pack("<Q", identity)))
                        n = struct.unpack("<I", exact(getcfg, 4))[0]
                        assert exact(getcfg, n) == config
            for token, origin in ((b"b" * 64, None), (TOKEN, "https://untrusted.example")):
                s, response = request(ws, token, origin)
                assert response.startswith(b"HTTP/1.1 403")
                s.close()
            for _ in range(2):
                s, response = request(ws)
                assert response.startswith(b"HTTP/1.1 101")
                assert b"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=" in response
                sockets.append(s)
            identity = profile_tag("integration-x64", config) if with_luau else tag(config)
            prod = producer(agent, config, identity=identity)
            sockets.append(prod)
            assert exact(prod, 4) == b"OKAY"
            messages = ["これはテストです。", "日本語\nsecond line 🦊", "あ" * 5000]
            for message in messages:
                wire = event(message)
                for lo, hi in ((0, 1), (1, 3), (3, 13), (13, len(wire))):
                    prod.sendall(wire[lo:hi])
                expected = (("123:" if with_luau else "") + message).encode()
                for viewer in sockets[:2]:
                    assert ws_read(viewer) == (1, expected)
            if with_luau:
                prod.sendall(event("DROP") + event("after-drop"))
                for viewer in sockets[:2]:
                    assert ws_read(viewer) == (1, b"123:after-drop")
            ping = masked(9, b"ping")
            sockets[0].sendall(ping[:1])
            sockets[0].sendall(ping[1:])
            assert ws_read(sockets[0]) == (10, b"ping")
            p.stdin.write("標準入力です。\n".encode())
            p.stdin.flush()
            for viewer in sockets[:2]:
                assert ws_read(viewer) == (1, (("0:" if with_luau else "") + "標準入力です。").encode())
            sockets[0].sendall(masked(8, struct.pack("!H", 1000)))
            assert ws_read(sockets[0]) == (8, struct.pack("!H", 1000))
            assert sockets[0].recv(1) == b""
            sockets[1].sendall(b"\x89\x00")  # Unmasked client frame.
            assert ws_read(sockets[1])[0] == 8
            with producer(agent, config, identity=identity ^ 1) as mismatch:
                assert mismatch.recv(1) == b""
            with producer(agent, config, identity=identity) as invalid:
                assert exact(invalid, 4) == b"OKAY"
                invalid.sendall(struct.pack("<I", 32769))
                assert invalid.recv(1) == b""
            with producer(agent, config, identity=identity) as invalid:
                assert exact(invalid, 4) == b"OKAY"
                invalid.sendall(event(b"\xed\xa0\x80"))
                assert invalid.recv(1) == b""
            print("PASS: authentication/origins, config fetch/identity, fragmented transport, two viewers,")
            print("      Japanese/emoji/multiline/extended frames, stdin, ping/pong/close, malformed input")
            if with_luau:
                print("PASS: Luau metadata, transformation, and nil/drop callback")
        finally:
            for s in sockets:
                s.close()
            p.terminate()
            try:
                p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
            log.close()
            if p.returncode not in (0, -15):
                print((root / "host.log").read_text(errors="replace"), file=sys.stderr)
        # Check that an insecure existing secret is rejected before listening.
        secret = root / "secret"
        secret.write_text(TOKEN.decode() + "\n")
        secret.chmod(0o644)
        check = subprocess.run([binary, "--token-file", str(secret)], capture_output=True, timeout=3)
        assert check.returncode != 0 and b"mode 600" in check.stderr
        print("PASS: insecure token-file permissions rejected")

if __name__ == "__main__":
    main()
