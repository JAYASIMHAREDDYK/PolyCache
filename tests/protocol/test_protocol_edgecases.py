#!/usr/bin/env python3
"""
Protocol edge case test suite for PolyCache.
Tests byte fragmentation, pipelining, malformed packets, and inline commands.
"""
import socket
import time
import sys

def run_protocol_tests(host="127.0.0.1", port=6379):
    print(f"Connecting to PolyCache at {host}:{port} for protocol edge-case testing...")
    
    # 1. Byte-by-byte fragmented transmission test
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    
    cmd = "*3\r\n$3\r\nSET\r\n$12\r\nfragment_key\r\n$14\r\nfragment_value\r\n"
    for b in cmd:
        s.sendall(b.encode("utf-8"))
        time.sleep(0.001) # 1ms gap between bytes
    
    resp = s.recv(1024).decode("utf-8")
    assert resp == "+OK\r\n", f"Fragmented command failed: {resp}"
    print("  [PASS] Byte-by-byte fragmented transmission")

    # 2. Pipelining test (100 commands in a single write buffer)
    pipeline_buf = ""
    for i in range(100):
        k = f"pipe_{i}"
        v = f"val_{i}"
        pipeline_buf += f"*3\r\n$3\r\nSET\r\n${len(k)}\r\n{k}\r\n${len(v)}\r\n{v}\r\n"
    
    s.sendall(pipeline_buf.encode("utf-8"))
    
    received_bytes = b""
    expected_response = b"+OK\r\n" * 100
    while len(received_bytes) < len(expected_response):
        chunk = s.recv(4096)
        if not chunk: break
        received_bytes += chunk
    
    assert received_bytes == expected_response, f"Pipelining response mismatch: received {len(received_bytes)} bytes"
    print("  [PASS] Pipelining (100 batched commands)")

    # 3. Inline command support
    s.sendall(b"PING\r\n")
    resp = s.recv(1024).decode("utf-8")
    assert resp == "+PONG\r\n", f"Inline PING failed: {resp}"

    s.sendall(b"GET fragment_key\r\n")
    resp = s.recv(1024).decode("utf-8")
    assert resp == "$14\r\nfragment_value\r\n", f"Inline GET failed: {resp}"
    print("  [PASS] Inline command format")

    # 4. Malformed syntax handling (server responds with error or closes cleanly)
    s.sendall(b"*invalid_number\r\n")
    resp = s.recv(1024).decode("utf-8")
    assert resp.startswith("-ERR") or resp.startswith("-"), f"Malformed syntax failed: {resp}"
    print("  [PASS] Malformed input rejection")

    s.close()
    print("\n[SUCCESS] All Protocol Edge-case tests passed successfully!")

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 6379
    run_protocol_tests(port=port)
