"""Exercise the native picker's host submission without opening the overlay."""
import pathlib
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from integration import TOKEN, connect, exact, frame, free_port, text_bytes


def main():
    host, picker = map(str, map(pathlib.Path.resolve, map(pathlib.Path, sys.argv[1:])))
    ws, agent = free_port(), free_port()
    while agent == ws:
        agent = free_port()
    with tempfile.TemporaryDirectory() as directory:
        token = pathlib.Path(directory) / "session.token"
        token.write_text(TOKEN.decode())
        process = subprocess.Popen([host, "--token", TOKEN.decode(), "--ws-port", str(ws), "--agent-port", str(agent)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:
                    with connect(agent):
                        break
                except OSError:
                    time.sleep(.03)
            errors = []
            with connect(agent) as listener:
                listener.sendall(b"THK2\x05" + TOKEN)
                assert exact(listener, 4) == b"OKAY"
                title = "Fixture 日本語 🦊"

                def respond():
                    try:
                        for status, message in ((1, "Attached to fixture"), (0, "No matching window")):
                            count = struct.unpack("<I", exact(listener, 4))[0]
                            assert exact(listener, count) == text_bytes(title)
                            listener.sendall(frame(bytes([status]) + text_bytes(message)))
                    except Exception as error:
                        errors.append(error)

                worker = threading.Thread(target=respond, daemon=True)
                worker.start()
                args = [picker, "--token-file", str(token), "--port", str(agent), "--window", title]
                success = subprocess.run(args, capture_output=True, timeout=10)
                assert success.returncode == 0 and b"Attached to fixture" in success.stdout, success.stderr
                failure = subprocess.run(args, capture_output=True, timeout=10)
                assert failure.returncode == 1 and b"No matching window" in failure.stderr, failure.stderr
                worker.join(timeout=5)
                assert not worker.is_alive()
                if errors:
                    raise errors[0]
            print("PASS: native picker submits Unicode window titles and returns attachment success or failure")
        finally:
            process.terminate()
            process.wait(timeout=5)


if __name__ == "__main__":
    main()
