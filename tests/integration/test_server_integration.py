#!/usr/bin/env python3
"""
Integration test suite for PolyCache server.
Validates all supported commands over raw TCP RESP2 connection.
"""
import socket
import time
import sys

def send_cmd(s, *args):
    # Construct RESP array
    msg = f"*{len(args)}\r\n"
    for arg in args:
        s_arg = str(arg)
        msg += f"${len(s_arg.encode('utf-8'))}\r\n{s_arg}\r\n"
    s.sendall(msg.encode("utf-8"))
    
    # Read response
    resp = s.recv(4096).decode("utf-8")
    return resp

def run_tests(host="127.0.0.1", port=6379):
    print(f"Connecting to PolyCache at {host}:{port}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))
    
    # 1. PING
    r = send_cmd(s, "PING")
    assert r == "+PONG\r\n", f"PING failed: {r}"
    r = send_cmd(s, "PING", "hello")
    assert r == "$5\r\nhello\r\n", f"PING with msg failed: {r}"
    print("  [PASS] PING")

    # 2. FLUSHDB
    r = send_cmd(s, "FLUSHDB")
    assert r == "+OK\r\n", f"FLUSHDB failed: {r}"
    r = send_cmd(s, "DBSIZE")
    assert r == ":0\r\n", f"DBSIZE failed: {r}"
    print("  [PASS] FLUSHDB & DBSIZE")

    # 3. SET & GET
    r = send_cmd(s, "SET", "mykey", "foobar")
    assert r == "+OK\r\n", f"SET failed: {r}"
    r = send_cmd(s, "GET", "mykey")
    assert r == "$6\r\nfoobar\r\n", f"GET failed: {r}"
    r = send_cmd(s, "GET", "nonexistent")
    assert r == "$-1\r\n", f"GET nonexistent failed: {r}"
    print("  [PASS] SET & GET")

    # 4. EXISTS & DEL
    r = send_cmd(s, "EXISTS", "mykey")
    assert r == ":1\r\n", f"EXISTS failed: {r}"
    r = send_cmd(s, "DEL", "mykey")
    assert r == ":1\r\n", f"DEL failed: {r}"
    r = send_cmd(s, "EXISTS", "mykey")
    assert r == ":0\r\n", f"EXISTS after DEL failed: {r}"
    print("  [PASS] EXISTS & DEL")

    # 5. INCR & DECR
    r = send_cmd(s, "INCR", "counter")
    assert r == ":1\r\n", f"INCR 1 failed: {r}"
    r = send_cmd(s, "INCR", "counter")
    assert r == ":2\r\n", f"INCR 2 failed: {r}"
    r = send_cmd(s, "DECR", "counter")
    assert r == ":1\r\n", f"DECR failed: {r}"
    print("  [PASS] INCR & DECR")

    # 6. EXPIRE & TTL
    send_cmd(s, "SET", "expkey", "tempval")
    r = send_cmd(s, "EXPIRE", "expkey", "2")
    assert r == ":1\r\n", f"EXPIRE failed: {r}"
    r = send_cmd(s, "TTL", "expkey")
    assert r in (":1\r\n", ":2\r\n"), f"TTL failed: {r}"
    time.sleep(2.2)
    r = send_cmd(s, "GET", "expkey")
    assert r == "$-1\r\n", f"GET expired key failed: {r}"
    r = send_cmd(s, "TTL", "expkey")
    assert r == ":-2\r\n", f"TTL expired key failed: {r}"
    print("  [PASS] EXPIRE & TTL")

    # 7. Sorted Sets: ZADD, ZRANGEBYSCORE, ZREM
    send_cmd(s, "DEL", "myzset")
    r = send_cmd(s, "ZADD", "myzset", "10", "alice", "20", "bob", "30", "charlie")
    assert r == ":3\r\n", f"ZADD failed: {r}"
    r = send_cmd(s, "ZRANGEBYSCORE", "myzset", "15", "35")
    assert "*2\r\n$3\r\nbob\r\n$7\r\ncharlie\r\n" in r, f"ZRANGEBYSCORE failed: {r}"
    r = send_cmd(s, "ZREM", "myzset", "bob")
    assert r == ":1\r\n", f"ZREM failed: {r}"
    r = send_cmd(s, "ZRANGEBYSCORE", "myzset", "0", "100")
    assert "bob" not in r and "alice" in r and "charlie" in r, f"ZREM validation failed: {r}"
    print("  [PASS] Sorted sets (ZADD, ZRANGEBYSCORE, ZREM)")

    # 8. INFO
    r = send_cmd(s, "INFO")
    assert "# Server" in r and "polycache_version:1.0.0" in r, f"INFO failed: {r}"
    assert "connected_clients:" in r, f"INFO clients missing: {r}"
    print("  [PASS] INFO command")

    s.close()
    print("\n[SUCCESS] All Integration tests passed successfully!")

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 6379
    run_tests(port=port)
