#!/usr/bin/env python3
"""Real Windows fixture injection test for CI. Not a Wine compatibility test."""
import argparse
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import time
from integration import exact, frame, profile_tag

TOKEN = b"a" * 64

def text(s):
    b = s.encode()
    return struct.pack("<I", len(b)) + b

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("--bits", type=int, choices=(4, 8), required=True,
                        help="target pointer width in bytes, supplied by CMake")
    args = parser.parse_args()
    root = pathlib.Path(args.directory).resolve()
    is_x86 = args.bits == 4
    client, dll, demo = (root / name for name in ("th-client.exe", "th-hook.dll", "th-demo.exe"))
    config = struct.pack("<II", 2, 1)
    config += b"".join(text(s) for s in ("dialogue", "th-demo.exe", "th_demo_text", "", ""))
    config += struct.pack("<BBBBi", 1 if is_x86 else 2, 0, 0, 0, 0)
    config += struct.pack("<BBBBIIII", 1, 2, 16 if is_x86 else 13, 0, 0, 4 if is_x86 else 40, 4096, 0)
    profile_id = "fixture-x86" if is_x86 else "fixture-x64"
    identity = profile_tag(profile_id, config)
    got_line, done = threading.Event(), threading.Event()
    errors = []
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    listener.settimeout(.25)
    port = listener.getsockname()[1]

    def backend():
        try:
            while not done.is_set():
                try:
                    s, _ = listener.accept()
                except socket.timeout:
                    continue
                with s:
                    s.settimeout(8)
                    hello = exact(s, 69)
                    assert hello[:4] == b"THK2" and hello[5:] == TOKEN
                    if hello[4] == 3:
                        n = struct.unpack("<I", exact(s, 4))[0]
                        query = exact(s, n)
                        assert query[:2] == bytes((1, 1 if is_x86 else 2))
                        process_len = struct.unpack_from("<I", query, 2)[0]
                        assert query[6:6+process_len].decode().lower() == "th-demo.exe"
                        item = (struct.pack("<I", len(profile_id)) + profile_id.encode() +
                                struct.pack("<I", len("FixtureEngine")) + b"FixtureEngine" +
                                struct.pack("<QB", identity, 1))
                        payload = b"\x01" + struct.pack("<I", 1) + item
                        s.sendall(frame(payload))
                        continue
                    if hello[4] == 4:
                        n = struct.unpack("<I", exact(s, 4))[0]
                        request = exact(s, n)
                        assert request[0] == 1 and struct.unpack_from("<Q", request, 1)[0] == identity
                        s.sendall(frame(config))
                        continue
                    assert hello[4] == 2 and struct.unpack("<Q", exact(s, 8))[0] == identity
                    s.sendall(b"OKAY")
                    while not done.is_set():
                        try:
                            size = struct.unpack("<I", exact(s, 4))[0]
                            payload = exact(s, size)
                        except EOFError:
                            break
                        kind, hook, pid, tid, count = struct.unpack("<BIIII", payload[:17])
                        value = payload[17:].decode("utf-8")
                        assert count == len(payload) - 17
                        if kind == 1:
                            assert hook == 0 and pid == process.pid and tid != 0
                            assert any(c in value for c in "日本語文章同次これ")
                            got_line.set()
        except Exception as exc:
            errors.append(exc)
            got_line.set()

    process = subprocess.Popen([str(demo)], stdout=subprocess.DEVNULL)
    thread = threading.Thread(target=backend, daemon=True)
    thread.start()
    try:
        time.sleep(.5)
        base = [str(client), "--pid", str(process.pid), "--dll", str(dll)]
        attach = subprocess.run([base[0], "attach", *base[1:], "--port", str(port), "--token", TOKEN.decode()], capture_output=True, timeout=25)
        if attach.returncode:
            raise RuntimeError(attach.stdout.decode(errors="replace") + attach.stderr.decode(errors="replace"))
        assert got_line.wait(12), "no text captured from real Windows fixture"
        if errors:
            raise errors[0]
        stopped = subprocess.run([base[0], "stop", *base[1:]], capture_output=True, timeout=20)
        assert stopped.returncode == 0, stopped.stderr.decode(errors="replace")
        print(f"PASS: actual {'x86' if is_x86 else 'x64'} fixture injection, UTF-16 capture, TCP output, and safe stop on Windows")
    finally:
        done.set()
        process.terminate()
        process.wait(timeout=5)
        listener.close()
        thread.join(timeout=2)

if __name__ == "__main__":
    main()
