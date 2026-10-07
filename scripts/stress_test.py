#!/usr/bin/env python3
"""
Stress test for concurrent connections and active traffic on PolyCache.
"""
import socket
import sys
import time

def run_stress_test(host="127.0.0.1", port=6379, num_connections=200):
    print(f"[stress] Opening {num_connections} concurrent sockets to {host}:{port}...")
    sockets = []
    
    try:
        for i in range(num_connections):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s.connect((host, port))
            sockets.append(s)
            if (i + 1) % 50 == 0:
                print(f"  Connected {i + 1}/{num_connections} clients...")
        
        print("[stress] All sockets established. Sending concurrent PING traffic...")
        for s in sockets:
            s.sendall(b"*1\r\n$4\r\nPING\r\n")
        
        for s in sockets:
            resp = s.recv(1024)
            assert resp == b"+PONG\r\n", f"Unexpected response: {resp}"
        
        print(f"[PASS] Successfully handled {num_connections} concurrent connections without errors!")
    finally:
        for s in sockets:
            try:
                s.close()
            except:
                pass

if __name__ == "__main__":
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 6379
    run_stress_test(port=port, num_connections=count)

