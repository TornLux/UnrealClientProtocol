"""
CLI tool for communicating with the UnrealClientProtocol TCP plugin.

Accepts either a single JSON command or a JSON array of commands.
Connects to the UE editor via TCP (4-byte LE length-prefixed framing).

Usage:
    python UCP.py -f commands.json
    python UCP.py {"type":"find","class":"/Script/Engine.World"}
    echo <json> | python UCP.py --stdin

Environment:
    UE_HOST    (default 127.0.0.1)
    UE_PORT    (default 9876)
    UE_TIMEOUT (default 30)
"""

import struct
import socket
import json
import sys
import os

UE_HOST = os.environ.get("UE_HOST", "127.0.0.1")
UE_PORT = int(os.environ.get("UE_PORT", "9876"))
TIMEOUT = float(os.environ.get("UE_TIMEOUT", "30"))


def send_receive(sock: socket.socket, payload: dict) -> dict:
    body = json.dumps(payload).encode("utf-8")
    sock.sendall(struct.pack("<I", len(body)) + body)
    raw_len = _recv_exact(sock, 4)
    resp_len = struct.unpack("<I", raw_len)[0]
    raw_body = _recv_exact(sock, resp_len)
    return json.loads(raw_body.decode("utf-8"))


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray()
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("UE connection closed unexpectedly")
        buf.extend(chunk)
    return bytes(buf)


def _simplify(resp: dict):
    """Strip success/result envelope.
    Success -> return result value directly.
    Failure -> return {error, expected?} only."""
    if resp.get("success"):
        return resp.get("result")
    out = {"error": resp.get("error", "Unknown error")}
    if "expected" in resp:
        out["expected"] = resp["expected"]
    return out


def execute(commands) -> str:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(TIMEOUT)
    try:
        sock.connect((UE_HOST, UE_PORT))

        if isinstance(commands, list):
            request = {"type": "batch", "commands": commands}
            resp = send_receive(sock, request)
            results = [_simplify(r) for r in resp.get("results", [])]
            return json.dumps(results, indent=2, ensure_ascii=False)
        else:
            resp = send_receive(sock, commands)
            return json.dumps(_simplify(resp), indent=2, ensure_ascii=False)
    except (ConnectionError, ConnectionRefusedError, OSError) as e:
        return json.dumps({"error": f"Cannot connect to UE ({e}). Is the editor running?"}, ensure_ascii=False)
    finally:
        sock.close()


def main():
    args = sys.argv[1:]

    if not args:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(1)

    if "--stdin" in args:
        raw = sys.stdin.read()
    elif args[0] == "-f" and len(args) >= 2:
        with open(args[1], "r", encoding="utf-8") as f:
            raw = f.read()
    else:
        raw = " ".join(args)

    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        print(json.dumps({"error": f"Invalid JSON: {e}"}))
        sys.exit(1)

    print(execute(data))


if __name__ == "__main__":
    main()
