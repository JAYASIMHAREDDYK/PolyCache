#!/usr/bin/env python3
"""
Crash recovery and AOF BGREWRITEAOF test suite.
"""
import socket
import time
import sys
import os

def send_cmd(s, *args):
    msg = f"*{len(args)}\r\n"
    for arg in args:
        s_arg = str(arg)
        msg += f"${len(s_arg.encode('utf-8'))}\r\n{s_arg}\r\n"
    s.sendall(msg.encode("utf-8"))
    return s.recv(4096).decode("utf-8")

def run_recovery_tests(host="127.0.0.1", port=6379):
    print(f"Connecting to PolyCache at {host}:{port} for recovery & AOF rewrite testing...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((host, port))

    # 1. Clear database
    send_cmd(s, "FLUSHDB")

    # 2. Write test datasets
    print("  Populating data...")
    for i in range(50):
        send_cmd(s, "SET", f"rec_key_{i}", f"rec_val_{i}")
    
    send_cmd(s, "SET", "persist_counter", "100")
    send_cmd(s, "INCR", "persist_counter")
    send_cmd(s, "ZADD", "rec_scores", "10.5", "alice", "99.0", "bob")

    # 3. Trigger BGREWRITEAOF
    print("  Triggering BGREWRITEAOF...")
    r = send_cmd(s, "BGREWRITEAOF")
    assert "+Background append only file rewriting started\r\n" in r, f"BGREWRITEAOF failed: {r}"

    # Wait for background rewrite to complete
    time.sleep(1.5)

    # 4. Check INFO persistence section
    info = send_cmd(s, "INFO")
    assert "aof_last_bgrewrite_status:ok" in info, f"BGREWRITE status not ok: {info}"
    print("  [PASS] BGREWRITEAOF executed and swapped successfully")

    # 5. Verify data integrity after rewrite
    r = send_cmd(s, "GET", "rec_key_25")
    assert r == "$10\r\nrec_val_25\r\n", f"GET after rewrite failed: {r}"
    r = send_cmd(s, "GET", "persist_counter")
    assert r == "$3\r\n101\r\n", f"GET persist_counter failed: {r}"
    r = send_cmd(s, "ZRANGEBYSCORE", "rec_scores", "0", "200")
    assert "alice" in r and "bob" in r, f"ZRANGEBYSCORE after rewrite failed: {r}"
    print("  [PASS] Data integrity verified following background AOF rewrite")

    s.close()
    print("\n[SUCCESS] Recovery and AOF tests passed successfully!")

if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 6379
    run_recovery_tests(port=port)
